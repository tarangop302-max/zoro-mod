#ifdef ANDROID
#include "../android_glfw_shim.h"
#endif
#include "net_graph.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../user.h"

/*
 * All of the constants below are NTL's own (main-mt.js: so / oo / zo / co /
 * lo / Qo / bo / Do / mo), so the graph behaves and looks like the one people
 * are used to there.
 */
#define NG_WINDOW_MS 120000.0 /* history shown: 2 minutes                  */
#define NG_SAMPLE_GAP_MS 33.0 /* at most one history sample per 33 ms      */
#define NG_REBUILD_MS 125.0   /* per-pixel averages refreshed every 125 ms */
#define NG_MAX_SAMPLES 4096   /* 120 s / 33 ms = 3637                      */
#define NG_MAX_COLS 2048

#define NG_COL(r, g, b, a)                                                  \
  ((ImU32)(((ImU32)(a) << 24) | ((ImU32)(b) << 16) | ((ImU32)(g) << 8) | \
           (ImU32)(r)))

#define NG_COL_MS NG_COL(255, 77, 109, 255) /* #ff4d6d  ping */
#define NG_COL_FPS NG_COL(0, 229, 255, 255) /* #00e5ff  fps  */

typedef struct {
  double t;       /* real-time ms                                    */
  float fps_plot; /* value to plot (smoothed, unless it is a spike)  */
  float ms_plot;  /* NAN when there was no ping (not in a game)      */
} ng_sample;

static ng_sample s_buf[NG_MAX_SAMPLES];
static int s_first;
static int s_count;
static double s_last_sample_t = -1e18;

/* Frame-rate estimate over a short (250 ms) window, so single slow frames are
   visible -- the 1-second counter in game_data would average them away. */
static int s_fps_init;
static double s_fps_win_start;
static double s_fps_frames;
static float s_fps_now;

/* NTL's smoothing state: wo (ping), uo (its mean absolute deviation), Bo (fps) */
static float s_wo;
static float s_uo = 4.0f;
static float s_bo;

/* Cached per-pixel averages + axis ranges. */
static float s_ms_bin[NG_MAX_COLS];
static float s_fps_bin[NG_MAX_COLS];
static int s_bin_cols;
static double s_bin_t = -1e18;
static float s_ms_lo, s_ms_hi, s_fps_lo, s_fps_hi;

void net_graph_reset(void) {
  s_first = 0;
  s_count = 0;
  s_last_sample_t = -1e18;
  s_fps_init = 0;
  s_fps_frames = 0;
  s_fps_now = 0;
  s_wo = 0;
  s_uo = 4.0f;
  s_bo = 0;
  s_bin_cols = 0;
  s_bin_t = -1e18;
}

static void ng_push(ng_sample s) {
  if (s_count == NG_MAX_SAMPLES) {
    s_first = (s_first + 1) % NG_MAX_SAMPLES;
    s_count--;
  }
  s_buf[(s_first + s_count) % NG_MAX_SAMPLES] = s;
  s_count++;
}

void net_graph_sample(tenv* env) {
  game_data* gd = &env->usr->gdata;
  double now = glfwGetTime() * 1000.0;

  /* glfwSetTime(0) on (re)connect moves the clock back: the old history is on
     a different timeline, drop it. */
  if (s_count > 0 && now < s_last_sample_t) net_graph_reset();

  if (!s_fps_init || now < s_fps_win_start) {
    s_fps_init = 1;
    s_fps_win_start = now;
    s_fps_frames = 0;
  }
  s_fps_frames += 1.0;
  double win = now - s_fps_win_start;
  if (win >= 250.0) {
    s_fps_now = (float)(s_fps_frames * 1000.0 / win);
    s_fps_frames = 0;
    s_fps_win_start = now;
  }

  if (now - s_last_sample_t < NG_SAMPLE_GAP_MS) return;
  s_last_sample_t = now;

  float fps = s_fps_now > 0 ? s_fps_now : (float)gd->data.fps;

  /* Ping is only meaningful while actually in a game. Use the newest real
     round-trip sample (0 = none received yet). */
  float ms = NAN;
  if (gd->data.follow_view) {
    int last = (gd->data.cping + PING_SAMPLE_COUNT - 1) % PING_SAMPLE_COUNT;
    float v = gd->data.pings[last];
    if (v > 0) ms = v;
  }

  /* --- ping: NTL's adaptive smoothing. Ordinary jitter is smoothed with an
     exponential filter; a jump larger than max(10, 2.3 * typical deviation)
     is a spike and is plotted raw. --- */
  float ms_plot = NAN;
  if (isfinite(ms)) {
    if (!(s_wo > 0)) s_wo = ms;
    float diff = ms - s_wo;
    float adiff = fabsf(diff);
    s_uo = 0.95f * s_uo + 0.05f * adiff;
    float kb = fmaxf(4.0f, fminf(14.0f, 1.7f * s_uo));
    float thr = fmaxf(10.0f, 2.3f * kb);
    int spike = ms > s_wo + thr || ms < s_wo - thr;
    s_wo += (adiff <= kb ? 0.09f : 0.24f) * diff;
    ms_plot = spike ? ms : s_wo;
  } else {
    s_wo = 0;
  }

  /* --- fps: same idea, thresholds of 2 fps. --- */
  if (!(s_bo > 0)) s_bo = fps;
  float fdiff = fps - s_bo;
  int fspike = fps > s_bo + 2.0f || fps < s_bo - 2.0f;
  s_bo += (fabsf(fdiff) <= 2.0f ? 0.08f : 0.22f) * fdiff;

  ng_push((ng_sample){now, fspike ? fps : s_bo, ms_plot});

  while (s_count > 0 && now - s_buf[s_first].t > NG_WINDOW_MS) {
    s_first = (s_first + 1) % NG_MAX_SAMPLES;
    s_count--;
  }
}

