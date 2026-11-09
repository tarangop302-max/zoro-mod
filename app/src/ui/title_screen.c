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
#include "../game/lifetime_stats.h"
#include "crystal_theme.h"
#include "lobby_bg.h"
#include "kills_gallery.h"
#include "clips_gallery.h"
#include "settings.h" /* ui_touch_scroll */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool g_sl_popup_open = false;

/* ============================================================================
 * Server picker (the list button next to the IPv4 box).
 *
 * A themed, fixed-size panel with: search (IP / id / name), filter chips,
 * favorites (starred servers are pinned first and remembered between
 * launches), sortable PING / SLOTS / ID columns like NTL's picker, ping bars,
 * a FASTEST badge and a "Use fastest server" button.
 * ========================================================================== */
enum { SL_F_ALL, SL_F_FAV, SL_F_BD, SL_F_OFFICIAL, SL_F_FAST, SL_F_COUNT };
static const char* SL_FILTER_NAMES[SL_F_COUNT] = {
    "All", "\xe2\x98\x85 Favorites", "Battledome", "Official", "Fast (<80ms)"};
enum { SL_S_PING, SL_S_SLOTS, SL_S_ID };

static int  s_sl_filter = SL_F_ALL;
static int  s_sl_sort   = SL_S_PING;
static bool s_sl_desc   = false;
static char s_sl_search[40] = "";
static const game_data* s_sl_cmp_gd = NULL;
static bool s_sl_isfav[MAX_SERVER_LIST];

static bool sl_ping_ok(int p) { return p >= 0 && p < 9999; }

static long sl_sort_key(const game_data* g, int i, bool* unknown) {
  switch (s_sl_sort) {
    case SL_S_SLOTS:
      *unknown = g->server_list.slots[i] < 0;
      return g->server_list.slots[i];
    case SL_S_ID:
      *unknown = g->server_list.sids[i] < 0;
      return g->server_list.sids[i];
    default:
      *unknown = !sl_ping_ok(g->server_list.pings[i]);
      return g->server_list.pings[i];
  }
}

/* Favorites first (like NTL), then the chosen column; unknown values last. */
static int sl_cmp(const void* a, const void* b) {
  int i = *(const int*)a, j = *(const int*)b;
  if (s_sl_isfav[i] != s_sl_isfav[j]) return s_sl_isfav[i] ? -1 : 1;
  bool ui, uj;
  long ki = sl_sort_key(s_sl_cmp_gd, i, &ui);
  long kj = sl_sort_key(s_sl_cmp_gd, j, &uj);
  if (ui != uj) return ui ? 1 : -1;
  int r = 0;
  if (!ui) {
    r = ki < kj ? -1 : (ki > kj ? 1 : 0);
    if (s_sl_desc) r = -r;
  }
  return r ? r : i - j;
}

static bool sl_contains_ci(const char* hay, const char* needle) {
  size_t nl = strlen(needle);
  if (nl == 0) return true;
  for (; *hay; hay++) {
    size_t k = 0;
    while (k < nl && hay[k] &&
           tolower((unsigned char)hay[k]) == tolower((unsigned char)needle[k]))
      k++;
    if (k == nl) return true;
  }
  return false;
}

static ImVec4 sl_ping_color(int ping) {
  if (!sl_ping_ok(ping)) return (ImVec4){0.70f, 0.64f, 0.88f, 1.0f};
  if (ping < 80)  return (ImVec4){0.30f, 1.00f, 0.40f, 1.0f};
  if (ping < 150) return (ImVec4){1.00f, 1.00f, 0.30f, 1.0f};
  if (ping < 300) return (ImVec4){1.00f, 0.65f, 0.20f, 1.0f};
  return (ImVec4){1.00f, 0.40f, 0.40f, 1.0f};
}

static ImU32 sl_col(ImVec4 c) { return igColorConvertFloat4ToU32(c); }
static ImU32 sl_col_tinted(ImVec4 c) {
  return igColorConvertFloat4ToU32(crystal_tint(c));
}

static void sl_text_r(ImDrawList* dl, float right, float y, ImU32 col,
                      const char* s) {
  ImVec2 ts;
  igCalcTextSize(&ts, s, NULL, false, -1.0f);
  ImDrawList_AddText_Vec2(dl, (ImVec2){right - ts.x, y}, col, s, NULL);
}

