#include "settings.h"

#include "hud_layout_editor.h"

#include "../user.h"
#include "../android_glfw_shim.h"
#include "crystal_theme.h"

void ui_settings_init(tenv* env) {}

void ui_settings(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  user_settings* usrs = &usr->usrs;
  ImGuiStyle* style = igGetStyle();
  ImGuiIO* io = igGetIO_Nil();
  game_data* gdata = &usr->gdata;
  /* Popup mode: opened mid-match with the Open settings hotkey. It is drawn
     as a compact, touchable window (tabs instead of 4 columns, small font,
     small buttons) that sits inside the area reserved for it on screen. */
  const bool popup = gdata->settings_popup;
  static int popup_tab = 0;

  int base_font = popup ? FONT_SIZE_SMALL : usrs->ui_font_size;
  igPushFont(usr->imgui_data.regular_font[base_font],
             usr->imgui_data.regular_font[base_font]->LegacySize);

  if (!gdata->settings_popup) {
    usr->r->global.bg_opacity = 0;
    usr->r->global.bd_opacity = 0;
    usr->r->global.minimap_opacity = 0;
  }

  if (popup) {
    /* Popup rect: centered box, ~36% x 65% of the screen. */
    float pw = ctx->size[0] * 0.363f;
    float ph = ctx->size[1] * 0.650f;
    igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 14.0f);
    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){10, 8});
    igPushStyleVar_Vec2(ImGuiStyleVar_FramePadding, (ImVec2){6, 3});
    igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, (ImVec2){6, 4});
    /* 10% background so the live game stays visible behind it. */
    igPushStyleColor_Vec4(ImGuiCol_WindowBg,
                          (ImVec4){0.086f, 0.063f, 0.145f, 0.10f});
    igPushStyleColor_Vec4(ImGuiCol_Border,
                          (ImVec4){0.690f, 0.580f, 0.960f, 0.55f});
    igSetNextWindowPos((ImVec2){ctx->size[0] * 0.4925f, ctx->size[1] * 0.523f},
                       ImGuiCond_Always, (ImVec2){0.5f, 0.5f});
    igSetNextWindowSize((ImVec2){pw, ph}, ImGuiCond_Always);
    igBegin("##settings_popup", NULL,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse);
#ifdef ANDROID
    /* Without a capture rect, touches here are treated as gameplay touches
       and never reach ImGui (same reason as the death screen). */
    {
      ImVec2 wp, wsz;
      igGetWindowPos(&wp);
      igGetWindowSize(&wsz);
      android_ui_capture_rect(wp.x, wp.y, wp.x + wsz.x, wp.y + wsz.y);
    }
#endif
  } else {
    crystal_draw_background_alpha(env, 1.0f);
  }
  crystal_push_theme();

  float frame_height = igGetFrameHeight();
  float child_window_height =
      ctx->size[1] - style->WindowPadding.y * 4 - frame_height;
#ifdef ANDROID
  int panel_columns = 2;
  child_window_height = (child_window_height - style->ItemSpacing.y) * 0.5f;
#else
  int panel_columns = 4;
