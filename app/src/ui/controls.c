#include "controls.h"

#include <stdio.h>

#include "../arrow_styles.h"
#include "../user.h"
#include "crystal_theme.h"
#include "key_buttons.h"
#include "settings.h"

static const char* const ARROW_STYLE_NAMES[ARROW_STYLE_COUNT] = {
    "Red Arrow", "Red 3D", "Blue 3D", "Blue Neon", "Colourful",
    "Wing 3D", "Yellow Double", "Purple", "Magenta", "Crimson Dart",
};

enum { TAB_LAYOUT = 0, TAB_AIM, TAB_ZOOM, TAB_COUNT };

static const char* const TAB_NAMES[TAB_COUNT] = {"Layout", "Aim", "Zoom"};

/* Same purple palette as the Settings popup. */
#define COL_SEL    ((ImVec4){0.510f, 0.294f, 0.910f, 0.95f})
#define COL_SEL_H  ((ImVec4){0.569f, 0.353f, 0.960f, 0.98f})
#define COL_SEL_A  ((ImVec4){0.450f, 0.243f, 0.850f, 1.00f})
#define COL_IDLE   ((ImVec4){0.200f, 0.160f, 0.320f, 0.85f})
#define COL_IDLE_H ((ImVec4){0.300f, 0.240f, 0.470f, 0.90f})

static int   s_tab = TAB_LAYOUT; /* "Reset this tab" acts on this one */
static float s_alpha = 0.72f;    /* panel opacity (header slider) */

void ui_controls_init(tenv* env) {}

/* ------------------------------------------------------------------ */
/* Small form helpers: one table row per setting, label | widget.      */
/* ------------------------------------------------------------------ */

static bool form_begin(const char* id) {
  if (!igBeginTable(id, 2, ImGuiTableFlags_None, (ImVec2){0.0f, 0.0f}, 0.0f))
    return false;
  igTableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed,
                     igGetFontSize() * 8.5f, 0);
  igTableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.0f, 0);
  return true;
}

static void form_row(const char* label) {
  igTableNextRow(ImGuiTableRowFlags_None, 0.0f);
  igTableSetColumnIndex(0);
  igAlignTextToFramePadding();
  igText("%s", label);
  igTableSetColumnIndex(1);
}

static bool form_slider(const char* label, const char* id, float* v, float mn,
                        float mx, const char* fmt) {
  form_row(label);
  igSetNextItemWidth(-1);
  return igSliderFloat(id, v, mn, mx, fmt, ImGuiSliderFlags_AlwaysClamp);
}

static bool form_check(const char* label, const char* id, bool* v) {
  form_row(label);
  return igCheckbox(id, v);
}

/* Purple toggle-style button: selected = bright purple, idle = dark glass. */
static bool pick_button(const char* label, bool selected, ImVec2 size) {
  igPushStyleColor_Vec4(ImGuiCol_Button, selected ? COL_SEL : COL_IDLE);
  igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, selected ? COL_SEL_H : COL_IDLE_H);
  igPushStyleColor_Vec4(ImGuiCol_ButtonActive, COL_SEL_A);
  bool pressed = igButton(label, size);
  igPopStyleColor(3);
  return pressed;
}

/* Cards: one column on narrow panels, two on wide ones. */
static int card_cols(void) {
  ImVec2 avail;
  igGetContentRegionAvail(&avail);
  return avail.x > igGetFontSize() * 38.0f ? 2 : 1;
}

static void card_next(void) { igTableNextColumn(); }

/* ------------------------------------------------------------------ */
/* Defaults (single source of truth for "Reset this tab").            */
/* ------------------------------------------------------------------ */

static void reset_boost(user_settings* usrs) {
  usrs->boost_pos_custom = false;
  usrs->boost_rel_x = usrs->ctrl_swap_sides ? 0.125f : 0.875f;
  usrs->boost_rel_y = 0.875f;
  usrs->boost_rel_size = 0.125f;
}