typedef struct {
  float star_w, name_x, name_right, id_r, slots_r, ping_r, ping_w, slots_w, id_w;
  bool show_id;
} sl_cols;

/* Column positions for a row starting at screen x0 and `w` wide. Used by the
   header and every row so they always line up. */
static sl_cols sl_layout(float x0, float w, float u, float row_h) {
  sl_cols c;
  c.star_w  = row_h;
  c.ping_w  = u * 7.5f;
  c.slots_w = u * 4.5f;
  c.id_w    = u * 3.8f;
  c.name_x  = x0 + c.star_w + u * 0.6f;
  c.ping_r  = x0 + w - u * 0.7f;
  c.slots_r = c.ping_r - c.ping_w - u * 0.4f;
  c.id_r    = c.slots_r - c.slots_w;
  c.show_id = (c.id_r - c.id_w - c.name_x) > u * 12.0f;
  c.name_right = (c.show_id ? c.id_r - c.id_w : c.slots_r - c.slots_w) -
                 u * 0.5f;
  return c;
}

/* Globe icon for the server-list button, drawn with the draw list so it is
   crisp at any size/DPI and follows the lobby theme colour.
   state: 0 = pinged & ready (green), 1 = working (amber), 2 = error (red),
          3 = idle (grey). */
static void sl_draw_globe(ImDrawList* dl, ImVec2 bmin, ImVec2 bmax, bool hot,
                          bool held, int state) {
  const float w = bmax.x - bmin.x;
  const float h = bmax.y - bmin.y;
  const ImVec2 c = {bmin.x + w * 0.5f, bmin.y + h * 0.5f};
  const float r = (w < h ? w : h) * 0.30f;
  const float th = fmaxf(1.4f, w * 0.040f);

  /* soft halo + body */
  ImDrawList_AddCircleFilled(
      dl, c, r * 1.38f,
      sl_col_tinted((ImVec4){0.647f, 0.420f, 1.0f, hot ? 0.28f : 0.14f}), 40);
  ImDrawList_AddCircleFilled(
      dl, c, r,
      sl_col_tinted((ImVec4){0.510f, 0.294f, 0.910f, held ? 0.85f : 0.55f}), 40);

  const ImU32 line = sl_col((ImVec4){0.93f, 0.89f, 1.0f, hot ? 1.0f : 0.90f});
  const ImU32 thin = sl_col((ImVec4){0.93f, 0.89f, 1.0f, hot ? 0.80f : 0.62f});

  /* latitudes (equator + two parallels) and the polar axis */
  ImDrawList_AddLine(dl, (ImVec2){c.x - r, c.y}, (ImVec2){c.x + r, c.y}, thin, th * 0.8f);
  for (int s = -1; s <= 1; s += 2) {
    float y = c.y + (float)s * r * 0.52f;
    float hw = r * 0.854f; /* sqrt(1 - 0.52^2) */
    ImDrawList_AddLine(dl, (ImVec2){c.x - hw, y}, (ImVec2){c.x + hw, y}, thin, th * 0.8f);
  }
  ImDrawList_AddLine(dl, (ImVec2){c.x, c.y - r}, (ImVec2){c.x, c.y + r}, thin, th * 0.8f);

  /* meridian ellipse */
  {
    enum { N = 36 };
    ImVec2 pts[N];
    for (int i = 0; i < N; i++) {
      float a = (float)i * 6.2831853f / (float)N;
      pts[i] = (ImVec2){c.x + cosf(a) * r * 0.46f, c.y + sinf(a) * r};
    }
    ImDrawList_AddPolyline(dl, pts, N, thin, ImDrawFlags_Closed, th * 0.8f);
  }

  /* outline */
  ImDrawList_AddCircle(dl, c, r, line, 40, th);

  /* status light (top-right corner of the button) */
  {
    ImVec4 sc = state == 0 ? (ImVec4){0.30f, 1.00f, 0.40f, 1.0f}
              : state == 1 ? (ImVec4){1.00f, 0.80f, 0.25f, 1.0f}
              : state == 2 ? (ImVec4){1.00f, 0.40f, 0.40f, 1.0f}
                           : (ImVec4){0.70f, 0.64f, 0.88f, 1.0f};
    ImVec2 p = {bmax.x - w * 0.17f, bmin.y + h * 0.17f};
    float pr = w * 0.085f;
    if (state == 1) /* gentle pulse while fetching / pinging */
      pr *= 0.85f + 0.25f * (0.5f + 0.5f * sinf((float)igGetTime() * 6.0f));
    ImDrawList_AddCircleFilled(dl, p, pr + 1.6f,
                               sl_col((ImVec4){0.04f, 0.02f, 0.10f, 0.85f}), 16);
    ImDrawList_AddCircleFilled(dl, p, pr, sl_col(sc), 16);
  }
}

