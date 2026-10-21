#include "controls.h"

#include <stdio.h>

#include "../arrow_styles.h"
#include "../user.h"
#include "key_buttons.h"
#include "settings.h"
#include "../game/recorder.h"

static const char* const ARROW_STYLE_NAMES[ARROW_STYLE_COUNT] = {
    "Red Arrow", "Red 3D", "Blue 3D", "Blue Neon", "Colourful",
    "Wing 3D", "Yellow Double", "Purple", "Magenta", "Crimson Dart",
};

enum {
  TAB_SCHEME = 0,
  TAB_AIM,
  TAB_BUTTONS,
  TAB_ZOOM,
  TAB_EXTRAS, /* Android only */
  TAB_COUNT
};

static const char* const TAB_NAMES[TAB_COUNT] = {
    "Scheme", "Aim", "Buttons", "Zoom", "Extras",
};

/* Tab currently shown; "Reset this tab" acts on this one. */
static int s_tab = TAB_SCHEME;

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

static bool form_check(const char* label, const char* id, bool* v,
                       const char* tip) {
  form_row(label);
  bool changed = igCheckbox(id, v);
  if (tip && igIsItemHovered(ImGuiHoveredFlags_None)) igSetTooltip("%s", tip);
  return changed;
}

/* Cards: one column on narrow panels (phones), two on wide ones (PC). */
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
    case TAB_SCHEME:
      usrs->ctrl_mode_trackpad = true;
      usrs->ctrl_trackpad_direct = false;
      usrs->ctrl_swap_sides = false;
      break;
    case TAB_AIM:
      usrs->arrow_size = 1.0f;
      usrs->arrow_sensitivity = 1.0f;
      usrs->boost_arrow_anim = false;
      usrs->arrow_style = 0;
      usrs->arrow_invisible = false;
      usrs->arrow_sync_with_zoom = true;
      break;
    case TAB_BUTTONS:
      reset_boost(usrs);
      usrs->boost_opacity = 1.0f;
      reset_joystick(usrs);
      usrs->joy_opacity = 1.0f;
      break;
    case TAB_ZOOM:
      usrs->zoom_sensitivity = 1.0f;
      usrs->zoom_step = 0.1f;
      usrs->smooth_zoom = false;
      usrs->zslider_rel_x = 0.968f;
      usrs->zslider_rel_y = 0.500f;
      usrs->zslider_rel_h = 0.280f;
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

static void tab_scheme(user_settings* usrs, ImGuiStyle* style,
                       bool arrow_mode) {
  bool swapped = usrs->ctrl_swap_sides;

  if (!igBeginTable("scheme_cards", card_cols(), ImGuiTableFlags_None,
                    (ImVec2){0.0f, 0.0f}, 0.0f))
    return;

#ifdef ANDROID
  card_next();
  igSeparatorText("Control scheme");
  {
    ImVec2 avail;
    igGetContentRegionAvail(&avail);
    float half_w = (avail.x - style->ItemSpacing.x) / 2.0f;

    if (!usrs->ctrl_mode_trackpad) {
      igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.168f, 0.468f, 0.768f, 1.0f});
      igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0.268f, 0.568f, 0.868f, 1.0f});
      igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0.068f, 0.368f, 0.668f, 1.0f});
    }
    if (igButton("Joystick", (ImVec2){half_w, 0.0f}))
      usrs->ctrl_mode_trackpad = false;
    if (!usrs->ctrl_mode_trackpad) igPopStyleColor(3);

    igSameLine(0, -1);

    if (usrs->ctrl_mode_trackpad) {
      igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.068f, 0.568f, 0.368f, 1.0f});
      igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0.168f, 0.668f, 0.468f, 1.0f});
      igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0.0f, 0.468f, 0.268f, 1.0f});
    }
    if (igButton("Arrow", (ImVec2){half_w, 0.0f}))
      usrs->ctrl_mode_trackpad = true;
    if (usrs->ctrl_mode_trackpad) igPopStyleColor(3);
  }