static void reset_joystick(user_settings* usrs) {
  usrs->joy_pos_custom = false;
  usrs->joy_rel_x = usrs->ctrl_swap_sides ? 0.875f : 0.125f;
  usrs->joy_rel_y = 0.825f;
  usrs->joy_rel_size = 0.175f;
}

static void reset_tab(user_settings* usrs, int tab) {
  switch (tab) {
    case TAB_LAYOUT:
      usrs->ctrl_mode_trackpad = true;
      usrs->ctrl_swap_sides = false;
      reset_boost(usrs);
      usrs->boost_opacity = 1.0f;
      reset_joystick(usrs);
      usrs->joy_opacity = 1.0f;
      break;
    case TAB_AIM:
      usrs->arrow_size = 1.0f;
      usrs->arrow_sensitivity = 1.0f;
      usrs->boost_arrow_anim = false;
      usrs->arrow_style = 0;
      usrs->arrow_invisible = false;
      usrs->arrow_sync_with_zoom = true;
      break;
    case TAB_ZOOM: /* zoom slider only; zoom step / smooth zoom stay in Settings */
      usrs->zoom_sensitivity = 1.0f;
      usrs->zslider_rel_x = 0.968f;
      usrs->zslider_rel_y = 0.500f;
      usrs->zslider_rel_h = 0.280f;
      usrs->zslider_thick = 1.0f;
      usrs->zslider_opacity = 1.0f;
      usrs->zslider_horizontal = false;
      usrs->zslider_hidden = false;
      break;
    default:
      break;
  }
}

/* ------------------------------------------------------------------ */
/* Tabs                                                                */
/* ------------------------------------------------------------------ */

/* Scheme + sides + boost + joystick ring + keyboard buttons, one tab. */
static void tab_layout(tenv* env, user_settings* usrs, ImGuiStyle* style,
                       bool joystick_mode, float frame_height) {
  bool swapped = usrs->ctrl_swap_sides;

  if (!igBeginTable("layout_cards", card_cols(), ImGuiTableFlags_None,
                    (ImVec2){0.0f, 0.0f}, 0.0f))
    return;

#ifdef ANDROID
  card_next();
  igSeparatorText("Control scheme");
  {
    ImVec2 avail;
    igGetContentRegionAvail(&avail);
    float half_w = (avail.x - style->ItemSpacing.x) / 2.0f;
    if (pick_button("Joystick", !usrs->ctrl_mode_trackpad, (ImVec2){half_w, 0.0f}))
      usrs->ctrl_mode_trackpad = false;
    igSameLine(0, -1);
    if (pick_button("Arrow", usrs->ctrl_mode_trackpad, (ImVec2){half_w, 0.0f}))
      usrs->ctrl_mode_trackpad = true;
  }
#endif

  card_next();
  igSeparatorText("Sides");
  {
    ImVec2 avail;
    igGetContentRegionAvail(&avail);
    if (pick_button(swapped ? "< Swapped >" : "> Default <", swapped,
                    (ImVec2){avail.x, 0.0f}))
      usrs->ctrl_swap_sides = !usrs->ctrl_swap_sides;
  }

  card_next();
  igSeparatorText("Boost button");
  if (form_begin("boost_tbl")) {
    form_check("Custom position", "##boost custom", &usrs->boost_pos_custom);
    igBeginDisabled(!usrs->boost_pos_custom);
    form_slider("X", "##boost x", &usrs->boost_rel_x, 0.05f, 0.95f, "%.2f");
    form_slider("Y", "##boost y", &usrs->boost_rel_y, 0.05f, 0.98f, "%.2f");
    form_slider("Size", "##boost size", &usrs->boost_rel_size, 0.06f, 0.22f, "%.3f");
    igEndDisabled();
    form_slider("Opacity", "##boost opacity", &usrs->boost_opacity, 0.0f, 1.0f, "%.2f");
    igEndTable();
  }
  if (igButton("Reset boost position", (ImVec2){-1, 0.0f})) reset_boost(usrs);

  card_next();
  igSeparatorText("Joystick ring");
  if (!joystick_mode)
    igTextDisabled("Only used with the Joystick scheme.");
  igBeginDisabled(!joystick_mode);
  if (form_begin("joy_tbl")) {
    form_check("Custom position", "##joy custom", &usrs->joy_pos_custom);
    igBeginDisabled(!usrs->joy_pos_custom);
    form_slider("X", "##joy x", &usrs->joy_rel_x, 0.05f, 0.95f, "%.2f");
    form_slider("Y", "##joy y", &usrs->joy_rel_y, 0.30f, 0.98f, "%.2f");
    form_slider("Size", "##joy size", &usrs->joy_rel_size, 0.08f, 0.28f, "%.3f");
    igEndDisabled();
    form_slider("Opacity", "##joy opacity", &usrs->joy_opacity, 0.0f, 1.0f, "%.2f");
    igEndTable();
  }
  if (igButton("Reset joystick position", (ImVec2){-1, 0.0f})) reset_joystick(usrs);
  igEndDisabled();

  card_next();
  igSeparatorText("Layout editor");
  igTextWrapped(
      "Add keyboard buttons and drag keys, boost, joystick and zoom slider "
      "into place. Resize keys with width and height.");
  igSpacing();
  if (igButton("Open layout editor", (ImVec2){-1, frame_height * 1.4f})) {
    save_user_settings(usrs);
    ui_key_buttons_open_editor(env);
  }

  igEndTable();
}

