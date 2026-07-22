#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "ListMenuScreen.h"

namespace microreader {

// Displays per-book reading statistics: times opened and total reading time.
// Populated by ReaderOptionsScreen before being pushed.
class StatsScreen final : public ListMenuScreen {
 public:
  StatsScreen() = default;

  void set_book_stats(const std::string& title, uint32_t times_opened, uint64_t reading_ms,
                      int progress_pct, uint64_t time_left_ms, uint32_t page_turns) {
    book_title_ = title;
    times_opened_ = times_opened;
    reading_ms_ = reading_ms;
    progress_pct_ = progress_pct;
    time_left_ms_ = time_left_ms;
    page_turns_ = page_turns;
  }

  const char* name() const override { return "Stats"; }

 protected:
  void on_start() override {}
  void on_select(int /*index*/) override {}
  void draw_all_(DrawBuffer& buf, std::optional<uint8_t> battery_pct = std::nullopt) const override;

 private:
  std::string book_title_;
  uint32_t times_opened_ = 0;
  uint64_t reading_ms_ = 0;
  int progress_pct_ = 0;
  uint64_t time_left_ms_ = 0;
  uint32_t page_turns_ = 0;
};

}  // namespace microreader
