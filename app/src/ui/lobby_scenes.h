#ifndef LOBBY_SCENES_H
#define LOBBY_SCENES_H

/* Only needs the cimgui draw-list types (not the whole mod / Vulkan headers),
   so it is pulled in directly. Same define user.h uses before cimgui.h. */
#ifndef CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#endif
#include "../cimgui/cimgui.h"

/* Animated overlays for the lobby background presets.
 *
 * Every scene is drawn ON TOP of the preset's 4-corner gradient, entirely with
 * ImDrawList primitives (no textures, no extra files), so it costs nothing in
 * the APK and scales to any rectangle -- the same function draws the
 * full-screen background and the small thumbnails in the picker.
 */
typedef enum lobby_style {
  LOBBY_STYLE_GLOW = 0, /* gradient + glow blobs only (the original presets) */
  LOBBY_STYLE_EMBERS,   /* rising sparks                                    */
  LOBBY_STYLE_SUNBURST, /* slowly rotating golden rays + gold dust          */
  LOBBY_STYLE_GRID,     /* synthwave sun + scrolling perspective grid       */
  LOBBY_STYLE_STARS,    /* twinkling stars + shooting star                  */
  LOBBY_STYLE_PETALS,   /* falling, swaying petals                          */
  LOBBY_STYLE_CODE,     /* falling code columns                             */
  LOBBY_STYLE_WAVES,    /* layered moving waves                             */
  LOBBY_STYLE_BLADE     /* three diagonal sword slashes                     */
} lobby_style;

/* p0/p1: rectangle to fill. t: time in seconds (pass a constant to freeze).
 * full: true for the real background, false for a thumbnail (fewer
 * particles). a/b: the preset's two accent colours (rgb 0..1). */
void lobby_scene_draw(ImDrawList* dl, int style, ImVec2 p0, ImVec2 p1,
                      float t, bool full, const float a[3], const float b[3]);

#endif
