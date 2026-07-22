#include "ReaderSyncStore.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../HeapLog.h"
#include "BookIndex.h"

#ifdef _WIN32
#include <filesystem>
namespace fs = std::filesystem;
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#ifdef ESP_PLATFORM
#include "esp_log.h"
#define SYNC_LOGI(fmt, ...) ESP_LOGI("reader_sync", fmt, ##__VA_ARGS__)
#define SYNC_LOGW(fmt, ...) ESP_LOGW("reader_sync", fmt, ##__VA_ARGS__)
#else
#define SYNC_LOGI(fmt, ...) (void)0
#define SYNC_LOGW(fmt, ...) (void)0
#endif

namespace microreader::reader_sync {
namespace {

constexpr char kSharedDirectoryName[] = ".nous-crossink-reader-sync";
constexpr char kBooksDirectoryName[] = "books";
constexpr char kCounterFileName[] = "counter-v1.bin";
constexpr char kRecordSuffix[] = ".ncrs";
constexpr size_t kRecordHashLength = 16;
constexpr size_t kRecordSuffixLength = sizeof(kRecordSuffix) - 1;
constexpr size_t kRecordFileNameLength = kRecordHashLength + kRecordSuffixLength;
constexpr std::array<uint8_t, 8> kRecordMagic = {'N', 'C', 'R', 'S', 'Y', 'N', 'C', '1'};
constexpr std::array<uint8_t, 8> kCounterMagic = {'N', 'C', 'R', 'S', 'E', 'Q', '0', '1'};
constexpr uint8_t kRecordVersion = 1;
constexpr size_t kHeaderSize = 32;
constexpr size_t kMaxPathBytes = 768;
constexpr size_t kMaxTitleBytes = 512;
constexpr size_t kMaxAuthorBytes = 512;

struct Record {
  std::string path;
  std::string title;
  std::string author;
  uint32_t recent_sequence = 0;
  uint32_t position_sequence = 0;
  uint32_t intra_spine_ppm = 0;
  uint16_t spine_index = 0;
  PositionSource position_source = PositionSource::None;
  enum class RecentState : uint8_t {
    None = 0,
    Present = 1,
    Removed = 2,
  } recent_state = RecentState::None;
};

void reset_record(Record& record) {
  // Preserve the string capacities while scanning multiple records. Besides
  // reducing fragmentation on the ESP32-C3, this makes a failed read leave a
  // completely empty record instead of partially retaining the previous one.
  record.path.clear();
  record.title.clear();
  record.author.clear();
  record.recent_sequence = 0;
  record.position_sequence = 0;
  record.intra_spine_ppm = 0;
  record.spine_index = 0;
  record.position_source = PositionSource::None;
  record.recent_state = Record::RecentState::None;
}

uint16_t read_u16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t read_u32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void write_u16(uint8_t* p, uint16_t value) {
  p[0] = static_cast<uint8_t>(value & 0xFFu);
  p[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
}

void write_u32(uint8_t* p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value & 0xFFu);
  p[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
  p[2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
  p[3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
}

uint64_t fnv1a64(std::string_view value) {
  uint64_t hash = 14695981039346656037ULL;
  for (unsigned char c : value) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::string sd_root(const char* data_dir) {
  if (!data_dir || data_dir[0] == '\0')
    return {};
  std::string root(data_dir);
  constexpr std::string_view suffix = "/.microreader";
  if (root.size() >= suffix.size() &&
      std::string_view(root).substr(root.size() - suffix.size()) == suffix) {
    root.resize(root.size() - suffix.size());
    return root;
  }
  return {};
}

std::string shared_root(const char* data_dir) {
  std::string root = sd_root(data_dir);
  if (root.empty())
    return {};
  root += '/';
  root += kSharedDirectoryName;
  return root;
}

std::string books_directory(const char* data_dir) {
  std::string path = shared_root(data_dir);
  if (!path.empty()) {
    path += '/';
    path += kBooksDirectoryName;
  }
  return path;
}

std::string local_path(const char* data_dir, std::string_view canonical) {
  std::string root = sd_root(data_dir);
  if (root.empty() || canonical.empty() || canonical.front() != '/')
    return {};
  root.append(canonical.data(), canonical.size());
  return root;
}

bool regular_file_exists(const std::string& path) {
#ifdef _WIN32
  std::error_code ec;
  return fs::is_regular_file(path, ec) && !ec;
#else
  struct stat info {};
  return ::stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
#endif
}

bool ensure_directories(const char* data_dir) {
  const std::string root = shared_root(data_dir);
  const std::string books = books_directory(data_dir);
  if (root.empty() || books.empty())
    return false;
#ifdef _WIN32
  std::error_code ec;
  fs::create_directories(books, ec);
  return !ec;
#else
  if (::mkdir(root.c_str(), 0775) != 0) {
    DIR* probe = opendir(root.c_str());
    if (!probe)
      return false;
    closedir(probe);
  }
  if (::mkdir(books.c_str(), 0775) != 0) {
    DIR* probe = opendir(books.c_str());
    if (!probe)
      return false;
    closedir(probe);
  }
  return true;
#endif
}

std::string record_path(const char* data_dir, std::string_view canonical) {
  const std::string dir = books_directory(data_dir);
  if (dir.empty())
    return {};
  char name[32];
  std::snprintf(name, sizeof(name), "%016llx%s", static_cast<unsigned long long>(fnv1a64(canonical)), kRecordSuffix);
  return dir + "/" + name;
}

bool read_exact(FILE* file, void* data, size_t size) {
  return size == 0 || std::fread(data, 1, size, file) == size;
}

bool read_record_file(const std::string& path, Record& record) {
  reset_record(record);
  FILE* file = std::fopen(path.c_str(), "rb");
  if (!file)
    return false;

  uint8_t header[kHeaderSize];
  const bool header_ok = read_exact(file, header, sizeof(header));
  if (!header_ok || !std::equal(kRecordMagic.begin(), kRecordMagic.end(), header) || header[8] != kRecordVersion) {
    std::fclose(file);
    return false;
  }

  const uint16_t path_len = read_u16(header + 26);
  const uint16_t title_len = read_u16(header + 28);
  const uint16_t author_len = read_u16(header + 30);
  const uint8_t raw_recent_state = header[10];
  const uint32_t recent_sequence = read_u32(header + 12);
  if (path_len == 0 || path_len > kMaxPathBytes || title_len > kMaxTitleBytes || author_len > kMaxAuthorBytes ||
      read_u32(header + 20) > kPositionScale ||
      raw_recent_state > static_cast<uint8_t>(Record::RecentState::Removed) ||
      (recent_sequence == 0 && raw_recent_state != static_cast<uint8_t>(Record::RecentState::None))) {
    std::fclose(file);
    return false;
  }

  const uint8_t raw_source = header[9];
  if (raw_source > static_cast<uint8_t>(PositionSource::Nous)) {
    std::fclose(file);
    return false;
  }

  record.path.resize(path_len);
  record.title.resize(title_len);
  record.author.resize(author_len);
  const bool body_ok = read_exact(file, record.path.data(), path_len) && read_exact(file, record.title.data(), title_len) &&
                       read_exact(file, record.author.data(), author_len) && std::fgetc(file) == EOF;
  std::fclose(file);
  if (!body_ok || record.path.empty() || record.path.front() != '/')
    return false;

  record.recent_sequence = recent_sequence;
  record.position_sequence = read_u32(header + 16);
  record.intra_spine_ppm = read_u32(header + 20);
  record.spine_index = read_u16(header + 24);
  record.position_source = static_cast<PositionSource>(raw_source);
  // State 0 with a nonzero sequence is the legacy version-1 representation of
  // an active recent entry.
  record.recent_state = raw_recent_state == 0 && recent_sequence != 0
                            ? Record::RecentState::Present
                            : static_cast<Record::RecentState>(raw_recent_state);
  return true;
}

bool read_record(const char* data_dir, std::string_view canonical, Record& record) {
  const std::string path = record_path(data_dir, canonical);
  if (path.empty() || !read_record_file(path, record))
    return false;
  // A path-hash collision must never overwrite a different book.
  return record.path == canonical;
}

bool promote_file(const std::string& tmp_path, const std::string& final_path) {
  const std::string backup_path = final_path + ".bak";
  std::remove(backup_path.c_str());
  const bool had_final = std::rename(final_path.c_str(), backup_path.c_str()) == 0;
  if (std::rename(tmp_path.c_str(), final_path.c_str()) != 0) {
    if (had_final)
      std::rename(backup_path.c_str(), final_path.c_str());
    std::remove(tmp_path.c_str());
    return false;
  }
  if (had_final)
    std::remove(backup_path.c_str());
  return true;
}

bool write_record(const char* data_dir, const Record& record) {
  if (!ensure_directories(data_dir) || record.path.empty() || record.path.size() > kMaxPathBytes ||
      record.title.size() > kMaxTitleBytes || record.author.size() > kMaxAuthorBytes ||
      record.intra_spine_ppm > kPositionScale)
    return false;

  const std::string final_path = record_path(data_dir, record.path);
  if (final_path.empty())
    return false;
  const std::string tmp_path = final_path + ".tmp";
  FILE* file = std::fopen(tmp_path.c_str(), "wb");
  if (!file)
    return false;

  uint8_t header[kHeaderSize] = {};
  std::copy(kRecordMagic.begin(), kRecordMagic.end(), header);
  header[8] = kRecordVersion;
  header[9] = static_cast<uint8_t>(record.position_source);
  header[10] = static_cast<uint8_t>(record.recent_state);
  write_u32(header + 12, record.recent_sequence);
  write_u32(header + 16, record.position_sequence);
  write_u32(header + 20, record.intra_spine_ppm);
  write_u16(header + 24, record.spine_index);
  write_u16(header + 26, static_cast<uint16_t>(record.path.size()));
  write_u16(header + 28, static_cast<uint16_t>(record.title.size()));
  write_u16(header + 30, static_cast<uint16_t>(record.author.size()));

  const bool ok = std::fwrite(header, 1, sizeof(header), file) == sizeof(header) &&
                  std::fwrite(record.path.data(), 1, record.path.size(), file) == record.path.size() &&
                  std::fwrite(record.title.data(), 1, record.title.size(), file) == record.title.size() &&
                  std::fwrite(record.author.data(), 1, record.author.size(), file) == record.author.size() &&
                  std::fflush(file) == 0;
  const bool close_ok = std::fclose(file) == 0;
  if (!ok || !close_ok) {
    std::remove(tmp_path.c_str());
    return false;
  }
  return promote_file(tmp_path, final_path);
}

template <typename Fn>
void for_each_record(const char* data_dir, Fn&& fn) {
  const std::string dir = books_directory(data_dir);
  if (dir.empty())
    return;
#ifdef _WIN32
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (ec)
      break;
    if (!entry.is_regular_file())
      continue;
    const std::string name = entry.path().filename().string();
    if (name.size() <= std::strlen(kRecordSuffix) ||
        name.compare(name.size() - std::strlen(kRecordSuffix), std::strlen(kRecordSuffix), kRecordSuffix) != 0)
      continue;
    Record record;
    if (read_record_file(entry.path().string(), record)) {
      const std::string expected_name = fs::path(record_path(data_dir, record.path)).filename().string();
      if (expected_name == name)
        fn(record);
    }
  }
#else
  // ESP-IDF's FAT VFS allocates both directory state and per-file fast-seek
  // state from the same small heap. Do not keep a DIR open while repeatedly
  // opening record files (or while callbacks inspect EPUB files): enumerate
  // the fixed-size record names first, close the directory, then read them.
  using RecordFileName = std::array<char, kRecordFileNameLength + 1>;
  std::vector<RecordFileName> names;
  names.reserve(32);

  DIR* directory = opendir(dir.c_str());
  if (!directory)
    return;
  while (dirent* entry = readdir(directory)) {
    const char* name = entry->d_name;
    const size_t name_len = std::strlen(name);
    if (name_len != kRecordFileNameLength ||
        std::strcmp(name + kRecordHashLength, kRecordSuffix) != 0)
      continue;
    RecordFileName stored{};
    std::memcpy(stored.data(), name, name_len);
    names.push_back(stored);
  }
  closedir(directory);

  Record record;
  std::string path;
  path.reserve(dir.size() + 1 + kRecordFileNameLength);
  for (const auto& name : names) {
    path.assign(dir);
    path.push_back('/');
    path.append(name.data(), kRecordFileNameLength);
    if (read_record_file(path, record) && record_path(data_dir, record.path) == path)
      fn(record);
  }
#endif
}

uint32_t scan_max_sequence(const char* data_dir) {
  uint32_t maximum = 0;
  for_each_record(data_dir, [&](const Record& record) {
    maximum = std::max(maximum, record.recent_sequence);
    maximum = std::max(maximum, record.position_sequence);
  });
  return maximum;
}

std::string counter_path(const char* data_dir) {
  const std::string root = shared_root(data_dir);
  return root.empty() ? std::string{} : root + "/" + kCounterFileName;
}

uint32_t read_counter(const char* data_dir) {
  const std::string path = counter_path(data_dir);
  FILE* file = path.empty() ? nullptr : std::fopen(path.c_str(), "rb");
  if (!file)
    return 0;
  uint8_t data[12];
  const bool ok = read_exact(file, data, sizeof(data)) && std::fgetc(file) == EOF &&
                  std::equal(kCounterMagic.begin(), kCounterMagic.end(), data);
  std::fclose(file);
  return ok ? read_u32(data + 8) : 0;
}

bool write_counter(const char* data_dir, uint32_t value) {
  if (!ensure_directories(data_dir))
    return false;
  const std::string final_path = counter_path(data_dir);
  const std::string tmp_path = final_path + ".tmp";
  FILE* file = std::fopen(tmp_path.c_str(), "wb");
  if (!file)
    return false;
  uint8_t data[12];
  std::copy(kCounterMagic.begin(), kCounterMagic.end(), data);
  write_u32(data + 8, value);
  const bool ok = std::fwrite(data, 1, sizeof(data), file) == sizeof(data) && std::fflush(file) == 0;
  const bool close_ok = std::fclose(file) == 0;
  if (!ok || !close_ok) {
    std::remove(tmp_path.c_str());
    return false;
  }
  return promote_file(tmp_path, final_path);
}

uint32_t next_sequence(const char* data_dir) {
  static std::string cached_root;
  static uint32_t cached_value = 0;
  const std::string root = shared_root(data_dir);
  if (root.empty())
    return 0;
  if (cached_root != root) {
    cached_root = root;
    cached_value = read_counter(data_dir);
    if (cached_value == 0)
      cached_value = scan_max_sequence(data_dir);
  }
  if (cached_value == UINT32_MAX)
    return 0;
  const uint32_t next = cached_value + 1;
  if (!write_counter(data_dir, next))
    return 0;
  cached_value = next;
  return next;
}

void update_metadata(Record& record, std::string_view canonical, std::string_view title, std::string_view author) {
  record.path.assign(canonical.data(), canonical.size());
  if (!title.empty() && title.size() <= kMaxTitleBytes)
    record.title.assign(title.data(), title.size());
  if (!author.empty() && author.size() <= kMaxAuthorBytes)
    record.author.assign(author.data(), author.size());
}

}  // namespace

std::string canonical_path(std::string_view path) {
  constexpr std::string_view mount = "/sdcard";
  if (path == mount)
    return "/";
  if (path.size() > mount.size() && path.substr(0, mount.size()) == mount && path[mount.size()] == '/')
    return std::string(path.substr(mount.size()));
  return std::string(path);
}

bool synchronize_recent_books(const char* data_dir, BookIndex& index, uint32_t& open_counter,
                              const std::string& index_path) {
  if (!ensure_directories(data_dir))
    return false;

  const auto& entries = index.entries();
  const auto& pool = index.pool();
  std::vector<size_t> opened;
  opened.reserve(entries.size());
  for (size_t i = 0; i < entries.size(); ++i) {
    if (entries[i].last_open_order > 0)
      opened.push_back(i);
  }
  std::sort(opened.begin(), opened.end(), [&](size_t a, size_t b) {
    return entries[a].last_open_order < entries[b].last_open_order;
  });

  // Bootstrap only missing records, oldest first, so existing Nous ordering is
  // preserved without displacing events already written by CrossInk.
  for (size_t index_pos : opened) {
    const auto& entry = entries[index_pos];
    const std::string path = entry.path.to_string(pool);
    const std::string canonical = canonical_path(path);
    Record record;
    if (!read_record(data_dir, canonical, record))
      record.path = canonical;
    if (record.recent_sequence != 0)
      continue;
    const uint32_t sequence = next_sequence(data_dir);
    if (sequence == 0)
      break;
    update_metadata(record, canonical, entry.title.view(pool), entry.author.view(pool));
    record.recent_sequence = sequence;
    record.recent_state = Record::RecentState::Present;
    if (!write_record(data_dir, record))
      SYNC_LOGW("failed to bootstrap recent record for %s", path.c_str());
  }

  bool changed = false;
  uint32_t maximum = open_counter;
  for_each_record(data_dir, [&](const Record& record) {
    maximum = std::max(maximum, record.recent_sequence);
    maximum = std::max(maximum, record.position_sequence);
    if (record.recent_sequence == 0)
      return;
    const std::string path = local_path(data_dir, record.path);
    bool found = false;
    for (const auto& entry : index.entries()) {
      if (entry.path.view(index.pool()) != path)
        continue;
      found = true;
      const uint32_t desired_order = record.recent_state == Record::RecentState::Removed ? 0 : record.recent_sequence;
      if (entry.last_open_order != desired_order) {
        index.set_last_opened(path, desired_order);
        changed = true;
      }
      break;
    }
    if (!found && record.recent_state == Record::RecentState::Present && BookIndex::is_book_path(path.c_str())) {
      // Existence checks must not allocate a FAT fast-seek table for the whole
      // EPUB merely to close it immediately.
      if (regular_file_exists(path) &&
          index.add_entry(path, record.title, record.author, record.recent_sequence, 0))
        changed = true;
    }
  });

  open_counter = maximum;
  if (changed) {
    index.save(index_path);
    SYNC_LOGI("imported shared recent-book order");
  }
  return changed;
}

uint32_t record_book_opened(const char* data_dir, std::string_view path, std::string_view title,
                            std::string_view author) {
  if (!ensure_directories(data_dir))
    return 0;
  const std::string canonical = canonical_path(path);
  if (canonical.empty() || canonical.front() != '/' || canonical.size() > kMaxPathBytes)
    return 0;
  Record record;
  if (!read_record(data_dir, canonical, record))
    record.path = canonical;
  const uint32_t sequence = next_sequence(data_dir);
  if (sequence == 0)
    return 0;
  update_metadata(record, canonical, title, author);
  record.recent_sequence = sequence;
  record.recent_state = Record::RecentState::Present;
  return write_record(data_dir, record) ? sequence : 0;
}

bool load_crossink_position(const char* data_dir, std::string_view path, Position& position) {
  const std::string canonical = canonical_path(path);
  Record record;
  if (!read_record(data_dir, canonical, record) || record.position_sequence == 0 ||
      record.position_source != PositionSource::CrossInk)
    return false;
  position.spine_index = record.spine_index;
  position.intra_spine_ppm = record.intra_spine_ppm;
  position.source = record.position_source;
  return true;
}

bool save_nous_position(const char* data_dir, std::string_view path, std::string_view title,
                        std::string_view author, uint16_t spine_index, uint32_t intra_spine_ppm) {
  const std::string canonical = canonical_path(path);
  if (canonical.empty() || canonical.front() != '/' || canonical.size() > kMaxPathBytes ||
      intra_spine_ppm > kPositionScale)
    return false;
  Record record;
  if (!read_record(data_dir, canonical, record))
    record.path = canonical;
  update_metadata(record, canonical, title, author);
  if (record.position_source == PositionSource::Nous && record.spine_index == spine_index &&
      record.intra_spine_ppm == intra_spine_ppm)
    return true;
  const uint32_t sequence = next_sequence(data_dir);
  if (sequence == 0)
    return false;
  record.position_sequence = sequence;
  record.position_source = PositionSource::Nous;
  record.spine_index = spine_index;
  record.intra_spine_ppm = intra_spine_ppm;
  return write_record(data_dir, record);
}

}  // namespace microreader::reader_sync