#endif
  float popup_btn_h = frame_height * 1.2f;
  if (popup) {
    panel_columns = 1;
    /* Tab row: General / Normal / Assist / Hotkeys */
    const char* tab_names[4] = {"General", "Normal", "Assist", "Hotkeys"};
    ImVec2 av;
    igGetContentRegionAvail(&av);
    float tw = (av.x - style->ItemSpacing.x * 3) / 4.0f;
    for (int t = 0; t < 4; t++) {
      if (t) igSameLine(0, -1);
      bool sel = (t == popup_tab);
      if (sel)
        igPushStyleColor_Vec4(ImGuiCol_Button,
                              (ImVec4){0.510f, 0.294f, 0.910f, 0.85f});
      if (igButton(tab_names[t], (ImVec2){tw, 0})) popup_tab = t;
      if (sel) igPopStyleColor(1);
    }
    igGetContentRegionAvail(&av);
    child_window_height =
        av.y - popup_btn_h - style->ItemSpacing.y * 2 - style->WindowPadding.y;
  }

  if (igBeginTable("settings_table", panel_columns, ImGuiTableFlags_None, (ImVec2){}, 0)) {
    igTableNextRow(ImGuiTableRowFlags_None, 0);
    igTableSetColumnIndex(0);

    if (!popup || popup_tab == 0) {
    igBeginChild_Str("general_settings_child_holder",
                     (ImVec2){-1, child_window_height}, ImGuiChildFlags_None,
                     ImGuiWindowFlags_None);
    igSeparatorText("General");
    if (igBeginTable("field:value", 2, ImGuiTableFlags_None, (ImVec2){}, 0)) {
      igTableNextRow(ImGuiTableRowFlags_None, 0);
      igTableSetColumnIndex(0);
      igIndent(style->WindowPadding.x);
      igAlignTextToFramePadding();
      igText("VSync");
      igAlignTextToFramePadding();
      igText("FPS limit");
      igAlignTextToFramePadding();
      igText("Performance mode");
      igAlignTextToFramePadding();
      igText("Cursor size");
      igAlignTextToFramePadding();
      igText("UI font size");
      igAlignTextToFramePadding();
      igText("Stats font size");
      igAlignTextToFramePadding();
      igText("Leaderboard font size");
      igAlignTextToFramePadding();
      igText("Names font size");
      igAlignTextToFramePadding();
      igText("Show snake scores");
      igAlignTextToFramePadding();
      igText("Smooth zoom");
      igAlignTextToFramePadding();
      igText("Ping / FPS graph");
      igAlignTextToFramePadding();
      igText("Zoom step");
      igAlignTextToFramePadding();
      igText("Border color");
      igAlignTextToFramePadding();
      igText("Adjust HUD Layout");
      igAlignTextToFramePadding();
      igText("Instant restart");
      igAlignTextToFramePadding();
      igText("Restart with right click");
      igAlignTextToFramePadding();
      igText("Quit with middle click");
      igAlignTextToFramePadding();
      igText("Laser color");
      igAlignTextToFramePadding();
      igText("Laser thickness");
      igAlignTextToFramePadding();
      igText("Bot circle after score");
      igAlignTextToFramePadding();
      igText("Bot radius multiplier");

      igTableSetColumnIndex(1);
      if (igCheckbox("##vsync", &usrs->vsync)) {
        env->config.vsync = usrs->vsync;
        twindow_request_refresh(env->wnd);
      }
      int fps_index = 0;
      const int fps_values[] = {0, 60, 90, 120, 144};
      for (int fi = 0; fi < 5; ++fi)
        if (usrs->fps_limit == fps_values[fi]) fps_index = fi;
      igSetNextItemWidth(-1);
      if (igCombo_Str_arr("##fps limit", &fps_index,
                          (const char*[]){"Device/VSync", "60 FPS", "90 FPS",
                                          "120 FPS", "144 FPS"}, 5, -1))
        usrs->fps_limit = fps_values[fps_index];
      igCheckbox("##performance mode", &usrs->performance_mode);
      igSetNextItemWidth(-1);
      igSliderInt("##cursor size", &usrs->cursor_size, 16, 64, "%d px",
                  ImGuiSliderFlags_AlwaysClamp);
      igSetNextItemWidth(-1);
      igCombo_Str_arr("##ui font size", (int*)&usrs->ui_font_size,
                      (const char*[]){"Small", "Regular", "Large"}, 3, -1);
      igSetNextItemWidth(-1);
      igCombo_Str_arr("##stats font size", (int*)&usrs->stats_font_size,
                      (const char*[]){"Small", "Regular", "Large"}, 3, -1);
      igSetNextItemWidth(-1);
      igCombo_Str_arr("##leaderboard font size", (int*)&usrs->lb_font_size,
                      (const char*[]){"Small", "Regular", "Large"}, 3, -1);
      igSetNextItemWidth(-1);
      igCombo_Str_arr("##snake name font size",
                      (int*)&usrs->snake_names_font_size,
                      (const char*[]){"Small", "Regular", "Large"}, 3, -1);
      igCheckbox("##snake scores", &usrs->snake_scores);
      igCheckbox("##smooth zoom", &usrs->smooth_zoom);
      igCheckbox("##net graph", &usrs->show_net_graph);
      igSetNextItemWidth(-1);
      igSliderFloat("##zoom step", &usrs->zoom_step, 0.05f, 0.5f, "%.2f",
                    ImGuiSliderFlags_AlwaysClamp);
      igSetNextItemWidth(-1);
      igColorEdit3("##border color", usrs->bd_color, ImGuiColorEditFlags_None);
      igBeginDisabled(popup); /* leaves the match, so not from the popup */
      if (igButton("Open editor##hud_layout", (ImVec2){-1, 0})) {
        ui_hud_layout_editor_enter(env);
      }
      igEndDisabled();
      igCheckbox("##instant restart", &usrs->instant_restart);
      igCheckbox("##restart rc", &usrs->restart_rc);
      igCheckbox("##quit mc", &usrs->quit_mc);
      igSetNextItemWidth(-1);
      igColorEdit4("##laser color", usrs->laser_color,
                   ImGuiColorEditFlags_AlphaBar);
      igSetNextItemWidth(-1);
      igSliderInt("##laser thickness", &usrs->laser_thickness, 1, 4, "%d px",
                  ImGuiSliderFlags_AlwaysClamp);
                  igSetNextItemWidth(-1);
      igSliderInt("##circle after", &usrs->bot_follow_circle_score, 1000, 6000, "%d",
                  ImGuiSliderFlags_AlwaysClamp);
                  igSetNextItemWidth(-1);
      igSliderInt("##rad mult", &usrs->bot_radius_mult, 10, 40, "%dx",
                  ImGuiSliderFlags_AlwaysClamp);
      igIndent(-style->WindowPadding.x);
      igEndTable();
    }
    igSpacing();
    igTextWrapped("FPS limit is a maximum, not a forced refresh rate. Actual FPS cannot exceed your phone's active display refresh rate. Android Auto mode may keep the screen at 60 Hz; select 90/120/144 Hz in the phone's Display settings to use a matching Vlither limit.");
    igTextDisabled("VSync can also cap rendering to the current display mode.");
    igEndChild();
    }

    if (!popup || popup_tab == 1 || popup_tab == 2) {
    if (popup) {
      igTableNextRow(ImGuiTableRowFlags_None, 0);
      igTableSetColumnIndex(0);
    } else {
      igTableSetColumnIndex(1);
    }
    igBeginChild_Str("mode_settings_child_holder",
                     (ImVec2){-1, child_window_height}, ImGuiChildFlags_None,
                     ImGuiWindowFlags_None);
    for (int i = 0; i < 2; i++) {
      if (popup && i != popup_tab - 1) continue;
      igPushID_Int(i + 1);
      gameplay_mode* mode = usrs->modes + i;
      igSeparatorText(i == 0 ? "Normal mode" : "Assist mode");

      if (igBeginTable("field:value", 2, ImGuiTableFlags_None, (ImVec2){}, 0)) {
        igTableNextRow(ImGuiTableRowFlags_None, 0);
        igTableSetColumnIndex(0);
        igIndent(style->WindowPadding.x);
        igAlignTextToFramePadding();
        igText("Show crosshair");
        igAlignTextToFramePadding();
        igText("Show background");
        igAlignTextToFramePadding();
        igText("Show accessories");
        igAlignTextToFramePadding();
        igText("Show shadows");
        igAlignTextToFramePadding();
        igText("Death effect");
        igAlignTextToFramePadding();
        igText("Outline player names");
        igAlignTextToFramePadding();
        igText("Segment separation");
        igAlignTextToFramePadding();
        igText("Background scale");
        igAlignTextToFramePadding();
        igText("Render mode");
        igAlignTextToFramePadding();
        igText("Transparent skin");
        igAlignTextToFramePadding();
        igText("Skin opacity");
        igAlignTextToFramePadding();
        igText("Center line (your snake)");
        if (i == 1) {
          igAlignTextToFramePadding();
          igText("White skin (enemies)");
        }
        igAlignTextToFramePadding();
        igText("Boost effect");
        igAlignTextToFramePadding();
        igText("Boost effect strength");
        igAlignTextToFramePadding();
        igText("Food shader");
        igAlignTextToFramePadding();
        igText("Food glow");
        igAlignTextToFramePadding();
        igText("Food scale");
        igAlignTextToFramePadding();
        igText("Food float");
        igAlignTextToFramePadding();
        igText("Food flicker");
        igAlignTextToFramePadding();
        igText("Uniform food color");

        igTableSetColumnIndex(1);
        igCheckbox("##crosshair", &mode->show_crosshair);
        igCheckbox("##bg", &mode->show_background);
        igCheckbox("##acc", &mode->show_accessories);
        igCheckbox("##shad", &mode->show_shadows);
        igCheckbox("##death effect", &mode->death_effect);
        igCheckbox("##player names outline", &mode->player_names_outline);
        igSetNextItemWidth(-1);
        igSliderFloat("##bps", &mode->qsm, 1, 4, "%.2f",
                      ImGuiSliderFlags_AlwaysClamp);
        igSetNextItemWidth(-1);
        igSliderFloat("##bgs", &mode->bg_scale, 0.05, 4, "%.2fx",
                      ImGuiSliderFlags_AlwaysClamp);
        igSetNextItemWidth(-1);
        igCombo_Str_arr("##render mode", &mode->render_mode,
                        (const char*[]){"Texture", "Solid", "Flat"}, 3, -1);

        igCheckbox("##transparent skin", &mode->transparent_skin);
        igBeginDisabled(!mode->transparent_skin);
        int opacity_percent =
            (int)(usrs->transparent_skin_opacity[i] * 100.0f + 0.5f);
        igSetNextItemWidth(-1);
        /* Assist mode's Flat render mode has no per-segment pattern to
           misalign at zero opacity (see redraw.c's flatten comment), so
           its floor is relaxed to 0% instead of the usual 15% minimum. */
        int opacity_min = (i == 1 && mode->render_mode == 2) ? 0 : 15;
        if (igSliderInt("##skin opacity", &opacity_percent, opacity_min, 85,
                        "%d%%", ImGuiSliderFlags_AlwaysClamp))
          usrs->transparent_skin_opacity[i] = opacity_percent / 100.0f;
        igEndDisabled();
        igCheckbox("##center line", &mode->center_line);
        if (i == 1) igCheckbox("##white skin enemies", &usrs->white_skin_enemies[1]);

        igCheckbox("##boost", &mode->show_boost);
        igSameLine(0, -1);
        igBeginDisabled(!mode->show_boost);
        igSetNextItemWidth(-1);
        igCombo_Str_arr("##boost type", &mode->boost_type,
                        (const char*[]){"Normal", "Simple"}, 2, -1);
        igSetNextItemWidth(-1);
        igSliderFloat("##boost strength", &mode->boost_strength, 0.25f, 3,
                      "%.2fx", ImGuiSliderFlags_AlwaysClamp);
        igEndDisabled();
        igSetNextItemWidth(-1);
        igCombo_Str_arr("##food type", &mode->food_type,
                        (const char*[]){"Solid", "Rings", "Hollow square",
                                        "Star outline", "Star solid",
                                        "Hollow triangle", "Asterisk",
                                        "Sparkle"},
                        8, 8);
        igCheckbox("##food glow", &usrs->food_glow[i]);
        igSetNextItemWidth(-1);
        igSliderFloat("##food scale", &mode->food_scale, 0.25f, 3, "%.2f",
                      ImGuiSliderFlags_AlwaysClamp);
        igCheckbox("##food float", &mode->food_float);
        igCheckbox("##food flicker", &mode->food_flicker);
        igCheckbox("##uniform food color", &mode->uniform_food_color);
        igSameLine(0, -1);
        igBeginDisabled(!mode->uniform_food_color);
        igSetNextItemWidth(-1);
        igColorEdit3("##fdcolor", mode->food_color, ImGuiColorEditFlags_None);
        igEndDisabled();
        igIndent(-style->WindowPadding.x);

        igEndTable();
      }
      igPopID();
    }
    igEndChild();
    }

    if (!popup || popup_tab == 3) {
    if (popup) {
      igTableNextRow(ImGuiTableRowFlags_None, 0);
      igTableSetColumnIndex(0);
    } else {
#ifdef ANDROID
    igTableNextRow(ImGuiTableRowFlags_None, 0);
    igTableSetColumnIndex(0);
#else
    igTableSetColumnIndex(2);
#endif
    }
    igBeginChild_Str("hotkey_child_window", (ImVec2){-1, child_window_height},
                     ImGuiChildFlags_None, ImGuiWindowFlags_None);
    igSeparatorText("Hotkeys");
    if (igBeginTable("field:value", 2, ImGuiTableFlags_None, (ImVec2){}, 0)) {
      igTableNextRow(ImGuiTableRowFlags_None, 0);
      igTableSetColumnIndex(0);
      igIndent(style->WindowPadding.x);
      for (int i = 0; i < NUM_HOTKEYS; i++) {
        hotkey* hk = usr_hotkey(usrs, i);
        igAlignTextToFramePadding();
        igText(hk->description);
      }
      igTableSetColumnIndex(1);

      for (int i = 0; i < NUM_HOTKEYS; i++) {
        hotkey* hk = usr_hotkey(usrs, i);
        igPushID_Int(i);
        igSetNextItemWidth(frame_height * 2);
        char preview_char[2] = {(char)hk->key, 0};
        if (igBeginCombo("##hotkey code", preview_char, ImGuiComboFlags_None)) {
          for (int c = 48; c < 58; c++) {
            char selectable_char[2] = {c, 0};
            bool is_in_use = false;
            for (int d = 0; d < NUM_HOTKEYS; d++) {
              if (c == usr_hotkey(usrs, d)->key &&
                  hk->key != usr_hotkey(usrs, d)->key) {
                is_in_use = true;
              }
            }
            if (igSelectable_Bool(selectable_char, c == hk->key,
                                  is_in_use ? ImGuiSelectableFlags_Disabled
                                            : ImGuiSelectableFlags_None,
                                  (ImVec2){})) {
              hk->key = c;
            }
          }
          for (int c = 65; c < 91; c++) {
            char selectable_char[2] = {c, 0};
            bool is_in_use = false;
            for (int d = 0; d < NUM_HOTKEYS; d++) {
              if (c == usr_hotkey(usrs, d)->key &&
                  hk->key != usr_hotkey(usrs, d)->key) {
                is_in_use = true;
              }
            }
            is_in_use = is_in_use || c == GLFW_KEY_M || c == GLFW_KEY_N;
            if (igSelectable_Bool(selectable_char, c == hk->key,
                                  is_in_use ? ImGuiSelectableFlags_Disabled
                                            : ImGuiSelectableFlags_None,
                                  (ImVec2){})) {
              hk->key = c;
            }
          }
          igEndCombo();
        }
        igSameLine(0, -1);
        ImVec2 rest;
        igGetContentRegionAvail(&rest);
        igSetNextItemWidth(rest.x - style->ItemInnerSpacing.x);
        if (i == HOTKEY_RESTART || i == HOTKEY_QUIT) {
          igBeginDisabled(true);
          igCombo_Str_arr("##hotkey mode", &(int){0}, (const char*[]){"Toggle"},
                          1, -1);
          igEndDisabled();
        } else {
          igCombo_Str_arr("##hotkey mode", &hk->mode,
                          (const char*[]){"Toggle", "Press and hold"}, 2, -1);
        }
        igPopID();
      }
      igIndent(-style->WindowPadding.x);

      igEndTable();
    }
    igEndChild();
    }

    if (!popup) {
#ifdef ANDROID
    igTableSetColumnIndex(1);
#else
    igTableSetColumnIndex(3);
#endif
    igBeginChild_Str("empty_col", (ImVec2){-1, child_window_height},
                     ImGuiChildFlags_None, ImGuiWindowFlags_None);
    igSeparatorText("Hotkeys");
    igEndChild();
    }

    igEndTable();
  }

  float btn_w = ctx->size[0] * 0.25f - style->ItemSpacing.x * 2;
  float btn_h = frame_height * 1.8f;
  float col2_x = ctx->size[0] * 0.5f + style->WindowPadding.x;
  float reset_y = ctx->size[1] - style->WindowPadding.y - btn_h * 2 - style->ItemSpacing.y;
  float ok_x = col2_x;
  float ok_y = ctx->size[1] - style->WindowPadding.y - btn_h;
  if (popup) {
    /* Reset and OK side by side along the bottom edge of the popup. */
    ImVec2 ws;
    igGetWindowSize(&ws);
    btn_h = popup_btn_h;
    btn_w = (ws.x - style->WindowPadding.x * 2 - style->ItemSpacing.x) * 0.5f;
    col2_x = style->WindowPadding.x;
    reset_y = ok_y = ws.y - style->WindowPadding.y - btn_h;
    ok_x = col2_x + btn_w + style->ItemSpacing.x;
  }
  igSetCursorPosX(col2_x);
  igSetCursorPosY(reset_y);
  if (igButton("Reset", (ImVec2){btn_w, btn_h})) {
    user_settings_default(usrs);
    env->config.vsync = usrs->vsync;
    twindow_request_refresh(env->wnd);
  }
  igSetCursorPosX(ok_x);
  igSetCursorPosY(ok_y);
  {
    ImVec2 ok_pos;
    igGetCursorScreenPos(&ok_pos);
    crystal_glow_rect(ok_pos, (ImVec2){btn_w, btn_h}, 0.647f, 0.420f, 1.0f);
  }
  igPushStyleColor_Vec4(ImGuiCol_Button,
                        (ImVec4){0.510f, 0.294f, 0.910f, 1.0f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                        (ImVec4){0.569f, 0.353f, 0.960f, 1.0f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonActive,
                        (ImVec4){0.450f, 0.243f, 0.850f, 1.0f});
  if (igButton("OK", (ImVec2){btn_w, btn_h})) {
    save_user_settings(usrs);
    if (gdata->settings_popup) {
      /* Opened mid-match via the Open settings hotkey: just close the popup
         and return to the game instead of dropping to the lobby. */
      gdata->settings_popup = false;
      usr_hotkey(usrs, HOTKEY_OPEN_SETTINGS)->active = false;
    } else {
      gdata->curr_screen = TITLE_SCREEN;
    }
  }
  crystal_sheen();
  igPopStyleColor(3);

  crystal_pop_theme();

  if (popup) {
    igEnd();
    igPopStyleColor(2);
    igPopStyleVar(4);
  }

  igPopFont();
}

void ui_settings_destroy(tenv* env) {}
