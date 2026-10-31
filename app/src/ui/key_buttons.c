#include "key_buttons.h"

#include "crystal_theme.h"
#include "../user.h"
#include "../game/game_data.h"
#ifdef ANDROID
#include "../android_glfw_shim.h"
#endif

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef IM_COL32
#define IM_COL32(R,G,B,A) \
  crystal_tint_u32((((ImU32)(A)<<24)|((ImU32)(B)<<16)|((ImU32)(G)<<8)|((ImU32)(R)<<0)))
#endif

#ifdef ANDROID
#include <android/log.h>
#define KBLOG(...) __android_log_print(ANDROID_LOG_DEBUG, "vlither_kb", __VA_ARGS__)
#else
#define KBLOG(...) (void)0
#endif

typedef struct key_entry {
  int glfw;
  const char *label;
} key_entry;

typedef struct key_row {
  const key_entry *keys;
  int count;
} key_row;

#define ROW_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const key_entry KEY_ROW_0[] = {
  {GLFW_KEY_ESCAPE,"Esc"}, {GLFW_KEY_F1,"F1"}, {GLFW_KEY_F2,"F2"},
  {GLFW_KEY_F3,"F3"}, {GLFW_KEY_F4,"F4"}, {GLFW_KEY_F5,"F5"},
  {GLFW_KEY_F6,"F6"}, {GLFW_KEY_F7,"F7"}, {GLFW_KEY_F8,"F8"},
  {GLFW_KEY_F9,"F9"}, {GLFW_KEY_F10,"F10"}, {GLFW_KEY_F11,"F11"},
  {GLFW_KEY_F12,"F12"}
};
static const key_entry KEY_ROW_1[] = {
  {GLFW_KEY_GRAVE_ACCENT,"`"}, {GLFW_KEY_1,"1"}, {GLFW_KEY_2,"2"},
  {GLFW_KEY_3,"3"}, {GLFW_KEY_4,"4"}, {GLFW_KEY_5,"5"},
  {GLFW_KEY_6,"6"}, {GLFW_KEY_7,"7"}, {GLFW_KEY_8,"8"},
  {GLFW_KEY_9,"9"}, {GLFW_KEY_0,"0"}, {GLFW_KEY_MINUS,"-"},
  {GLFW_KEY_EQUAL,"="}, {GLFW_KEY_BACKSPACE,"Bksp"}
};
static const key_entry KEY_ROW_2[] = {
  {GLFW_KEY_TAB,"Tab"}, {GLFW_KEY_Q,"Q"}, {GLFW_KEY_W,"W"},
  {GLFW_KEY_E,"E"}, {GLFW_KEY_R,"R"}, {GLFW_KEY_T,"T"},
  {GLFW_KEY_Y,"Y"}, {GLFW_KEY_U,"U"}, {GLFW_KEY_I,"I"},
  {GLFW_KEY_O,"O"}, {GLFW_KEY_P,"P"}, {GLFW_KEY_LEFT_BRACKET,"["},
  {GLFW_KEY_RIGHT_BRACKET,"]"}, {GLFW_KEY_BACKSLASH,"\\"}
};
static const key_entry KEY_ROW_3[] = {
  {GLFW_KEY_CAPS_LOCK,"Caps"}, {GLFW_KEY_A,"A"}, {GLFW_KEY_S,"S"},
  {GLFW_KEY_D,"D"}, {GLFW_KEY_F,"F"}, {GLFW_KEY_G,"G"},
  {GLFW_KEY_H,"H"}, {GLFW_KEY_J,"J"}, {GLFW_KEY_K,"K"},
  {GLFW_KEY_L,"L"}, {GLFW_KEY_SEMICOLON,";"},
  {GLFW_KEY_APOSTROPHE,"'"}, {GLFW_KEY_ENTER,"Enter"}
};
static const key_entry KEY_ROW_4[] = {
  {GLFW_KEY_LEFT_SHIFT,"Shift"}, {GLFW_KEY_Z,"Z"}, {GLFW_KEY_X,"X"},
  {GLFW_KEY_C,"C"}, {GLFW_KEY_V,"V"}, {GLFW_KEY_B,"B"},
  {GLFW_KEY_N,"N"}, {GLFW_KEY_M,"M"}, {GLFW_KEY_COMMA,","},
  {GLFW_KEY_PERIOD,"."}, {GLFW_KEY_SLASH,"/"},
  {GLFW_KEY_RIGHT_SHIFT,"Shift"}
};
static const key_entry KEY_ROW_5[] = {
  {GLFW_KEY_LEFT_CONTROL,"Ctrl"}, {GLFW_KEY_LEFT_ALT,"Alt"},
  {GLFW_KEY_SPACE,"Space"}, {GLFW_KEY_RIGHT_ALT,"Alt"},
  {GLFW_KEY_RIGHT_CONTROL,"Ctrl"}, {GLFW_KEY_LEFT,"<-"},
  {GLFW_KEY_UP,"Up"}, {GLFW_KEY_DOWN,"Down"}, {GLFW_KEY_RIGHT,"->"}
};
static const key_row KEY_ROWS[] = {
  {KEY_ROW_0, ROW_COUNT(KEY_ROW_0)},
  {KEY_ROW_1, ROW_COUNT(KEY_ROW_1)},
  {KEY_ROW_2, ROW_COUNT(KEY_ROW_2)},
  {KEY_ROW_3, ROW_COUNT(KEY_ROW_3)},
  {KEY_ROW_4, ROW_COUNT(KEY_ROW_4)},
  {KEY_ROW_5, ROW_COUNT(KEY_ROW_5)}
};
static const int KEY_ROWS_COUNT = ROW_COUNT(KEY_ROWS);

/* What is currently selected / being dragged in the editor. */
enum { SEL_NONE = 0, SEL_KEY, SEL_BOOST, SEL_JOY, SEL_ZOOM };

static struct {
  bool editor_open;
  bool picker_open;
  bool picker_changes_selected;
  int selected_idx;
  int drag_idx;
  float drag_off_x;
  float drag_off_y;
  int sel_kind;  /* SEL_* of the selected item (selected_idx = key slot) */
  int drag_kind; /* SEL_* of the item being dragged */
  /* Last-frame screen rects of the editor's own windows (tap filtering). */
  bool panel_valid;
  bool panel_hidden; /* edit panel collapsed to a small tab */
  int panel_side;    /* 0 = auto (opposite the item), 1 = left, 2 = right */
  float panel_x0, panel_y0, panel_x1, panel_y1;
  bool bar_valid;
  float bar_x0, bar_y0, bar_x1, bar_y1;

#ifdef ANDROID
  tenv *env;
  int native_pointer[MAX_KEY_BTNS];
  bool native_down[MAX_KEY_BTNS];
  bool native_press_pending[MAX_KEY_BTNS];
  bool native_release_pending[MAX_KEY_BTNS];
  bool native_release_deferred[MAX_KEY_BTNS];
  float hit_x0[MAX_KEY_BTNS];
  float hit_y0[MAX_KEY_BTNS];
  float hit_x1[MAX_KEY_BTNS];
  float hit_y1[MAX_KEY_BTNS];
  bool hit_valid[MAX_KEY_BTNS];
#endif
  bool pressed[MAX_KEY_BTNS];
} s_kb = {
  .editor_open = false,
  .picker_open = false,
  .picker_changes_selected = false,
  .selected_idx = -1,
  .drag_idx = -1
};

/* Keys are sized by two independent numbers:
     width  = KEY_REF_SIZE-based width * key_btn_wmul   (0.5x - 3.0x)
     height = rel_size                                  (0.5x - 1.5x of
                                                         KEY_REF_SIZE)
   so the Height slider changes only the height and the Width slider only the
   width. Circle keys have no separate width: their size follows rel_size. */
#define KEY_REF_SIZE 0.060f
#define KEY_W_MIN 0.5f
#define KEY_W_MAX 3.0f
#define KEY_H_MIN 0.5f
#define KEY_H_MAX 1.5f