static void tab_aim(tuser_data* usr, user_settings* usrs, ImGuiStyle* style,
                    bool arrow_mode) {
  igBeginDisabled(!arrow_mode);

  if (igBeginTable("aim_cards", card_cols(), ImGuiTableFlags_None,
                   (ImVec2){0.0f, 0.0f}, 0.0f)) {
    card_next();
    igSeparatorText("Touch arrow");
    if (!arrow_mode) igTextDisabled("Only used with the Arrow scheme.");
    if (form_begin("arrow_tbl")) {
      form_slider("Size", "##arrow size", &usrs->arrow_size, 0.40f, 2.50f, "%.2f");
      form_slider("Sensitivity", "##arrow sens", &usrs->arrow_sensitivity, 0.25f,
                  3.00f, "%.2f");
      form_check("Invisible arrow", "##arrow invisible", &usrs->arrow_invisible);
      form_check("Grows on boost", "##arrow grows boost", &usrs->boost_arrow_anim);
      form_check("Sync with zoom", "##arrow sync zoom", &usrs->arrow_sync_with_zoom);
      igEndTable();
    }
    igTextDisabled("Drag to move the arrow away from the head.");
    igTextDisabled(usrs->arrow_sync_with_zoom
                       ? "Arrow size follows game zoom."
                       : "Arrow keeps a fixed on-screen size while zooming.");

    card_next();
    igSeparatorText("Arrow design");
    if (!arrow_mode) igTextDisabled("Only used with the Arrow scheme.");
    {
      int shown = usrs->arrow_style;
      if (shown < 0 || shown >= ARROW_STYLE_COUNT) shown = 0;
      igText("Selected: %s", ARROW_STYLE_NAMES[shown]);
      ImVec2 avail;
      igGetContentRegionAvail(&avail);
      float tile = (avail.x - style->ItemSpacing.x * 2.0f) / 3.0f;
      if (tile > 74.0f) tile = 74.0f;
      if (tile < 42.0f) tile = 42.0f;
      if (igBeginTable("arrow_design_picker", 3, ImGuiTableFlags_None,
                       (ImVec2){0, 0}, 0)) {
        for (int i = 0; i < ARROW_STYLE_COUNT; ++i) {
          igTableNextColumn();
          char arrow_id[32];
          snprintf(arrow_id, sizeof arrow_id, "##arrow_style_%d", i);
          bool selected = usrs->arrow_style == i;
          igPushStyleColor_Vec4(ImGuiCol_Button, selected ? COL_SEL : COL_IDLE);
          igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                                selected ? COL_SEL_H : COL_IDLE_H);
          bool picked = false;
          if (usr->r && usr->r->arrow_atlas_ds) {
            float u0, v0, u1, v1;
            arrow_style_uv_bounds(i, &u0, &v0, &u1, &v1);
            ImTextureRef tex = {NULL, (ImTextureID)usr->r->arrow_atlas_ds};
            picked = igImageButton(arrow_id, tex,
                                   (ImVec2){tile, tile / arrow_style_aspect(i)},
                                   (ImVec2){u0, v0}, (ImVec2){u1, v1},
                                   (ImVec4){0, 0, 0, 0}, (ImVec4){1, 1, 1, 1});
          } else {
            picked = igButton(ARROW_STYLE_NAMES[i], (ImVec2){tile, tile});
          }
          if (picked) usrs->arrow_style = i;
          if (igIsItemHovered(0)) igSetTooltip("%s", ARROW_STYLE_NAMES[i]);
          igPopStyleColor(2);
        }
        igEndTable();
      }
    }
    igEndTable();
  }
  igEndDisabled();
}

