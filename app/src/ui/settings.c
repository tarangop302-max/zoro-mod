#include "settings.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

#include "hud_layout_editor.h"
#include "key_buttons.h"
#include "../game/backup.h"
#include "../game/ntl_tags.h"

#include "../user.h"
#include "../android_glfw_shim.h"
#include "crystal_theme.h"

void ui_settings_init(tenv* env) {}

/* Popup panel opacity (runtime only, adjustable from the popup header). */
static float s_popup_alpha = 0.82f;

/* Touch drag-to-scroll for the popup's child panels (ImGui has no native
   touch scrolling). Call right after igBeginChild_Str(), once per panel,
   with a unique idx. Dragging on a label / empty space scrolls the panel 1:1;
   dragging a slider or button still operates that widget instead. The thick
   scrollbar also works.

   Fling: when the finger lifts while still moving, the panel keeps gliding
   with the speed the finger actually had (measured over the last ~0.1 s, in
   px/second, so it does not depend on the frame rate) and slows down smoothly.
   A slow drag that stops before lifting does not fling at all; a quick flick
   travels far. Touching the panel again stops the glide at once.

   Robustness: the drag owner is released on ANY finger-up (not only by the
   panel that owned it) and also if the owning panel stops being drawn
   mid-drag (closed popup) -- previously that left every other panel unable
   to scroll until restart. Items that are real widgets (slider, button...)
   still take the touch; plain tiles drawn with the draw list do not. */
#define TOUCH_SCROLL_SLOP 10.0f     /* px before a press becomes a scroll    */
#define TOUCH_FLING_MAX 3500.0f     /* px/s cap, keeps one flick sane        */
#define TOUCH_FLING_MIN 120.0f      /* px/s below this: no glide             */
#define TOUCH_FRICTION 3.6f         /* 1/s: glide distance = speed / this    */

static float ts_clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static void popup_touch_scroll(tenv* env, int idx) {
  static int owner = -1;       /* panel that owns the current drag */
  static bool dragging = false;
  static bool blocked = false; /* a widget took the touch: don't scroll */
  static float last_y = 0.0f, start_y = 0.0f;
  static int owner_frame = 0;
  static int glide_owner = -1;
  static float glide_vel = 0.0f; /* px/s, + = finger moved down */
  /* last few finger samples, for the fling speed */
  static float hist_y[8];
  static double hist_t[8];
  static int hist_i = 0, hist_n = 0;

  touch_state* t = &env->wnd->ui_touch;
  const int frame = igGetFrameCount();
  const double now = igGetTime();
  float dt = igGetIO_Nil()->DeltaTime;
  dt = ts_clampf(dt, 0.001f, 0.05f);

  /* The owning panel vanished mid-drag (popup closed): free the lock. */
  if (owner != -1 && frame - owner_frame > 3) {
    owner = -1;
    dragging = false;
  }

  /* ImGui marks a press on EMPTY space inside a window as "active" too (it is
     the window's internal move id, even for NoMove windows). That is not a
     widget, so it must not count as one. Only a real item (slider, button,
     combo, scrollbar...) being active should stop the drag-scroll. */
  const ImGuiID move_id = igGetID_Str("#MOVE");
  const ImGuiID active = igGetActiveID();
  const bool item_active = (active != 0 && active != move_id);

  if (!t->down) {
    blocked = false;
    if (owner != -1) { /* finger just lifted: whoever owned it, release it */
      float v = 0.0f;
      if (dragging && hist_n >= 2) {
        int newest = (hist_i + 7) & 7;
        if (now - hist_t[newest] <= 0.08) { /* still moving when it lifted */
          int oldest = newest;
          int use = hist_n < 8 ? hist_n : 8;
          for (int k = 1; k < use; ++k) {
            int ix = (newest - k) & 7;
            if (hist_t[newest] - hist_t[ix] > 0.12) break;
            oldest = ix;
          }
          double span = hist_t[newest] - hist_t[oldest];
          if (span >= 0.016)
            v = (float)((hist_y[newest] - hist_y[oldest]) / span);
        }
      }
      v = ts_clampf(v, -TOUCH_FLING_MAX, TOUCH_FLING_MAX);
      if (fabsf(v) >= TOUCH_FLING_MIN) {
        glide_owner = owner;
        glide_vel = v;
      } else {
        glide_owner = -1;
        glide_vel = 0.0f;
      }
      owner = -1;
      dragging = false;
    }
    if (glide_owner == idx) {
      float cur = igGetScrollY();
      float target = ts_clampf(cur - glide_vel * dt, 0.0f, igGetScrollMaxY());
      if (fabsf(glide_vel) < 25.0f || fabsf(target - cur) < 0.01f) {
        glide_vel = 0.0f; /* slowed to a stop, or hit the top / bottom */
        glide_owner = -1;
      } else {
        igSetScrollY_Float(target);
        glide_vel *= expf(-TOUCH_FRICTION * dt);
      }
    }
    return;
  }

  if (owner == -1 && !blocked) {
    if (igIsWindowHovered(ImGuiHoveredFlags_None) && !item_active) {
      owner = idx;
      owner_frame = frame;
      dragging = false;
      last_y = start_y = t->y;
      glide_owner = -1; /* a new touch stops any glide */
      glide_vel = 0.0f;
      hist_n = 0;
      hist_i = 0;
    }
    return;
  }
  if (owner != idx) return;
  owner_frame = frame;

  if (item_active) {
    /* A slider/button/combo/scrollbar grabbed this touch. */
    owner = -1;
    blocked = true;
    return;
  }
  if (!dragging && fabsf(t->y - start_y) > TOUCH_SCROLL_SLOP) {
    dragging = true;
    hist_n = 0;
    hist_i = 0;
  }
  if (dragging) {
    float dy = t->y - last_y;
    igSetScrollY_Float(
        ts_clampf(igGetScrollY() - dy, 0.0f, igGetScrollMaxY()));
    hist_y[hist_i] = t->y;
    hist_t[hist_i] = now;
    hist_i = (hist_i + 1) & 7;
    if (hist_n < 8) hist_n++;
  }
  last_y = t->y;
}