/* Height limit: a (non-circle) key may be at most 80% as tall as it is wide.
   Returns the largest Height-slider value (in KEY_REF_SIZE units) allowed for
   this key at width multiplier wmul. */
#define KEY_H_MAX_RATIO 0.80f
static float key_h_limit(const custom_key_btn *b, float wmul) {
  float label_extra = (float)strlen(b->label) * 0.08f;
  float half_w_in_ref = (1.10f + label_extra) * wmul;
  float lim = KEY_H_MAX_RATIO * half_w_in_ref / 0.66f;
  return lim < KEY_H_MIN ? KEY_H_MIN : lim;
}

static float sane_wmul(float w) {
  return (isfinite(w) && w >= KEY_W_MIN && w <= KEY_W_MAX) ? w : 1.0f;
}

static void button_extents(const custom_key_btn *b, float screen_h, float wmul,
                           bool circle, float *half_w, float *half_h) {
  float size = b->rel_size * screen_h;
  *half_h = size * 0.66f;
  if (circle) {
    /* Circles scale as a whole with rel_size. */
    float label_extra = (float)strlen(b->label) * size * 0.08f;
    *half_w = size * 1.10f + label_extra;
    return;
  }
  /* Width no longer follows the height: it comes from a fixed reference. */
  float ref = KEY_REF_SIZE * screen_h;
  float label_extra = (float)strlen(b->label) * ref * 0.08f;
  *half_w = (ref * 1.10f + label_extra) * wmul;
}

static int find_free_slot(user_settings *usrs) {
  for (int i = 0; i < MAX_KEY_BTNS; ++i)
    if (!usrs->key_btns[i].active) return i;
  return -1;
}

static void set_button_key(custom_key_btn *b, int glfw_key,
                           const char *label) {
  if (!b) return;
  b->glfw_key = glfw_key;
  strncpy(b->label, label, sizeof(b->label) - 1);
  b->label[sizeof(b->label) - 1] = 0;
}

static int add_key_btn(user_settings *usrs, int glfw_key, const char *label) {
  int idx = find_free_slot(usrs);
  if (idx < 0) return -1;
  custom_key_btn *b = &usrs->key_btns[idx];
  memset(b, 0, sizeof(*b));
  b->active = true;
  set_button_key(b, glfw_key, label);
  b->rel_x = 0.50f + ((idx % 3) - 1) * 0.10f;
  b->rel_y = 0.50f + ((idx / 3) % 3 - 1) * 0.12f;
  b->rel_size = 0.060f;
  b->opacity = 0.85f;
  usrs->key_btn_shape[idx] = 0;
  usrs->key_btn_wmul[idx] = 1.0f;
  KBLOG("added keyboard button idx=%d key=%d label=%s", idx, glfw_key, label);
  return idx;
}

#ifdef ANDROID
static void native_release_slot_now(int i) {
  if (i < 0 || i >= MAX_KEY_BTNS) return;
  if (s_kb.env && s_kb.env->usr) {
    custom_key_btn *b = &s_kb.env->usr->usrs.key_btns[i];
    int key = b->glfw_key;
    if (key > 0 && key < 512)
      s_kb.env->usr->gdata.data.fake_key_down[key] = false;
  }
  s_kb.pressed[i] = false;
  s_kb.native_down[i] = false;
  s_kb.native_press_pending[i] = false;
  s_kb.native_release_pending[i] = false;
  s_kb.native_release_deferred[i] = false;
  s_kb.native_pointer[i] = -1;
}

static bool native_hit_render_space(const custom_key_btn *b, int i,
                                    float x, float y, float render_w,
                                    float render_h) {
  float cx = b->rel_x * render_w;
  float cy = b->rel_y * render_h;
  float hw, hh;
  float wm = 1.0f;
  bool circ = false;
  if (s_kb.env && s_kb.env->usr) {
    circ = s_kb.env->usr->usrs.key_btn_shape[i] == 1;
    wm = sane_wmul(s_kb.env->usr->usrs.key_btn_wmul[i]);
  }
  button_extents(b, render_h, wm, circ, &hw, &hh);
  hw *= 1.28f;
  hh *= (b->rel_y < 0.35f ? 1.55f : 1.32f);
  if (x >= cx - hw && x <= cx + hw && y >= cy - hh && y <= cy + hh)
    return true;

  if (s_kb.hit_valid[i] && x >= s_kb.hit_x0[i] && x <= s_kb.hit_x1[i] &&
      y >= s_kb.hit_y0[i] && y <= s_kb.hit_y1[i])
    return true;
  return false;
}

static bool native_button_hit(const custom_key_btn *b, int i, float raw_x,
                              float raw_y) {
  if (!s_kb.env || !s_kb.env->wnd || !s_kb.env->ctx || !b) return false;
  float render_w = (float)s_kb.env->ctx->size[0];
  float render_h = (float)s_kb.env->ctx->size[1];
  float native_w = (float)s_kb.env->wnd->size[0];
  float native_h = (float)s_kb.env->wnd->size[1];
  if (s_kb.env->wnd->native_window) {
    int actual_w = ANativeWindow_getWidth(s_kb.env->wnd->native_window);
    int actual_h = ANativeWindow_getHeight(s_kb.env->wnd->native_window);
    if (actual_w > 0 && actual_h > 0) {
      native_w = (float)actual_w;
      native_h = (float)actual_h;
    }
  }
  if (native_w < 1 || native_h < 1 || render_w < 1 || render_h < 1)
    return false;

  float x = raw_x * render_w / native_w;
  float y = raw_y * render_h / native_h;
  if (native_hit_render_space(b, i, x, y, render_w, render_h)) return true;

  bool orientation_mismatch = (native_w < native_h) != (render_w < render_h);
  if (orientation_mismatch) {
    float cw_x = raw_y * render_w / native_h;
    float cw_y = (native_w - raw_x) * render_h / native_w;
    if (native_hit_render_space(b, i, cw_x, cw_y, render_w, render_h))
      return true;
    float ccw_x = (native_h - raw_y) * render_w / native_h;
    float ccw_y = raw_x * render_h / native_w;
    if (native_hit_render_space(b, i, ccw_x, ccw_y, render_w, render_h))
      return true;
  }
  return false;
}

static void native_apply_pending(tenv *env) {
  if (!env || !env->usr) return;
  for (int i = 0; i < MAX_KEY_BTNS; ++i) {
    custom_key_btn *b = &env->usr->usrs.key_btns[i];
    int key = b->glfw_key;
    if (s_kb.native_release_deferred[i]) {
      if (key > 0 && key < 512) env->usr->gdata.data.fake_key_down[key] = false;
      s_kb.pressed[i] = false;
      s_kb.native_release_deferred[i] = false;
    }
    if (s_kb.native_press_pending[i]) {
      if (key > 0 && key < 512) {
        env->usr->gdata.data.fake_key_pressed[key] = true;
        env->usr->gdata.data.fake_key_down[key] = true;
      }
      s_kb.pressed[i] = true;
      s_kb.native_press_pending[i] = false;
      if (!s_kb.native_down[i]) {
        s_kb.native_release_pending[i] = false;
        s_kb.native_release_deferred[i] = true;
      }
      continue;
    }
    if (s_kb.native_down[i]) {
      if (key > 0 && key < 512) env->usr->gdata.data.fake_key_down[key] = true;
      s_kb.pressed[i] = true;
    } else if (s_kb.native_release_pending[i]) {
      if (key > 0 && key < 512) env->usr->gdata.data.fake_key_down[key] = false;
      s_kb.pressed[i] = false;
      s_kb.native_release_pending[i] = false;
    }
  }
}

