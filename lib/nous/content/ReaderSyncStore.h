#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace microreader {

class BookIndex;

namespace reader_sync {

constexpr uint32_t kPositionScale = 1000000;

enum class PositionSource : uint8_t {
  None = 0,
  CrossInk = 1,
  Nous = 2,
};

struct Position {
  uint16_t spine_index = 0;
  uint32_t intra_spine_ppm = 0;
  PositionSource source = PositionSource::None;
};

// Convert Nous's mounted path (/sdcard/books/a.epub) to the path both
// firmwares store in the shared protocol (/books/a.epub).
std::string canonical_path(std::string_view path);

// Bootstrap missing shared recents from Nous, then import shared recency
// sequence numbers into the already-loaded BookIndex. Native read-time data is
// left untouched. Returns true when book_index.dat was changed and saved.
bool synchronize_recent_books(const char* data_dir, BookIndex& index, uint32_t& open_counter,
                              const std::string& index_path);

// Record a Nous book-open event. Returns the shared sequence number, or zero
// when the shared store is unavailable.
uint32_t record_book_opened(const char* data_dir, std::string_view path, std::string_view title,
                            std::string_view author);

// Load a position only when it was last written by CrossInk. Nous-authored
// positions are deliberately ignored to prevent approximation round-trips.
bool load_crossink_position(const char* data_dir, std::string_view path, Position& position);

// Save a Nous-authored position. Metadata is used to make imported recents
// useful even before the book is opened in the other firmware.
bool save_nous_position(const char* data_dir, std::string_view path, std::string_view title,
                        std::string_view author, uint16_t spine_index, uint32_t intra_spine_ppm);

}  // namespace reader_sync
}  // namespace microreader
