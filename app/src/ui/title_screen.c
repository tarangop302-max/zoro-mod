#include "title_screen.h"
#include <math.h>
#ifdef ANDROID
#include "../android_glfw_shim.h"
#include "../android_jni.h"
#endif

#include "../network/server.h"
#include "../user.h"
#include "../game/screenshot.h"
#include "../game/snakey_rain.h"
#include "crystal_theme.h"
#include "kills_gallery.h"
#include "clips_gallery.h"

bool g_sl_popup_open = false;

/* The crystal/purple glass theme helpers (crystal_draw_background,
   crystal_push_theme/pop_theme, crystal_sheen, crystal_glow_rect) used to
   live only in this file. They've moved to crystal_theme.c/.h so the skin
   editor and other screens can share the exact same look. */

void ui_title_screen_init(tenv* env) {}

/* Snakey Rain: small ON/OFF button under Quit (top-right) that opens a
   settings popup. Every change is saved and applied live -- no restart. */
static void draw_snakey_rain_homepage(tenv* env, float frame_height) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  user_settings* usrs = &usr->usrs;
  ImGuiStyle* style = igGetStyle();

  const float btn_w = 190.0f;
  float btn_x = ctx->size[0] - btn_w - style->WindowPadding.x * 4;
  /* Quit sits at WindowPadding.y * 4 with height frame_height; go below it. */
  float btn_y = style->WindowPadding.y * 4 + frame_height + style->ItemSpacing.y * 2;

  char label[64];
  snprintf(label, sizeof label, "Snakey Rain: %s##snakey_rain_home",
           usrs->snakey_rain_enabled ? "ON" : "OFF");

  crystal_push_theme();

  igSetCursorPosX(btn_x);
  igSetCursorPosY(btn_y);
  if (igButton(label, (ImVec2){btn_w, frame_height}))
    igOpenPopup_Str("Snakey Rain Settings", 0);
  crystal_sheen();

  float popup_w = fminf(390.0f, ctx->size[0] - 24.0f);
  igSetNextWindowSize((ImVec2){popup_w, 0.0f}, ImGuiCond_Appearing);
  if (igBeginPopup("Snakey Rain Settings", ImGuiWindowFlags_NoSavedSettings)) {
    igPushFont(usr->imgui_data.regular_font[usrs->ui_font_size],
               usr->imgui_data.regular_font[usrs->ui_font_size]->LegacySize);

    igSeparatorText("Snakey Rain");
    bool enabled = usrs->snakey_rain_enabled;
    if (igCheckbox("Enable Snakey Rain", &enabled)) {
      usrs->snakey_rain_enabled = enabled;
      save_user_settings(usrs);
      snakey_rain_apply_settings(env);
    }
    igSpacing();

    igBeginDisabled(!usrs->snakey_rain_enabled);

    igSetNextItemWidth(-1.0f);
    igInputTextWithHint("##snakey_username", "Snakey Rain username",
                        usrs->snakey_rain_username,
                        sizeof usrs->snakey_rain_username,
                        ImGuiInputTextFlags_None, NULL, NULL);
    if (igIsItemDeactivatedAfterEdit()) {
      save_user_settings(usrs);
      snakey_rain_apply_settings(env);
    }

    igSetNextItemWidth(-1.0f);
    igInputTextWithHint("##snakey_password", "Snakey Rain password",
                        usrs->snakey_rain_password,
                        sizeof usrs->snakey_rain_password,
                        ImGuiInputTextFlags_Password, NULL, NULL);
    if (igIsItemDeactivatedAfterEdit()) {
      save_user_settings(usrs);
      snakey_rain_apply_settings(env);
    }

    igText("Maximum bots");
    igSetNextItemWidth(-1.0f);
    igSliderInt("##snakey_max_bots", &usrs->snakey_rain_max_bots, 1, 1000,
                "%d", ImGuiSliderFlags_AlwaysClamp);
    if (igIsItemDeactivatedAfterEdit()) {
      save_user_settings(usrs);
      snakey_rain_apply_settings(env);
    }

    igText("Bot in-game name (max 24)");
    igSetNextItemWidth(-1.0f);
    igInputTextWithHint("##snakey_bot_name", "Name shown on bots",
                        usrs->snakey_rain_bot_name,
                        sizeof usrs->snakey_rain_bot_name,
                        ImGuiInputTextFlags_None, NULL, NULL);
    if (igIsItemDeactivatedAfterEdit()) {
      save_user_settings(usrs);
      snakey_rain_apply_settings(env);
    }

    igText("Bot skin code");
    igSetNextItemWidth(-1.0f);
    igInputTextWithHint("##snakey_bot_skin", "Skin string (same as extension)",
                        usrs->snakey_rain_bot_skin,
                        sizeof usrs->snakey_rain_bot_skin,
                        ImGuiInputTextFlags_None, NULL, NULL);
    if (igIsItemDeactivatedAfterEdit()) {
      save_user_settings(usrs);
      snakey_rain_apply_settings(env);
    }

    igEndDisabled();

    igSpacing();
    igTextWrapped("Credentials, bot name/skin, and the selected game server "
                  "are sent to snakeyrain.com when bots start.");
    if (snakey_rain_enabled_at_start()) {
      igTextColored((ImVec4){0.35f, 1.0f, 0.55f, 1.0f}, "Active now.");
    } else if (usrs->snakey_rain_enabled) {
      igTextColored((ImVec4){1.0f, 0.85f, 0.35f, 1.0f},
                    "Enabled - will connect when you play.");
    }

    ImVec2 avail;
    igGetContentRegionAvail(&avail);
    if (igButton("Close", (ImVec2){avail.x, 0.0f})) igCloseCurrentPopup();

    igPopFont();
    igEndPopup();
  }

  crystal_pop_theme();
}