bool ui_key_buttons_native_press(float x, float y, int pointer_id) {
  if (!s_kb.env || !s_kb.env->usr || s_kb.editor_open ||
      s_kb.env->usr->gdata.curr_screen != PLAYING)
    return false;
  user_settings *usrs = &s_kb.env->usr->usrs;
  for (int i = MAX_KEY_BTNS - 1; i >= 0; --i) {
    custom_key_btn *b = &usrs->key_btns[i];
    if (!b->active || s_kb.native_pointer[i] >= 0) continue;
    if (!native_button_hit(b, i, x, y)) continue;
    s_kb.native_pointer[i] = pointer_id;
    s_kb.native_down[i] = true;
    s_kb.native_press_pending[i] = true;
    s_kb.native_release_pending[i] = false;
    s_kb.native_release_deferred[i] = false;
    s_kb.pressed[i] = true;
    return true;
  }
  return false;
}

bool ui_key_buttons_native_release(int pointer_id) {
  bool handled = false;
  for (int i = 0; i < MAX_KEY_BTNS; ++i) {
    if (s_kb.native_pointer[i] != pointer_id) continue;
    s_kb.native_pointer[i] = -1;
    s_kb.native_down[i] = false;
    s_kb.native_release_pending[i] = true;
    handled = true;
  }
  return handled;
}

void ui_key_buttons_native_cancel_all(void) {
  for (int i = 0; i < MAX_KEY_BTNS; ++i)
    if (s_kb.native_pointer[i] >= 0 || s_kb.native_down[i] ||
        s_kb.native_press_pending[i] || s_kb.native_release_pending[i] ||
        s_kb.native_release_deferred[i])
      native_release_slot_now(i);
}
#endif

void ui_key_buttons_init(tenv *env) {
  memset(&s_kb, 0, sizeof(s_kb));
  s_kb.selected_idx = -1;
  s_kb.drag_idx = -1;
#ifdef ANDROID
  s_kb.env = env;
  for (int i = 0; i < MAX_KEY_BTNS; ++i) s_kb.native_pointer[i] = -1;
#else
  (void)env;
#endif
}

void ui_key_buttons_destroy(tenv *env) {
#ifdef ANDROID
  ui_key_buttons_native_cancel_all();
  s_kb.env = NULL;
#endif
  (void)env;
}

/* Screen to go back to when the editor is closed: PLAYING when it was opened
   from a live match with the Keyboard layout hotkey, CONTROLS otherwise. */
static int s_return_screen = CONTROLS;
/* True when the editor was opened from the in-match Settings popup: the popup
   is hidden while editing and brought back on leaving. */
static bool s_reopen_popup = false;

void ui_key_buttons_open_editor(tenv *env) {
  if (!env || !env->usr) return;
  {
    game_data *gd = &env->usr->gdata;
    s_return_screen = (gd->curr_screen == PLAYING || gd->curr_screen == SETTINGS)
                          ? gd->curr_screen
                          : CONTROLS;
    s_reopen_popup = (gd->curr_screen == PLAYING && gd->settings_popup);
    if (s_reopen_popup) {
      gd->settings_popup = false;
      usr_hotkey(&env->usr->usrs, HOTKEY_OPEN_SETTINGS)->active = false;
    }
  }
#ifdef ANDROID
  ui_key_buttons_native_cancel_all();
#endif
  memset(s_kb.pressed, 0, sizeof(s_kb.pressed));
  s_kb.editor_open = true;
  s_kb.picker_open = false;
  s_kb.picker_changes_selected = false;
  s_kb.selected_idx = -1;
  s_kb.drag_idx = -1;
  s_kb.sel_kind = SEL_NONE;
  s_kb.drag_kind = SEL_NONE;
  env->usr->gdata.curr_screen = KEYBOARD_EDITOR;
}

static void close_editor(tenv *env) {
  if (!env || !env->usr) return;
  save_user_settings(&env->usr->usrs);
  s_kb.editor_open = false;
  s_kb.picker_open = false;
  s_kb.picker_changes_selected = false;
  s_kb.selected_idx = -1;
  s_kb.drag_idx = -1;
  s_kb.sel_kind = SEL_NONE;
  s_kb.drag_kind = SEL_NONE;
  env->usr->gdata.curr_screen = s_return_screen;
  if (s_reopen_popup) {
    s_reopen_popup = false;
    env->usr->gdata.settings_popup = true;
    usr_hotkey(&env->usr->usrs, HOTKEY_OPEN_SETTINGS)->active = true;
  }
}

static void read_pointer(tenv *env, bool *down, bool *clicked, bool *released,
                         float *mx, float *my) {
  ImGuiIO *io = igGetIO_Nil();
  *down = io && igIsMouseDown_Nil(0);
  *clicked = io && igIsMouseClicked_Bool(0, false);
  *released = io && igIsMouseReleased_Nil(0);
  *mx = io ? io->MousePos.x : 0;
  *my = io ? io->MousePos.y : 0;
#ifdef ANDROID
  touch_state *ui_touch = &env->wnd->ui_touch;
  if (ui_touch->just_down) {
    *clicked = true;
    *mx = ui_touch->x;
    *my = ui_touch->y;
  }
  if (ui_touch->just_up) {
    *released = true;
    *mx = ui_touch->x;
    *my = ui_touch->y;
  }
  /* ImGui's MousePos is only refreshed when its frame starts, so it trails the
     finger by a frame. While a finger is down use the live touch position. */
  if (ui_touch->down) {
    *mx = ui_touch->x;
    *my = ui_touch->y;
  }
  *down = *down || ui_touch->down;
#else
  (void)env;
#endif
}

static void draw_button(ImDrawList *dl, custom_key_btn *b, float sw, float sh,
                        bool editor, bool selected, bool pressed, bool circle,
                        float wmul, ImVec2 *p0, ImVec2 *p1) {
  float cx = b->rel_x * sw;
  float cy = b->rel_y * sh;
  float hw, hh;
  button_extents(b, sh, wmul, circle, &hw, &hh);
  ImVec2 ts;
  igCalcTextSize(&ts, b->label, NULL, false, -1.0f);
  float radius = hw > hh ? hw : hh;
  if (ts.x * 0.60f > radius) radius = ts.x * 0.60f;
  if (ts.y * 0.95f > radius) radius = ts.y * 0.95f;
  radius += sh * 0.010f;
  if (circle) {
    hw = radius;
    hh = radius;
  }
  *p0 = (ImVec2){cx - hw, cy - hh};
  *p1 = (ImVec2){cx + hw, cy + hh};
  int alpha = (int)(b->opacity * 255.0f);
  if (alpha < 20) alpha = 20;
  ImU32 bg;
  ImU32 border;
  if (editor) {
    bg = IM_COL32(70, 48, 128, alpha);
    border = selected ? IM_COL32(222, 196, 255, 255)
                      : IM_COL32(165, 135, 240, alpha);
  } else if (pressed) {
    bg = IM_COL32(45, 170, 95, alpha);
    border = IM_COL32(110, 255, 165, alpha);
  } else {
    bg = IM_COL32(45, 45, 50, (int)(alpha * 0.78f));
    border = IM_COL32(210, 210, 220, (int)(alpha * 0.72f));
  }
  if (circle) {
    ImDrawList_AddCircleFilled(dl, (ImVec2){cx, cy}, radius, bg, 28);
    ImDrawList_AddCircle(dl, (ImVec2){cx, cy}, radius, border, 28,
                         selected ? 3.0f : 1.8f);
  } else {
    float rounding = hh * 0.28f;
    ImDrawList_AddRectFilled(dl, *p0, *p1, bg, rounding, 0);
    ImDrawList_AddRect(dl, *p0, *p1, border, rounding, 0,
                       selected ? 3.0f : 1.8f);
  }
  ImDrawList_AddText_Vec2(dl,
      (ImVec2){cx - ts.x * 0.5f, cy - ts.y * 0.5f},
      IM_COL32(255, 255, 255, alpha), b->label, NULL);
}