static void sl_header_cell(ImDrawList* dl, const char* label, int col_id,
                           float right, float width, float y, float h,
                           bool left_aligned, float left_x) {
  float x0 = left_aligned ? left_x : right - width;
  igSetCursorScreenPos((ImVec2){x0, y});
  char id[24];
  snprintf(id, sizeof(id), "##slh%d", col_id);
  if (igInvisibleButton(id, (ImVec2){width, h}, 0)) {
    if (s_sl_sort == col_id) {
      s_sl_desc = !s_sl_desc;
    } else {
      s_sl_sort = col_id;
      s_sl_desc = (col_id == SL_S_SLOTS); /* busiest first by default */
    }
  }
  ImU32 col = sl_col((ImVec4){0.80f, 0.72f, 0.98f, s_sl_sort == col_id ? 1.0f : 0.75f});
  float ty = y + (h - igGetTextLineHeight()) * 0.5f;
  ImVec2 ts;
  igCalcTextSize(&ts, label, NULL, false, -1.0f);
  float arrow = 0.0f;
  if (s_sl_sort == col_id) arrow = igGetFontSize() * 0.9f;
  float tx = left_aligned ? left_x : right - ts.x - arrow;
  ImDrawList_AddText_Vec2(dl, (ImVec2){tx, ty}, col, label, NULL);
  if (s_sl_sort == col_id) {
    float ax = tx + ts.x + igGetFontSize() * 0.25f;
    float ay = y + h * 0.5f;
    float s  = igGetFontSize() * 0.22f;
    if (s_sl_desc)
      ImDrawList_AddTriangleFilled(dl, (ImVec2){ax, ay - s}, (ImVec2){ax + s * 2, ay - s},
                                   (ImVec2){ax + s, ay + s}, col);
    else
      ImDrawList_AddTriangleFilled(dl, (ImVec2){ax, ay + s}, (ImVec2){ax + s * 2, ay + s},
                                   (ImVec2){ax + s, ay - s}, col);
  }
}