/* Shared with controls.c so its panel can drag-scroll on touch screens. */
void ui_touch_scroll(tenv* env, int idx) { popup_touch_scroll(env, idx); }

/* ---- option rows -----------------------------------------------------------
 * Every option is one row: its name on the left, its widget on the right, and
 * a dimmed one-line description underneath. All options go through opt_begin()
 * / opt_end(), which is also what the search bar filters on (name + description,
 * every word typed must match). */
static struct {
  char query[64];      /* search text, "" = no search                          */
  bool searching;
  const char* section; /* header drawn before the first visible row           */
  bool section_shown;
  const char* desc;    /* description of the row being drawn                  */
  float col_x;         /* x where the widgets start                           */
  int hits;            /* rows drawn so far (search results)                  */
} SO;

static bool ci_has(const char* hay, const char* needle) {
  size_t n = strlen(needle);
  if (!n) return true;
  for (; *hay; ++hay) {
    size_t k = 0;
    while (k < n && hay[k] && tolower((unsigned char)hay[k]) == tolower((unsigned char)needle[k])) ++k;
    if (k == n) return true;
  }
  return false;
}

/* Every space-separated word of the query must appear in the name or text. */
static bool opt_matches(const char* name, const char* desc) {
  if (!SO.searching) return true;
  char word[64];
  const char* p = SO.query;
  while (*p) {
    while (*p == ' ') ++p;
    size_t n = 0;
    while (p[n] && p[n] != ' ' && n < sizeof word - 1) { word[n] = p[n]; ++n; }
    word[n] = 0;
    p += n;
    if (n && !ci_has(name, word) && !ci_has(desc, word) && !ci_has(SO.section, word))
      return false;
  }
  return true;
}

static void section_begin(tenv* env, const char* title) {
  (void)env;
  SO.section = title;
  SO.section_shown = false;
  ImVec2 av;
  igGetContentRegionAvail(&av);
  SO.col_x = igGetCursorPosX() + av.x * 0.46f;
}

static void section_header_now(void) {
  if (SO.section && !SO.section_shown) {
    /* the options are indented; the header line spans the full width */
    igIndent(-igGetStyle()->WindowPadding.x);
    igSeparatorText(SO.section);
    igIndent(igGetStyle()->WindowPadding.x);
    SO.section_shown = true;
    ImVec2 av;
    igGetContentRegionAvail(&av);
    SO.col_x = igGetCursorPosX() + av.x * 0.46f;
  }
}

/* Returns false when the search hides this option. */
static bool opt_begin(const char* name, const char* desc) {
  if (!opt_matches(name, desc)) return false;
  section_header_now();
  SO.hits++;
  SO.desc = desc;
  igAlignTextToFramePadding();
  igTextUnformatted(name, NULL);
  igSameLine(SO.col_x, 0);
  return true;
}

static void opt_end(void) {
  if (SO.desc && SO.desc[0]) {
    igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.58f, 0.52f, 0.74f, 1.0f});
    igTextWrapped("%s", SO.desc);
    igPopStyleColor(1);
  }
  igDummy((ImVec2){0, 2.0f});
  SO.desc = NULL;
}

/* A free-standing note under a section (e.g. the backup status line). */
static void opt_note(const char* text) {
  igPushStyleColor_Vec4(ImGuiCol_Text, (ImVec4){0.58f, 0.52f, 0.74f, 1.0f});
  igTextWrapped("%s", text);
  igPopStyleColor(1);
}

static const char* const kSizeNames[] = {"Small", "Regular", "Large"};