static void draw_gameplay_buttons(tenv *env) {
  tuser_data *usr = env->usr;
  user_settings *usrs = &usr->usrs;
  float sw = (float)env->ctx->size[0];
  float sh = (float)env->ctx->size[1];
  bool mouse_down, mouse_clicked, mouse_released;
  float mx, my;
  read_pointer(env, &mouse_down, &mouse_clicked, &mouse_released, &mx, &my);
#ifdef ANDROID
  s_kb.env = env;
  native_apply_pending(env);
#endif
  ImDrawList *dl = igGetForegroundDrawList_ViewportPtr(igGetMainViewport());
  for (int i = 0; i < MAX_KEY_BTNS; ++i) {
    custom_key_btn *b = &usrs->key_btns[i];
    if (!b->active) continue;
    ImVec2 p0, p1;
    draw_button(dl, b, sw, sh, false, false, s_kb.pressed[i],
                usrs->key_btn_shape[i] == 1,
                sane_wmul(usrs->key_btn_wmul[i]), &p0, &p1);
#ifdef ANDROID
    float pad = sh * 0.018f;
    s_kb.hit_x0[i] = p0.x - pad;
    s_kb.hit_y0[i] = p0.y - pad;
    s_kb.hit_x1[i] = p1.x + pad;
    s_kb.hit_y1[i] = p1.y + pad;
    s_kb.hit_valid[i] = true;
    android_ui_capture_rect(s_kb.hit_x0[i], s_kb.hit_y0[i],
                            s_kb.hit_x1[i], s_kb.hit_y1[i]);
#endif
    bool over = mx >= p0.x && mx <= p1.x && my >= p0.y && my <= p1.y;
    if (mouse_clicked && over && !s_kb.pressed[i]) {
      s_kb.pressed[i] = true;
      int key = b->glfw_key;
      if (key > 0 && key < 512) {
        usr->gdata.data.fake_key_pressed[key] = true;
        usr->gdata.data.fake_key_down[key] = true;
      }
    }
    if (s_kb.pressed[i]) {
      int key = b->glfw_key;
      if (key > 0 && key < 512) usr->gdata.data.fake_key_down[key] = true;
    }
    bool native_owns = false;
#ifdef ANDROID
    native_owns = s_kb.native_down[i] || s_kb.native_pointer[i] >= 0 ||
                  s_kb.native_press_pending[i] ||
                  s_kb.native_release_deferred[i];
#endif
    if (mouse_released && s_kb.pressed[i] && !native_owns) {
      s_kb.pressed[i] = false;
      int key = b->glfw_key;
      if (key > 0 && key < 512) usr->gdata.data.fake_key_down[key] = false;
    }
  }
  (void)mouse_down;
}

/* ======================================================================== */
/* Themed editor: keys + boost button + joystick ring + zoom slider.        */
/* Same crystal-purple look as the Settings popup.                          */
/* ======================================================================== */

static bool point_in_rect(float x, float y, float x0, float y0,
                          float x1, float y1) {
  return x >= x0 && x <= x1 && y >= y0 && y <= y1;
}

#define KB_TXT_PURPLE ((ImVec4){0.80f, 0.72f, 1.0f, 1.0f})
#define KB_SEL        ((ImVec4){0.510f, 0.294f, 0.910f, 0.95f})
#define KB_SEL_H      ((ImVec4){0.569f, 0.353f, 0.960f, 0.98f})
#define KB_SEL_A      ((ImVec4){0.450f, 0.243f, 0.850f, 1.00f})

typedef struct kb_circle {
  float cx, cy, r;
} kb_circle;

typedef struct kb_elem {
  int kind; /* SEL_KEY / SEL_BOOST / SEL_JOY / SEL_ZOOM */
  int idx;  /* key slot for SEL_KEY */
  bool circle;
  float cx, cy, r;
  ImVec2 p0, p1, badge;
} kb_elem;

static void clear_selection(void) {
  s_kb.sel_kind = SEL_NONE;
  s_kb.selected_idx = -1;
  s_kb.drag_kind = SEL_NONE;
  s_kb.drag_idx = -1;
  s_kb.panel_hidden = false;
}

static void select_item(int kind, int idx) {
  s_kb.sel_kind = kind;
  s_kb.selected_idx = (kind == SEL_KEY) ? idx : -1;
}

/* Same geometry as ui_overlay.c, so what you drag here is what you get. */
static kb_circle boost_geom(const user_settings *u, float sw, float sh) {
  kb_circle g;
  float margin = sw * 0.025f;
  if (u->boost_pos_custom) {
    g.r = sh * u->boost_rel_size;
    g.cx = sw * u->boost_rel_x;
    g.cy = sh * u->boost_rel_y;
  } else {
    g.r = sh * 0.125f;
    g.cx = u->ctrl_swap_sides ? (g.r + margin) : (sw - g.r - margin);
    g.cy = sh - g.r - margin;
  }
  return g;
}

static kb_circle joy_geom(const user_settings *u, float sw, float sh) {
  kb_circle g;
  float margin = sw * 0.025f;
  if (u->joy_pos_custom) {
    g.r = sh * u->joy_rel_size;
    g.cx = sw * u->joy_rel_x;
    g.cy = sh * u->joy_rel_y;
  } else {
    g.r = sh * 0.175f;
    g.cx = u->ctrl_swap_sides ? (sw - g.r - margin) : (g.r + margin);
    g.cy = sh - g.r - margin;
  }
  return g;
}

static void zoom_geom(const user_settings *u, float sw, float sh, float *x0,
                      float *y0, float *x1, float *y1, float *cx, float *cy) {
  float thick = u->zslider_thick;
  if (!(thick >= 0.5f && thick <= 3.0f)) thick = 1.0f;
  float half_l = sh * u->zslider_rel_h;
  float half_t = sh * 0.022f * thick;
  *cx = sw * u->zslider_rel_x;
  *cy = sh * u->zslider_rel_y;
  if (u->zslider_horizontal) {
    *x0 = *cx - half_l;
    *x1 = *cx + half_l;
    *y0 = *cy - half_t * 1.5f;
    *y1 = *cy + half_t * 1.5f;
  } else {
    *x0 = *cx - half_t * 1.5f;
    *x1 = *cx + half_t * 1.5f;
    *y0 = *cy - half_l;
    *y1 = *cy + half_l;
  }
}

/* A circle control that still follows the default (swap-aware) spot becomes
   "custom" the first time it is dragged or resized, starting from where it
   currently is, so it never jumps. */
static void circle_ensure(bool *custom, float *rx, float *ry, float *rs,
                          kb_circle g, float sw, float sh) {
  if (*custom) return;
  *custom = true;
  *rx = g.cx / sw;
  *ry = g.cy / sh;
  *rs = g.r / sh;
}

/* ---- form helpers (label | widget rows) ---- */

static bool form_begin(const char *id) {
  if (!igBeginTable(id, 2, ImGuiTableFlags_None, (ImVec2){0.0f, 0.0f}, 0.0f))
    return false;
  igTableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed,
                     igGetFontSize() * 5.6f, 0);
  igTableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.0f, 0);
  return true;
}

static void form_row(const char *label) {
  igTableNextRow(ImGuiTableRowFlags_None, 0.0f);
  igTableSetColumnIndex(0);
  igAlignTextToFramePadding();
  igText("%s", label);
  igTableSetColumnIndex(1);
}

static bool form_slider(const char *label, const char *id, float *v, float mn,
                        float mx, const char *fmt) {
  form_row(label);
  igSetNextItemWidth(-1);
  return igSliderFloat(id, v, mn, mx, fmt, ImGuiSliderFlags_AlwaysClamp);
}

static bool form_check(const char *label, const char *id, bool *v) {
  form_row(label);
  return igCheckbox(id, v);
}

static bool tinted_button(const char *label, ImVec2 size, ImVec4 c, ImVec4 ch,
                          ImVec4 ca) {
  igPushStyleColor_Vec4(ImGuiCol_Button, c);
  igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, ch);
  igPushStyleColor_Vec4(ImGuiCol_ButtonActive, ca);
  bool pressed = igButton(label, size);
  igPopStyleColor(3);
  return pressed;
}

static bool purple_button(const char *label, ImVec2 size) {
  return tinted_button(label, size, KB_SEL, KB_SEL_H, KB_SEL_A);
}

