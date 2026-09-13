#include "key_buttons.h"

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
  (((ImU32)(A)<<24)|((ImU32)(B)<<16)|((ImU32)(G)<<8)|((ImU32)(R)<<0))
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

static struct {
  bool editor_open;
  bool picker_open;
  bool picker_changes_selected;
  int selected_idx;
  int drag_idx;
  float drag_off_x;
  float drag_off_y;

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

/* Boost button / joystick "virtual" entries on the keyboard-button editor
   canvas. These aren't stored in usrs->key_btns[] (that array is keyboard
   buttons only) -- they drag/resize the SAME boost_rel_*/joy_rel_* fields
   already used by the real touch overlay (ui_overlay.c) and by the numeric
   sliders in Controls, so adjusting them here changes the real thing, not
   a separate copy. Kept as their own small piece of state (rather than
   folded into s_kb.selected_idx/drag_idx, which index usrs->key_btns[])
   so there's no risk of an out-of-range key_btns[] access. */
typedef enum { VBTN_NONE = 0, VBTN_BOOST, VBTN_JOY } vbtn_kind;
static vbtn_kind s_vbtn_selected = VBTN_NONE;
static vbtn_kind s_vbtn_dragging = VBTN_NONE;
static float s_vbtn_drag_off_x = 0.0f;
static float s_vbtn_drag_off_y = 0.0f;

static void button_extents(const custom_key_btn *b, float screen_h,
                           float *half_w, float *half_h) {
  float size = b->rel_size * screen_h;
  float label_extra = (float)strlen(b->label) * size * 0.08f;
  *half_w = size * 1.10f + label_extra;
  *half_h = size * 0.66f;
  if (*half_w < *half_h * 1.35f) *half_w = *half_h * 1.35f;
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
  button_extents(b, render_h, &hw, &hh);
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

void ui_key_buttons_open_editor(tenv *env) {
  if (!env || !env->usr) return;
#ifdef ANDROID
  ui_key_buttons_native_cancel_all();
#endif
  memset(s_kb.pressed, 0, sizeof(s_kb.pressed));
  s_kb.editor_open = true;
  s_kb.picker_open = false;
  s_kb.picker_changes_selected = false;
  s_kb.selected_idx = -1;
  s_kb.drag_idx = -1;
  s_vbtn_selected = VBTN_NONE;
  s_vbtn_dragging = VBTN_NONE;
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
  s_vbtn_selected = VBTN_NONE;
  s_vbtn_dragging = VBTN_NONE;
  env->usr->gdata.curr_screen = CONTROLS;
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
  *down = *down || ui_touch->down;
#else
  (void)env;
#endif
}

static void draw_button(ImDrawList *dl, custom_key_btn *b, float sw, float sh,
                        bool editor, bool selected, bool pressed, bool circle,
                        ImVec2 *p0, ImVec2 *p1) {
  float cx = b->rel_x * sw;
  float cy = b->rel_y * sh;
  float hw, hh;
  button_extents(b, sh, &hw, &hh);
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
    bg = IM_COL32(55, 70, 95, alpha);
    border = selected ? IM_COL32(255, 205, 70, 255)
                      : IM_COL32(145, 170, 210, alpha);
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
                usrs->key_btn_shape[i] == 1, &p0, &p1);
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

static bool point_in_rect(float x, float y, float x0, float y0,
                          float x1, float y1) {
  return x >= x0 && x <= x1 && y >= y0 && y <= y1;
}

/* Mirrors the real default-position formula in ui_overlay.c exactly, so
   what's shown here (while not yet customized) matches what actually
   appears in a real match. */
static void vbtn_geometry(user_settings *usrs, vbtn_kind k, float sw,
                          float sh, float *cx, float *cy, float *r) {
  float margin = sw * 0.025f;
  bool swapped = usrs->ctrl_swap_sides;
  if (k == VBTN_BOOST) {
    if (usrs->boost_pos_custom) {
      *r  = sh * usrs->boost_rel_size;
      *cx = sw * usrs->boost_rel_x;
      *cy = sh * usrs->boost_rel_y;
    } else {
      *r  = sh * 0.125f;
      *cx = swapped ? (*r + margin) : (sw - *r - margin);
      *cy = sh - *r - margin;
    }
  } else {
    if (usrs->joy_pos_custom) {
      *r  = sh * usrs->joy_rel_size;
      *cx = sw * usrs->joy_rel_x;
      *cy = sh * usrs->joy_rel_y;
    } else {
      *r  = sh * 0.175f;
      *cx = swapped ? (sw - *r - margin) : (*r + margin);
      *cy = sh - *r - margin;
    }
  }
}

static void draw_vbtn(ImDrawList *dl, vbtn_kind k, float cx, float cy,
                      float r, bool selected, float sh, float *ex,
                      float *ey, float *badge_r) {
  const char *label = (k == VBTN_BOOST) ? "Boost" : "Joystick";
  ImU32 bg = IM_COL32(55, 70, 95, 160);
  ImU32 border = selected ? IM_COL32(255, 205, 70, 255)
                          : IM_COL32(145, 170, 210, 160);
  ImDrawList_AddCircleFilled(dl, (ImVec2){cx, cy}, r, bg, 40);
  ImDrawList_AddCircle(dl, (ImVec2){cx, cy}, r, border, 40,
                       selected ? 3.0f : 1.8f);
  ImVec2 ts;
  igCalcTextSize(&ts, label, NULL, false, -1.0f);
  ImDrawList_AddText_Vec2(dl, (ImVec2){cx - ts.x * 0.5f, cy - ts.y * 0.5f},
                          IM_COL32(255, 255, 255, 255), label, NULL);
  *badge_r = sh * 0.022f;
  if (*badge_r < 15.0f) *badge_r = 15.0f;
  *ex = cx + r * 0.7071f;
  *ey = cy - r * 0.7071f;
}

/* Non-interactive reference boxes for the other HUD elements a keyboard
   button might need to avoid -- leaderboard, teammates list, minimap, and
   the team/public chat panel. Repositioning those lives in their own
   dedicated editors (HUD_LAYOUT_EDITOR for the first three, the TEAM CHAT
   panel's own "Adjust position"/"Adjust size" for the last), so these are
   just labeled outlines showing where each currently sits. Minimap
   position/size is exact; leaderboard/teammates use a nominal box size
   since their real size depends on live match data this screen doesn't
   have. */
static void draw_ref_box(ImDrawList *dl, float x, float y, float w, float h,
                         const char *label, ImU32 col) {
  ImDrawList_AddRect(dl, (ImVec2){x, y}, (ImVec2){x + w, y + h}, col, 6.0f,
                     0, 1.5f);
  ImVec2 ts;
  igCalcTextSize(&ts, label, NULL, false, -1.0f);
  float lx = x + 4.0f, ly = y - ts.y - 3.0f;
  if (ly < 2.0f) ly = y + 4.0f;
  ImDrawList_AddRectFilled(dl, (ImVec2){lx - 3, ly - 1},
                           (ImVec2){lx + ts.x + 3, ly + ts.y + 1},
                           IM_COL32(0, 0, 0, 150), 3.0f, 0);
  ImDrawList_AddText_Vec2(dl, (ImVec2){lx, ly}, col, label, NULL);
}

static void draw_hud_reference_overlays(tenv *env, ImDrawList *dl, float sw,
                                        float sh) {
  user_settings *usrs = &env->usr->usrs;
  ImGuiStyle *style = igGetStyle();
  ImU32 col = IM_COL32(190, 160, 255, 200);
  float pad_x = style->WindowPadding.x;
  float pad_y = style->WindowPadding.y;

  /* No live match here, so there's no real score data to size these
     against -- these are position markers only (a nominal box size),
     never actual leaderboard/teammate entries. When not custom, the
     anchor math below is copied from ui_overlay.c's real default
     placement (not a guessed fraction of the screen), so the box lands
     where the real thing actually sits either way. */

  float lb_scale = usrs->leaderboard_pos_custom ? usrs->leaderboard_scale
                                                : 0.72f;
  float lb_w = sw * 0.18f * lb_scale;
  float lb_h = sh * 0.22f * lb_scale;
  float lb_x, lb_y;
  if (usrs->leaderboard_pos_custom) {
    lb_x = usrs->leaderboard_rel_x * sw;
    lb_y = usrs->leaderboard_rel_y * sh;
  } else {
    /* Real default (ui_overlay.c): hugs the top-right corner. */
    lb_x = sw - lb_w - pad_x;
    lb_y = pad_y;
  }
  draw_ref_box(dl, lb_x, lb_y, lb_w, lb_h, "Leaderboard", col);

  float tm_w = sw * 0.18f;
  float tm_h = sh * 0.16f;
  float tm_x, tm_y;
  if (usrs->teammates_pos_custom) {
    tm_x = usrs->teammates_rel_x * sw;
    tm_y = usrs->teammates_rel_y * sh;
  } else {
    /* Real default (ui_overlay.c): right-aligned, stacked directly
       below the leaderboard. */
    tm_x = sw - tm_w - pad_x;
    tm_y = lb_y + lb_h + style->ItemSpacing.y * 2.0f;
  }
  draw_ref_box(dl, tm_x, tm_y, tm_w, tm_h, "Teammates", col);

  float mm_max = fminf(sw, sh) * 0.46f;
  float mm_size = fminf((float)usrs->minimap_size, mm_max);
  float mm_x, mm_y;
  if (usrs->minimap_pos_custom) {
    mm_x = usrs->minimap_rel_x * sw - mm_size * 0.5f;
    mm_y = usrs->minimap_rel_y * sh - mm_size * 0.5f;
  } else {
    /* Real default (ui_overlay.c): bottom-right corner, with room
       reserved below for the nickname/IP/ping/timer stats block. */
    float mm_bottom_reserve = igGetFrameHeight() * 7.0f;
    mm_x = sw - mm_size - pad_x;
    mm_y = sh - mm_size - pad_y - mm_bottom_reserve;
  }
  draw_ref_box(dl, mm_x, mm_y, mm_size, mm_size, "Map", col);

  {
    /* rel_x/y/w/h always hold the real current position, custom or not
       (global_chat.c fills in the default every frame it draws). */
    float x = usrs->public_chat_rel_x * sw;
    float y = usrs->public_chat_rel_y * sh;
    float w = usrs->public_chat_rel_w * sw;
    float h = usrs->public_chat_rel_h * sh;
    if (w > 4.0f && h > 4.0f)
      draw_ref_box(dl, x, y, w, h, "Team/Public Chat", col);
  }
}

static void draw_vbtn_edit_panel(tenv *env, float sw, float sh) {
  if (s_vbtn_selected == VBTN_NONE) return;
  user_settings *usrs = &env->usr->usrs;
  bool is_boost = (s_vbtn_selected == VBTN_BOOST);
  float pw = sw * 0.25f;
  if (pw < 280.0f) pw = 280.0f;
  if (pw > 390.0f) pw = 390.0f;
  float ph = sh * 0.42f;
  float px = sw - pw - 18.0f;
  float py = (sh - ph) * 0.5f;
#ifdef ANDROID
  android_ui_capture_rect(px, py, px + pw, py + ph);
#endif
  igSetNextWindowPos((ImVec2){px, py}, ImGuiCond_Always, (ImVec2){});
  igSetNextWindowSize((ImVec2){pw, ph}, ImGuiCond_Always);
  igSetNextWindowBgAlpha(0.97f);
  if (igBegin("##vbtn_edit", NULL,
              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
              ImGuiWindowFlags_NoSavedSettings)) {
    igTextColored((ImVec4){0.50f, 0.78f, 1.0f, 1.0f}, "%s",
                  is_boost ? "Edit boost button" : "Edit joystick");
    igSeparator();
    bool *pos_custom = is_boost ? &usrs->boost_pos_custom
                                : &usrs->joy_pos_custom;
    float *rel_size = is_boost ? &usrs->boost_rel_size : &usrs->joy_rel_size;
    float *opacity = is_boost ? &usrs->boost_opacity : &usrs->joy_opacity;
    if (*pos_custom) {
      igText("Size");
      igSetNextItemWidth(-1);
      if (is_boost)
        igSliderFloat("##vbtn_size", rel_size, 0.06f, 0.22f, "%.3f",
                      ImGuiSliderFlags_AlwaysClamp);
      else
        igSliderFloat("##vbtn_size", rel_size, 0.08f, 0.28f, "%.3f",
                      ImGuiSliderFlags_AlwaysClamp);
    } else {
      igTextWrapped("Drag it on the canvas to switch to a custom position.");
    }
    igText("Opacity");
    igSetNextItemWidth(-1);
    igSliderFloat("##vbtn_opacity", opacity, 0.0f, 1.0f, "%.2f",
                  ImGuiSliderFlags_AlwaysClamp);
    igSpacing();
    if (*pos_custom && igButton("Reset to default position", (ImVec2){-1, 0})) {
      *pos_custom = false;
      if (is_boost) {
        usrs->boost_rel_x = usrs->ctrl_swap_sides ? 0.125f : 0.875f;
        usrs->boost_rel_y = 0.875f;
        usrs->boost_rel_size = 0.125f;
      } else {
        usrs->joy_rel_x = usrs->ctrl_swap_sides ? 0.875f : 0.125f;
        usrs->joy_rel_y = 0.825f;
        usrs->joy_rel_size = 0.175f;
      }
    }
    if (igButton("Close editor", (ImVec2){-1, 0})) s_vbtn_selected = VBTN_NONE;
  }
  igEnd();
}

static void draw_key_picker(tenv *env, float sw, float sh) {
  user_settings *usrs = &env->usr->usrs;
  float pw = sw * 0.94f;
  float ph = sh * 0.72f;
  float px = (sw - pw) * 0.5f;
  float py = sh * 0.12f;
#ifdef ANDROID
  android_ui_capture_rect(px, py, px + pw, py + ph);
#endif
  igSetNextWindowPos((ImVec2){px, py}, ImGuiCond_Always, (ImVec2){});
  igSetNextWindowSize((ImVec2){pw, ph}, ImGuiCond_Always);
  igSetNextWindowBgAlpha(0.985f);
  if (igBegin("##keyboard_picker", NULL,
              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
              ImGuiWindowFlags_NoSavedSettings)) {
    igTextColored((ImVec4){0.45f, 0.90f, 0.58f, 1.0f},
                  s_kb.picker_changes_selected ? "Choose the replacement key"
                                               : "Choose a key to add");
    igSameLine(0, -1);
    if (igButton("Back##picker", (ImVec2){0, 0})) {
      s_kb.picker_open = false;
      s_kb.picker_changes_selected = false;
    }
    igSeparator();
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
              if (idx >= 0) s_kb.selected_idx = idx;
            }
            s_kb.picker_open = false;
            s_kb.picker_changes_selected = false;
          }
          igPopID();
        }
        igEndTable();
      }
    }
  }
  igEnd();
}