void ui_title_screen(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  user_settings* usrs = &usr->usrs;
  ImGuiStyle* style = igGetStyle();
  ImGuiIO* io = igGetIO_Nil();
  game_data* gdata = &usr->gdata;

  igPushFont(usr->imgui_data.regular_font[usrs->ui_font_size],
             usr->imgui_data.regular_font[usrs->ui_font_size]->LegacySize);

  usr->r->global.bg_opacity = 0;
  usr->r->global.bd_opacity = 0;
  usr->r->global.minimap_opacity = 0;

  crystal_draw_background(env);
  crystal_push_theme();

  float frame_height = igGetFrameHeight();

  float logo_size = 400;
  float logo_gap = 5;

  igPushFont(usr->imgui_data.mono_font[usrs->ui_font_size],
             usr->imgui_data.mono_font[usrs->ui_font_size]->LegacySize);

  /* Lobby-only stats block, bottom-left corner. Stacked upward from the
     bottom edge so a 4th line just means one more row -- no other offsets
     to touch. Order (bottom to top): best length, timer, kills, score. */
  float stats_margin_x = style->WindowPadding.x;
  float stats_margin_y = style->WindowPadding.y;
  float stats_row = igGetFrameHeight() - style->ItemSpacing.x;

  int tot_sec = (int)usrs->play_time;
  int hours = tot_sec / 3600;
  int minutes = (tot_sec % 3600) / 60;
  int seconds = tot_sec % 60;

  igSetCursorPosX(stats_margin_x);
  igSetCursorPosY(ctx->size[1] - stats_margin_y - stats_row * 4);
  igTextColored((ImVec4){0.85f, 0.80f, 1.0f, 0.55f}, "\ue99e");
  igSameLine(0, -1);
  igTextColored((ImVec4){0.85f, 0.80f, 1.0f, 0.55f}, "%d", usrs->score);

  igSetCursorPosX(stats_margin_x);
  igSetCursorPosY(ctx->size[1] - stats_margin_y - stats_row * 3);
  igTextColored((ImVec4){0.85f, 0.80f, 1.0f, 0.55f}, "\ueaeb");
  igSameLine(0, -1);
  igTextColored((ImVec4){0.85f, 0.80f, 1.0f, 0.55f}, "%d", usrs->kills);

  igSetCursorPosX(stats_margin_x);
  igSetCursorPosY(ctx->size[1] - stats_margin_y - stats_row * 2);
  igTextColored((ImVec4){0.85f, 0.80f, 1.0f, 0.65f}, "\ue952");
  igSameLine(0, -1);
  igTextColored((ImVec4){0.85f, 0.80f, 1.0f, 0.65f}, "%02d:%02d:%02d", hours,
                minutes, seconds);

  /* NEW: 4th line, best-ever length. Reuses the trophy glyph (no dedicated
     "length" icon exists in this atlas -- see ui_overlay.c's in-game HUD,
     which treats score/length as the same computed quantity) but in gold
     with a "Best" label so it doesn't read as a duplicate of the score
     line above. */
  igSetCursorPosX(stats_margin_x);
  igSetCursorPosY(ctx->size[1] - stats_margin_y - stats_row * 1);
  igTextColored((ImVec4){1, 0.85f, 0.4f, 0.7f}, "\ue99e");
  igSameLine(0, -1);
  igTextColored((ImVec4){1, 0.85f, 0.4f, 0.7f}, "Best %d", usrs->best_length);

  igPopFont();

  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f + style->ItemSpacing.y);
  igPushItemWidth(logo_size);
  igInputTextWithHint("##nickname_input", "Nickname", usrs->nickname,
                      MAX_NICKNAME_LEN + 1, ImGuiInputTextFlags_None, NULL,
                      NULL);
  crystal_sheen();

  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f + style->ItemSpacing.y * 2 +
                  frame_height);
  float sl_btn_w = frame_height;
  igPushItemWidth(logo_size - sl_btn_w - style->ItemSpacing.x);
  igInputTextWithHint("##ipv4_input", "IPv4:Port", usrs->ipv4, MAX_IPV4_LEN + 1,
                      ImGuiInputTextFlags_None, NULL, NULL);
  crystal_sheen();
  igPopItemWidth();
  igPopItemWidth();

  igSameLine(0, style->ItemSpacing.x);
  igPushFont(usr->imgui_data.mono_font[usrs->ui_font_size],
             usr->imgui_data.mono_font[usrs->ui_font_size]->LegacySize);
  if (igButton("\ue9c9##sl_btn", (ImVec2){sl_btn_w, sl_btn_w})) {
    if (!gdata->server_list.fetching && !gdata->server_list.fetched)
      server_list_fetch(env);
    igOpenPopup_Str("##sl_popup", 0);
  }
  crystal_sheen();
  igPopFont();

  server_list_poll(env);

  if (gdata->server_list.fetched && gdata->server_list.count > 0 &&
      !gdata->server_list.pinging && gdata->server_list.pings_done == 0) {
    server_list_start_ping(env);
  }

  g_sl_popup_open = igBeginPopup("##sl_popup", 0);
  if (g_sl_popup_open) {
    igPushFont(usr->imgui_data.regular_font[usrs->ui_font_size],
               usr->imgui_data.regular_font[usrs->ui_font_size]->LegacySize);

    if (gdata->server_list.fetching) {
      igTextColored((ImVec4){0.8f, 0.8f, 0.3f, 1.0f},
                    "Fetching official server list...");
    } else if (gdata->server_list.fetch_error) {
      igTextColored((ImVec4){0.9f, 0.4f, 0.4f, 1.0f},
                    "Couldn't fetch official list \xe2\x80\x94 custom servers still available.");
    } else if (gdata->server_list.fetched && gdata->server_list.count > 0) {
      if (gdata->server_list.pinging) {
        char prog[56];
        snprintf(prog, sizeof(prog), "Pinging... %d/%d",
                 gdata->server_list.pings_done, gdata->server_list.count);
        igTextColored((ImVec4){0.8f, 0.8f, 0.3f, 1.0f}, prog);
      } else {
        char hdr[48];
        snprintf(hdr, sizeof(hdr), "%d servers (best ping first)",
                 gdata->server_list.count);
        igTextColored((ImVec4){0.5f, 0.9f, 0.5f, 1.0f}, hdr);
      }
    }

    igSeparator();

    if (gdata->server_list.count > 0) {
      igBeginChild_Str("##sl_scroll", (ImVec2){340, 320},
                       ImGuiChildFlags_None, 0);

      bool sorted = !gdata->server_list.pinging &&
                    gdata->server_list.pings_done > 0;

      for (int j = 0; j < gdata->server_list.count; j++) {
        int i         = sorted ? gdata->server_list.sorted_order[j] : j;
        int ping      = gdata->server_list.pings[i];
        bool is_custom = i < gdata->server_list.custom_count;

        char name_buf[40];
        if (is_custom) {
          snprintf(name_buf, sizeof(name_buf), "\xe2\x98\x85 %s",
                    CUSTOM_SERVER_NAMES[i]);
        } else {
          snprintf(name_buf, sizeof(name_buf), "%s", gdata->server_list.ips[i]);
        }

        char label[80];
        if (ping < 0) {
          snprintf(label, sizeof(label), "%-26s  --", name_buf);
        } else if (ping >= 9999) {
          snprintf(label, sizeof(label), "%-26s  !!ms", name_buf);
        } else {
          snprintf(label, sizeof(label), "%-26s  %dms", name_buf, ping);
        }

        bool pushed_color = false;
        if (is_custom) {
          igPushStyleColor_Vec4(ImGuiCol_Text,
            (ImVec4){1.0f, 0.82f, 0.25f, 1.0f});
          pushed_color = true;
        } else if (ping >= 0 && ping < 9999) {
          ImVec4 col;
          if      (ping <  80) col = (ImVec4){0.3f, 1.0f, 0.4f, 1.0f};
          else if (ping < 150) col = (ImVec4){1.0f, 1.0f, 0.3f, 1.0f};
          else if (ping < 300) col = (ImVec4){1.0f, 0.65f, 0.2f, 1.0f};
          else                  col = (ImVec4){1.0f, 0.4f, 0.4f, 1.0f};
          igPushStyleColor_Vec4(ImGuiCol_Text, col);
          pushed_color = true;
        }

        if (igSelectable_Bool(label, false,
                              ImGuiSelectableFlags_None, (ImVec2){0, 0})) {
          strncpy(usrs->ipv4, gdata->server_list.ips[i], MAX_IPV4_LEN);
          usrs->ipv4[MAX_IPV4_LEN] = '\0';
          igCloseCurrentPopup();
        }
        if (is_custom && igIsItemHovered(0)) {
          igSetTooltip("%s", gdata->server_list.ips[i]);
        }

        if (pushed_color) igPopStyleColor(1);
      }
      igEndChild();
      igSeparator();
    }

    if (!gdata->server_list.fetching) {
      if (igButton("Refresh##sl_refresh", (ImVec2){0, 0}))
        server_list_fetch(env);
    }

    igPopFont();
    igEndPopup();
  }

  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f + style->ItemSpacing.y * 3 +
                  frame_height * 2);

  {
    ImVec2 play_pos;
    igGetCursorScreenPos(&play_pos);
    crystal_glow_rect(play_pos, (ImVec2){logo_size, frame_height}, 0.647f,
                      0.420f, 1.0f);
  }
  igPushStyleColor_Vec4(ImGuiCol_Button,
                        (ImVec4){0.510f, 0.294f, 0.910f, 1.0f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                        (ImVec4){0.569f, 0.353f, 0.960f, 1.0f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonActive,
                        (ImVec4){0.450f, 0.243f, 0.850f, 1.0f});
  if (igButton("\uea1c Play", (ImVec2){logo_size})) {
    usr->gdata.conn = CONNECTING;
    usr->gdata.curr_screen = PLAYING;
    glfwSetTime(0);
    screenshot_run_reset();
    server_connect(env);
  }
  crystal_sheen();
  igPopStyleColor(3);

  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f + style->ItemSpacing.y * 4 +
                  frame_height * 3);
  if (igButton("\ue90c Skin editor",
               (ImVec2){logo_size / 2 - style->ItemSpacing.x / 2}))
    usr->gdata.curr_screen = SKIN_EDITOR;
  crystal_sheen();
  igSameLine(0, -1);
  if (igButton("\ue991 Settings",
               (ImVec2){logo_size / 2 - style->ItemSpacing.x / 2})) {
    usr->gdata.curr_screen = SETTINGS;
  }
  crystal_sheen();
  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f + style->ItemSpacing.y * 5 +
                  frame_height * 4);
  if (igButton("\ue991 Controls",
               (ImVec2){logo_size / 2 - style->ItemSpacing.x / 2})) {
    usr->gdata.curr_screen = CONTROLS;
  }
  crystal_sheen();
  igSameLine(0, -1);
  if (igButton("Chat",
               (ImVec2){logo_size / 2 - style->ItemSpacing.x / 2})) {
    usr->gdata.curr_screen = NTL_PANEL;
  }
  crystal_sheen();

  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f + style->ItemSpacing.y * 6 +
                  frame_height * 5);
  /* Voice Chat: matches Vlither's panel layout. Vlither's button opens a
     full voice-chat screen backed by mic capture/network code this repo
     doesn't have yet, so for now this opens the existing team chat panel
     instead of a dead button. Swap the target screen here once/if a real
     voice backend is ported. */
  if (igButton("Voice Chat", (ImVec2){logo_size})) {
    usr->gdata.curr_screen = NTL_PANEL;
  }
  crystal_sheen();

  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f + style->ItemSpacing.y * 7 +
                  frame_height * 6);
  if (igButton("\ue90c Kill Shots", (ImVec2){logo_size})) {
    /* ui_kills_gallery_init()/_destroy() are only ever called once each,
       at app startup/shutdown (see main.c's tinit/tdestroy) -- so the
       gallery's own s_scanned flag (kills_gallery.c) stayed true for the
       rest of the app's life after the first visit, and new screenshots
       saved after that never showed up until the whole game restarted.
       Force a fresh disk scan (and properly release any GPU thumbnail
       textures from the previous visit) every time we enter the screen. */
    ui_kills_gallery_destroy(env);
    ui_kills_gallery_init(env);
    usr->gdata.curr_screen = KILLS_GALLERY;
  }
  crystal_sheen();

  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f + style->ItemSpacing.y * 8 +
                  frame_height * 7);
  if (igButton("\ue90c Clips", (ImVec2){logo_size})) {
    /* Same fix as Kill Shots above -- clips_gallery.c's s_scanned flag
       only ever got reset at app shutdown, so a clip saved after the
       first visit to this screen never appeared until a restart. */
    ui_clips_gallery_destroy(env);
    ui_clips_gallery_init(env);
    usr->gdata.curr_screen = CLIPS_GALLERY;
  }
  crystal_sheen();

  crystal_pop_theme();

  /* Quit: deliberately NOT part of the main column above (same idea as the
     approved mockup) -- it's the one action you don't want accidentally
     emphasized alongside Play, so it lives in its own quiet corner with
     muted colors instead of the ambient glass theme. */
  {
    float quit_w = 130.0f;
    float quit_h = frame_height;
    float quit_x = ctx->size[0] - quit_w - style->WindowPadding.x * 4;
    float quit_y = style->WindowPadding.y * 4;

    igPushStyleColor_Vec4(ImGuiCol_Border,
                          (ImVec4){0.545f, 0.470f, 0.720f, 0.18f});
    igPushStyleColor_Vec4(ImGuiCol_Button,
                          (ImVec4){0.353f, 0.294f, 0.510f, 0.10f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                          (ImVec4){0.400f, 0.340f, 0.560f, 0.16f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonActive,
                          (ImVec4){0.430f, 0.360f, 0.600f, 0.22f});
    igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.600f, 0.560f, 0.700f, 1.0f});
    igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, 10.0f);

    igSetCursorPosX(quit_x);
    igSetCursorPosY(quit_y);
    if (igButton("\ue9b6 Quit", (ImVec2){quit_w, quit_h})) {
      env->config.running = false;
      save_user_settings(usrs);
    }

    igPopStyleVar(1);
    igPopStyleColor(5);
  }

  draw_snakey_rain_homepage(env, frame_height);

  igPopFont();
}

void ui_title_screen_destroy(tenv* env) {}