/* Zoom slider widget only. Zoom step / smooth zoom live in Settings. */
static void tab_zoom(user_settings* usrs) {
  igSeparatorText("Zoom slider");
  if (form_begin("zoom_tbl")) {
    form_slider("X position", "##zoom x", &usrs->zslider_rel_x, 0.02f, 0.98f, "%.2f");
    form_slider("Y position", "##zoom y", &usrs->zslider_rel_y, 0.10f, 0.90f, "%.2f");
    form_slider("Height", "##zoom h", &usrs->zslider_rel_h, 0.08f, 0.48f, "%.2f");
    form_slider("Thickness", "##zoom thick", &usrs->zslider_thick, 0.5f, 3.0f, "%.2fx");
    form_slider("Opacity", "##zoom opacity", &usrs->zslider_opacity, 0.0f, 1.0f, "%.2f");
    form_slider("Speed", "##zoom speed", &usrs->zoom_sensitivity, 0.2f, 3.0f, "%.1f");
    form_check("Horizontal", "##zoom horizontal", &usrs->zslider_horizontal);
    form_check("Hide zoom bar", "##zoom hidden", &usrs->zslider_hidden);
    igEndTable();
  }
}

/* ------------------------------------------------------------------ */

void ui_controls(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  user_settings* usrs = &usr->usrs;
  ImGuiStyle* style = igGetStyle();
  game_data* gdata = &usr->gdata;

  /* Same small font + scale as the Settings popup. */
  igPushFont(usr->imgui_data.regular_font[FONT_SIZE_SMALL],
             usr->imgui_data.regular_font[FONT_SIZE_SMALL]->LegacySize);

  float ui_s = 1.0f;
  {
    float short_edge = ctx->size[0] < ctx->size[1] ? ctx->size[0] : ctx->size[1];
    ui_s = short_edge / 720.0f;
    if (ui_s < 0.75f) ui_s = 0.75f;
    if (ui_s > 1.30f) ui_s = 1.30f;
  }

#ifdef ANDROID
  const bool arrow_mode = usrs->ctrl_mode_trackpad;
  const bool joystick_mode = !usrs->ctrl_mode_trackpad;
#else
  /* PC port: no scheme switch yet, so nothing is dimmed. */
  const bool arrow_mode = true;
  const bool joystick_mode = true;
#endif

  /* Centered panel, same footprint as the lobby Settings popup, so the live
     control preview stays visible on both sides. */
  float panel_w = ctx->size[0] * 0.52f;
  float panel_h = ctx->size[1] * 0.88f;
  if (panel_w < 320.0f) panel_w = ctx->size[0] - 20.0f;
  if (panel_h < 320.0f) panel_h = ctx->size[1] - 20.0f;
  float panel_x = (ctx->size[0] - panel_w) * 0.5f;
  float panel_y = (ctx->size[1] - panel_h) * 0.5f;

  igSetCursorPos((ImVec2){panel_x, panel_y});
  igPushStyleVar_Float(ImGuiStyleVar_ChildRounding, 14.0f);
  igPushStyleVar_Float(ImGuiStyleVar_ChildBorderSize, 1.0f);
  igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){12 * ui_s, 10 * ui_s});
  igPushStyleVar_Vec2(ImGuiStyleVar_FramePadding, (ImVec2){8 * ui_s, 7 * ui_s});
  igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, (ImVec2){8 * ui_s, 9 * ui_s});
  igPushStyleVar_Float(ImGuiStyleVar_ScrollbarSize, 22.0f * ui_s);
  igPushStyleColor_Vec4(ImGuiCol_ChildBg, (ImVec4){0.070f, 0.050f, 0.120f, s_alpha});
  igPushStyleColor_Vec4(ImGuiCol_Border, (ImVec4){0.690f, 0.580f, 0.960f, 0.55f});
  crystal_push_theme();

  float frame_height = igGetFrameHeight();
  float btn_h = frame_height * 1.2f;

  if (igBeginChild_Str("controls_panel_root", (ImVec2){panel_w, panel_h},
                       ImGuiChildFlags_Borders,
                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
                           ImGuiWindowFlags_NoScrollbar |
                           ImGuiWindowFlags_NoScrollWithMouse)) {
    /* Header: title + panel opacity (same as Settings). */
    {
      ImVec2 hv;
      igGetContentRegionAvail(&hv);
      igAlignTextToFramePadding();
      igTextColored((ImVec4){0.80f, 0.72f, 1.0f, 1.0f}, "CONTROLS");
      float sw = hv.x * 0.42f;
      igSameLine(style->WindowPadding.x + hv.x - sw, 0);
      igSetNextItemWidth(sw);
      igSliderFloat("##controls_alpha", &s_alpha, 0.30f, 1.0f, "Opacity %.2f",
                    ImGuiSliderFlags_AlwaysClamp);
    }

    /* Tab row. */
    {
      ImVec2 av;
      igGetContentRegionAvail(&av);
      float tw = (av.x - style->ItemSpacing.x * (TAB_COUNT - 1)) / TAB_COUNT;
      for (int t = 0; t < TAB_COUNT; ++t) {
        if (t) igSameLine(0, -1);
        if (pick_button(TAB_NAMES[t], t == s_tab, (ImVec2){tw, btn_h})) s_tab = t;
      }
    }
    igSeparator();

    float footer_h = btn_h + style->ItemSpacing.y * 2.0f + 4.0f;
    if (igBeginChild_Str("controls_scroll_area", (ImVec2){0.0f, -footer_h},
                         ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
#ifdef ANDROID
      ui_touch_scroll(env, 10);
#endif
      switch (s_tab) {
        case TAB_LAYOUT:
          tab_layout(env, usrs, style, joystick_mode, frame_height);
          break;
        case TAB_AIM:
          tab_aim(usr, usrs, style, arrow_mode);
          break;
        case TAB_ZOOM:
          tab_zoom(usrs);
          break;
        default:
          break;
      }
    }
    igEndChild();

    igSeparator();
    {
      ImVec2 avail;
      igGetContentRegionAvail(&avail);
      float gap = style->ItemSpacing.x;
      float bw = (avail.x - gap) * 0.5f;

      if (igButton("Reset this tab", (ImVec2){bw, btn_h})) reset_tab(usrs, s_tab);
      igSameLine(0, gap);

      ImVec2 ok_pos;
      igGetCursorScreenPos(&ok_pos);
      crystal_glow_rect(ok_pos, (ImVec2){bw, btn_h}, 0.647f, 0.420f, 1.0f);
      igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.510f, 0.294f, 0.910f, 1.0f});
      igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0.569f, 0.353f, 0.960f, 1.0f});
      igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0.450f, 0.243f, 0.850f, 1.0f});
      if (igButton("OK", (ImVec2){bw, btn_h})) {
        save_user_settings(usrs);
        gdata->curr_screen = TITLE_SCREEN;
      }
      crystal_sheen();
      igPopStyleColor(3);
    }
  }
  igEndChild();

  crystal_pop_theme();
  igPopStyleColor(2);
  igPopStyleVar(6);
  igPopFont();
}

void ui_controls_destroy(tenv* env) {}