static void sec_general(tenv* env) {
  user_settings* usrs = &env->usr->usrs;
  section_begin(env, "General");
  igIndent(igGetStyle()->WindowPadding.x);

  if (opt_begin("VSync", "Waits for the screen's refresh to avoid tearing. Can also cap your FPS.")) {
    if (igCheckbox("##vsync", &usrs->vsync)) {
      env->config.vsync = usrs->vsync;
      twindow_request_refresh(env->wnd);
    }
    opt_end();
  }
  if (opt_begin("FPS limit",
                "A maximum, not a forced refresh rate: FPS cannot exceed your display's refresh "
                "rate. Android Auto may keep 60 Hz - pick 90/120/144 Hz in the phone's Display "
                "settings to use a matching limit.")) {
    int fps_index = 0;
    const int fps_values[] = {0, 60, 90, 120, 144};
    for (int fi = 0; fi < 5; ++fi)
      if (usrs->fps_limit == fps_values[fi]) fps_index = fi;
    igSetNextItemWidth(-1);
    if (igCombo_Str_arr("##fps limit", &fps_index,
                        (const char*[]){"Device/VSync", "60 FPS", "90 FPS", "120 FPS", "144 FPS"},
                        5, -1))
      usrs->fps_limit = fps_values[fps_index];
    opt_end();
  }
  if (opt_begin("Performance mode", "Turns off shadows so the game runs smoother on weaker phones.")) {
    igCheckbox("##performance mode", &usrs->performance_mode);
    opt_end();
  }
  bool lat_on = user_settings_latency_comp(usrs);
  if (opt_begin("Latency compensation",
                "Your turns show when the server receives them, and other snakes' heads are drawn "
                "where the server has them now - what you see is what can hit you.")) {
    if (igCheckbox("##latency comp", &lat_on)) user_settings_set_latency_comp(usrs, lat_on);
    opt_end();
  }
  if (opt_begin("Turn delay",
                "How much of that wait is applied: 0% shows your turns instantly, 100% exactly when "
                "the server gets them. Keep 100% for a true picture.")) {
    int delay_pct = user_settings_turn_delay_pct(usrs);
    igBeginDisabled(!lat_on);
    igSetNextItemWidth(-1);
    if (igSliderInt("##turn delay", &delay_pct, 0, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp))
      user_settings_set_turn_delay_pct(usrs, delay_pct);
    igEndDisabled();
    opt_end();
  }
  if (opt_begin("Cursor size", "Size of the on-screen cursor.")) {
    igSetNextItemWidth(-1);
    igSliderInt("##cursor size", &usrs->cursor_size, 16, 64, "%d px", ImGuiSliderFlags_AlwaysClamp);
    opt_end();
  }
  if (opt_begin("Dynamic minimap", "Shrinks the minimap a little as the arena gets smaller.")) {
    bool dyn_mm = user_settings_dynamic_minimap(usrs);
    if (igCheckbox("##dynamic minimap", &dyn_mm)) user_settings_set_dynamic_minimap(usrs, dyn_mm);
    opt_end();
  }
  if (opt_begin("UI font size", "Size of the text in menus and windows.")) {
    igSetNextItemWidth(-1);
    igCombo_Str_arr("##ui font size", (int*)&usrs->ui_font_size, kSizeNames, 3, -1);
    opt_end();
  }
  if (opt_begin("Stats font size", "Size of the stats text on the in-game HUD.")) {
    igSetNextItemWidth(-1);
    igCombo_Str_arr("##stats font size", (int*)&usrs->stats_font_size, kSizeNames, 3, -1);
    opt_end();
  }
  if (opt_begin("Leaderboard font size", "Size of the text in the leaderboard.")) {
    igSetNextItemWidth(-1);
    igCombo_Str_arr("##leaderboard font size", (int*)&usrs->lb_font_size, kSizeNames, 3, -1);
    opt_end();
  }
  if (opt_begin("Leaderboard title", "Text shown above the leaderboard. Leave empty for the default.")) {
    igSetNextItemWidth(-1);
    igInputTextWithHint("##leaderboard title", LB_TITLE_DEFAULT, usrs->lb_title,
                        LB_TITLE_MAX + 1, ImGuiInputTextFlags_None, NULL, NULL);
    opt_end();
  }
  if (opt_begin("Leaderboard title colour", "Colour (and opacity) of the leaderboard title.")) {
    igSetNextItemWidth(-1);
    igColorEdit4("##leaderboard title colour", usrs->lb_title_color, ImGuiColorEditFlags_AlphaBar);
    opt_end();
  }
  if (opt_begin("Leaderboard text colour", "Tick to colour every leaderboard name and score the same instead of using skin colours.")) {
    igCheckbox("##leaderboard names colour on", &usrs->lb_names_color_enabled);
    igSameLine(0, -1);
    igBeginDisabled(!usrs->lb_names_color_enabled);
    igSetNextItemWidth(-1);
    igColorEdit4("##leaderboard names colour", usrs->lb_names_color, ImGuiColorEditFlags_AlphaBar);
    igEndDisabled();
    opt_end();
  }
  if (opt_begin("Leaderboard own line colour", "Tick to give your own leaderboard line its own colour.")) {
    igCheckbox("##leaderboard own colour on", &usrs->lb_own_color_enabled);
    igSameLine(0, -1);
    igBeginDisabled(!usrs->lb_own_color_enabled);
    igSetNextItemWidth(-1);
    igColorEdit4("##leaderboard own colour", usrs->lb_own_color, ImGuiColorEditFlags_AlphaBar);
    igEndDisabled();
    opt_end();
  }
  if (opt_begin("Names font size", "Size of the player names shown above snakes.")) {
    igSetNextItemWidth(-1);
    igCombo_Str_arr("##snake name font size", (int*)&usrs->snake_names_font_size, kSizeNames, 3, -1);
    opt_end();
  }
  if (opt_begin("Show snake scores", "Shows each snake's score next to its name.")) {
    igCheckbox("##snake scores", &usrs->snake_scores);
    opt_end();
  }
  if (opt_begin("Smooth zoom", "Zoom changes glide smoothly instead of jumping.")) {
    igCheckbox("##smooth zoom", &usrs->smooth_zoom);
    opt_end();
  }
  if (opt_begin("Ping / FPS graph", "Shows a small ping and FPS graph while you play.")) {
    igCheckbox("##net graph", &usrs->show_net_graph);
    opt_end();
  }
  if (opt_begin("Zoom step", "How much the view changes with each zoom step (mouse wheel / zoom keys).")) {
    igSetNextItemWidth(-1);
    igSliderFloat("##zoom step", &usrs->zoom_step, 0.05f, 0.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    opt_end();
  }
  if (opt_begin("Border color", "Color of the arena border.")) {
    igSetNextItemWidth(-1);
    igColorEdit3("##border color", usrs->bd_color, ImGuiColorEditFlags_None);
    opt_end();
  }
  if (opt_begin("Adjust HUD Layout", "Move and resize the on-screen HUD elements. Works mid-match too.")) {
    if (igButton("Open editor##hud_layout", (ImVec2){-1, 0})) ui_hud_layout_editor_enter(env);
    opt_end();
  }
  if (opt_begin("Keyboard layout", "Place and edit the on-screen keyboard buttons.")) {
    if (igButton("Open editor##key_layout", (ImVec2){-1, 0})) {
      save_user_settings(usrs);
      ui_key_buttons_open_editor(env);
    }
    opt_end();
  }
  if (opt_begin("Instant restart", "After you die, jump straight into a new game without waiting.")) {
    igCheckbox("##instant restart", &usrs->instant_restart);
    opt_end();
  }
  if (opt_begin("Restart with right click", "The right mouse button restarts the game.")) {
    igCheckbox("##restart rc", &usrs->restart_rc);
    opt_end();
  }
  if (opt_begin("Quit with middle click", "The middle mouse button leaves the game.")) {
    igCheckbox("##quit mc", &usrs->quit_mc);
    opt_end();
  }
  if (opt_begin("Laser color", "Color of the aiming laser line.")) {
    igSetNextItemWidth(-1);
    igColorEdit4("##laser color", usrs->laser_color, ImGuiColorEditFlags_AlphaBar);
    opt_end();
  }
  if (opt_begin("Laser thickness", "Thickness of the aiming laser line.")) {
    igSetNextItemWidth(-1);
    igSliderInt("##laser thickness", &usrs->laser_thickness, 1, 4, "%d px", ImGuiSliderFlags_AlwaysClamp);
    opt_end();
  }
#ifdef ANDROID
  if (opt_begin("Joystick laser length", "How far the laser reaches with the joystick (100% = to the screen edge).")) {
    int laser_pct = user_settings_joy_laser_pct(usrs);
    igSetNextItemWidth(-1);
    if (igSliderInt("##joy laser length", &laser_pct, 10, 100, "%d%%", ImGuiSliderFlags_AlwaysClamp))
      user_settings_set_joy_laser_pct(usrs, laser_pct);
    opt_end();
  }
#endif
  if (opt_begin("Centre ring radius", "Radius of the ring drawn around the arena centre.")) {
    igSetNextItemWidth(-1);
    igSliderFloat("##center ring radius", &usrs->center_ring_radius, 16.0f, 600.0f, "%.0f",
                  ImGuiSliderFlags_AlwaysClamp);
    opt_end();
  }
  if (opt_begin("Centre ring colour", "Color of the arena centre ring.")) {
    igSetNextItemWidth(-1);
    igColorEdit3("##center ring colour", usrs->center_ring_color, ImGuiColorEditFlags_None);
    opt_end();
  }
  if (opt_begin("Assist turn path", "In Assist mode, draws the path your snake will take when turning.")) {
    igCheckbox("##turn path", &usrs->turn_path_enabled);
    opt_end();
  }
  if (opt_begin("Turn path width", "Line width of the Assist turn path.")) {
    igSetNextItemWidth(-1);
    igSliderFloat("##turn path width", &usrs->turn_path_width, 1.0f, 12.0f, "%.1f px",
                  ImGuiSliderFlags_AlwaysClamp);
    opt_end();
  }
  if (opt_begin("Turn path colour", "Color of the turn path while it is clear.")) {
    igSetNextItemWidth(-1);
    igColorEdit3("##turn path colour", usrs->turn_path_color, ImGuiColorEditFlags_None);
    opt_end();
  }
  if (opt_begin("Turn path danger colour", "Color of the turn path where it would hit something.")) {
    igSetNextItemWidth(-1);
    igColorEdit3("##turn path danger colour", usrs->turn_path_danger_color, ImGuiColorEditFlags_None);
    opt_end();
  }
  if (opt_begin("Show bot thinking", "While the bot plays, draws on screen what it is thinking.")) {
    igCheckbox("##bot vis", &usrs->bot_vis);
    opt_end();
  }
  if (opt_begin("Bot circle after score", "Score at which the bot stops hunting and starts circling.")) {
    igSetNextItemWidth(-1);
    igSliderInt("##circle after", &usrs->bot_follow_circle_score, 1000, 6000, "%d",
                ImGuiSliderFlags_AlwaysClamp);
    opt_end();
  }
  if (opt_begin("Bot radius multiplier", "How wide the bot's turning circle is (higher = wider).")) {
    igSetNextItemWidth(-1);
    igSliderInt("##rad mult", &usrs->bot_radius_mult, 10, 40, "%dx", ImGuiSliderFlags_AlwaysClamp);
    opt_end();
  }
  igIndent(-igGetStyle()->WindowPadding.x);

  /* Backup */
  section_begin(env, "Backup");
  igIndent(igGetStyle()->WindowPadding.x);
  if (opt_begin("Export backup",
                "Saves all your settings, control layouts and Team Chat quick messages to a file.")) {
    if (igButton("Export##backup_export", (ImVec2){-1, 0})) backup_request_export(env);
    opt_end();
  }
  if (opt_begin("Import backup",
                "Restores everything from a backup file, e.g. after clearing data or reinstalling.")) {
    if (igButton("Import##backup_import", (ImVec2){-1, 0})) backup_request_import();
    opt_end();
  }
  if (SO.section_shown && backup_status()[0]) opt_note(backup_status());
  igIndent(-igGetStyle()->WindowPadding.x);
}

static void sec_mode(tenv* env, int i) {
  user_settings* usrs = &env->usr->usrs;
  gameplay_mode* mode = usrs->modes + i;
  igPushID_Int(i + 1);
  section_begin(env, i == 0 ? "Normal mode" : "Assist mode");
  igIndent(igGetStyle()->WindowPadding.x);

  if (opt_begin("Show crosshair", "Shows a crosshair on the screen.")) {
    igCheckbox("##crosshair", &mode->show_crosshair);
    opt_end();
  }
  if (opt_begin("Show background", "Draws the game's background pattern.")) {
    igCheckbox("##bg", &mode->show_background);
    opt_end();
  }
  if (opt_begin("Show accessories", "Draws snake accessories such as hats and glasses.")) {
    igCheckbox("##acc", &mode->show_accessories);
    opt_end();
  }
  if (opt_begin("Show NTL tags", "Shows NTL players' tags on their snakes in this mode.")) {
    bool on = ntl_tags_show_get(i);
    if (igCheckbox("##ntl tags", &on)) ntl_tags_show_set(i, on);
    opt_end();
  }
  if (opt_begin("Show shadows", "Draws shadows under snakes (off while Performance mode is on).")) {
    igCheckbox("##shad", &mode->show_shadows);
    opt_end();
  }
  if (opt_begin("Death effect", "Plays the explosion effect when a snake dies.")) {
    igCheckbox("##death effect", &mode->death_effect);
    opt_end();
  }
  if (opt_begin("Outline player names", "Gives names an outline so they are easier to read.")) {
    igCheckbox("##player names outline", &mode->player_names_outline);
    opt_end();
  }
  if (opt_begin("Segment separation", "Spacing between the drawn body segments of snakes.")) {
    igSetNextItemWidth(-1);
    igSliderFloat("##bps", &mode->qsm, 1, 4, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    opt_end();
  }
  if (opt_begin("Background scale", "Size of the background pattern.")) {
    igSetNextItemWidth(-1);
    igSliderFloat("##bgs", &mode->bg_scale, 0.05, 4, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    opt_end();
  }
  if (opt_begin("Render mode", "How snakes are drawn: textured skins, solid colour or a flat style.")) {
    igSetNextItemWidth(-1);
    igCombo_Str_arr("##render mode", &mode->render_mode,
                    (const char*[]){"Texture", "Solid", "Flat", "Rounded plain", "Squared plain",
                                    "Vlither textured", "Striped plain"},
                    NUM_RENDER_MODES, -1);
    opt_end();
  }
  if (opt_begin("Transparent skin", "Makes snakes see-through so you can see what is underneath.")) {
    igCheckbox("##transparent skin", &mode->transparent_skin);
    opt_end();
  }
  if (opt_begin("Skin opacity", "How see-through snakes are when Transparent skin is on.")) {
    igBeginDisabled(!mode->transparent_skin);
    int opacity_percent = (int)(usrs->transparent_skin_opacity[i] * 100.0f + 0.5f);
    igSetNextItemWidth(-1);
    /* Assist mode's Flat render mode has no per-segment pattern to misalign
       at zero opacity (see redraw.c's flatten comment), so its floor is
       relaxed to 0% instead of the usual 15% minimum. */
    int opacity_min = (i == 1 && mode->render_mode == 2) ? 0 : 15;
    if (igSliderInt("##skin opacity", &opacity_percent, opacity_min, 85, "%d%%",
                    ImGuiSliderFlags_AlwaysClamp))
      usrs->transparent_skin_opacity[i] = opacity_percent / 100.0f;
    igEndDisabled();
    opt_end();
  }
  if (opt_begin("Center line (your snake)", "Draws a thin line down the middle of your own snake.")) {
    igCheckbox("##center line", &mode->center_line);
    opt_end();
  }
  if (opt_begin("Centre line width", "How thick the centre line on your snake is.")) {
    igBeginDisabled(!mode->center_line);
    igSetNextItemWidth(-1);
    igSliderFloat("##center line width", &usrs->center_line_width[i], CENTER_LINE_WIDTH_MIN,
                  CENTER_LINE_WIDTH_MAX, "%.1f px", ImGuiSliderFlags_AlwaysClamp);
    igEndDisabled();
    opt_end();
  }
  if (i == 1 && opt_begin("White skin (enemies)", "Draws every other snake as a plain white silhouette.")) {
    igCheckbox("##white skin enemies", &usrs->white_skin_enemies[1]);
    opt_end();
  }
  if (opt_begin("Head dot", "Draws a dot on your snake's head.")) {
    igCheckbox("##head dot", &usrs->head_dot_enabled[i]);
    opt_end();
  }
  if (opt_begin("Head dot size", "Size of the head dot.")) {
    igBeginDisabled(!usrs->head_dot_enabled[i]);
    igSetNextItemWidth(-1);
    igSliderFloat("##head dot size", &usrs->head_dot_size[i], HEAD_DOT_SIZE_MIN, HEAD_DOT_SIZE_MAX,
                  "%.0f px", ImGuiSliderFlags_AlwaysClamp);
    igEndDisabled();
    opt_end();
  }
  if (opt_begin("Head dot colour", "Color of the head dot.")) {
    igBeginDisabled(!usrs->head_dot_enabled[i]);
    igSetNextItemWidth(-1);
    igColorEdit3("##head dot colour", usrs->head_dot_colors[i], ImGuiColorEditFlags_None);
    igEndDisabled();
    opt_end();
  }
  if (opt_begin("Boost effect", "Shows the boost effect on snakes; the box picks its style.")) {
    igCheckbox("##boost", &mode->show_boost);
    igSameLine(0, -1);
    igBeginDisabled(!mode->show_boost);
    igSetNextItemWidth(-1);
    igCombo_Str_arr("##boost type", &mode->boost_type, (const char*[]){"Normal", "Simple"}, 2, -1);
    igEndDisabled();
    opt_end();
  }
  if (opt_begin("Boost effect strength", "How strong the boost effect looks.")) {
    igBeginDisabled(!mode->show_boost);
    igSetNextItemWidth(-1);
    igSliderFloat("##boost strength", &mode->boost_strength, 0.25f, 3, "%.2fx",
                  ImGuiSliderFlags_AlwaysClamp);
    igEndDisabled();
    opt_end();
  }
  if (opt_begin("Food shader", "The shape and style food is drawn in.")) {
    igSetNextItemWidth(-1);
    igCombo_Str_arr("##food type", &mode->food_type,
                    (const char*[]){"Solid", "Rings", "Hollow square", "Star outline", "Star solid",
                                    "Hollow triangle", "Asterisk", "Sparkle"},
                    8, 8);
    opt_end();
  }
  if (opt_begin("Food glow", "Adds a glow around food.")) {
    igCheckbox("##food glow", &usrs->food_glow[i]);
    opt_end();
  }
  if (opt_begin("Food scale", "Size of food.")) {
    igSetNextItemWidth(-1);
    igSliderFloat("##food scale", &mode->food_scale, 0.25f, 3, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    opt_end();
  }
  if (opt_begin("Food float", "Food drifts gently around instead of staying still.")) {
    igCheckbox("##food float", &mode->food_float);
    opt_end();
  }
  if (opt_begin("Food flicker", "Food flickers instead of shining steadily.")) {
    igCheckbox("##food flicker", &mode->food_flicker);
    opt_end();
  }
  if (opt_begin("Uniform food color", "Draws all food in one color; pick the color in the box.")) {
    igCheckbox("##uniform food color", &mode->uniform_food_color);
    igSameLine(0, -1);
    igBeginDisabled(!mode->uniform_food_color);
    igSetNextItemWidth(-1);
    igColorEdit3("##fdcolor", mode->food_color, ImGuiColorEditFlags_None);
    igEndDisabled();
    opt_end();
  }
  igIndent(-igGetStyle()->WindowPadding.x);
  igPopID();
}

static const char* hotkey_desc(int i) {
  switch (i) {
    case HOTKEY_HUD: return "Shows or hides the HUD.";
    case HOTKEY_SHOW_NAMES: return "Shows or hides player names above snakes.";
    case HOTKEY_BIG_FOOD: return "Hides small food so only the big food shows.";
    case HOTKEY_ASSIST: return "Switches between Normal and Assist mode.";
    case HOTKEY_BOT: return "Turns the auto-play bot on or off.";
    case HOTKEY_MENU: return "Shows or hides the on-screen list of hotkeys.";
    case HOTKEY_RESTART: return "Leaves this game and starts a new one.";
    case HOTKEY_QUIT: return "Leaves the game and returns to the lobby.";
    case HOTKEY_OPEN_SETTINGS: return "Opens this Settings window during a match.";
    case HOTKEY_EYES_BACK: return "Toggles the Eyes back steering trick (from NTL).";
    case HOTKEY_KEY_EDITOR: return "Opens the on-screen keyboard layout editor.";
    default: return "";
  }
}

static void sec_hotkeys(tenv* env) {
  user_settings* usrs = &env->usr->usrs;
  ImGuiStyle* style = igGetStyle();
  float frame_height = igGetFrameHeight();
  section_begin(env, "Hotkeys");
  igIndent(style->WindowPadding.x);
  for (int i = 0; i < NUM_HOTKEYS; i++) {
    hotkey* hk = usr_hotkey(usrs, i);
    igPushID_Int(i);
    if (opt_begin(hk->description, hotkey_desc(i))) {
      igSetNextItemWidth(frame_height * 2);
      char preview_char[2] = {(char)hk->key, 0};
      if (igBeginCombo("##hotkey code", preview_char, ImGuiComboFlags_None)) {
        for (int c = 48; c < 58; c++) {
          char selectable_char[2] = {c, 0};
          bool is_in_use = false;
          for (int d = 0; d < NUM_HOTKEYS; d++)
            if (c == usr_hotkey(usrs, d)->key && hk->key != usr_hotkey(usrs, d)->key)
              is_in_use = true;
          if (igSelectable_Bool(selectable_char, c == hk->key,
                                is_in_use ? ImGuiSelectableFlags_Disabled : ImGuiSelectableFlags_None,
                                (ImVec2){}))
            hk->key = c;
        }
        for (int c = 65; c < 91; c++) {
          char selectable_char[2] = {c, 0};
          bool is_in_use = false;
          for (int d = 0; d < NUM_HOTKEYS; d++)
            if (c == usr_hotkey(usrs, d)->key && hk->key != usr_hotkey(usrs, d)->key)
              is_in_use = true;
          is_in_use = is_in_use || c == GLFW_KEY_M || c == GLFW_KEY_N;
          if (igSelectable_Bool(selectable_char, c == hk->key,
                                is_in_use ? ImGuiSelectableFlags_Disabled : ImGuiSelectableFlags_None,
                                (ImVec2){}))
            hk->key = c;
        }
        igEndCombo();
      }
      igSameLine(0, -1);
      ImVec2 rest;
      igGetContentRegionAvail(&rest);
      igSetNextItemWidth(rest.x - style->ItemInnerSpacing.x);
      if (i == HOTKEY_RESTART || i == HOTKEY_QUIT) {
        igBeginDisabled(true);
        igCombo_Str_arr("##hotkey mode", &(int){0}, (const char*[]){"Toggle"}, 1, -1);
        igEndDisabled();
      } else {
        igCombo_Str_arr("##hotkey mode", &hk->mode, (const char*[]){"Toggle", "Press and hold"}, 2, -1);
      }
      opt_end();
    }
    igPopID();
  }
  igIndent(-style->WindowPadding.x);
}

void ui_settings(tenv* env) {
  tuser_data* usr = env->usr;
  tcontext* ctx = env->ctx;
  user_settings* usrs = &usr->usrs;
  ImGuiStyle* style = igGetStyle();
  ImGuiIO* io = igGetIO_Nil();

  /* Panel opacity is remembered between launches (loaded once, saved when the
     slider is released). */
  static bool s_alpha_loaded = false;
  if (!s_alpha_loaded) {
    s_popup_alpha = user_settings_get_panel_alpha(usrs, 0, 0.82f, 0.30f);
    s_alpha_loaded = true;
  }
  game_data* gdata = &usr->gdata;
  /* Popup mode: opened mid-match with the Open settings hotkey. It is drawn
     as a compact, touchable window (tabs instead of 4 columns, small font,
     small buttons) that sits inside the area reserved for it on screen. */
  const bool ingame_popup = gdata->settings_popup;
  /* The lobby's Settings button now opens this same popup panel (a bit
     larger than the in-game one) over the live background-preview game,
     instead of a fullscreen page. */
  const bool lobby_popup = !ingame_popup && gdata->curr_screen == SETTINGS;
  const bool popup = ingame_popup || lobby_popup;
  static int popup_tab = 0;

  int base_font = popup ? FONT_SIZE_SMALL : usrs->ui_font_size;
  igPushFont(usr->imgui_data.regular_font[base_font],
             usr->imgui_data.regular_font[base_font]->LegacySize);

  if (!popup) {
    usr->r->global.bg_opacity = 0;
    usr->r->global.bd_opacity = 0;
    usr->r->global.minimap_opacity = 0;
  }

  float ui_s = 1.0f;
  {
    float short_edge = ctx->size[0] < ctx->size[1] ? ctx->size[0] : ctx->size[1];
    ui_s = short_edge / 720.0f;
    if (ui_s < 0.75f) ui_s = 0.75f;
    if (ui_s > 1.30f) ui_s = 1.30f;
  }

  if (popup) {
    /* Popup rect: centered box, ~42% x 76% of the screen in-game; the
       lobby version is a little bigger (~52% x 88%). */
    float pw = ctx->size[0] * (lobby_popup ? 0.52f : 0.42f);
    float ph = ctx->size[1] * (lobby_popup ? 0.88f : 0.76f);
    igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 14.0f);
    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding, (ImVec2){12 * ui_s, 10 * ui_s});
    /* Roomier frames/spacing = bigger touch targets that are easy to hit. */
    igPushStyleVar_Vec2(ImGuiStyleVar_FramePadding, (ImVec2){8 * ui_s, 7 * ui_s});
    igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, (ImVec2){8 * ui_s, 9 * ui_s});
    igPushStyleVar_Float(ImGuiStyleVar_ScrollbarSize, 22.0f * ui_s);
    /* Mostly opaque dark panel so text is readable over the live game
       (opacity slider in the header). */
    igPushStyleColor_Vec4(ImGuiCol_WindowBg,
                          (ImVec4){0.070f, 0.050f, 0.120f, s_popup_alpha});
    igPushStyleColor_Vec4(ImGuiCol_Border,
                          (ImVec4){0.690f, 0.580f, 0.960f, 0.55f});
    igSetNextWindowPos((ImVec2){ctx->size[0] * 0.5f, ctx->size[1] * 0.5f},
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
    /* Header: title + panel opacity */
    {
      ImVec2 hv;
      igGetContentRegionAvail(&hv);
      igAlignTextToFramePadding();
      igTextColored((ImVec4){0.80f, 0.72f, 1.0f, 1.0f}, "SETTINGS");
      float sw = hv.x * 0.42f;
      igSameLine(style->WindowPadding.x + hv.x - sw, 0);
      igSetNextItemWidth(sw);
      if (igSliderFloat("##popup_alpha", &s_popup_alpha, 0.30f, 1.0f,
                        "Opacity %.2f", ImGuiSliderFlags_AlwaysClamp))
        user_settings_set_panel_alpha(usrs, 0, s_popup_alpha);
      if (igIsItemDeactivatedAfterEdit()) save_user_settings(usrs);
    }
    /* Tab row: General / Normal / Assist / Hotkeys */
    const char* tab_names[4] = {"General", "Normal", "Assist", "Hotkeys"};
    ImVec2 av;
    igGetContentRegionAvail(&av);
    float tw = (av.x - style->ItemSpacing.x * 3) / 4.0f;
    for (int t = 0; t < 4; t++) {
      if (t) igSameLine(0, -1);
      bool sel = (t == popup_tab);
      igPushStyleColor_Vec4(ImGuiCol_Button,
          sel ? (ImVec4){0.510f, 0.294f, 0.910f, 0.95f}
              : (ImVec4){0.200f, 0.160f, 0.320f, 0.85f});
      if (igButton(tab_names[t], (ImVec2){tw, popup_btn_h})) popup_tab = t;
      igPopStyleColor(1);
    }
    /* Search: finds options in all four tabs by name or description. */
    {
      ImVec2 sav;
      igGetContentRegionAvail(&sav);
      igSetNextItemWidth(sav.x - popup_btn_h - style->ItemSpacing.x);
      igInputTextWithHint("##settings_search", "Search settings...", SO.query,
                          sizeof SO.query, ImGuiInputTextFlags_None, NULL, NULL);
      igSameLine(0, -1);
      igBeginDisabled(SO.query[0] == 0);
      if (igButton("X##search_clear", (ImVec2){popup_btn_h, 0})) SO.query[0] = 0;
      igEndDisabled();
    }
    igSeparator();
    igGetContentRegionAvail(&av);
    child_window_height =
        av.y - popup_btn_h - style->ItemSpacing.y * 2 - style->WindowPadding.y;
  }

  SO.searching = popup && SO.query[0] != 0;
  SO.hits = 0;

  if (popup && SO.searching) {
    /* one list with every matching option from all four tabs */
    igBeginChild_Str("settings_search_child", (ImVec2){-1, child_window_height},
                     ImGuiChildFlags_None, ImGuiWindowFlags_None);
    popup_touch_scroll(env, 3);
    sec_general(env);
    sec_mode(env, 0);
    sec_mode(env, 1);
    sec_hotkeys(env);
    if (SO.hits == 0) igTextDisabled("No settings match \"%s\".", SO.query);
    igEndChild();
  } else if (popup) {
    igBeginChild_Str(popup_tab == 0 ? "general_settings_child_holder"
                     : popup_tab == 3 ? "hotkey_child_window"
                                      : "mode_settings_child_holder",
                     (ImVec2){-1, child_window_height}, ImGuiChildFlags_None,
                     ImGuiWindowFlags_None);
    popup_touch_scroll(env, popup_tab == 0 ? 0 : popup_tab == 3 ? 2 : 1);
    if (popup_tab == 0) sec_general(env);
    else if (popup_tab == 3) sec_hotkeys(env);
    else sec_mode(env, popup_tab - 1);
    igEndChild();
  } else if (igBeginTable("settings_table", panel_columns, ImGuiTableFlags_None, (ImVec2){}, 0)) {
    /* full-screen (non-popup) layout: one column per page */
    igTableNextRow(ImGuiTableRowFlags_None, 0);
    igTableSetColumnIndex(0);
    igBeginChild_Str("general_settings_child_holder", (ImVec2){-1, child_window_height},
                     ImGuiChildFlags_None, ImGuiWindowFlags_None);
    sec_general(env);
    igEndChild();

#ifdef ANDROID
    igTableNextRow(ImGuiTableRowFlags_None, 0);
    igTableSetColumnIndex(0);
#else
    igTableSetColumnIndex(1);
#endif
    igBeginChild_Str("mode_settings_child_holder", (ImVec2){-1, child_window_height},
                     ImGuiChildFlags_None, ImGuiWindowFlags_None);
    sec_mode(env, 0);
    sec_mode(env, 1);
    igEndChild();

#ifdef ANDROID
    igTableNextRow(ImGuiTableRowFlags_None, 0);
    igTableSetColumnIndex(0);
#else
    igTableSetColumnIndex(2);
#endif
    igBeginChild_Str("hotkey_child_window", (ImVec2){-1, child_window_height},
                     ImGuiChildFlags_None, ImGuiWindowFlags_None);
    sec_hotkeys(env);
    igEndChild();
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
  {
    /* Reliable taps make an accidental Reset easy, so it needs a second tap
       within 3 seconds. */
    static double reset_armed_until = 0.0;
    bool armed = igGetTime() < reset_armed_until;
    if (armed)
      igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.780f, 0.220f, 0.260f, 1.0f});
    if (igButton(armed ? "Tap again to reset" : "Reset",
                 (ImVec2){btn_w, btn_h})) {
      if (armed) {
        user_settings_default(usrs);
        env->config.vsync = usrs->vsync;
        twindow_request_refresh(env->wnd);
        reset_armed_until = 0.0;
      } else {
        reset_armed_until = igGetTime() + 3.0;
      }
    }
    if (armed) igPopStyleColor(1);
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

#ifdef ANDROID
  /* Colour pickers and combo lists are separate ImGui popup windows that can
     extend outside the settings window. During a match a touch outside the
     registered rectangle is a gameplay touch, so those popups never saw
     taps (colours did nothing, dropdowns could not be picked). While any
     popup is open, let ImGui own the whole screen. */
  if (ingame_popup && igIsPopupOpen_Str(NULL, ImGuiPopupFlags_AnyPopup))
    android_ui_capture_rect(0, 0, (float)ctx->size[0], (float)ctx->size[1]);
#endif

  if (popup) {
    igEnd();
    igPopStyleColor(2);
    igPopStyleVar(5);
  }

  igPopFont();
}

void ui_settings_destroy(tenv* env) {}