#endif

  card_next();
  igSeparatorText("Layout");
  igTextColored((ImVec4){0.60f, 0.60f, 0.60f, 1.0f}, "Sides:");
  igSameLine(0, 6.0f);
  if (swapped)
    igTextColored((ImVec4){0.47f, 0.71f, 1.00f, 1.0f}, "Swapped");
  else
    igTextColored((ImVec4){0.55f, 0.88f, 0.55f, 1.0f}, "Default");
  igSpacing();
  {
    ImVec2 avail;
    igGetContentRegionAvail(&avail);
    if (swapped) {
      igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.30f, 0.55f, 0.88f, 0.65f});
      igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0.40f, 0.65f, 0.98f, 0.75f});
      igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0.20f, 0.45f, 0.78f, 0.80f});
    }
    if (igButton(swapped ? "< Swap Sides >" : "> Swap Sides <",
                 (ImVec2){avail.x, 0.0f})) {
      usrs->ctrl_swap_sides = !usrs->ctrl_swap_sides;
    }
    if (swapped) igPopStyleColor(3);
  }

  card_next();
  igSeparatorText("Aim");
  if (arrow_mode) {
    if (form_begin("aim_tbl")) {
      form_check("Instant aim", "##arrow instant aim", &usrs->ctrl_trackpad_direct,
                 NULL);
      igEndTable();
    }
    igTextDisabled(
        "Arrow points straight from your touch-down point to your finger.");
  } else {
    igTextDisabled("Instant aim is available with the Arrow scheme.");
  }

  igEndTable();
}