static void draw_edit_panel(tenv *env, float sw, float sh) {
  if (s_kb.selected_idx < 0 || s_kb.selected_idx >= MAX_KEY_BTNS) return;
  custom_key_btn *b = &env->usr->usrs.key_btns[s_kb.selected_idx];
  if (!b->active) {
    s_kb.selected_idx = -1;
    return;
  }
  float pw = sw * 0.25f;
  if (pw < 280.0f) pw = 280.0f;
  if (pw > 390.0f) pw = 390.0f;
  float ph = sh * 0.70f;
  float px = sw - pw - 18.0f;
  float py = (sh - ph) * 0.5f;
#ifdef ANDROID
  android_ui_capture_rect(px, py, px + pw, py + ph);
#endif
  igSetNextWindowPos((ImVec2){px, py}, ImGuiCond_Always, (ImVec2){});
  igSetNextWindowSize((ImVec2){pw, ph}, ImGuiCond_Always);
  igSetNextWindowBgAlpha(0.97f);
  if (igBegin("##keyboard_button_edit", NULL,
              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
              ImGuiWindowFlags_NoSavedSettings)) {
    igTextColored((ImVec4){0.50f, 0.78f, 1.0f, 1.0f}, "Edit keyboard button");
    igSeparator();
    igText("Key: %s", b->label);
    if (igButton("Change key", (ImVec2){-1, 0})) {
      s_kb.picker_open = true;
      s_kb.picker_changes_selected = true;
    }
    igSpacing();
    igText("Position X");
    igSetNextItemWidth(-1);
    igSliderFloat("##kb_x", &b->rel_x, 0.03f, 0.97f, "%.2f",
                  ImGuiSliderFlags_AlwaysClamp);
    igText("Position Y");
    igSetNextItemWidth(-1);
    igSliderFloat("##kb_y", &b->rel_y, 0.05f, 0.95f, "%.2f",
                  ImGuiSliderFlags_AlwaysClamp);
    igText("Size");
    igSetNextItemWidth(-1);
    igSliderFloat("##kb_size", &b->rel_size, 0.030f, 0.140f, "%.3f",
                  ImGuiSliderFlags_AlwaysClamp);
    igText("Opacity");
    igSetNextItemWidth(-1);
    igSliderFloat("##kb_opacity", &b->opacity, 0.10f, 1.0f, "%.2f",
                  ImGuiSliderFlags_AlwaysClamp);
    bool circle_button = env->usr->usrs.key_btn_shape[s_kb.selected_idx] == 1;
    if (igCheckbox("Circle button", &circle_button))
      env->usr->usrs.key_btn_shape[s_kb.selected_idx] = circle_button ? 1 : 0;
    igSpacing();
    igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.62f, 0.12f, 0.12f, 1.0f});
    if (igButton("Delete", (ImVec2){-1, 0})) {
#ifdef ANDROID
      if (s_kb.native_pointer[s_kb.selected_idx] >= 0)
        native_release_slot_now(s_kb.selected_idx);
#endif
      memset(b, 0, sizeof(*b));
      env->usr->usrs.key_btn_shape[s_kb.selected_idx] = 0;
      s_kb.selected_idx = -1;
    }
    igPopStyleColor(1);
    if (s_kb.selected_idx >= 0 && igButton("Close editor", (ImVec2){-1, 0}))
      s_kb.selected_idx = -1;
  }
  igEnd();
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
  /* The editor canvas must be on the background layer. Drawing on the
     foreground layer hides every ImGui window (toolbar and picker/edit
     panel) even though the custom-drawn keys remain visible.

     This used to be a fully opaque black fill, which is why the real
     leaderboard/minimap/teammates list were never visible here even
     though HUD_LAYOUT_EDITOR (this screen's sibling) already rendered
     the real game underneath its own overlay. Now that KEYBOARD_EDITOR
     also renders the real game first (see main.c), this only needs to
     be a dimming scrim -- enough to keep the white title/edit-badge
     text readable, not enough to hide where the HUD actually sits. */
  ImDrawList *bg = igGetBackgroundDrawList(igGetMainViewport());
  ImDrawList_AddRectFilled(bg, (ImVec2){0, 0}, (ImVec2){sw, sh},
                           IM_COL32(0, 0, 0, 130), 0, 0);

  bool mouse_down, mouse_clicked, mouse_released;
  float mx, my;
  read_pointer(env, &mouse_down, &mouse_clicked, &mouse_released, &mx, &my);

  /* Keep the primary controls in the top-left safe area so they are visible
     above Android gesture/navigation insets on every aspect ratio. */
  const float panel_x0 = 18.0f;
  const float panel_y0 = 18.0f;
  const float panel_x1 = 286.0f;
  const float panel_y1 = 200.0f;
  bool pointer_on_panel = point_in_rect(mx, my, panel_x0, panel_y0,
                                        panel_x1, panel_y1);
  if (s_kb.selected_idx >= 0 || s_vbtn_selected != VBTN_NONE) {
    float epw = sw * 0.25f;
    if (epw < 280.0f) epw = 280.0f;
    if (epw > 390.0f) epw = 390.0f;
    pointer_on_panel = pointer_on_panel ||
      point_in_rect(mx, my, sw - epw - 18.0f, sh * 0.15f,
                    sw - 18.0f, sh * 0.85f);
  }
  if (s_kb.picker_open) pointer_on_panel = true;

  ImVec2 title_size;
  const char *title = "Adjust keyboard buttons";
  igCalcTextSize(&title_size, title, NULL, false, -1.0f);
  ImDrawList_AddText_Vec2(bg, (ImVec2){sw * 0.5f - title_size.x * 0.5f, 18.0f},
                          IM_COL32(255, 255, 255, 235), title, NULL);
  const char *hint = "Tap a button, boost, or joystick to edit it.";
  ImVec2 hint_size;
  igCalcTextSize(&hint_size, hint, NULL, false, -1.0f);
  ImDrawList_AddText_Vec2(bg,
      (ImVec2){sw * 0.5f - hint_size.x * 0.5f, 45.0f},
      IM_COL32(170, 170, 175, 220), hint, NULL);

  draw_hud_reference_overlays(env, bg, sw, sh);

  {
    vbtn_kind kinds[2];
    int nk = 0;
    kinds[nk++] = VBTN_BOOST;
    if (!usrs->ctrl_mode_trackpad) kinds[nk++] = VBTN_JOY;
    for (int vi = 0; vi < nk; vi++) {
      vbtn_kind k = kinds[vi];
      float cx, cy, r;
      vbtn_geometry(usrs, k, sw, sh, &cx, &cy, &r);
      float ex, ey, badge_r;
      draw_vbtn(bg, k, cx, cy, r, s_vbtn_selected == k, sh, &ex, &ey,
               &badge_r);
      ImDrawList_AddCircleFilled(bg, (ImVec2){ex, ey}, badge_r,
                                 IM_COL32(165, 45, 45, 245), 20);
      const char *edit_label = "Edit";
      ImVec2 es;
      igCalcTextSize(&es, edit_label, NULL, false, -1.0f);
      ImDrawList_AddText_Vec2(bg, (ImVec2){ex - es.x * 0.5f, ey - es.y * 0.5f},
                              IM_COL32(255, 255, 255, 255), edit_label, NULL);

      if (mouse_clicked && !pointer_on_panel) {
        float edx = mx - ex, edy = my - ey;
        if (edx * edx + edy * edy <= badge_r * badge_r * 1.25f) {
          s_vbtn_selected = k;
          s_kb.selected_idx = -1;
          s_vbtn_dragging = VBTN_NONE;
          continue;
        }
        float ddx = mx - cx, ddy = my - cy;
        if (ddx * ddx + ddy * ddy <= r * r) {
          s_vbtn_selected = k;
          s_kb.selected_idx = -1;
          s_vbtn_dragging = k;
          s_vbtn_drag_off_x = mx - cx;
          s_vbtn_drag_off_y = my - cy;
          if (k == VBTN_BOOST && !usrs->boost_pos_custom)
            usrs->boost_rel_size = 0.125f;
          if (k == VBTN_JOY && !usrs->joy_pos_custom)
            usrs->joy_rel_size = 0.175f;
        }
      }
    }
  }

  for (int i = 0; i < MAX_KEY_BTNS; ++i) {
    custom_key_btn *b = &usrs->key_btns[i];
    if (!b->active) continue;
    ImVec2 p0, p1;
    draw_button(bg, b, sw, sh, true, s_kb.selected_idx == i, false,
                usrs->key_btn_shape[i] == 1, &p0, &p1);
    float badge_r = sh * 0.022f;
    if (badge_r < 15.0f) badge_r = 15.0f;
    float ex = p1.x;
    float ey = p0.y;
    ImDrawList_AddCircleFilled(bg, (ImVec2){ex, ey}, badge_r,
                               IM_COL32(165, 45, 45, 245), 20);
    const char *edit_label = "Edit";
    ImVec2 es;
    igCalcTextSize(&es, edit_label, NULL, false, -1.0f);
    ImDrawList_AddText_Vec2(bg, (ImVec2){ex - es.x * 0.5f, ey - es.y * 0.5f},
                            IM_COL32(255,255,255,255), edit_label, NULL);

    if (mouse_clicked && !pointer_on_panel) {
      float edx = mx - ex, edy = my - ey;
      if (edx * edx + edy * edy <= badge_r * badge_r * 1.25f) {
        s_kb.selected_idx = i;
        s_kb.drag_idx = -1;
        s_vbtn_selected = VBTN_NONE;
        continue;
      }
      if (point_in_rect(mx, my, p0.x, p0.y, p1.x, p1.y)) {
        /* The full button is the edit target. It also becomes the drag
           target, matching the floating-button editor's old behaviour. */
        s_kb.selected_idx = i;
        s_kb.drag_idx = i;
        s_vbtn_selected = VBTN_NONE;
        s_kb.drag_off_x = mx - b->rel_x * sw;
        s_kb.drag_off_y = my - b->rel_y * sh;
      }
    }
  }

  if (s_kb.drag_idx >= 0 && mouse_down && !s_kb.picker_open) {
    custom_key_btn *b = &usrs->key_btns[s_kb.drag_idx];
    b->rel_x = (mx - s_kb.drag_off_x) / sw;
    b->rel_y = (my - s_kb.drag_off_y) / sh;
    float hw, hh;
    button_extents(b, sh, &hw, &hh);
    float rx = hw / sw;
    float ry = hh / sh;
    if (b->rel_x < rx) b->rel_x = rx;
    if (b->rel_x > 1.0f - rx) b->rel_x = 1.0f - rx;
    if (b->rel_y < ry + 0.06f) b->rel_y = ry + 0.06f;
    if (b->rel_y > 1.0f - ry) b->rel_y = 1.0f - ry;
  }
  if (mouse_released) s_kb.drag_idx = -1;

  if (s_vbtn_dragging != VBTN_NONE && mouse_down) {
    bool is_boost = (s_vbtn_dragging == VBTN_BOOST);
    bool *pos_custom = is_boost ? &usrs->boost_pos_custom
                                : &usrs->joy_pos_custom;
    float *rxp = is_boost ? &usrs->boost_rel_x : &usrs->joy_rel_x;
    float *ryp = is_boost ? &usrs->boost_rel_y : &usrs->joy_rel_y;
    float rsize = is_boost ? usrs->boost_rel_size : usrs->joy_rel_size;
    *pos_custom = true;
    *rxp = (mx - s_vbtn_drag_off_x) / sw;
    *ryp = (my - s_vbtn_drag_off_y) / sh;
    float rx = (sh * rsize) / sw;
    float ry = rsize;
    if (*rxp < rx) *rxp = rx;
    if (*rxp > 1.0f - rx) *rxp = 1.0f - rx;
    if (*ryp < ry) *ryp = ry;
    if (*ryp > 1.0f - ry) *ryp = 1.0f - ry;
  }
  if (mouse_released) s_vbtn_dragging = VBTN_NONE;

  igSetNextWindowPos((ImVec2){panel_x0, panel_y0}, ImGuiCond_Always,
                     (ImVec2){});
  igSetNextWindowSize((ImVec2){panel_x1 - panel_x0, panel_y1 - panel_y0},
                      ImGuiCond_Always);
  igSetNextWindowBgAlpha(0.97f);
  if (igBegin("##keyboard_editor_controls", NULL,
              ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
              ImGuiWindowFlags_NoSavedSettings)) {
    int free_slot = find_free_slot(usrs);
    if (free_slot < 0) igBeginDisabled(true);
    if (igButton("Add new button", (ImVec2){-1, 48.0f})) {
      s_kb.picker_open = true;
      s_kb.picker_changes_selected = false;
    }
    if (free_slot < 0) igEndDisabled();
    if (free_slot < 0)
      igTextColored((ImVec4){1.0f,0.55f,0.35f,1.0f}, "Maximum buttons reached");
    if (igButton("Back", (ImVec2){-1, 48.0f})) close_editor(env);
  }
  igEnd();

  if (s_kb.selected_idx >= 0)
    draw_edit_panel(env, sw, sh);
  if (s_vbtn_selected != VBTN_NONE)
    draw_vbtn_edit_panel(env, sw, sh);
  if (s_kb.picker_open) draw_key_picker(env, sw, sh);
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
