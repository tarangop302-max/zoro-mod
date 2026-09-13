#include "death_screen.h"

#include "../user.h"
#ifdef ANDROID
#include "../android_glfw_shim.h"
#endif

void ui_death_screen(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  user_settings* usrs = &usr->usrs;
  game_data* gdata = &usr->gdata;
  ImGuiStyle* style = igGetStyle();

  /* The real background blur (same multi-tap technique already used
   * behind the Settings screen) is drawn separately in main.c's trender()
   * -- it has to happen before ui_viewport()'s ImGui pass this frame, not
   * in here, so it ends up behind this window instead of on top of it.
   * That call already includes its own dark tint, so this popup doesn't
   * need a separate dimming rect of its own on top of it. */

  igPushFont(usr->imgui_data.regular_font[usrs->ui_font_size],
             usr->imgui_data.regular_font[usrs->ui_font_size]->LegacySize);

  igPushStyleVar_Float(ImGuiStyleVar_FrameRounding, 12.0f);
  igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 18.0f);
  igPushStyleColor_Vec4(ImGuiCol_WindowBg,
                        (ImVec4){0.086f, 0.063f, 0.145f, 0.94f});
  igPushStyleColor_Vec4(ImGuiCol_Border,
                        (ImVec4){0.690f, 0.580f, 0.960f, 0.35f});
  igPushStyleColor_Vec4(ImGuiCol_Button,
                        (ImVec4){0.373f, 0.290f, 0.607f, 0.28f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                        (ImVec4){0.430f, 0.330f, 0.680f, 0.34f});
  igPushStyleColor_Vec4(ImGuiCol_ButtonActive,
                        (ImVec4){0.470f, 0.360f, 0.720f, 0.42f});
  igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.945f, 0.925f, 1.0f, 1.0f});

  igSetNextWindowPos((ImVec2){ctx->size[0] * 0.5f, ctx->size[1] * 0.5f},
                     ImGuiCond_Always, (ImVec2){0.5f, 0.5f});

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                           ImGuiWindowFlags_NoSavedSettings |
                           ImGuiWindowFlags_NoMove |
                           ImGuiWindowFlags_NoResize |
                           ImGuiWindowFlags_NoTitleBar |
                           ImGuiWindowFlags_AlwaysAutoResize;

  /* No close button and no ESC-to-close -- this stays up until Lobby or
   * Restart is clicked, per the request that it "will not close until
   * we click on lobby or restart buttons". */
  if (igBegin("##death_screen", NULL, flags)) {
#ifdef ANDROID
    /* Without this, taps on Lobby/Restart never reach ImGui: the raw
     * Android touch router (twindow_android.c) only forwards a touch as
     * a UI touch when it falls inside a rect registered here, or while
     * g_panel_open is true -- and g_panel_open's screen list doesn't
     * include the death popup. Every other tappable ImGui surface in
     * this codebase (key_buttons, chat, HUD editor handles) registers
     * its own rect the same way; this popup just never did. */
    ImVec2 win_pos, win_size;
    igGetWindowPos(&win_pos);
    igGetWindowSize(&win_size);
    android_ui_capture_rect(win_pos.x, win_pos.y, win_pos.x + win_size.x,
                            win_pos.y + win_size.y);
#endif

    float card_w = 640.0f;
    float pad = style->WindowPadding.x;
    ImVec2 ts;

    igCalcTextSize(&ts, "Run ended", NULL, false, -1.0f);
    igSetCursorPosX(pad + (card_w - ts.x) * 0.5f);
    igTextColored((ImVec4){0.718f, 0.651f, 0.910f, 1.0f}, "Run ended");

    igPushFont(usr->imgui_data.mono_font[usrs->ui_font_size],
               usr->imgui_data.mono_font[usrs->ui_font_size]->LegacySize);
    igCalcTextSize(&ts, usrs->nickname, NULL, false, -1.0f);
    igSetCursorPosX(pad + (card_w - ts.x) * 0.5f);
    igTextColored((ImVec4){0.596f, 0.518f, 0.769f, 1.0f}, "%s",
                  usrs->nickname);
    igPopFont();

    igDummy((ImVec2){card_w, 22.0f});

    const char* len_label = "Final length";
    igCalcTextSize(&ts, len_label, NULL, false, -1.0f);
    igSetCursorPosX(pad + (card_w - ts.x) * 0.5f);
    igTextColored((ImVec4){0.718f, 0.651f, 0.910f, 1.0f}, "%s", len_label);

    igPushFont(usr->imgui_data.mono_font_bold[FONT_SIZE_LARGE],
               usr->imgui_data.mono_font_bold[FONT_SIZE_LARGE]->LegacySize);
    char len_buf[32];
    snprintf(len_buf, sizeof(len_buf), "%d", usrs->score);
    igCalcTextSize(&ts, len_buf, NULL, false, -1.0f);
    igSetCursorPosX(pad + (card_w - ts.x) * 0.5f);
    igTextColored((ImVec4){1.0f, 1.0f, 1.0f, 1.0f}, "%s", len_buf);
    igPopFont();

    igDummy((ImVec2){card_w, 18.0f});

    /* Kills + time this run, side by side -- smaller than the hero number
     * above so "Final length" still reads as the headline stat. */
    int tot_sec = (int)usrs->play_time;
    int hours = tot_sec / 3600;
    int minutes = (tot_sec % 3600) / 60;
    int seconds = tot_sec % 60;
    char kills_buf[32];
    char time_buf[32];
    snprintf(kills_buf, sizeof(kills_buf), "\ueaeb %d", usrs->kills);
    snprintf(time_buf, sizeof(time_buf), "\ue952 %02d:%02d:%02d", hours,
             minutes, seconds);

    igPushFont(usr->imgui_data.mono_font[usrs->ui_font_size],
               usr->imgui_data.mono_font[usrs->ui_font_size]->LegacySize);
    ImVec2 ks, tsz;
    igCalcTextSize(&ks, kills_buf, NULL, false, -1.0f);
    igCalcTextSize(&tsz, time_buf, NULL, false, -1.0f);
    float gap = 48.0f;
    float total_w = ks.x + tsz.x + gap;
    igSetCursorPosX(pad + (card_w - total_w) * 0.5f);
    igTextColored((ImVec4){0.827f, 0.788f, 0.929f, 1.0f}, "%s", kills_buf);
    igSameLine(0, gap);
    igTextColored((ImVec4){0.827f, 0.788f, 0.929f, 1.0f}, "%s", time_buf);
    igPopFont();

    igDummy((ImVec2){card_w, 26.0f});
    igSeparator();
    igDummy((ImVec2){card_w, 20.0f});

    float btn_w = (card_w - style->ItemSpacing.x) * 0.5f;
    float btn_h = 74.0f;

    if (igButton("Lobby##death_screen", (ImVec2){btn_w, btn_h})) {
      /* Mirrors the existing manual-restart/quit hotkey pattern in
       * loop.c exactly (is_closing + restart_req) -- the already-proven
       * disconnect/reconnect state machine there takes it from here
       * once gdata->closed goes true, no new logic needed for that
       * part. */
      gdata->death_pending = false;
      if (gdata->connection) gdata->connection->is_closing = true;
    }

    igSameLine(0, style->ItemSpacing.x);

    igPushStyleColor_Vec4(ImGuiCol_Button,
                          (ImVec4){0.510f, 0.294f, 0.910f, 1.0f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                          (ImVec4){0.569f, 0.353f, 0.960f, 1.0f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonActive,
                          (ImVec4){0.450f, 0.243f, 0.850f, 1.0f});
    if (igButton("\uea1c Restart##death_screen", (ImVec2){btn_w, btn_h})) {
      gdata->death_pending = false;
      gdata->restart_req = true;
      if (gdata->connection) gdata->connection->is_closing = true;
    }
    igPopStyleColor(3);
  }
  igEnd();

  igPopStyleColor(6);
  igPopStyleVar(2);
  igPopFont();
}
