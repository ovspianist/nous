#pragma once

// Small-only form of ui_font_profile.h for low-level drawing and reader UI
// code that does not need to compile the larger menu font assets.
#ifdef NOUS_USE_UNITREIGN_UI_FONT
#include "ui_font_small.h"
#else
#include "ui_fonts/legacy/ui_font_small.h"
#endif