static bool danger_button(const char *label, ImVec2 size) {
  return tinted_button(label, size, (ImVec4){0.62f, 0.14f, 0.20f, 0.95f},
                       (ImVec4){0.72f, 0.20f, 0.27f, 1.0f},
                       (ImVec4){0.52f, 0.10f, 0.16f, 1.0f});
}

/* Bottom row shared by every edit panel: [secondary] [Done]. */
static void panel_bottom_row(const char *left_label, bool left_danger,
                             bool *left_pressed, bool *done) {
  igSpacing();
  ImVec2 av;
  igGetContentRegionAvail(&av);
  float gap = igGetStyle()->ItemSpacing.x;
  float bw = (av.x - gap) * 0.5f;
  float bh = igGetFrameHeight() * 1.15f;
  *left_pressed = left_danger ? danger_button(left_label, (ImVec2){bw, bh})
                              : igButton(left_label, (ImVec2){bw, bh});
  igSameLine(0, gap);
  *done = purple_button("Done", (ImVec2){bw, bh});
}

/* ---- floating edit panel ----
   It only exists while something is selected, sits on the side of the screen
   opposite to the selected item, and turns into a faint, click-through ghost
   while you drag, so it never blocks placing anything. */

static bool panel_begin(const char *id, float sw, float sh, float ui_s,
                        float elem_cx, bool dragging) {
  float pw = sw * 0.30f;
  if (pw < 270.0f * ui_s) pw = 270.0f * ui_s;
  if (pw > 400.0f * ui_s) pw = 400.0f * ui_s;
  float margin = 14.0f * ui_s;
  bool right = elem_cx < sw * 0.5f;
  if (s_kb.panel_side == 1) right = false;
  if (s_kb.panel_side == 2) right = true;
  igPushStyleVar_Float(ImGuiStyleVar_Alpha, dragging ? 0.28f : 1.0f);
  ImGuiWindowFlags fl = ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                        ImGuiWindowFlags_NoNav |
                        ImGuiWindowFlags_NoSavedSettings;
  if (dragging) fl |= ImGuiWindowFlags_NoInputs;

  if (s_kb.panel_hidden) {
    /* Collapsed: just a small tab so the whole screen is free for placing
       keys. Tap it to bring the full panel back. */
    igSetNextWindowPos(
        (ImVec2){right ? sw - margin : margin, sh * 0.5f}, ImGuiCond_Always,
        (ImVec2){right ? 1.0f : 0.0f, 0.5f});
    igSetNextWindowSizeConstraints((ImVec2){0.0f, 0.0f},
                                   (ImVec2){sw, sh}, NULL, NULL);
    bool open = igBegin(id, NULL, fl);
    if (open) {
      if (igButton("Show panel", (ImVec2){0, 0})) s_kb.panel_hidden = false;
      if (!dragging) {
        ImVec2 p, sz;
        igGetWindowPos(&p);
        igGetWindowSize(&sz);
        s_kb.panel_x0 = p.x;
        s_kb.panel_y0 = p.y;
        s_kb.panel_x1 = p.x + sz.x;
        s_kb.panel_y1 = p.y + sz.y;
        s_kb.panel_valid = true;
      }
    }
    return false; /* caller skips its content; panel_end() still runs */
  }

  igSetNextWindowPos(
      (ImVec2){right ? sw - margin : margin, sh * 0.5f}, ImGuiCond_Always,
      (ImVec2){right ? 1.0f : 0.0f, 0.5f});
  igSetNextWindowSizeConstraints((ImVec2){pw, 0.0f},
                                 (ImVec2){pw, sh * 0.92f}, NULL, NULL);
  bool open = igBegin(id, NULL, fl);
  if (open) {
    /* Layout helpers: hide the panel, or swap it to the other side of the
       screen, so any spot on the canvas can be used for a key. */
    if (igButton("Hide", (ImVec2){0, 0})) s_kb.panel_hidden = true;
    igSameLine(0, 8.0f * ui_s);
    if (igButton(right ? "Move left" : "Move right", (ImVec2){0, 0}))
      s_kb.panel_side = right ? 1 : 2;
  }
  if (open && !dragging) {
    ImVec2 p, s;
    igGetWindowPos(&p);
    igGetWindowSize(&s);
    s_kb.panel_x0 = p.x;
    s_kb.panel_y0 = p.y;
    s_kb.panel_x1 = p.x + s.x;
    s_kb.panel_y1 = p.y + s.y;
    s_kb.panel_valid = true;
  }
  return open;
}

static void panel_end(void) {
  igEnd();
  igPopStyleVar(1);
}

static void panel_key(tenv *env, float sw, float sh, float ui_s,
                      bool dragging) {
  user_settings *u = &env->usr->usrs;
  int i = s_kb.selected_idx;
  if (i < 0 || i >= MAX_KEY_BTNS || !u->key_btns[i].active) {
    clear_selection();
    return;
  }
  custom_key_btn *b = &u->key_btns[i];
  bool circle = u->key_btn_shape[i] == 1;
  if (panel_begin("##kb_edit_key", sw, sh, ui_s, b->rel_x * sw, dragging)) {
    igTextColored(KB_TXT_PURPLE, "EDIT KEY");
    igSameLine(0, 10.0f * ui_s);
    igText("%s", b->label);
    if (igButton("Change key", (ImVec2){-1, 0})) {
      s_kb.picker_open = true;
      s_kb.picker_changes_selected = true;
    }
    if (form_begin("kbk_tbl")) {
      if (!circle)
        form_slider("Width", "##kb_w", &u->key_btn_wmul[i], KEY_W_MIN,
                    KEY_W_MAX, "%.2fx");
      float hm = b->rel_size / KEY_REF_SIZE;
      float hmax = circle ? KEY_H_MAX
                          : key_h_limit(b, sane_wmul(u->key_btn_wmul[i]));
      if (!circle && hm > hmax) { /* width was reduced: keep height <= 80% */
        hm = hmax;
        b->rel_size = hm * KEY_REF_SIZE;
      }
      if (form_slider(circle ? "Size" : "Height", "##kb_h", &hm, KEY_H_MIN,
                      hmax, "%.2fx"))
        b->rel_size = hm * KEY_REF_SIZE;
      form_slider("Opacity", "##kb_op", &b->opacity, 0.10f, 1.0f, "%.2f");
      bool circ = circle;
      if (form_check("Circle", "##kb_circle", &circ))
        u->key_btn_shape[i] = circ ? 1 : 0;
      igEndTable();
    }
    if (igCollapsingHeader_TreeNodeFlags("Fine position", 0)) {
      if (form_begin("kbk_pos")) {
        form_slider("X", "##kb_x", &b->rel_x, 0.03f, 0.97f, "%.2f");
        form_slider("Y", "##kb_y", &b->rel_y, 0.05f, 0.95f, "%.2f");
        igEndTable();
      }
    }
    bool del, done;
    panel_bottom_row("Delete", true, &del, &done);
    if (del) {
#ifdef ANDROID
      if (s_kb.native_pointer[i] >= 0) native_release_slot_now(i);
#endif
      memset(b, 0, sizeof(*b));
      u->key_btn_shape[i] = 0;
      u->key_btn_wmul[i] = 1.0f;
      clear_selection();
    } else if (done) {
      clear_selection();
    }
  }
  panel_end();
}

static void panel_circle(const char *id, const char *title, bool *custom,
                         float *rx, float *ry, float *rs, float *op,
                         kb_circle g, float sz_min, float sz_max, float sw,
                         float sh, float ui_s, bool dragging, bool *reset,
                         bool *done) {
  *reset = false;
  *done = false;
  if (panel_begin(id, sw, sh, ui_s, g.cx, dragging)) {
    igTextColored(KB_TXT_PURPLE, "%s", title);
    if (form_begin("c_tbl")) {
      float sz = g.r / sh;
      if (form_slider("Size", "##c_size", &sz, sz_min, sz_max, "%.3f")) {
        circle_ensure(custom, rx, ry, rs, g, sw, sh);
        *rs = sz;
      }
      form_slider("Opacity", "##c_op", op, 0.0f, 1.0f, "%.2f");
      igEndTable();
    }
    if (igCollapsingHeader_TreeNodeFlags("Fine position", 0)) {
      if (form_begin("c_pos")) {
        float fx = g.cx / sw, fy = g.cy / sh;
        if (form_slider("X", "##c_x", &fx, 0.05f, 0.95f, "%.2f")) {
          circle_ensure(custom, rx, ry, rs, g, sw, sh);
          *rx = fx;
        }
        if (form_slider("Y", "##c_y", &fy, 0.05f, 0.98f, "%.2f")) {
          circle_ensure(custom, rx, ry, rs, g, sw, sh);
          *ry = fy;
        }
        igEndTable();
      }
    }
    panel_bottom_row("Reset", false, reset, done);
  }
  panel_end();
}