static void draw_server_picker(tenv* env) {
  tuser_data* usr = env->usr;
  user_settings* usrs = &usr->usrs;
  game_data* gdata = &usr->gdata;
  ImGuiStyle* style = igGetStyle();
  const int total = gdata->server_list.count;

  /* ---- status line shown under the panel title ---- */
  char subtitle[96];
  if (gdata->server_list.fetching) {
    snprintf(subtitle, sizeof(subtitle), "Fetching official server list...");
  } else if (gdata->server_list.fetch_error) {
    snprintf(subtitle, sizeof(subtitle),
             "Couldn't fetch official list \xe2\x80\x94 custom servers still available.");
  } else if (gdata->server_list.pinging) {
    snprintf(subtitle, sizeof(subtitle), "Pinging... %d/%d",
             gdata->server_list.pings_done, total);
  } else if (gdata->server_list.pings_done > 0) {
    snprintf(subtitle, sizeof(subtitle), "%d servers \xe2\x80\x94 pinged just now",
             total);
  } else {
    snprintf(subtitle, sizeof(subtitle), "%d servers", total);
  }

  float frame_h = igGetFrameHeight();
  float footer_h = frame_h * 1.6f + style->ItemSpacing.y * 2.0f;
  crystal_panel_set_layout(0.70f, 0.90f, footer_h);
  g_sl_popup_open =
      crystal_panel_begin(env, "Select Server", "SELECT SERVER", subtitle, 700.0f);
  if (!g_sl_popup_open) return;

  /* NOTE: no igPushFont here. crystal_panel_begin() already pushed the regular
     UI font for the whole panel. A second push made inside the panel body
     and still active at crystal_panel_end_body() (EndChild) trips Dear ImGui's
     "Missing PopFont()" error-recovery assert and aborts the app. */
  const float u = igGetFontSize();

  /* ---- search + refresh ---- */
  ImVec2 av;
  igGetContentRegionAvail(&av);
  float rf_w = frame_h * 3.4f;
  igPushItemWidth(av.x - rf_w - style->ItemSpacing.x);
  igInputTextWithHint("##sl_search", "Search IP, ID or name", s_sl_search,
                      sizeof(s_sl_search), ImGuiInputTextFlags_None, NULL, NULL);
  crystal_sheen();
  igPopItemWidth();
  igSameLine(0, -1);
  igBeginDisabled(gdata->server_list.fetching);
  if (igButton("Refresh##sl_refresh", (ImVec2){rf_w, 0})) server_list_fetch(env);
  igEndDisabled();
  crystal_sheen();

  /* ---- filter chips (wrap onto a second line on narrow screens) ---- */
  {
    float line_w = 0.0f;
    for (int f = 0; f < SL_F_COUNT; f++) {
      ImVec2 ts;
      igCalcTextSize(&ts, SL_FILTER_NAMES[f], NULL, false, -1.0f);
      float w = ts.x + style->FramePadding.x * 2.0f + 8.0f;
      if (f > 0) {
        if (line_w + style->ItemSpacing.x + w <= av.x) {
          igSameLine(0, -1);
          line_w += style->ItemSpacing.x;
        } else {
          line_w = 0.0f;
        }
      }
      line_w += w;
      bool sel = (s_sl_filter == f);
      if (sel) {
        igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.510f, 0.294f, 0.910f, 1.0f});
        igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0.569f, 0.353f, 0.960f, 1.0f});
        igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0.450f, 0.243f, 0.850f, 1.0f});
      }
      char lab[48];
      snprintf(lab, sizeof(lab), "%s##slf%d", SL_FILTER_NAMES[f], f);
      if (igButton(lab, (ImVec2){w, 0})) s_sl_filter = f;
      if (sel) igPopStyleColor(3);
    }
  }

  /* ---- build the visible list ---- */
  static int order[MAX_SERVER_LIST];
  int n = 0;
  int best = -1;
  for (int i = 0; i < total; i++) {
    int p = gdata->server_list.pings[i];
    if (sl_ping_ok(p) && (best < 0 || p < gdata->server_list.pings[best])) best = i;
  }
  const bool show_badge = best >= 0 && !gdata->server_list.pinging;
  for (int i = 0; i < total; i++) {
    const char* ip = gdata->server_list.ips[i];
    bool is_custom = i < gdata->server_list.custom_count;
    int p = gdata->server_list.pings[i];
    s_sl_isfav[i] = server_fav_is(ip);
    switch (s_sl_filter) {
      case SL_F_FAV:      if (!s_sl_isfav[i]) continue; break;
      case SL_F_BD:       if (!is_custom) continue; break;
      case SL_F_OFFICIAL: if (is_custom) continue; break;
      case SL_F_FAST:     if (!(sl_ping_ok(p) && p < 80)) continue; break;
      default: break;
    }
    if (s_sl_search[0]) {
      char idbuf[16];
      snprintf(idbuf, sizeof(idbuf), "%d", gdata->server_list.sids[i]);
      if (!sl_contains_ci(ip, s_sl_search) &&
          !(is_custom && sl_contains_ci(CUSTOM_SERVER_NAMES[i], s_sl_search)) &&
          !(gdata->server_list.sids[i] >= 0 && sl_contains_ci(idbuf, s_sl_search)))
        continue;
    }
    order[n++] = i;
  }
  s_sl_cmp_gd = gdata;
  qsort(order, (size_t)n, sizeof(int), sl_cmp);

  /* ---- sortable column header ---- */
  const float row_h = frame_h * 1.25f;
  const float hdr_h = frame_h * 0.9f;
  {
    ImDrawList* dl = igGetWindowDrawList();
    ImVec2 hp;
    igGetCursorScreenPos(&hp);
    ImVec2 av_h;
    igGetContentRegionAvail(&av_h);
    float hw = av_h.x - style->ScrollbarSize; /* rows lose this to the scrollbar */
    sl_cols c = sl_layout(hp.x, hw, u, row_h);
    igDummy((ImVec2){av_h.x, hdr_h});
    ImVec2 after;
    igGetCursorScreenPos(&after);
    ImDrawList_AddText_Vec2(dl, (ImVec2){c.name_x, hp.y + (hdr_h - igGetTextLineHeight()) * 0.5f},
                            sl_col((ImVec4){0.80f, 0.72f, 0.98f, 0.75f}), "SERVER", NULL);
    sl_header_cell(dl, "PING", SL_S_PING, c.ping_r, c.ping_w, hp.y, hdr_h, false, 0);
    sl_header_cell(dl, "SLOTS", SL_S_SLOTS, c.slots_r, c.slots_w, hp.y, hdr_h, false, 0);
    if (c.show_id)
      sl_header_cell(dl, "ID", SL_S_ID, c.id_r, c.id_w, hp.y, hdr_h, false, 0);
    igSetCursorScreenPos(after);
  }

  /* ---- rows ---- */
  int toggle_idx = -1;
  igBeginChild_Str("##sl_scroll", (ImVec2){0, 0}, ImGuiChildFlags_None, 0);
  ui_touch_scroll(env, 25);
  {
    ImDrawList* dl = igGetWindowDrawList();
    ImVec2 av2;
    igGetContentRegionAvail(&av2);
    const float row_w = av2.x;

    if (n == 0) {
      igTextDisabled(total == 0 ? "No servers yet." : "No servers match.");
    }

    for (int j = 0; j < n; j++) {
      int i = order[j];
      const char* ip = gdata->server_list.ips[i];
      bool is_custom = i < gdata->server_list.custom_count;
      bool fav = s_sl_isfav[i];
      bool is_best = show_badge && i == best;
      int ping = gdata->server_list.pings[i];
      int slots = gdata->server_list.slots[i];
      int sid = gdata->server_list.sids[i];

      ImVec2 rp;
      igGetCursorScreenPos(&rp);
      sl_cols c = sl_layout(rp.x, row_w, u, row_h);

      ImVec4 bgc = is_best ? (ImVec4){0.30f, 1.00f, 0.40f, 0.12f}
                  : fav    ? (ImVec4){1.00f, 0.82f, 0.25f, 0.10f}
                           : (ImVec4){0.510f, 0.294f, 0.910f, 0.16f};
      ImDrawList_AddRectFilled(dl, rp, (ImVec2){rp.x + row_w, rp.y + row_h},
                               sl_col_tinted(bgc), 10.0f, 0);

      igPushID_Int(i);
      if (igInvisibleButton("##fav", (ImVec2){c.star_w, row_h}, 0)) toggle_idx = i;
      igSameLine(0, 0);
      bool pick = igSelectable_Bool("##pick", false, ImGuiSelectableFlags_None,
                                    (ImVec2){row_w - c.star_w, row_h});
      bool hovered = igIsItemHovered(0);
      igPopID();

      float ty = rp.y + (row_h - igGetTextLineHeight()) * 0.5f;

      /* favorite star */
      {
        ImVec2 ts;
        igCalcTextSize(&ts, "\xe2\x98\x85", NULL, false, -1.0f);
        ImDrawList_AddText_Vec2(
            dl, (ImVec2){rp.x + (c.star_w - ts.x) * 0.5f, ty},
            fav ? sl_col((ImVec4){1.00f, 0.82f, 0.25f, 1.0f})
                : sl_col((ImVec4){0.70f, 0.64f, 0.88f, 0.35f}),
            "\xe2\x98\x85", NULL);
      }

      /* name (custom servers by name, official ones by IP) + FASTEST badge */
      {
        char name[48];
        if (is_custom)
          snprintf(name, sizeof(name), "%s", CUSTOM_SERVER_NAMES[i]);
        else
          snprintf(name, sizeof(name), "%s", ip);
        ImDrawList_PushClipRect(dl, (ImVec2){c.name_x, rp.y},
                                (ImVec2){c.name_right, rp.y + row_h}, true);
        ImDrawList_AddText_Vec2(
            dl, (ImVec2){c.name_x, ty},
            is_custom ? sl_col((ImVec4){1.0f, 0.82f, 0.25f, 1.0f})
                      : sl_col((ImVec4){0.945f, 0.925f, 1.0f, 1.0f}),
            name, NULL);
        if (is_best) {
          ImVec2 ns, bs;
          igCalcTextSize(&ns, name, NULL, false, -1.0f);
          igCalcTextSize(&bs, "FASTEST", NULL, false, -1.0f);
          float bx = c.name_x + ns.x + u * 0.7f;
          float pad = u * 0.4f;
          if (bx + bs.x + pad * 2 < c.name_right) {
            ImDrawList_AddRectFilled(dl, (ImVec2){bx, ty - 1},
                                     (ImVec2){bx + bs.x + pad * 2, ty + bs.y + 1},
                                     sl_col((ImVec4){0.30f, 1.0f, 0.40f, 1.0f}), 8.0f, 0);
            ImDrawList_AddText_Vec2(dl, (ImVec2){bx + pad, ty},
                                    sl_col((ImVec4){0.03f, 0.14f, 0.06f, 1.0f}),
                                    "FASTEST", NULL);
          }
        }
        ImDrawList_PopClipRect(dl);
      }

      char buf[24];
      ImU32 dim = sl_col((ImVec4){0.78f, 0.72f, 0.95f, 1.0f});
      if (c.show_id) {
        if (sid >= 0) snprintf(buf, sizeof(buf), "%d", sid);
        else snprintf(buf, sizeof(buf), "-");
        sl_text_r(dl, c.id_r, ty, dim, buf);
      }
      if (slots >= 0) snprintf(buf, sizeof(buf), "%d", slots);
      else snprintf(buf, sizeof(buf), "-");
      sl_text_r(dl, c.slots_r, ty, dim, buf);

      /* ping: signal bars + milliseconds */
      {
        ImVec4 pc = sl_ping_color(ping);
        if (ping < 0)            snprintf(buf, sizeof(buf), "--");
        else if (ping >= 9999)   snprintf(buf, sizeof(buf), "!!ms");
        else                     snprintf(buf, sizeof(buf), "%dms", ping);
        sl_text_r(dl, c.ping_r, ty, sl_col(pc), buf);

        int lvl = !sl_ping_ok(ping) ? 0 : ping < 80 ? 4 : ping < 150 ? 3 : ping < 300 ? 2 : 1;
        float bw = u * 0.28f, gap = u * 0.14f;
        float bx = c.ping_r - u * 3.6f - 4 * (bw + gap);
        float base = rp.y + row_h * 0.5f + u * 0.55f;
        for (int b = 0; b < 4; b++) {
          float bh = u * (0.35f + 0.28f * (float)b);
          ImU32 bc = b < lvl ? sl_col(pc)
                             : sl_col((ImVec4){0.78f, 0.72f, 0.95f, 0.25f});
          ImDrawList_AddRectFilled(
              dl, (ImVec2){bx + (float)b * (bw + gap), base - bh},
              (ImVec2){bx + (float)b * (bw + gap) + bw, base}, bc, 1.0f, 0);
        }
      }

      if (is_custom && hovered) igSetTooltip("%s", ip);

      if (pick) {
        strncpy(usrs->ipv4, ip, MAX_IPV4_LEN);
        usrs->ipv4[MAX_IPV4_LEN] = '\0';
        igCloseCurrentPopup();
      }
    }
  }
  igEndChild();
  if (toggle_idx >= 0) server_fav_toggle(gdata->server_list.ips[toggle_idx]);

  crystal_panel_end_body();

  /* ---- pinned footer: use the fastest server ---- */
  {
    float fb_h = frame_h * 1.6f;
    ImVec2 fav;
    igGetContentRegionAvail(&fav);
    bool can = best >= 0 && !gdata->server_list.pinging;
    igBeginDisabled(!can);
    igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.510f, 0.294f, 0.910f, 1.0f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, (ImVec4){0.569f, 0.353f, 0.960f, 1.0f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonActive, (ImVec4){0.450f, 0.243f, 0.850f, 1.0f});
    char lab[64];
    if (best >= 0 && !gdata->server_list.pinging)
      snprintf(lab, sizeof(lab), "Use fastest server (%dms)##sl_best",
               gdata->server_list.pings[best]);
    else
      snprintf(lab, sizeof(lab), "Use fastest server##sl_best");
    if (igButton(lab, (ImVec2){fav.x, fb_h}) && can) {
      strncpy(usrs->ipv4, gdata->server_list.ips[best], MAX_IPV4_LEN);
      usrs->ipv4[MAX_IPV4_LEN] = '\0';
      igCloseCurrentPopup();
    }
    igPopStyleColor(3);
    igEndDisabled();
    crystal_sheen();
  }

  crystal_panel_end();
}

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

  float popup_w = fminf(460.0f, ctx->size[0] - 24.0f);
  igSetNextWindowSize((ImVec2){popup_w, 0.0f}, ImGuiCond_Appearing);
  /* Never taller than the screen; the panel scrolls (thick scrollbar and
     drag-to-scroll) when it does not fit, e.g. on a landscape phone. */
  igSetNextWindowSizeConstraints((ImVec2){0.0f, 0.0f},
                                 (ImVec2){100000.0f, ctx->size[1] * 0.92f},
                                 NULL, NULL);
  igPushStyleVar_Float(ImGuiStyleVar_ScrollbarSize, 26.0f);
  if (igBeginPopup("Snakey Rain Settings", ImGuiWindowFlags_NoSavedSettings)) {
    igPushFont(usr->imgui_data.regular_font[usrs->ui_font_size],
               usr->imgui_data.regular_font[usrs->ui_font_size]->LegacySize);

    igSeparatorText("Snakey Rain");
    /* Status, account, bots and bot look -- shared with the in-game settings
       window (gear button on the Snakey Rain bar). */
    snakey_rain_draw_settings_panel(env);
    igSpacing();

    ImVec2 avail;
    igGetContentRegionAvail(&avail);
    if (igButton("Close", (ImVec2){avail.x, 0.0f})) igCloseCurrentPopup();

    igPopFont();
    igEndPopup();
  }
  igPopStyleVar(1);

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

  ui_lobby_bg_draw(env); /* lobby only: gradient / preset / uploaded picture */
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
  {
    int best_kills = lifetime_stats_best_match_kills();
    if (best_kills < usrs->kills) best_kills = usrs->kills;
    igSameLine(0, -1);
    igTextColored((ImVec4){1, 0.85f, 0.4f, 0.7f}, "  Best %d  Total %d",
                  best_kills, lifetime_stats_total_kills());
  }

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
  igTextColored((ImVec4){1, 0.85f, 0.4f, 0.7f}, "Best %d",
                lifetime_stats_best_length() > usrs->best_length
                    ? lifetime_stats_best_length()
                    : usrs->best_length);

  igPopFont();

  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f + style->ItemSpacing.y);
  float nn_btn_w = frame_height;
  igPushItemWidth(logo_size - nn_btn_w - style->ItemSpacing.x);
  igInputTextWithHint("##nickname_input", "Nickname", usrs->nickname,
                      MAX_NICKNAME_LEN + 1, ImGuiInputTextFlags_None, NULL,
                      NULL);
  /* Remember the name once editing is finished (like NTL's "change" event). */
  if (igIsItemDeactivatedAfterEdit())
    user_settings_add_saved_name(usrs, usrs->nickname);
  crystal_sheen();
  igPopItemWidth();

  /* Previous names: a dropdown button next to the nickname box, like NTL's
     scroll button. Click a name to use it, X to forget it. */
  igSameLine(0, style->ItemSpacing.x);
  if (igArrowButton("##nn_btn", ImGuiDir_Down)) igOpenPopup_Str("##nn_popup", 0);
  if (igIsItemHovered(0)) igSetTooltip("Previous names");
  crystal_sheen();
  if (igBeginPopup("##nn_popup", 0)) {
    igPushFont(usr->imgui_data.regular_font[usrs->ui_font_size],
               usr->imgui_data.regular_font[usrs->ui_font_size]->LegacySize);
    if (usrs->saved_name_count == 0) {
      igTextDisabled("No previous names yet");
    } else {
      int remove_idx = -1;
      igBeginChild_Str("##nn_scroll", (ImVec2){300, 260}, ImGuiChildFlags_None,
                       0);
      for (int i = 0; i < usrs->saved_name_count; ++i) {
        igPushID_Int(i);
        float x_w = igGetFrameHeight();
        if (igSelectable_Bool(usrs->saved_names[i], false,
                              ImGuiSelectableFlags_None,
                              (ImVec2){300 - x_w - style->ItemSpacing.x * 2 -
                                           style->ScrollbarSize,
                                       0})) {
          strncpy(usrs->nickname, usrs->saved_names[i], MAX_NICKNAME_LEN);
          usrs->nickname[MAX_NICKNAME_LEN] = '\0';
          igCloseCurrentPopup();
        }
        igSameLine(0, -1);
        if (igSmallButton("X")) remove_idx = i;
        igPopID();
      }
      igEndChild();
      if (remove_idx >= 0)
        user_settings_remove_saved_name(usrs, remove_idx);
    }
    igPopFont();
    igEndPopup();
  }

  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f + style->ItemSpacing.y * 2 +
                  frame_height);
  float sl_btn_w = frame_height;
  igPushItemWidth(logo_size);
  igPushItemWidth(logo_size - sl_btn_w - style->ItemSpacing.x);
  /* No blanks: an address never contains a space, and keyboards that insert
     one after a word/number would otherwise break the IP. */
  igInputTextWithHint("##ipv4_input", "IPv4:Port", usrs->ipv4, MAX_IPV4_LEN + 1,
                      ImGuiInputTextFlags_CharsNoBlank, NULL, NULL);
  crystal_sheen();
  igPopItemWidth();
  igPopItemWidth();

  igSameLine(0, style->ItemSpacing.x);
  /* Server-list button: the globe is drawn as vectors on top of an empty
     button (no icon-font glyph), with a small status light in the corner. */
  if (igButton("##sl_btn", (ImVec2){sl_btn_w, sl_btn_w})) {
    if (!gdata->server_list.fetching && !gdata->server_list.fetched)
      server_list_fetch(env);
    igOpenPopup_Str("Select Server", 0);
  }
  {
    ImVec2 bmin, bmax;
    igGetItemRectMin(&bmin);
    igGetItemRectMax(&bmax);
    int state = gdata->server_list.fetch_error ? 2
              : (gdata->server_list.fetching || gdata->server_list.pinging) ? 1
              : (gdata->server_list.pings_done > 0) ? 0 : 3;
    sl_draw_globe(igGetWindowDrawList(), bmin, bmax, igIsItemHovered(0),
                  igIsItemActive(), state);
  }
  if (igIsItemHovered(0)) igSetTooltip("Choose a server");
  crystal_sheen();

  server_list_poll(env);

  if (gdata->server_list.fetched && gdata->server_list.count > 0 &&
      !gdata->server_list.pinging && gdata->server_list.pings_done == 0) {
    server_list_start_ping(env);
  }

  draw_server_picker(env);

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
    user_settings_add_saved_name(usrs, usrs->nickname);
    usr->gdata.conn_fail[0] = '\0';
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
  if (igButton("TEAM CHAT",
               (ImVec2){logo_size / 2 - style->ItemSpacing.x / 2})) {
    usr->gdata.curr_screen = NTL_PANEL;
  }
  crystal_sheen();

  igSetCursorPosX(ctx->size[0] / 2.0f - logo_size / 2);
  igSetCursorPosY(ctx->size[1] / 2.0f + style->ItemSpacing.y * 6 +
                  frame_height * 5);
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
  igSetCursorPosY(ctx->size[1] / 2.0f + style->ItemSpacing.y * 7 +
                  frame_height * 6);
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

  /* Why the last join attempt failed (see gdata->conn_fail). */
  if (usr->gdata.conn_fail[0]) {
    ImVec2 ts;
    igCalcTextSize(&ts, usr->gdata.conn_fail, NULL, false, -1.0f);
    igSetCursorPosX((ctx->size[0] - ts.x) * 0.5f);
    igSetCursorPosY(style->WindowPadding.y * 4);
    igTextColored((ImVec4){1.0f, 0.42f, 0.42f, 1.0f}, "%s",
                  usr->gdata.conn_fail);
  }

  draw_snakey_rain_homepage(env, frame_height);
  ui_lobby_bg_button(env, frame_height);

  igPopFont();
}

void ui_title_screen_destroy(tenv* env) { ui_lobby_bg_destroy(env); }
