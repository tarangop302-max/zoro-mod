#include "title_screen.h"
#ifdef ANDROID
#include "../android_glfw_shim.h"
#include "../android_jni.h"
#endif

#include "../network/server.h"
#include "../user.h"

bool g_sl_popup_open = false;

/* ============================================================================
 * Crystal / purple glass theme -- this screen only.
 *
 * ImGui doesn't have real gradient fills or blur, so "glass" is faked with:
 *   - a 4-corner gradient background rect + a couple of soft layered-circle
 *     glow blobs, drawn once before any widgets so they sit behind everything
 *   - flat translucent-purple frame/button colors (pushed for the whole
 *     screen, popped at the end -- other screens are unaffected)
 *   - a thin light "sheen" line along each widget's top edge afterward, to
 *     read as a glass catch-light rather than a flat color swatch
 *   - for the Play button specifically, a soft rounded glow drawn behind it
 *     (several translucent rounded rects of increasing size) plus a brighter
 *     fill, since it's meant to be the one standout action on the screen
 * ============================================================================
 */

static ImU32 crystal_col(float r, float g, float b, float a) {
  return igColorConvertFloat4ToU32((ImVec4){r, g, b, a});
}

static void crystal_draw_background(tenv* env) {
  tcontext* ctx = env->ctx;
  ImDrawList* dl = igGetWindowDrawList();
  float w = ctx->size[0];
  float h = ctx->size[1];

  ImDrawList_AddRectFilledMultiColor(
      dl, (ImVec2){0, 0}, (ImVec2){w, h},
      crystal_col(0.078f, 0.039f, 0.141f, 1.0f) /* top-left */,
      crystal_col(0.176f, 0.106f, 0.306f, 1.0f) /* top-right */,
      crystal_col(0.141f, 0.082f, 0.259f, 1.0f) /* bottom-right */,
      crystal_col(0.114f, 0.063f, 0.212f, 1.0f) /* bottom-left */);

  /* Two soft purple glow blobs (fake radial gradients via layered,
     decreasing-alpha circles), roughly matching the approved mockup's
     glow positions. */
  ImVec2 glow_a = {w * 0.72f, h * 0.30f};
  ImVec2 glow_b = {w * 0.20f, h * 0.80f};
  for (int i = 6; i >= 1; i--) {
    ImDrawList_AddCircleFilled(dl, glow_a, 70.0f + i * 55.0f,
                                crystal_col(0.592f, 0.353f, 1.0f, 0.02f * i),
                                48);
    ImDrawList_AddCircleFilled(dl, glow_b, 60.0f + i * 48.0f,
                                crystal_col(0.353f, 0.235f, 0.784f, 0.016f * i),
                                48);
  }
}

/* Thin light line along a widget's top edge -- call right after drawing it
   (uses the last item's rect). */
static void crystal_sheen(void) {
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
   with its known screen-space position/size, so the glow ends up behind. */
static void crystal_glow_rect(ImVec2 pos, ImVec2 size, float r, float g,
                               float b) {
  ImDrawList* dl = igGetWindowDrawList();
  for (int i = 6; i >= 1; i--) {
    float expand = i * 5.5f;
    ImDrawList_AddRectFilled(
        dl, (ImVec2){pos.x - expand, pos.y - expand},
        (ImVec2){pos.x + size.x + expand, pos.y + size.y + expand},
        crystal_col(r, g, b, 0.045f * i), 14.0f + expand * 0.4f,
        ImDrawFlags_None);
  }
}

/* Pushes the ambient glass colors used by most widgets on this screen.
   Must be matched with crystal_pop_theme(). Returns nothing; caller just
   needs to pop the same fixed counts. */
static void crystal_push_theme(void) {
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

static void crystal_pop_theme(void) {
  igPopStyleColor(CRYSTAL_COLOR_COUNT);
  igPopStyleVar(CRYSTAL_STYLEVAR_COUNT);
}

void ui_title_screen_init(tenv* env) {}

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

  igPopFont();
}

void ui_title_screen_destroy(tenv* env) {}