static void panel_zoom(user_settings *u, float sw, float sh, float ui_s,
                       bool dragging) {
  if (!(u->zslider_thick >= 0.5f && u->zslider_thick <= 3.0f))
    u->zslider_thick = 1.0f;
  if (panel_begin("##kb_edit_zoom", sw, sh, ui_s, u->zslider_rel_x * sw,
                  dragging)) {
    igTextColored(KB_TXT_PURPLE, "EDIT ZOOM SLIDER");
    if (form_begin("z_tbl")) {
      form_slider("Length", "##z_len", &u->zslider_rel_h, 0.08f, 0.48f, "%.2f");
      form_slider("Thickness", "##z_thick", &u->zslider_thick, 0.5f, 3.0f,
                  "%.2fx");
      form_slider("Opacity", "##z_op", &u->zslider_opacity, 0.0f, 1.0f, "%.2f");
      form_check("Horizontal", "##z_hor", &u->zslider_horizontal);
      igEndTable();
    }
    if (igCollapsingHeader_TreeNodeFlags("Fine position", 0)) {
      if (form_begin("z_pos")) {
        form_slider("X", "##z_x", &u->zslider_rel_x, 0.02f, 0.98f, "%.2f");
        form_slider("Y", "##z_y", &u->zslider_rel_y, 0.10f, 0.90f, "%.2f");
        igEndTable();
      }
    }
    bool hide, done;
    panel_bottom_row("Hide bar", false, &hide, &done);
    if (hide) {
      u->zslider_hidden = true;
      clear_selection();
    } else if (done) {
      clear_selection();
    }
  }
  panel_end();
}

static void draw_edit_panel(tenv *env, float sw, float sh, float ui_s,
                            bool dragging) {
  user_settings *u = &env->usr->usrs;
  bool reset, done;
  switch (s_kb.sel_kind) {
    case SEL_KEY:
      panel_key(env, sw, sh, ui_s, dragging);
      break;
    case SEL_BOOST: {
      kb_circle g = boost_geom(u, sw, sh);
      panel_circle("##kb_edit_boost", "EDIT BOOST BUTTON", &u->boost_pos_custom,
                   &u->boost_rel_x, &u->boost_rel_y, &u->boost_rel_size,
                   &u->boost_opacity, g, 0.06f, 0.22f, sw, sh, ui_s, dragging,
                   &reset, &done);
      if (reset) {
        u->boost_pos_custom = false;
        u->boost_rel_x = u->ctrl_swap_sides ? 0.125f : 0.875f;
        u->boost_rel_y = 0.875f;
        u->boost_rel_size = 0.125f;
      }
      if (done) clear_selection();
      break;
    }
    case SEL_JOY: {
      kb_circle g = joy_geom(u, sw, sh);
      panel_circle("##kb_edit_joy", "EDIT JOYSTICK", &u->joy_pos_custom,
                   &u->joy_rel_x, &u->joy_rel_y, &u->joy_rel_size,
                   &u->joy_opacity, g, 0.08f, 0.28f, sw, sh, ui_s, dragging,
                   &reset, &done);
      if (reset) {
        u->joy_pos_custom = false;
        u->joy_rel_x = u->ctrl_swap_sides ? 0.875f : 0.125f;
        u->joy_rel_y = 0.825f;
        u->joy_rel_size = 0.175f;
      }
      if (done) clear_selection();
      break;
    }
    case SEL_ZOOM:
      panel_zoom(u, sw, sh, ui_s, dragging);
      break;
    default:
      break;
  }
}

static void draw_key_picker(tenv *env, float sw, float sh, float ui_s) {
  user_settings *usrs = &env->usr->usrs;
  float pw = sw * 0.94f;
  float ph = sh * 0.76f;
  float px = (sw - pw) * 0.5f;
  float py = sh * 0.10f;
  igSetNextWindowPos((ImVec2){px, py}, ImGuiCond_Always, (ImVec2){});
  igSetNextWindowSize((ImVec2){pw, ph}, ImGuiCond_Always);
  igSetNextWindowBgAlpha(0.97f);
  if (igBegin("##keyboard_picker", NULL,
              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
                  ImGuiWindowFlags_NoSavedSettings)) {
    igAlignTextToFramePadding();
    igTextColored(KB_TXT_PURPLE, s_kb.picker_changes_selected
                                     ? "CHOOSE THE REPLACEMENT KEY"
                                     : "CHOOSE A KEY TO ADD");
    igSameLine(0, 14.0f * ui_s);
    if (igButton("Back##picker", (ImVec2){0, 0})) {
      s_kb.picker_open = false;
      s_kb.picker_changes_selected = false;
    }
    if (!s_kb.picker_changes_selected && usrs->zslider_hidden) {
      igSameLine(0, 14.0f * ui_s);
      if (purple_button("Show zoom slider", (ImVec2){0, 0})) {
        usrs->zslider_hidden = false;
        s_kb.picker_open = false;
        select_item(SEL_ZOOM, -1);
      }
    }
    igSeparator();
    bool no_slot = !s_kb.picker_changes_selected && find_free_slot(usrs) < 0;
    if (no_slot)
      igTextColored((ImVec4){1.0f, 0.55f, 0.35f, 1.0f},
                    "Maximum buttons reached");
    igBeginDisabled(no_slot);
    float row_h = sh * 0.065f;
    if (row_h < 34.0f) row_h = 34.0f;
    if (row_h > 62.0f) row_h = 62.0f;
    for (int r = 0; r < KEY_ROWS_COUNT; ++r) {
      const key_row *row = &KEY_ROWS[r];
      char table_id[32];
      snprintf(table_id, sizeof(table_id), "##keyrow%d", r);
      if (igBeginTable(table_id, row->count, ImGuiTableFlags_None,
                       (ImVec2){0, row_h + 4.0f}, 0)) {
        for (int k = 0; k < row->count; ++k) {
          igTableNextColumn();
          ImVec2 avail;
          igGetContentRegionAvail(&avail);
          igPushID_Int(r * 100 + k);
          if (igButton(row->keys[k].label, (ImVec2){avail.x, row_h})) {
            if (s_kb.picker_changes_selected && s_kb.selected_idx >= 0 &&
                s_kb.selected_idx < MAX_KEY_BTNS &&
                usrs->key_btns[s_kb.selected_idx].active) {
              set_button_key(&usrs->key_btns[s_kb.selected_idx],
                             row->keys[k].glfw, row->keys[k].label);
            } else {
              int idx = add_key_btn(usrs, row->keys[k].glfw,
                                    row->keys[k].label);
              if (idx >= 0) select_item(SEL_KEY, idx);
            }
            s_kb.picker_open = false;
            s_kb.picker_changes_selected = false;
          }
          igPopID();
        }
        igEndTable();
      }
    }
    igEndDisabled();
  }
  igEnd();
}