/* Average the samples into one value per plot pixel and work out the axis
   ranges, exactly as NTL's B5() does. */
static void ng_rebuild(double now, int cols) {
  static float ms_sum[NG_MAX_COLS];
  static float fps_sum[NG_MAX_COLS];
  static int ms_n[NG_MAX_COLS];
  static int fps_n[NG_MAX_COLS];

  memset(ms_sum, 0, sizeof(float) * cols);
  memset(fps_sum, 0, sizeof(float) * cols);
  memset(ms_n, 0, sizeof(int) * cols);
  memset(fps_n, 0, sizeof(int) * cols);

  double per_col = NG_WINDOW_MS / cols;
  long long base = (long long)floor(now / per_col) - cols + 1;
  for (int i = 0; i < s_count; i++) {
    const ng_sample* s = &s_buf[(s_first + i) % NG_MAX_SAMPLES];
    if (s->t > now) continue;
    long long c = (long long)floor(s->t / per_col) - base;
    if (c < 0 || c >= cols) continue;
    if (isfinite(s->ms_plot)) {
      ms_sum[c] += s->ms_plot;
      ms_n[c]++;
    }
    fps_sum[c] += s->fps_plot;
    fps_n[c]++;
  }

  for (int i = 0; i < cols; i++) {
    s_ms_bin[i] = ms_n[i] ? ms_sum[i] / ms_n[i] : NAN;
    s_fps_bin[i] = fps_n[i] ? fps_sum[i] / fps_n[i] : NAN;
  }

  /* Ranges ignore the newest (still filling) column, like NTL. */
  float ms_lo = INFINITY, ms_hi = -INFINITY;
  float f_lo = INFINITY, f_hi = -INFINITY;
  for (int pass = 0; pass < 2; pass++) {
    int end = pass == 0 ? cols - 1 : cols;
    if (pass == 1 && isfinite(ms_lo) && isfinite(f_lo)) break;
    for (int i = 0; i < end; i++) {
      if (!isnan(s_ms_bin[i])) {
        if (s_ms_bin[i] < ms_lo) ms_lo = s_ms_bin[i];
        if (s_ms_bin[i] > ms_hi) ms_hi = s_ms_bin[i];
      }
      if (!isnan(s_fps_bin[i])) {
        if (s_fps_bin[i] < f_lo) f_lo = s_fps_bin[i];
        if (s_fps_bin[i] > f_hi) f_hi = s_fps_bin[i];
      }
    }
  }
  if (!isfinite(ms_lo)) {
    ms_lo = 0;
    ms_hi = 40;
  }
  if (!isfinite(f_lo)) f_hi = fmaxf(0, s_fps_now);

  float span = fmaxf(1.0f, ms_hi - ms_lo);
  float headroom = fmaxf(8.0f, 0.45f * span);
  ms_lo = fmaxf(0.0f, ms_lo - fmaxf(1.0f, 0.08f * span));
  ms_hi += headroom;

  s_ms_lo = ms_lo;
  s_ms_hi = ms_hi;
  s_fps_lo = 0;
  s_fps_hi = (isfinite(f_hi) && f_hi > 0) ? f_hi : 1.0f;
  s_bin_cols = cols;
  s_bin_t = now;
}

void net_graph_default_size(float base_font_px, float* out_w, float* out_h) {
  *out_w = 20.0f * base_font_px;
  *out_h = 5.2f * base_font_px;
}

static void ng_text(ImDrawList* dl, float x, float y, ImU32 col,
                    const char* text) {
  ImDrawList_AddText_Vec2(dl, (ImVec2){x, y}, col, text, NULL);
}