static void tab_aim(tuser_data* usr, user_settings* usrs, ImGuiStyle* style,
                    bool arrow_mode) {
  if (!arrow_mode)
    igTextDisabled(
        "Arrow settings apply to the Arrow scheme. Switch scheme in the "
        "Scheme tab.");
  igBeginDisabled(!arrow_mode);

  if (igBeginTable("aim_cards", card_cols(), ImGuiTableFlags_None,
                   (ImVec2){0.0f, 0.0f}, 0.0f)) {
    card_next();
    igSeparatorText("Touch arrow");
    if (form_begin("arrow_tbl")) {
      form_slider("Size", "##arrow size", &usrs->arrow_size, 0.40f, 2.50f, "%.2f");
      form_slider("Sensitivity", "##arrow sens", &usrs->arrow_sensitivity, 0.25f,
                  3.00f, "%.2f");
      form_check("Invisible arrow", "##arrow invisible", &usrs->arrow_invisible,
                 NULL);
      form_check("Grows on boost", "##arrow grows boost", &usrs->boost_arrow_anim,
                 NULL);
      form_check("Sync with zoom", "##arrow sync zoom",
                 &usrs->arrow_sync_with_zoom, NULL);
      igEndTable();
    }
    if (usrs->arrow_sync_with_zoom)
      igTextDisabled("Arrow size follows game zoom.");
    else
      igTextDisabled("Arrow keeps a fixed on-screen size while zooming.");

    card_next();
    igSeparatorText("Arrow design");
    {
      int displayed_arrow_style = usrs->arrow_style;
      if (displayed_arrow_style < 0 || displayed_arrow_style >= ARROW_STYLE_COUNT)
        displayed_arrow_style = 0;
      igText("Selected: %s", ARROW_STYLE_NAMES[displayed_arrow_style]);
      ImVec2 arrow_picker_avail;
      igGetContentRegionAvail(&arrow_picker_avail);
      float arrow_tile =
          (arrow_picker_avail.x - style->ItemSpacing.x * 2.0f) / 3.0f;
      if (arrow_tile > 74.0f) arrow_tile = 74.0f;
      if (arrow_tile < 42.0f) arrow_tile = 42.0f;
      if (igBeginTable("arrow_design_picker", 3, ImGuiTableFlags_None,
                       (ImVec2){0, 0}, 0)) {
        for (int i = 0; i < ARROW_STYLE_COUNT; ++i) {
          igTableNextColumn();
          char arrow_id[32];
          snprintf(arrow_id, sizeof arrow_id, "##arrow_style_%d", i);
          bool selected = usrs->arrow_style == i;
          igPushStyleColor_Vec4(ImGuiCol_Button,
                                selected ? (ImVec4){0.72f, 0.12f, 0.16f, 0.90f}
                                         : (ImVec4){0.05f, 0.07f, 0.10f, 0.72f});
          igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                                (ImVec4){0.28f, 0.42f, 0.64f, 0.90f});
          bool picked = false;
          if (usr->r && usr->r->arrow_atlas_ds) {
            float u0, v0, u1, v1;
            arrow_style_uv_bounds(i, &u0, &v0, &u1, &v1);
            float preview_w = arrow_tile;
            float preview_h = arrow_tile / arrow_style_aspect(i);
            ImTextureRef arrow_tex = {NULL, (ImTextureID)usr->r->arrow_atlas_ds};
            picked = igImageButton(arrow_id, arrow_tex,
                                   (ImVec2){preview_w, preview_h},
                                   (ImVec2){u0, v0}, (ImVec2){u1, v1},
                                   (ImVec4){0, 0, 0, 0}, (ImVec4){1, 1, 1, 1});
          } else {
            picked = igButton(ARROW_STYLE_NAMES[i],
                              (ImVec2){arrow_tile, arrow_tile});
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

static void tab_buttons(tenv* env, user_settings* usrs, bool joystick_mode,
                        float frame_height) {
  if (!igBeginTable("button_cards", card_cols(), ImGuiTableFlags_None,
                    (ImVec2){0.0f, 0.0f}, 0.0f))
    return;

  card_next();
  igSeparatorText("Boost button");
  if (form_begin("boost_tbl")) {
    form_check("Custom position", "##boost custom", &usrs->boost_pos_custom, NULL);
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
    igTextDisabled("Joystick ring is only used with the Joystick scheme.");
  igBeginDisabled(!joystick_mode);
  if (form_begin("joy_tbl")) {
    form_check("Custom position", "##joy custom", &usrs->joy_pos_custom, NULL);
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
  igSeparatorText("Keyboard buttons");
  igTextWrapped("Create and arrange only the keyboard buttons you need.");
  igSpacing();
  if (igButton("Adjust keyboard buttons", (ImVec2){-1, frame_height * 1.7f})) {
    save_user_settings(usrs);
    ui_key_buttons_open_editor(env);
  }

  igEndTable();
}

static void tab_zoom(user_settings* usrs) {
  if (!igBeginTable("zoom_cards", card_cols(), ImGuiTableFlags_None,
                    (ImVec2){0.0f, 0.0f}, 0.0f))
    return;

  card_next();
  igSeparatorText("Zoom slider");
  if (form_begin("zoom_tbl")) {
    form_slider("X position", "##zoom x", &usrs->zslider_rel_x, 0.02f, 0.98f, "%.2f");
    form_slider("Y position", "##zoom y", &usrs->zslider_rel_y, 0.10f, 0.90f, "%.2f");
    form_slider("Height", "##zoom h", &usrs->zslider_rel_h, 0.08f, 0.48f, "%.2f");
    form_slider("Opacity", "##zoom opacity", &usrs->zslider_opacity, 0.0f, 1.0f, "%.2f");
    form_check("Horizontal", "##zoom horizontal", &usrs->zslider_horizontal, NULL);
    form_check("Hide zoom bar", "##zoom hidden", &usrs->zslider_hidden, NULL);
    igEndTable();
  }
  if (igButton("Reset zoom slider", (ImVec2){-1, 0.0f})) {
    usrs->zslider_rel_x = 0.968f;
    usrs->zslider_rel_y = 0.500f;
    usrs->zslider_rel_h = 0.280f;
    usrs->zslider_opacity = 1.0f;
    usrs->zslider_horizontal = false;
    usrs->zslider_hidden = false;
  }

  card_next();
  igSeparatorText("Zoom behaviour");
  if (form_begin("zoom_beh_tbl")) {
    form_slider("Speed", "##zoom speed", &usrs->zoom_sensitivity, 0.2f, 3.0f, "%.1f");
    form_slider("Zoom step", "##zoom step", &usrs->zoom_step, 0.05f, 0.5f, "%.2f");
    form_check("Smooth zoom", "##smooth zoom", &usrs->smooth_zoom, NULL);
    igEndTable();
  }
  igTextDisabled("Zoom step also applies to the mouse wheel and keys.");

  igEndTable();
}

#ifdef ANDROID
static void tab_extras(user_settings* usrs, float frame_height) {
  (void)usrs;
  igSeparatorText("Screen recording");
  igTextWrapped(
      "A small record button also appears in-game. Clips are saved "
      "in the app -- use Save in the Clips gallery to also add one "
      "to your phone's Gallery.");
  igSpacing();
  bool recording_now = recorder_is_recording();
  if (recording_now) {
    igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.647f, 0.176f, 0.176f, 1.0f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0.75f, 0.22f, 0.22f, 1.0f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0.55f, 0.14f, 0.14f, 1.0f});
  } else {
    igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.16f, 0.55f, 0.30f, 1.0f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0.20f, 0.65f, 0.36f, 1.0f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0.13f, 0.45f, 0.25f, 1.0f});
  }
  if (igButton(recording_now ? "Stop Recording" : "Start Recording",
               (ImVec2){-1, frame_height * 1.7f})) {
    recorder_toggle();
  }
  igPopStyleColor(3);
}
#endif

/* ------------------------------------------------------------------ */

void ui_controls(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  user_settings* usrs = &usr->usrs;
  ImGuiStyle* style = igGetStyle();
  game_data* gdata = &usr->gdata;

  igPushFont(usr->imgui_data.regular_font[usrs->ui_font_size],
             usr->imgui_data.regular_font[usrs->ui_font_size]->LegacySize);

  float frame_height = igGetFrameHeight();

#ifdef ANDROID
  const int tab_count = TAB_COUNT;
  const bool arrow_mode = usrs->ctrl_mode_trackpad;
  const bool joystick_mode = !usrs->ctrl_mode_trackpad;
#else
  /* PC port: no scheme switch yet, so nothing is dimmed. */
  const int tab_count = TAB_COUNT - 1;
  const bool arrow_mode = true;
  const bool joystick_mode = true;
#endif

  /* Centered translucent panel so the player can still see the live
     gameplay/control preview behind it. */
  float panel_w = ctx->size[0] * 0.86f;
  float panel_h = ctx->size[1] * 0.82f;
#ifdef ANDROID
  if (panel_w > 1180.0f) panel_w = 1180.0f;
  if (panel_h > ctx->size[1] - 36.0f) panel_h = ctx->size[1] - 36.0f;
#else
  if (panel_w > 1320.0f) panel_w = 1320.0f;
  if (panel_h > 920.0f) panel_h = 920.0f;
#endif
  if (panel_w < 320.0f) panel_w = ctx->size[0] - 20.0f;
  if (panel_h < 320.0f) panel_h = ctx->size[1] - 20.0f;
  float panel_x = (ctx->size[0] - panel_w) * 0.5f;
  float panel_y = (ctx->size[1] - panel_h) * 0.5f;

  igSetCursorPos((ImVec2){panel_x, panel_y});
  igPushStyleVar_Float(ImGuiStyleVar_ChildRounding, 18.0f);
  igPushStyleVar_Float(ImGuiStyleVar_ChildBorderSize, 1.0f);
  igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){18.0f, 16.0f});
  igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, 8.0f);
  igPushStyleColor_Vec4(ImGuiCol_ChildBg, (ImVec4){0.03f, 0.06f, 0.10f, 0.58f});
  igPushStyleColor_Vec4(ImGuiCol_Border, (ImVec4){0.19f, 0.40f, 0.78f, 0.38f});
  igPushStyleColor_Vec4(ImGuiCol_Separator, (ImVec4){0.28f, 0.42f, 0.63f, 0.38f});

  if (igBeginChild_Str("controls_panel_root", (ImVec2){panel_w, panel_h},
                       ImGuiChildFlags_Borders,
                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav)) {
    igText("Controls");

    float footer_h = frame_height * 1.8f + style->ItemSpacing.y + 10.0f;

    if (igBeginTabBar("controls_tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
      for (int t = 0; t < tab_count; ++t) {
        if (!igBeginTabItem(TAB_NAMES[t], NULL, ImGuiTabItemFlags_None))
          continue;
        s_tab = t;

        if (igBeginChild_Str("controls_scroll_area", (ImVec2){0.0f, -footer_h},
                             ImGuiChildFlags_None,
                             ImGuiWindowFlags_NoBackground)) {
#ifdef ANDROID
          ui_touch_scroll(env, 10);
#endif
          switch (t) {
            case TAB_SCHEME:
              tab_scheme(usrs, style, arrow_mode);
              break;
            case TAB_AIM:
              tab_aim(usr, usrs, style, arrow_mode);
              break;
            case TAB_BUTTONS:
              tab_buttons(env, usrs, joystick_mode, frame_height);
              break;
            case TAB_ZOOM:
              tab_zoom(usrs);
              break;
#ifdef ANDROID
            case TAB_EXTRAS:
              tab_extras(usrs, frame_height);
              break;
#endif
            default:
              break;
          }
        }
        igEndChild();
        igEndTabItem();
      }
      igEndTabBar();
    }

    igSeparator();
    {
      ImVec2 avail;
      igGetContentRegionAvail(&avail);
      float btn_gap = style->ItemSpacing.x;
      float btn_w = (avail.x - btn_gap) * 0.5f;
      if (btn_w < 120.0f) btn_w = (avail.x > 0.0f) ? avail.x : 120.0f;
      float btn_h = frame_height * 1.8f;

      if (igButton("Reset this tab", (ImVec2){btn_w, btn_h})) {
        reset_tab(usrs, s_tab);
      }
      igSameLine(0, btn_gap);
      if (igButton("OK", (ImVec2){btn_w, btn_h})) {
        save_user_settings(usrs);
        gdata->curr_screen = TITLE_SCREEN;
      }
    }
  }
  igEndChild();

  igPopStyleColor(3);
  igPopStyleVar(4);
  igPopFont();
}

void ui_controls_destroy(tenv* env) {}