static void draw_circle_proxy(ImDrawList *dl, kb_circle g, float opacity,
                              const char *label, bool selected, bool ring) {
  int a = (int)(opacity * 255.0f);
  if (a < 70) a = 70;
  ImVec2 c = {g.cx, g.cy};
  ImDrawList_AddCircleFilled(dl, c, g.r, IM_COL32(110, 70, 205, a / 3), 48);
  ImDrawList_AddCircle(dl, c, g.r,
                       selected ? IM_COL32(222, 196, 255, 255)
                                : IM_COL32(165, 135, 240, a),
                       48, selected ? 3.0f : 1.8f);
  if (ring)
    ImDrawList_AddCircle(dl, c, g.r * 0.32f, IM_COL32(165, 135, 240, a / 2),
                         32, 1.5f);
  ImVec2 ts;
  igCalcTextSize(&ts, label, NULL, false, -1.0f);
  ImDrawList_AddText_Vec2(dl, (ImVec2){g.cx - ts.x * 0.5f, g.cy - ts.y * 0.5f},
                          IM_COL32(255, 255, 255, 235), label, NULL);
}

/* Moves the item being dragged to the finger. Runs BEFORE anything is drawn
   (see ui_key_buttons_update() and draw_editor()), so the picture always shows
   the position of the current frame instead of trailing one frame behind the
   finger. Safe to call more than once per frame. */
static void apply_drag(tenv *env) {
  if (!env || !env->usr || !env->ctx) return;
  user_settings *usrs = &env->usr->usrs;
  float sw = (float)env->ctx->size[0];
  float sh = (float)env->ctx->size[1];
  bool mouse_down, mouse_clicked, mouse_released;
  float mx, my;
  read_pointer(env, &mouse_down, &mouse_clicked, &mouse_released, &mx, &my);
  if (s_kb.drag_kind != SEL_NONE && mouse_down && !s_kb.picker_open) {
    float nx = (mx - s_kb.drag_off_x) / sw;
    float ny = (my - s_kb.drag_off_y) / sh;
    if (s_kb.drag_kind == SEL_KEY && s_kb.drag_idx >= 0) {
      custom_key_btn *b = &usrs->key_btns[s_kb.drag_idx];
      b->rel_x = nx;
      b->rel_y = ny;
      float hw, hh;
      button_extents(b, sh, sane_wmul(usrs->key_btn_wmul[s_kb.drag_idx]),
                     usrs->key_btn_shape[s_kb.drag_idx] == 1, &hw, &hh);
      float rx = hw / sw;
      float ry = hh / sh;
      if (b->rel_x < rx) b->rel_x = rx;
      if (b->rel_x > 1.0f - rx) b->rel_x = 1.0f - rx;
      if (b->rel_y < ry + 0.06f) b->rel_y = ry + 0.06f;
      if (b->rel_y > 1.0f - ry) b->rel_y = 1.0f - ry;
    } else if (s_kb.drag_kind == SEL_BOOST || s_kb.drag_kind == SEL_JOY) {
      bool boost = s_kb.drag_kind == SEL_BOOST;
      float *rx_p = boost ? &usrs->boost_rel_x : &usrs->joy_rel_x;
      float *ry_p = boost ? &usrs->boost_rel_y : &usrs->joy_rel_y;
      float rs = boost ? usrs->boost_rel_size : usrs->joy_rel_size;
      float rx = rs * sh / sw;
      if (nx < rx) nx = rx;
      if (nx > 1.0f - rx) nx = 1.0f - rx;
      if (ny < rs) ny = rs;
      if (ny > 1.0f - rs) ny = 1.0f - rs;
      *rx_p = nx;
      *ry_p = ny;
    } else if (s_kb.drag_kind == SEL_ZOOM) {
      if (nx < 0.02f) nx = 0.02f;
      if (nx > 0.98f) nx = 0.98f;
      if (ny < 0.10f) ny = 0.10f;
      if (ny > 0.90f) ny = 0.90f;
      usrs->zslider_rel_x = nx;
      usrs->zslider_rel_y = ny;
    }
  }
}

void ui_key_buttons_update(tenv *env) {
  if (!env || !env->usr) return;
  if (env->usr->gdata.curr_screen == KEYBOARD_EDITOR && s_kb.editor_open)
    apply_drag(env);
}

