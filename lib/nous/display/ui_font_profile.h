#pragma once

// Keep the fork's preferred UI typography separate from Unitreign's bundled
// Inter assets. This makes upstream font refreshes merge without replacing the
// menu and library-list appearance used by this build.
//
// Define NOUS_USE_UNITREIGN_UI_FONT to build with the upstream Inter profile.
#ifdef NOUS_USE_UNITREIGN_UI_FONT
#include "ui_font_header.h"
#include "ui_font_large.h"
#include "ui_font_medium.h"
#else
#include "ui_fonts/legacy/ui_font_header.h"
#include "ui_fonts/legacy/ui_font_large.h"
#include "ui_fonts/legacy/ui_font_medium.h"
#endif

#include "ui_font_profile_small.h"
