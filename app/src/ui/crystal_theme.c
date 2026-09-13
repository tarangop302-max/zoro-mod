#include "crystal_theme.h"

static ImU32 crystal_col(float r, float g, float b, float a) {
  return igColorConvertFloat4ToU32((ImVec4){r, g, b, a});
}

/* A soft circular glow with a smooth falloff, built from many thin,
   NON-overlapping stroked rings (each ring's alpha directly IS the visible
   alpha there -- no compounding blend math to get wrong) rather than a
   handful of overlapping filled discs. A small number of overlapping filled
   circles very visibly banded into hard concentric rings on-device (their
   alphas compound where they overlap, and that compounded total jumps in
   big, increasingly large steps toward the outer edge -- exactly the
   "target/bullseye" artifact seen in testing), since ImGui has no real
   gradient fill to fall back on. Many thin rings with a smoothly-decreasing
   alpha curve is the standard workaround and reads as an actual soft glow
   instead of stacked circles. */
static void crystal_draw_glow_blob(ImDrawList* dl, ImVec2 center,
                                    float radius, float r, float g, float b,
                                    float peak_alpha) {
  const int RINGS = 40;
  float step = radius / RINGS;
  for (int i = RINGS; i >= 1; i--) {
    float t = (float)i / RINGS;             /* 1.0 at edge, ~0 at center */
    float falloff = (1.0f - t) * (1.0f - t); /* smooth, 0 at edge */
    float alpha = peak_alpha * falloff;
    if (alpha < 0.002f) continue;
    ImDrawList_AddCircle(dl, center, step * i, crystal_col(r, g, b, alpha),
                        64, step * 1.6f);
  }
}

void crystal_draw_background_alpha(tenv* env, float alpha) {
  tcontext* ctx = env->ctx;
  ImDrawList* dl = igGetWindowDrawList();
  float w = ctx->size[0];
  float h = ctx->size[1];

  ImDrawList_AddRectFilledMultiColor(
      dl, (ImVec2){0, 0}, (ImVec2){w, h},
      crystal_col(0.078f, 0.039f, 0.141f, alpha) /* top-left */,
      crystal_col(0.176f, 0.106f, 0.306f, alpha) /* top-right */,
      crystal_col(0.141f, 0.082f, 0.259f, alpha) /* bottom-right */,
      crystal_col(0.114f, 0.063f, 0.212f, alpha) /* bottom-left */);

  /* Two soft purple glow blobs, roughly matching the approved mockup's
     glow positions. Scaled by the same alpha so a translucent background
     (e.g. the skin editor, which has a live preview sitting underneath)
     gets a proportionally subtler glow rather than two full-strength
     blobs sitting on top of a faint wash. */
  ImVec2 glow_a = {w * 0.72f, h * 0.30f};
  ImVec2 glow_b = {w * 0.20f, h * 0.80f};
  crystal_draw_glow_blob(dl, glow_a, 400.0f, 0.592f, 0.353f, 1.0f,
                        0.30f * alpha);
  crystal_draw_glow_blob(dl, glow_b, 340.0f, 0.353f, 0.235f, 0.784f,
                        0.24f * alpha);
}

void crystal_draw_background(tenv* env) {
  crystal_draw_background_alpha(env, 1.0f);
}

/* Thin light line along a widget's top edge -- call right after drawing it
   (uses the last item's rect). */
void crystal_sheen(void) {
  ImVec2 mn, mx;
  igGetItemRectMin(&mn);
  igGetItemRectMax(&mx);
  if (mx.x - mn.x < 20) return;
  ImDrawList* dl = igGetWindowDrawList();
  ImDrawList_AddLine(dl, (ImVec2){mn.x + 10, mn.y + 1.5f},
                      (ImVec2){mx.x - 10, mn.y + 1.5f},
                      crystal_col(1.0f, 1.0f, 1.0f, 0.12f), 1.5f);
}

/* Soft rounded glow behind an upcoming widget -- call BEFORE drawing it,
   with its known screen-space position/size, so the glow ends up behind.
   Kept deliberately tight: packed-together button rows need a small expand
   distance or the glow bleeds into neighboring rows instead of reading as a
   glow around just one button. */
void crystal_glow_rect(ImVec2 pos, ImVec2 size, float r, float g, float b) {
  ImDrawList* dl = igGetWindowDrawList();
  for (int i = 4; i >= 1; i--) {
    float expand = i * 2.5f;
    ImDrawList_AddRectFilled(
        dl, (ImVec2){pos.x - expand, pos.y - expand},
        (ImVec2){pos.x + size.x + expand, pos.y + size.y + expand},
        crystal_col(r, g, b, 0.05f * i), 14.0f + expand * 0.4f,
        ImDrawFlags_None);
  }
}

/* Pushes the ambient glass colors used by most widgets on a crystal-themed
   screen. Must be matched with crystal_pop_theme(). */
void crystal_push_theme(void) {
  igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, 12.0f);

  igPushStyleColor_Vec4(ImGuiCol_Border,
                        (ImVec4){0.690f, 0.580f, 0.960f, 0.35f});
  igPushStyleColor_Vec4(ImGuiCol_FrameBg,
                        (ImVec4){0.373f, 0.290f, 0.607f, 0.28f});
  igPushStyleColor_Vec4(ImGuiCol_FrameBgHovered,
                        (ImVec4){0.430f, 0.330f, 0.680f, 0.34f});
  igPushStyleColor_Vec4(ImGuiCol_FrameBgActive,
                        (ImVec4){0.470f, 0.360f, 0.720f, 0.42f});
  igPushStyleColor_Vec4(ImGuiCol_Button,
                        (ImVec4){0.373f, 0.290f, 0.607f, 0.28f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                        (ImVec4){0.430f, 0.330f, 0.680f, 0.34f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonActive,
                        (ImVec4){0.470f, 0.360f, 0.720f, 0.42f});
  igPushStyleColor_Vec4(ImGuiCol_Header,
                        (ImVec4){0.430f, 0.330f, 0.680f, 0.34f});
  igPushStyleColor_Vec4(ImGuiCol_HeaderHovered,
                        (ImVec4){0.470f, 0.360f, 0.720f, 0.42f});
  igPushStyleColor_Vec4(ImGuiCol_Text,
                        (ImVec4){0.945f, 0.925f, 1.0f, 1.0f});
}

#define CRYSTAL_COLOR_COUNT 10
#define CRYSTAL_STYLEVAR_COUNT 1

void crystal_pop_theme(void) {
  igPopStyleColor(CRYSTAL_COLOR_COUNT);
  igPopStyleVar(CRYSTAL_STYLEVAR_COUNT);
}
