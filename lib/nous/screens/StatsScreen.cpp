#include "StatsScreen.h"

#include <cstdio>
#include <cstring>

#include "../display/DrawBuffer.h"

namespace microreader {

void StatsScreen::draw_all_(DrawBuffer& buf, std::optional<uint8_t>) const {
  const int W = buf.width();
  const int H = buf.height();
  buf.fill(true);

  if (!ui_font_.valid()) return;

  const BitmapFont& hf = header_font_.valid() ? header_font_ : ui_font_;
  const BitmapFont& uf = ui_font_;
  const BitmapFont& sf = subtitle_font_.valid() ? subtitle_font_ : ui_font_;

  static constexpr int kLM = 16;
  static constexpr int kRM = 16;

  int y = 14;

  // ── Title ──────────────────────────────────────────────────────────────
  buf.draw_text_proportional(kLM, y + hf.baseline(), "Statistics", strlen("Statistics"), hf, false);
  y += hf.y_advance() + 4;
  buf.fill_rect(0, y, W, 1, false);
  y += 6;

  // Book title
  if (!book_title_.empty()) {
    const std::string trunc = book_title_.size() > 38 ? book_title_.substr(0, 35) + "..." : book_title_;
    buf.draw_text_proportional(kLM, y + sf.baseline(), trunc.c_str(), trunc.size(), sf, false);
    y += sf.y_advance();
  }
  y += 18;

  // ── Progress % (large, centered) ───────────────────────────────────────
  char pct_buf[8];
  std::snprintf(pct_buf, sizeof(pct_buf), "%d%%", progress_pct_);
  {
    const int pw = static_cast<int>(hf.word_width(pct_buf, std::strlen(pct_buf), FontStyle::Regular));
    buf.draw_text_proportional((W - pw) / 2, y + hf.baseline(), pct_buf, std::strlen(pct_buf), hf, false);
  }
  y += hf.y_advance() + 8;

  // ── Progress bar ───────────────────────────────────────────────────────
  const int bar_x = kLM;
  const int bar_w = W - kLM - kRM;
  static constexpr int kBarH = 5;
  buf.fill_rect(bar_x, y, bar_w, kBarH, false);
  buf.fill_rect(bar_x + 1, y + 1, bar_w - 2, kBarH - 2, true);
  const int filled = (bar_w - 2) * progress_pct_ / 100;
  if (filled > 0)
    buf.fill_rect(bar_x + 1, y + 1, filled, kBarH - 2, false);
  y += kBarH + 20;

  // ── Divider ────────────────────────────────────────────────────────────
  buf.fill_rect(kLM, y, W - kLM - kRM, 1, false);
  y += 14;

  // ── Stats grid (2 columns) ─────────────────────────────────────────────
  const int col_w = (W - kLM - kRM) / 2;
  const int row_h = sf.y_advance() + 3 + uf.y_advance();

  auto draw_stat = [&](int col, int top_y, const char* label, const char* value) {
    const int x = kLM + col * col_w;
    buf.draw_text_proportional(x, top_y + sf.baseline(), label, std::strlen(label), sf, false);
    buf.draw_text_proportional(x, top_y + sf.y_advance() + 3 + uf.baseline(),
                               value, std::strlen(value), uf, false);
  };

  char rt_buf[24], tl_buf[24], to_buf[12], pt_buf[12], ppm_buf[12];

  // Reading Time
  {
    uint64_t mins = reading_ms_ / 60000ULL;
    uint64_t hrs  = mins / 60ULL;
    uint64_t mn   = mins % 60ULL;
    if (hrs > 0)
      std::snprintf(rt_buf, sizeof(rt_buf), "%uh %02um", static_cast<unsigned>(hrs), static_cast<unsigned>(mn));
    else
      std::snprintf(rt_buf, sizeof(rt_buf), "%um", static_cast<unsigned>(mn));
  }

  // Time Left
  if (time_left_ms_ > 0) {
    uint64_t mins = time_left_ms_ / 60000ULL;
    uint64_t hrs  = mins / 60ULL;
    uint64_t mn   = mins % 60ULL;
    if (hrs > 0)
      std::snprintf(tl_buf, sizeof(tl_buf), "~%uh %02um", static_cast<unsigned>(hrs), static_cast<unsigned>(mn));
    else
      std::snprintf(tl_buf, sizeof(tl_buf), "~%um", static_cast<unsigned>(mn));
  } else {
    std::snprintf(tl_buf, sizeof(tl_buf), "—");
  }

  std::snprintf(to_buf,  sizeof(to_buf),  "%u", static_cast<unsigned>(times_opened_));
  std::snprintf(pt_buf,  sizeof(pt_buf),  "%u", static_cast<unsigned>(page_turns_));

  if (page_turns_ > 0 && reading_ms_ > 0) {
    uint64_t x10 = (static_cast<uint64_t>(page_turns_) * 600000ULL) / reading_ms_;
    std::snprintf(ppm_buf, sizeof(ppm_buf), "%llu.%llu",
                  static_cast<unsigned long long>(x10 / 10),
                  static_cast<unsigned long long>(x10 % 10));
  } else {
    std::snprintf(ppm_buf, sizeof(ppm_buf), "—");
  }

  // Row 1: Reading Time  |  Time Left
  draw_stat(0, y, "Reading Time", rt_buf);
  draw_stat(1, y, "Time Left",   tl_buf);
  y += row_h + 14;

  // Row 2: Opened  |  Page Turns
  draw_stat(0, y, "Opened",      to_buf);
  draw_stat(1, y, "Pages/Min",   ppm_buf);
  y += row_h + 14;

  // Row 3: Page Turns (left)
  draw_stat(0, y, "Page Turns",  pt_buf);
}

}  // namespace microreader