static void draw_editor(tenv *env) {
  tuser_data *usr = env->usr;
  user_settings *usrs = &usr->usrs;
  float sw = (float)env->ctx->size[0];
  float sh = (float)env->ctx->size[1];
#ifdef ANDROID
  s_kb.env = env;
  android_ui_capture_rect(0, 0, sw, sh);
#endif

  float ui_s = (sw < sh ? sw : sh) / 720.0f;
  if (ui_s < 0.75f) ui_s = 0.75f;
  if (ui_s > 1.30f) ui_s = 1.30f;

  /* Everything below draws on the background layer so ImGui windows stay on
     top; the real game (see bg_preview.c) is dimmed behind it. */
  ImDrawList *bg = igGetBackgroundDrawList(igGetMainViewport());
  ImDrawList_AddRectFilled(bg, (ImVec2){0, 0}, (ImVec2){sw, sh},
                           IM_COL32(8, 5, 20, 150), 0, 0);

  apply_drag(env);

  bool mouse_down, mouse_clicked, mouse_released;
  float mx, my;
  read_pointer(env, &mouse_down, &mouse_clicked, &mouse_released, &mx, &my);

  const bool dragging = s_kb.drag_kind != SEL_NONE && mouse_down;

  /* Where UI windows were last frame: taps there are not canvas taps. */
  bool on_ui = s_kb.picker_open;
  if (s_kb.bar_valid &&
      point_in_rect(mx, my, s_kb.bar_x0, s_kb.bar_y0, s_kb.bar_x1,
                    s_kb.bar_y1))
    on_ui = true;
  if (s_kb.panel_valid && s_kb.sel_kind != SEL_NONE && !dragging &&
      point_in_rect(mx, my, s_kb.panel_x0, s_kb.panel_y0, s_kb.panel_x1,
                    s_kb.panel_y1))
    on_ui = true;
  s_kb.panel_valid = false; /* set again if a panel is drawn this frame */

  float badge_r = sh * 0.022f;
  if (badge_r < 15.0f) badge_r = 15.0f;

  /* ---- build + draw every editable item ---- */
  kb_elem el[MAX_KEY_BTNS + 3];
  int n = 0;

  for (int i = 0; i < MAX_KEY_BTNS; ++i) {
    custom_key_btn *b = &usrs->key_btns[i];
    if (!b->active) continue;
    bool circ = usrs->key_btn_shape[i] == 1;
    ImVec2 p0, p1;
    draw_button(bg, b, sw, sh, true,
                s_kb.sel_kind == SEL_KEY && s_kb.selected_idx == i, false, circ,
                sane_wmul(usrs->key_btn_wmul[i]), &p0, &p1);
    kb_elem *e = &el[n++];
    e->kind = SEL_KEY;
    e->idx = i;
    e->circle = false;
    e->p0 = p0;
    e->p1 = p1;
    e->badge = (ImVec2){p1.x, p0.y};
  }

  {
    kb_circle g = boost_geom(usrs, sw, sh);
    draw_circle_proxy(bg, g, usrs->boost_opacity, "BOOST",
                      s_kb.sel_kind == SEL_BOOST, false);
    kb_elem *e = &el[n++];
    e->kind = SEL_BOOST;
    e->idx = -1;
    e->circle = true;
    e->cx = g.cx;
    e->cy = g.cy;
    e->r = g.r;
    e->badge = (ImVec2){g.cx + g.r * 0.7071f, g.cy - g.r * 0.7071f};
  }

  /* The joystick ring only exists in the Joystick scheme. */
  if (!usrs->ctrl_mode_trackpad) {
    kb_circle g = joy_geom(usrs, sw, sh);
    draw_circle_proxy(bg, g, usrs->joy_opacity, "JOYSTICK",
                      s_kb.sel_kind == SEL_JOY, true);
    kb_elem *e = &el[n++];
    e->kind = SEL_JOY;
    e->idx = -1;
    e->circle = true;
    e->cx = g.cx;
    e->cy = g.cy;
    e->r = g.r;
    e->badge = (ImVec2){g.cx + g.r * 0.7071f, g.cy - g.r * 0.7071f};
  }

  if (!usrs->zslider_hidden) {
    float x0, y0, x1, y1, zcx, zcy;
    zoom_geom(usrs, sw, sh, &x0, &y0, &x1, &y1, &zcx, &zcy);
    bool sel = s_kb.sel_kind == SEL_ZOOM;
    int a = (int)(usrs->zslider_opacity * 255.0f);
    if (a < 70) a = 70;
    float rr = (x1 - x0 < y1 - y0 ? x1 - x0 : y1 - y0) * 0.5f;
    ImDrawList_AddRectFilled(bg, (ImVec2){x0, y0}, (ImVec2){x1, y1},
                             IM_COL32(110, 70, 205, a / 3), rr, 0);
    ImDrawList_AddRect(bg, (ImVec2){x0, y0}, (ImVec2){x1, y1},
                       sel ? IM_COL32(222, 196, 255, 255)
                           : IM_COL32(165, 135, 240, a),
                       rr, 0, sel ? 3.0f : 1.8f);
    ImVec2 ts;
    igCalcTextSize(&ts, "ZOOM", NULL, false, -1.0f);
    ImDrawList_AddText_Vec2(bg, (ImVec2){zcx - ts.x * 0.5f, y0 - ts.y - 4.0f},
                            IM_COL32(255, 255, 255, 235), "ZOOM", NULL);
    kb_elem *e = &el[n++];
    e->kind = SEL_ZOOM;
    e->idx = -1;
    e->circle = false;
    e->p0 = (ImVec2){x0, y0};
    e->p1 = (ImVec2){x1, y1};
    e->badge = (ImVec2){x1, y0};
  }

  /* Edit badges. */
  for (int k = 0; k < n; ++k) {
    ImDrawList_AddCircleFilled(bg, el[k].badge, badge_r,
                               IM_COL32(130, 75, 232, 245), 20);
    ImVec2 es;
    igCalcTextSize(&es, "Edit", NULL, false, -1.0f);
    ImDrawList_AddText_Vec2(
        bg, (ImVec2){el[k].badge.x - es.x * 0.5f, el[k].badge.y - es.y * 0.5f},
        IM_COL32(255, 255, 255, 255), "Edit", NULL);
  }

  /* ---- tap / drag handling ---- */
  if (mouse_clicked && !on_ui) {
    int hit = -1;
    bool hit_badge = false;
    for (int k = 0; k < n; ++k) {
      float bx = mx - el[k].badge.x, by = my - el[k].badge.y;
      bool on_badge = bx * bx + by * by <= badge_r * badge_r * 1.25f;
      bool on_body;
      if (el[k].circle) {
        float dx = mx - el[k].cx, dy = my - el[k].cy;
        on_body = dx * dx + dy * dy <= el[k].r * el[k].r;
      } else {
        on_body = point_in_rect(mx, my, el[k].p0.x, el[k].p0.y, el[k].p1.x,
                                el[k].p1.y);
      }
      if (on_badge || on_body) {
        hit = k;
        hit_badge = on_badge;
      }
    }
    if (hit < 0) {
      clear_selection();
    } else {
      kb_elem *e = &el[hit];
      select_item(e->kind, e->idx);
      s_kb.drag_kind = SEL_NONE;
      s_kb.drag_idx = -1;
      if (!hit_badge) {
        s_kb.drag_kind = e->kind;
        s_kb.drag_idx = e->idx;
        float ccx, ccy;
        if (e->kind == SEL_KEY) {
          ccx = usrs->key_btns[e->idx].rel_x * sw;
          ccy = usrs->key_btns[e->idx].rel_y * sh;
        } else if (e->kind == SEL_BOOST) {
          kb_circle g = boost_geom(usrs, sw, sh);
          circle_ensure(&usrs->boost_pos_custom, &usrs->boost_rel_x,
                        &usrs->boost_rel_y, &usrs->boost_rel_size, g, sw, sh);
          ccx = usrs->boost_rel_x * sw;
          ccy = usrs->boost_rel_y * sh;
        } else if (e->kind == SEL_JOY) {
          kb_circle g = joy_geom(usrs, sw, sh);
          circle_ensure(&usrs->joy_pos_custom, &usrs->joy_rel_x,
                        &usrs->joy_rel_y, &usrs->joy_rel_size, g, sw, sh);
          ccx = usrs->joy_rel_x * sw;
          ccy = usrs->joy_rel_y * sh;
        } else {
          ccx = usrs->zslider_rel_x * sw;
          ccy = usrs->zslider_rel_y * sh;
        }
        s_kb.drag_off_x = mx - ccx;
        s_kb.drag_off_y = my - ccy;
      }
    }
  }

  if (mouse_released) {
    s_kb.drag_kind = SEL_NONE;
    s_kb.drag_idx = -1;
  }

  /* ---- themed windows ---- */
  igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 14.0f);
  igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, 1.0f);
  igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding,
                      (ImVec2){12 * ui_s, 10 * ui_s});
  igPushStyleVar_Vec2(ImGuiStyleVar_FramePadding, (ImVec2){8 * ui_s, 7 * ui_s});
  igPushStyleVar_Vec2(ImGuiStyleVar_ItemSpacing, (ImVec2){8 * ui_s, 8 * ui_s});
  igPushStyleColor_Vec4(ImGuiCol_WindowBg, (ImVec4){0.070f, 0.050f, 0.120f, 0.94f});
  igPushStyleColor_Vec4(ImGuiCol_Border, (ImVec4){0.690f, 0.580f, 0.960f, 0.55f});
  crystal_push_theme();

  /* Slim toolbar pill, top-centre: title + Add + Done. */
  igSetNextWindowPos((ImVec2){sw * 0.5f, 12.0f * ui_s}, ImGuiCond_Always,
                     (ImVec2){0.5f, 0.0f});
  if (igBegin("##kb_toolbar", NULL,
              ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
                  ImGuiWindowFlags_NoScrollbar |
                  ImGuiWindowFlags_NoSavedSettings)) {
    igAlignTextToFramePadding();
    igTextColored(KB_TXT_PURPLE, "ADJUST CONTROLS");
    igSameLine(0, 12.0f * ui_s);
    if (igButton("+ Add", (ImVec2){0, 0})) {
      s_kb.picker_open = true;
      s_kb.picker_changes_selected = false;
    }
    igSameLine(0, 8.0f * ui_s);
    if (purple_button("Done", (ImVec2){0, 0})) close_editor(env);
    ImVec2 p, s;
    igGetWindowPos(&p);
    igGetWindowSize(&s);
    s_kb.bar_x0 = p.x;
    s_kb.bar_y0 = p.y;
    s_kb.bar_x1 = p.x + s.x;
    s_kb.bar_y1 = p.y + s.y;
    s_kb.bar_valid = true;
  }
  igEnd();

  if (s_kb.bar_valid && s_kb.sel_kind == SEL_NONE && !s_kb.picker_open) {
    const char *hint = "Tap an item to edit it, drag to move it.";
    ImVec2 hs;
    igCalcTextSize(&hs, hint, NULL, false, -1.0f);
    ImDrawList_AddText_Vec2(
        bg, (ImVec2){sw * 0.5f - hs.x * 0.5f, s_kb.bar_y1 + 6.0f},
        IM_COL32(200, 190, 235, 210), hint, NULL);
  }

  if (s_kb.sel_kind != SEL_NONE && !s_kb.picker_open)
    draw_edit_panel(env, sw, sh, ui_s, dragging);
  if (s_kb.picker_open) draw_key_picker(env, sw, sh, ui_s);

  crystal_pop_theme();
  igPopStyleColor(2);
  igPopStyleVar(5);
}

void ui_key_buttons(tenv *env) {
  if (!env || !env->usr || !env->ctx) return;
  tuser_data *usr = env->usr;
  igPushFont(usr->imgui_data.regular_font[FONT_SIZE_SMALL],
             usr->imgui_data.regular_font[FONT_SIZE_SMALL]->LegacySize);
  if (usr->gdata.curr_screen == KEYBOARD_EDITOR && s_kb.editor_open)
    draw_editor(env);
  else if (usr->gdata.curr_screen == PLAYING)
    draw_gameplay_buttons(env);
  igPopFont();
}
