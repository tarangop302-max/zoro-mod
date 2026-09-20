#ifndef NET_GRAPH_H
#define NET_GRAPH_H

#include <thermite.h>

/* Same define user.h / ntl_team.h use: without it cimgui.h declares ImDrawList,
   ImVec2, ... as opaque types, and whichever header is included first would
   decide that for the whole translation unit. */
#ifndef CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#endif
#include "../cimgui/cimgui.h"

/*
 * Ping (ms) + FPS history graph, modelled on the one in the NTL mod: the last
 * two minutes of both, ping in pink-red on the left axis, FPS in cyan on the
 * right axis, sampled ~30 times a second and drawn with per-pixel averaging so
 * a lag spike shows up as a spike instead of being smoothed away.
 */

/* Record one frame. Call once per game frame while connected; it decides
   internally when a new history sample is due. Recording continues whether or
   not the graph is currently drawn, so turning it on shows real history. */
void net_graph_sample(tenv* env);

/* Forget all history (e.g. after the clock base was reset). */
void net_graph_reset(void);

/* Default box size for a given base font size in pixels (the stats font). */
void net_graph_default_size(float base_font_px, float* out_w, float* out_h);

/* Draw the graph into `dl` inside the rectangle (x, y, w, h). `font` is the
   mono font to label it with and `label_px` the label size in pixels. */
void net_graph_draw(ImDrawList* dl, ImFont* font, float label_px, float x,
                    float y, float w, float h);

#endif
