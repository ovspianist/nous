#pragma once
// TestBooks.h — single source of truth for the curated test book list
// and shared discovery helpers used across test files.

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#ifndef TEST_FIXTURES_DIR
#define TEST_FIXTURES_DIR "."
#endif

namespace test_books {

namespace fs = std::filesystem;

inline std::string workspace_root() {
  std::string fixtures = TEST_FIXTURES_DIR;
  // fixtures = .../microreader2/test/fixtures → workspace = 3 levels up
  auto pos = fixtures.rfind('/');
  if (pos == std::string::npos)
    pos = fixtures.rfind('\\');
  std::string up1 = fixtures.substr(0, pos);
  pos = up1.rfind('/');
  if (pos == std::string::npos)
    pos = up1.rfind('\\');
  std::string up2 = up1.substr(0, pos);
  pos = up2.rfind('/');
  if (pos == std::string::npos)
    pos = up2.rfind('\\');
  return up2.substr(0, pos);
}

inline std::vector<std::string> discover_epubs(const std::string& dir) {
  std::vector<std::string> result;
  if (!fs::exists(dir))
    return result;
  for (auto& entry : fs::recursive_directory_iterator(dir)) {
    if (entry.is_regular_file()) {
      auto ext = entry.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      if (ext == ".epub")
        result.push_back(entry.path().string());
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

// Curated list of 15 representative EPUBs covering a range of sizes,
// image content, CSS complexity, and previously-problematic books.
inline std::vector<std::string> get_curated_books() {
  static const char* kBooks[] = {
      "gutenberg/pg11-images.epub",    // Alice in Wonderland — small, simple
      "gutenberg/pg84-images.epub",    // Frankenstein — medium classic
      "gutenberg/pg1342-images.epub",  // Pride and Prejudice — large
      "gutenberg/pg2701-images.epub",  // Moby Dick — large, many images
      "gutenberg/pg345-images.epub",   // Dracula — previously problematic (UTF-8 split)
      "gutenberg/pg74-images.epub",    // Tom Sawyer — previously problematic (NBSP split)
      "gutenberg/pg100-images.epub",   // Complete Shakespeare — large, many chapters
      "gutenberg/pg2600-images.epub",  // War and Peace — very large
      "gutenberg/pg219-images.epub",   // Heart of Darkness — short novella
      "gutenberg/pg5200-images.epub",  // Metamorphosis (Kafka) — short, simple
      "gutenberg/pg4300-images.epub",  // Ulysses (Joyce) — complex prose
      "gutenberg/pg120-images.epub",   // Treasure Island — many images
      "gutenberg/pg1661-images.epub",  // Sherlock Holmes — medium
      "gutenberg/pg514-images.epub",   // Little Women — medium
      "gutenberg/pg1727-images.epub",  // The Odyssey — classical, special chars
  };
  std::string base = workspace_root() + "/microreader/test/books/";
  std::vector<std::string> all;
  for (auto& b : kBooks) {
    std::string path = base + b;
    if (fs::exists(path))
      all.push_back(path);
  }
  return all;
}

// Single representative book for smoke/fast runs in unit_tests.
// Uses Dracula (medium classic, previously had UTF-8 split bugs) for good coverage.
// Falls back to first available curated book if dracula is missing.
inline std::vector<std::string> get_smoke_books() {
  std::string path = workspace_root() + "/microreader/test/books/gutenberg/pg345-images.epub";
  if (fs::exists(path))
    return {path};
  auto all = get_curated_books();
  if (!all.empty())
    return {all.front()};
  return {};
}

// All .epub files under test/books/ (gutenberg + other).
inline std::vector<std::string> get_all_books() {
  std::string root = workspace_root();
  std::vector<std::string> all;
  for (auto& dir : {root + "/microreader/test/books/gutenberg", root + "/microreader/test/books/other"}) {
    auto books = discover_epubs(dir);
    all.insert(all.end(), books.begin(), books.end());
  }
  return all;
}

// GTest name generator: stem with non-alphanumeric chars replaced by '_'.
inline std::string epub_test_name(const std::string& path) {
  auto name = fs::path(path).stem().string();
  for (auto& c : name)
    if (!std::isalnum(static_cast<unsigned char>(c)))
      c = '_';
  return name;
}

}  // namespace test_books

// ---------------------------------------------------------------------------
// Convenience macro: instantiates a parameterized test suite with the smoke
// book list in unit_tests (SMOKE_TESTS_ONLY) or the curated book list in
// microreader_tests. Usage:
//
//   INSTANTIATE_EPUB_TESTS(MyTestSuite);
//
// This produces INSTANTIATE_TEST_SUITE_P(SmokeBooks, ...) or
// INSTANTIATE_TEST_SUITE_P(AllBooks, ...) as appropriate, with a GTest
// name that uses the EPUB filename stem.
// ---------------------------------------------------------------------------
#define EPUB_TEST_PARAM_NAME \
  [](const ::testing::TestParamInfo<std::string>& info) { return test_books::epub_test_name(info.param); }

#ifdef SMOKE_TESTS_ONLY
#define INSTANTIATE_EPUB_TESTS(TestSuite)                                                             \
  INSTANTIATE_TEST_SUITE_P(SmokeBooks, TestSuite, ::testing::ValuesIn(test_books::get_smoke_books()), \
                           EPUB_TEST_PARAM_NAME)
#else
#define INSTANTIATE_EPUB_TESTS(TestSuite)                                                             \
  INSTANTIATE_TEST_SUITE_P(AllBooks, TestSuite, ::testing::ValuesIn(test_books::get_curated_books()), \
                           EPUB_TEST_PARAM_NAME)
#endif