/* One polyline per run of consecutive columns that have data. */
static void ng_line(ImDrawList* dl, const float* bins, int cols, float px,
                    float py, float ph, float lo, float span, ImU32 col) {
  static ImVec2 pts[NG_MAX_COLS];
  int n = 0;
  for (int i = 0; i <= cols; i++) {
    if (i < cols && !isnan(bins[i])) {
      pts[n++] = (ImVec2){px + i, py + (1.0f - (bins[i] - lo) / span) * ph};
    } else {
      if (n >= 2) ImDrawList_AddPolyline(dl, pts, n, col, 0, 2.0f);
      n = 0;
    }
  }
}

void net_graph_draw(ImDrawList* dl, ImFont* font, float label_px, float x,
                    float y, float w, float h) {
  /* Layout in units of the label size; NTL uses 44 / 16 / 20 px around a 10 px
     font for the side / top / bottom margins. */
  float lf = label_px;
  float m_side = 2.8f * lf;
  float m_top = 1.5f * lf;
  float m_bottom = 1.7f * lf;

  float px = x + m_side;
  float py = y + m_top;
  float pw = w - 2.0f * m_side;
  float ph = h - m_top - m_bottom;
  if (pw < 40.0f || ph < 20.0f) return;

  int cols = (int)pw;
  if (cols > NG_MAX_COLS) cols = NG_MAX_COLS;

  double now = glfwGetTime() * 1000.0;
  if (cols != s_bin_cols || now < s_bin_t || now - s_bin_t >= NG_REBUILD_MS)
    ng_rebuild(now, cols);

  float ms_span = fmaxf(0.001f, s_ms_hi - s_ms_lo);
  float fps_span = fmaxf(0.001f, s_fps_hi - s_fps_lo);

  ImDrawList_PushClipRect(dl, (ImVec2){x, y}, (ImVec2){x + w, y + h}, true);

  /* panel */
  ImDrawList_AddRectFilled(dl, (ImVec2){x, y}, (ImVec2){x + w, y + h},
                           NG_COL(0, 0, 0, 77), 4.0f, 0);
  ImDrawList_AddRect(dl, (ImVec2){x, y}, (ImVec2){x + w, y + h},
                     NG_COL(255, 255, 255, 36), 4.0f, 0, 1.0f);
  ImDrawList_AddRect(dl, (ImVec2){px, py}, (ImVec2){px + pw, py + ph},
                     NG_COL(255, 255, 255, 31), 0.0f, 0, 1.0f);

  igPushFont(font, lf);
  float text_h = igGetTextLineHeight();
  char buf[32];
  ImVec2 sz;

  /* horizontal grid + axis labels: ping on the left, fps on the right */
  for (int j = 0; j <= 4; j++) {
    float t = j / 4.0f;
    float gy = py + roundf((1.0f - t) * ph);
    ImDrawList_AddLine(dl, (ImVec2){px, gy}, (ImVec2){px + pw, gy},
                       NG_COL(255, 255, 255, (j == 0 || j == 4) ? 36 : 20),
                       1.0f);

    snprintf(buf, sizeof buf, "%.0f", s_ms_lo + t * (s_ms_hi - s_ms_lo));
    ng_text(dl, x + 0.4f * lf, gy - text_h * 0.5f, NG_COL_MS, buf);

    snprintf(buf, sizeof buf, "%.0f", s_fps_lo + t * (s_fps_hi - s_fps_lo));
    igCalcTextSize(&sz, buf, NULL, false, -1);
    ng_text(dl, x + w - 0.4f * lf - sz.x, gy - text_h * 0.5f, NG_COL_FPS, buf);
  }

  /* vertical grid + time labels */
  for (int i = 0; i <= 4; i++) {
    float t = i / 4.0f;
    float gx = px + roundf(t * pw);
    ImDrawList_AddLine(dl, (ImVec2){gx, py}, (ImVec2){gx, py + ph},
                       NG_COL(255, 255, 255, 15), 1.0f);
    int secs = (int)roundf((1.0f - t) * (float)(NG_WINDOW_MS / 1000.0));
    if (secs > 0)
      snprintf(buf, sizeof buf, "-%ds", secs);
    else
      snprintf(buf, sizeof buf, "now");
    igCalcTextSize(&sz, buf, NULL, false, -1);
    ng_text(dl, gx - sz.x * 0.5f, py + ph + 0.2f * lf,
            NG_COL(255, 255, 255, 158), buf);
  }

  /* the two curves */
  ng_line(dl, s_ms_bin, cols, px, py, ph, s_ms_lo, ms_span, NG_COL_MS);
  ng_line(dl, s_fps_bin, cols, px, py, ph, s_fps_lo, fps_span, NG_COL_FPS);

  /* legend */
  igCalcTextSize(&sz, "PING", NULL, false, -1);
  ng_text(dl, px + 0.4f * lf, y + 0.25f * lf, NG_COL_MS, "PING");
  ng_text(dl, px + 0.4f * lf + sz.x + 0.9f * lf, y + 0.25f * lf, NG_COL_FPS,
          "FPS");

  igPopFont();
  ImDrawList_PopClipRect(dl);
}
