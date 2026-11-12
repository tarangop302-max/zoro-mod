#ifndef UI_FLAGS_H
#define UI_FLAGS_H

#include <stdbool.h>
#ifndef CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#endif
#include "../cimgui/cimgui.h"

/* Draws a small country flag (2-letter ISO code, e.g. "JP") into the box
   [pmin, pmax] using the draw list only -- no image files needed, crisp at any
   size. Returns false (and draws a neutral placeholder) for unknown codes. */
bool ui_flag_draw(ImDrawList* dl, ImVec2 pmin, ImVec2 pmax, const char* cc);

#endif
