#include "ntl_team.h"
#include "../user.h"
#include "../external/mongoose.h"
#include "global_chat.h"
#include "ntl_emoji.h"
#include "thermite/jsr_network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#ifdef ANDROID
#include "../android_glfw_shim.h"
#endif

#include "../ui/crystal_theme.h"
#include "../ui/lobby_bg.h"
#include "../ui/settings.h"

#ifndef IM_COL32
#define IM_COL32(R,G,B,A) crystal_tint_u32((((ImU32)(A)<<24)|((ImU32)(B)<<16)|((ImU32)(G)<<8)|((ImU32)(R))))
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define NTL_MAX_TEAM 64
#define NTL_URL_MAX 2048
#define NTL_POLL_SECONDS 1.0
#define NTL_RETRY_BASE_SECONDS 1.0
#define NTL_RETRY_MAX_SECONDS 6.0
#define NTL_REQUEST_TIMEOUT_SECONDS 7.0
#define NTL_CONNECTED_GRACE_SECONDS 35.0
#define NTL_CHAT_HISTORY_MAX 80
#define NTL_CHAT_SNAPSHOT_MAX 1024

typedef struct {
  char nick[64], msg[NTL_CHAT_SNAPSHOT_MAX], srv[64], dt[128];
  float x, y;
  int sid, score, rank;
} ntl_member;
typedef struct {
  struct mg_mgr mgr;
  struct mg_connection *request_conn;
  bool ready, request_active;
  double next_poll, request_started, last_success;
  ntl_member members[NTL_MAX_TEAM]; int count;
  char pending_msg[256];
  char inflight_msg[256];
  char input[256]; bool chat_open; bool players_open;
  char request_path[NTL_URL_MAX]; int last_http_status; bool last_request_ok;
  int consecutive_failures;
  struct { char nick[64], text[256]; double time; } history[NTL_CHAT_HISTORY_MAX];
  int history_count, history_start;
  char profile_name[MAX_NTL_TEAM_NAME + 1];
  bool chat_restore_size;
  float chat_x, chat_y, chat_w, chat_h;
  float players_x, players_y, players_w, players_h;
  bool layout_dirty;
  struct {
    char key[96];
    char msg[NTL_CHAT_SNAPSHOT_MAX];
  } seen[NTL_MAX_TEAM];
  int seen_count; bool scroll_chat_bottom;
  bool feed_baselined;
  bool select_chat_tab;
  char active_team_id[96];
  char active_auth_key[96];
  char request_team_id[96];
  char request_auth_key[96];
  char request_client_id[9];
  char last_sent_text[256];
  double last_sent_time;
  char last_nickname[MAX_NICKNAME_LEN + 1];
  char last_presence_server[MAX_IPV4_LEN + 1];
  bool last_presence_playing;
  /* Team dot UI: "already taken" notice, custom emoji box. */
  char dup_name[40];
  double dup_until;
  char emoji_input[24];
  int emoji_status; /* 0 none, 1 applied, 2 not in list */
} ntl_state;
static ntl_state S;

static float ntl_clampf(float v, float lo, float hi) {
  if (hi < lo) hi = lo;
  return v < lo ? lo : (v > hi ? hi : v);
}
static bool ntl_feed_is_fresh(void);
static void ntl_queue_message(const user_settings *us);
static const char *ntl_clean_name(const char *name);
static void ntl_reset_feed(bool clear_history);
static void ntl_ensure_client_id(user_settings *us);
static void ntl_schedule_retry(double now);
static size_t ntl_append_utf8(char *out, size_t cap, size_t n,
                              unsigned int cp);

static void urlenc(char *out,size_t cap,const char *in){size_t n=0;for(;*in&&n+4<cap;in++){unsigned char c=*in;if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')out[n++]=c;else{snprintf(out+n,cap-n,"%%%02X",c);n+=3;}}out[n]=0;}
static int ntl_hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + c - 'a';
  if (c >= 'A' && c <= 'F') return 10 + c - 'A';
  return -1;
}

static bool field_str(const char *a, const char *b, const char *key,
                      char *out, size_t cap) {
  if (!out || cap == 0) return false;
  out[0] = 0;

  char pattern[64];
  snprintf(pattern, sizeof pattern, "\"%s\"", key);
  const char *q = strstr(a, pattern);
  if (!q || q >= b) return false;

  q = strchr(q, ':');
  if (!q || q >= b) return false;
  q++;
  while (q < b && isspace((unsigned char)*q)) q++;
  if (q >= b || *q != '"') return false;
  q++;

  size_t n = 0;
  while (q < b && *q != '"') {
    if (*q != '\\') {
      if (n + 1 >= cap) break;
      out[n++] = *q++;
      continue;
    }

    q++;
    if (q >= b) break;
    char esc = *q++;

    if (esc == 'u') {
      if (q + 4 > b) continue;
      int h0 = ntl_hex_value(q[0]);
      int h1 = ntl_hex_value(q[1]);
      int h2 = ntl_hex_value(q[2]);
      int h3 = ntl_hex_value(q[3]);
      if (h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0) continue;

      unsigned int cp =
          (unsigned int)((h0 << 12) | (h1 << 8) | (h2 << 4) | h3);
      q += 4;

      if (cp >= 0xd800 && cp <= 0xdbff && q + 6 <= b &&
          q[0] == '\\' && q[1] == 'u') {
        int l0 = ntl_hex_value(q[2]);
        int l1 = ntl_hex_value(q[3]);
        int l2 = ntl_hex_value(q[4]);
        int l3 = ntl_hex_value(q[5]);
        if (l0 >= 0 && l1 >= 0 && l2 >= 0 && l3 >= 0) {
          unsigned int low =
              (unsigned int)((l0 << 12) | (l1 << 8) | (l2 << 4) | l3);
          if (low >= 0xdc00 && low <= 0xdfff) {
            cp = 0x10000u + ((cp - 0xd800u) << 10) + (low - 0xdc00u);
            q += 6;
          }
        }
      }

      size_t next = ntl_append_utf8(out, cap, n, cp);
      if (next == n) break;
      n = next;
      continue;
    }

    char decoded = esc;
    switch (esc) {
      case '"': decoded = '"'; break;
      case '\\': decoded = '\\'; break;
      case '/': decoded = '/'; break;
      case 'b': decoded = '\b'; break;
      case 'f': decoded = '\f'; break;
      case 'n': decoded = '\n'; break;
      case 'r': decoded = '\r'; break;
      case 't': decoded = '\t'; break;
      default: break;
    }
    if (n + 1 >= cap) break;
    out[n++] = decoded;
  }

  out[n] = 0;
  return true;
}
static double field_num(const char *a, const char *b, const char *key, double fallback) {
  char pattern[64];
  snprintf(pattern, sizeof pattern, "\"%s\"", key);

  const char *q = strstr(a, pattern);
  if (!q || q >= b) return fallback;

  q = strchr(q, ':');
  if (!q || q >= b) return fallback;
  q++;

  while (q < b && isspace((unsigned char)*q)) q++;
  if (q >= b) return fallback;

  /* The endpoint can serialize numeric fields as JSON strings. Accept both
     normal JSON numbers and quoted numeric strings. */
  if (*q == '\"') {
    q++;
    while (q < b && isspace((unsigned char)*q)) q++;
  }

  char *endptr = NULL;
  double value = strtod(q, &endptr);
  if (endptr == q || endptr > b) return fallback;
  return value;
}
static size_t ntl_append_utf8(char *out, size_t cap, size_t n,
                              unsigned int cp) {
  if (cp <= 0x7f) {
    if (n + 1 < cap) out[n++] = (char)cp;
  } else if (cp <= 0x7ff) {
    if (n + 2 < cap) {
      out[n++] = (char)(0xc0 | (cp >> 6));
      out[n++] = (char)(0x80 | (cp & 0x3f));
    }
  } else if (cp <= 0xffff && !(cp >= 0xd800 && cp <= 0xdfff)) {
    if (n + 3 < cap) {
      out[n++] = (char)(0xe0 | (cp >> 12));
      out[n++] = (char)(0x80 | ((cp >> 6) & 0x3f));
      out[n++] = (char)(0x80 | (cp & 0x3f));
    }
  } else if (cp <= 0x10ffff) {
    if (n + 4 < cap) {
      out[n++] = (char)(0xf0 | (cp >> 18));
      out[n++] = (char)(0x80 | ((cp >> 12) & 0x3f));
      out[n++] = (char)(0x80 | ((cp >> 6) & 0x3f));
      out[n++] = (char)(0x80 | (cp & 0x3f));
    }
  }
  return n;
}

/* NTL stores a browser-style message snapshot per client. Keep line breaks
   while decoding so a snapshot containing several queued messages can be
   compared with the previous snapshot without losing any intermediate text. */
static void ntl_decode_chat_snapshot(char *text, size_t cap) {
  if (!text || cap == 0) return;

  char out[NTL_CHAT_SNAPSHOT_MAX];
  size_t n = 0;
  bool last_space = true;
  const char *p = text;

  while (*p && n + 1 < sizeof out) {
    unsigned int cp = 0;
    size_t consumed = 0;
    bool line_break = false;

    if (!strncmp(p, "&nbsp;", 6)) { cp = ' '; consumed = 6; }
    else if (!strncmp(p, "&nbsp", 5)) { cp = ' '; consumed = 5; }
    else if (!strncmp(p, "&#160;", 6)) { cp = ' '; consumed = 6; }
    else if (!strncmp(p, "&amp;", 5)) { cp = '&'; consumed = 5; }
    else if (!strncmp(p, "&lt;", 4)) { cp = '<'; consumed = 4; }
    else if (!strncmp(p, "&gt;", 4)) { cp = '>'; consumed = 4; }
    else if (!strncmp(p, "&quot;", 6)) { cp = '"'; consumed = 6; }
    else if (!strncmp(p, "&#39;", 5) || !strncmp(p, "&apos;", 6)) {
      cp = '\'';
      consumed = p[1] == '#' ? 5 : 6;
    } else if (!strncmp(p, "<br>", 4) || !strncmp(p, "<BR>", 4)) {
      consumed = 4;
      line_break = true;
    } else if (!strncmp(p, "<br/>", 5) || !strncmp(p, "<BR/>", 5)) {
      consumed = 5;
      line_break = true;
    } else if (!strncmp(p, "<br />", 6) || !strncmp(p, "<BR />", 6)) {
      consumed = 6;
      line_break = true;
    } else if (p[0] == '&' && p[1] == '#') {
      const char *q = p + 2;
      int base = 10;
      if (*q == 'x' || *q == 'X') { base = 16; q++; }
      char *endptr = NULL;
      unsigned long value = strtoul(q, &endptr, base);
      if (endptr != q && *endptr == ';' && value <= 0x10ffffUL) {
        cp = (unsigned int)value;
        consumed = (size_t)(endptr - p) + 1;
      }
    }

    if (consumed) {
      p += consumed;
      if (line_break) {
        while (n > 0 && out[n - 1] == ' ') n--;
        if (n > 0 && out[n - 1] != '\n' && n + 1 < sizeof out)
          out[n++] = '\n';
        last_space = true;
        continue;
      }
      if (cp == 0xa0 || cp == '\r' || cp == '\t') cp = ' ';
      if (cp == '\n') {
        while (n > 0 && out[n - 1] == ' ') n--;
        if (n > 0 && out[n - 1] != '\n' && n + 1 < sizeof out)
          out[n++] = '\n';
        last_space = true;
      } else if (cp == ' ') {
        if (!last_space && n + 1 < sizeof out) out[n++] = ' ';
        last_space = true;
      } else {
        size_t next = ntl_append_utf8(out, sizeof out, n, cp);
        if (next == n) break;
        n = next;
        last_space = false;
      }
      continue;
    }

    unsigned char c = (unsigned char)*p++;
    if (c == '\n') {
      while (n > 0 && out[n - 1] == ' ') n--;
      if (n > 0 && out[n - 1] != '\n' && n + 1 < sizeof out)
        out[n++] = '\n';
      last_space = true;
    } else if (c == '\r' || c == '\t' || c == ' ') {
      if (!last_space && n + 1 < sizeof out) out[n++] = ' ';
      last_space = true;
    } else {
      out[n++] = (char)c;
      last_space = false;
    }
  }

  while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\n')) n--;
  out[n] = 0;
  strncpy(text, out, cap - 1);
  text[cap - 1] = 0;
}

static void ntl_normalize_chat_text(char *text, size_t cap) {
  if (!text || cap == 0) return;

  char decoded[NTL_CHAT_SNAPSHOT_MAX];
  strncpy(decoded, text, sizeof decoded - 1);
  decoded[sizeof decoded - 1] = 0;
  ntl_decode_chat_snapshot(decoded, sizeof decoded);

  size_t n = 0;
  bool last_space = true;
  for (const unsigned char *p = (const unsigned char *)decoded;
       *p && n + 1 < cap; ++p) {
    if (*p == '\n' || *p == '\r' || *p == '\t' || *p == ' ') {
      if (!last_space) text[n++] = ' ';
      last_space = true;
    } else {
      text[n++] = (char)*p;
      last_space = false;
    }
  }
  while (n > 0 && text[n - 1] == ' ') n--;
  text[n] = 0;
}

static void add_history(const char *nick, const char *text) {
  if (!text || !text[0]) return;

  char clean_text[256];
  strncpy(clean_text, text, sizeof clean_text - 1);
  clean_text[sizeof clean_text - 1] = 0;
  ntl_normalize_chat_text(clean_text, sizeof clean_text);
  if (!clean_text[0]) return;

  char clean_nick[64];
  const char *display_name = ntl_clean_name(nick);
  strncpy(clean_nick, display_name && display_name[0] ? display_name : "Player",
          sizeof clean_nick - 1);
  clean_nick[sizeof clean_nick - 1] = 0;
  ntl_normalize_chat_text(clean_nick, sizeof clean_nick);
  if (!clean_nick[0]) strcpy(clean_nick, "Player");

  int idx;
  if (S.history_count < NTL_CHAT_HISTORY_MAX) {
    idx = (S.history_start + S.history_count) % NTL_CHAT_HISTORY_MAX;
    S.history_count++;
  } else {
    idx = S.history_start;
    S.history_start = (S.history_start + 1) % NTL_CHAT_HISTORY_MAX;
  }
  strncpy(S.history[idx].nick, clean_nick, sizeof S.history[idx].nick - 1);
  S.history[idx].nick[sizeof S.history[idx].nick - 1] = 0;
  strncpy(S.history[idx].text, clean_text, sizeof S.history[idx].text - 1);
  S.history[idx].text[sizeof S.history[idx].text - 1] = 0;
  S.history[idx].time = mg_millis() / 1000.0;
  S.scroll_chat_bottom = true;
}

static void ntl_message_key(const char *nick, char *out, size_t cap) {
  if (!out || cap == 0) return;
  out[0] = 0;
  if (!nick) return;

  bool has_client_id = strlen(nick) >= 8;
  for (int i = 0; has_client_id && i < 8; ++i)
    has_client_id = isxdigit((unsigned char)nick[i]) != 0;

  if (has_client_id) {
    size_t n = cap > 9 ? 8 : cap - 1;
    for (size_t i = 0; i < n; ++i)
      out[i] = (char)tolower((unsigned char)nick[i]);
    out[n] = 0;
  } else {
    strncpy(out, nick, cap - 1);
    out[cap - 1] = 0;
  }
}

static int seen_index_for(const char *nick) {
  char key[96];
  ntl_message_key(nick, key, sizeof key);
  for (int i = 0; i < S.seen_count; ++i)
    if (!strcmp(S.seen[i].key, key)) return i;
  return -1;
}

static const char *seen_message_for(const char *nick) {
  int index = seen_index_for(nick);
  return index >= 0 ? S.seen[index].msg : NULL;
}

static void remember_message(const char *nick, const char *msg) {
  char key[96];
  ntl_message_key(nick, key, sizeof key);
  int index = seen_index_for(nick);

  if (index < 0) {
    if (S.seen_count >= NTL_MAX_TEAM) return;
    index = S.seen_count++;
    strncpy(S.seen[index].key, key, sizeof S.seen[index].key - 1);
    S.seen[index].key[sizeof S.seen[index].key - 1] = 0;
  }

  strncpy(S.seen[index].msg, msg ? msg : "", sizeof S.seen[index].msg - 1);
  S.seen[index].msg[sizeof S.seen[index].msg - 1] = 0;
}

static size_t ntl_snapshot_overlap(const char *old_msg, const char *new_msg) {
  if (!old_msg || !new_msg) return 0;
  size_t old_len = strlen(old_msg);
  size_t new_len = strlen(new_msg);
  size_t best = 0;

  /* Match complete message records only. A raw character suffix match could
     turn "hello" followed by "okay" into "kay" because both touch on "o". */
  for (size_t start = 0; start < old_len; ++start) {
    if (start > 0 && old_msg[start - 1] != '\n') continue;
    size_t overlap = old_len - start;
    if (overlap <= best || overlap > new_len) continue;
    if (new_msg[overlap] != 0 && new_msg[overlap] != '\n') continue;
    if (!memcmp(old_msg + start, new_msg, overlap)) best = overlap;
  }
  return best;
}

static void ntl_add_snapshot_delta(const char *nick, const char *delta) {
  if (!delta) return;
  while (*delta == '\n' || *delta == ' ') delta++;

  const char *p = delta;
  while (*p) {
    const char *end = strchr(p, '\n');
    size_t len = end ? (size_t)(end - p) : strlen(p);

    /* The desktop Vlither reference joins a burst using " | ". Accept that
       separator too, while the official extension normally uses <br>. */
    const char *segment = p;
    const char *segment_end = p + len;
    while (segment < segment_end) {
      const char *pipe = NULL;
      for (const char *q = segment; q + 2 < segment_end; ++q) {
        if (q[0] == ' ' && q[1] == '|' && q[2] == ' ') {
          pipe = q;
          break;
        }
      }

      const char *part_end = pipe ? pipe : segment_end;
      while (segment < part_end && isspace((unsigned char)*segment)) segment++;
      while (part_end > segment &&
             isspace((unsigned char)part_end[-1])) part_end--;

      if (part_end > segment) {
        char message[256];
        size_t part_len = (size_t)(part_end - segment);
        if (part_len >= sizeof message) part_len = sizeof message - 1;
        memcpy(message, segment, part_len);
        message[part_len] = 0;
        add_history(nick, message);
      }

      if (!pipe) break;
      segment = pipe + 3;
    }

    if (!end) break;
    p = end + 1;
  }
}

static bool ntl_snapshot_contains_message(const char *snapshot,
                                          const char *message) {
  if (!snapshot || !message || !message[0]) return false;

  char target[256];
  strncpy(target, message, sizeof target - 1);
  target[sizeof target - 1] = 0;
  ntl_normalize_chat_text(target, sizeof target);

  const char *p = snapshot;
  while (*p) {
    const char *end = strchr(p, '\n');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    char part[256];
    if (len >= sizeof part) len = sizeof part - 1;
    memcpy(part, p, len);
    part[len] = 0;
    ntl_normalize_chat_text(part, sizeof part);
    if (!strcmp(part, target)) return true;
    if (!end) break;
    p = end + 1;
  }
  return false;
}

static bool ntl_client_id_is_valid(const char *id) {
  if (!id || strlen(id) != 8) return false;
  for (int i = 0; i < 8; ++i) {
    char c = id[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

static void ntl_ensure_client_id(user_settings *us) {
  if (!us || ntl_client_id_is_valid(us->ntl_client_id)) return;

  unsigned char random_bytes[4] = {0};
  if (!mg_random(random_bytes, sizeof random_bytes)) {
    uint64_t fallback = mg_millis() ^ (uintptr_t)us;
    for (int i = 0; i < 4; ++i)
      random_bytes[i] = (unsigned char)(fallback >> (i * 8));
  }
  snprintf(us->ntl_client_id, sizeof us->ntl_client_id, "%02x%02x%02x%02x",
           random_bytes[0], random_bytes[1], random_bytes[2], random_bytes[3]);
  save_user_settings(us);
}

static double ntl_retry_delay(void) {
  int step = S.consecutive_failures > 0 ? S.consecutive_failures - 1 : 0;
  if (step > 3) step = 3;
  double delay = NTL_RETRY_BASE_SECONDS * (double)(1 << step);
  return delay > NTL_RETRY_MAX_SECONDS ? NTL_RETRY_MAX_SECONDS : delay;
}

static void ntl_schedule_retry(double now) {
  if (S.consecutive_failures < 1000) S.consecutive_failures++;
  S.last_request_ok = false;
  S.next_poll = now + ntl_retry_delay();
}

static void ntl_reset_feed(bool clear_history) {
  memset(S.members, 0, sizeof S.members);
  S.count = 0;
  memset(S.seen, 0, sizeof S.seen);
  S.seen_count = 0;
  S.feed_baselined = false;
  S.last_success = 0.0;
  S.last_request_ok = false;
  S.last_http_status = 0;
  S.select_chat_tab = true;
  S.pending_msg[0] = 0;
  S.inflight_msg[0] = 0;
  S.consecutive_failures = 0;
  S.last_sent_text[0] = 0;
  S.last_sent_time = 0.0;
  if (clear_history) {
    memset(S.history, 0, sizeof S.history);
    S.history_count = 0;
    S.history_start = 0;
    S.scroll_chat_bottom = false;
  }
}

static void ntl_sync_active_credentials(const user_settings *us) {
  if (!us) return;
  if (!strcmp(S.active_team_id, us->ntl_team_id) &&
      !strcmp(S.active_auth_key, us->ntl_auth_key))
    return;

  ntl_reset_feed(true);
  strncpy(S.active_team_id, us->ntl_team_id, sizeof S.active_team_id - 1);
  S.active_team_id[sizeof S.active_team_id - 1] = 0;
  strncpy(S.active_auth_key, us->ntl_auth_key, sizeof S.active_auth_key - 1);
  S.active_auth_key[sizeof S.active_auth_key - 1] = 0;
}

static const char *ntl_find_object_end(const char *start,
                                       const char *document_end) {
  int depth = 0;
  bool in_string = false;
  bool escaped = false;

  for (const char *p = start; p < document_end; ++p) {
    char c = *p;
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }

    if (c == '"') {
      in_string = true;
    } else if (c == '{') {
      depth++;
    } else if (c == '}') {
      depth--;
      if (depth == 0) return p;
    }
  }
  return NULL;
}

static bool parse_members(const char *s, size_t len) {
  ntl_member next[NTL_MAX_TEAM];
  int next_count = 0;
  bool baseline_only = !S.feed_baselined;
  bool own_message_echoed = !S.inflight_msg[0];
  memset(next, 0, sizeof next);

  const char *end = s + len;
  const char *p = s;
  while (next_count < NTL_MAX_TEAM && (p = strchr(p, '{')) && p < end) {
    const char *e = ntl_find_object_end(p, end);
    if (!e || e >= end) break;

    ntl_member *m = &next[next_count];
    memset(m, 0, sizeof *m);
    m->sid = -1;
    if (field_str(p, e, "nick", m->nick, sizeof m->nick)) {
      field_str(p, e, "msg", m->msg, sizeof m->msg);
      field_str(p, e, "srv", m->srv, sizeof m->srv);
      field_str(p, e, "dt", m->dt, sizeof m->dt);
      ntl_normalize_chat_text(m->nick, sizeof m->nick);
      ntl_decode_chat_snapshot(m->msg, sizeof m->msg);
      ntl_normalize_chat_text(m->dt, sizeof m->dt);
      m->x = (float)field_num(p, e, "valx", 0);
      m->y = (float)field_num(p, e, "valy", 0);
      m->sid = (int)field_num(p, e, "sid", -1);
      m->score = (int)field_num(p, e, "score", 0);
      m->rank = (int)field_num(p, e, "rank", 0);

      if (S.inflight_msg[0] && strlen(m->nick) >= 8 &&
          !strncmp(m->nick, S.request_client_id, 8) &&
          ntl_snapshot_contains_message(m->msg, S.inflight_msg))
        own_message_echoed = true;

      const char *old = seen_message_for(m->nick);
      if (!baseline_only && m->msg[0]) {
        if (!old || !old[0]) {
          /* A player first appearing after the initial baseline may already
             carry a real message. The extension displays it immediately. */
          ntl_add_snapshot_delta(m->nick, m->msg);
        } else if (strcmp(old, m->msg) != 0) {
          /* NTL can return an accumulated browser message buffer. Match the
             official extension by removing the previous snapshot prefix. The
             suffix/prefix overlap also handles a bounded server-side buffer
             that rotates older text out. */
          size_t overlap = ntl_snapshot_overlap(old, m->msg);
          ntl_add_snapshot_delta(m->nick, m->msg + overlap);
        }
      }
      remember_message(m->nick, m->msg);
      next_count++;
    }
    p = e + 1;
  }

  memcpy(S.members, next, sizeof next);
  S.count = next_count;
  S.feed_baselined = true;
  return own_message_echoed;
}

static void cb(struct mg_connection *c, int ev, void *ev_data) {
  /* Ignore final events from a request that has already timed out or has been
     superseded. This prevents an old close/error event from breaking a newer
     poll. */
  if (c != S.request_conn) return;

  if (ev == MG_EV_CONNECT) {
    struct mg_tls_opts tls = {.skip_verification = 1};
    mg_tls_init(c, &tls);
    mg_printf(c,
              "GET %s HTTP/1.1\r\nHost: ntl-slither.com\r\n"
              "User-Agent: Vlither/4.1\r\n"
              "Accept: application/json,text/plain,*/*\r\n"
              "Connection: close\r\n\r\n",
              S.request_path);
  } else if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = ev_data;
    double now = mg_millis() / 1000.0;
    S.last_http_status = mg_http_status(hm);
    bool request_matches_active =
        !strcmp(S.request_team_id, S.active_team_id) &&
        !strcmp(S.request_auth_key, S.active_auth_key);
    S.last_request_ok = S.last_http_status == 200 && request_matches_active;

    if (S.last_request_ok) {
      size_t body_len = hm->body.len;
      char *body = malloc(body_len + 1);
      if (body) {
        memcpy(body, hm->body.buf, body_len);
        body[body_len] = 0;
        bool message_echoed = parse_members(body, body_len);
        free(body);
        S.last_success = now;
        S.consecutive_failures = 0;
        /* Match the extension protocol: keep publishing a chat message until
           this same eight-character client ID is returned with that message.
           This prevents a short connection interruption from producing only a
           local echo that other NTL clients never see. */
        if (message_echoed) S.inflight_msg[0] = 0;
        S.next_poll = now + NTL_POLL_SECONDS;
      } else {
        ntl_schedule_retry(now);
      }
    } else {
      ntl_schedule_retry(now);
    }

    S.request_active = false;
    S.request_conn = NULL;
    c->is_draining = 1;
  } else if (ev == MG_EV_ERROR) {
    double now = mg_millis() / 1000.0;
    S.request_active = false;
    S.request_conn = NULL;
    ntl_schedule_retry(now);
  } else if (ev == MG_EV_CLOSE) {
    /* A close before an HTTP message is a failed heartbeat. Normal closes are
       ignored because MG_EV_HTTP_MSG already clears request_conn. */
    double now = mg_millis() / 1000.0;
    S.request_active = false;
    S.request_conn = NULL;
    ntl_schedule_retry(now);
  }
}
static snake *local_snake(game_data *g) {
  int count = tdarray_length(g->data.snakes);
  for (int i = 0; i < count; i++) {
    if (g->data.snakes[i].id == g->data.snake_id) return &g->data.snakes[i];
  }
  return NULL;
}

static void ntl_poll_request(tenv *env) {
  tuser_data *u = env->usr;
  user_settings *us = &u->usrs;
  game_data *g = &u->gdata;

  if (S.request_active) return;
  ntl_sync_active_credentials(us);
  if (!us->ntl_enabled || strlen(us->ntl_auth_key) < 16 ||
      strlen(us->ntl_team_id) < 16) {
    return;
  }

  ntl_ensure_client_id(us);

  char raw_nick[sizeof us->ntl_client_id + MAX_NICKNAME_LEN + 2];
  const char *visible_nick = us->nickname[0] ? us->nickname : "Vlither";
  snprintf(raw_nick, sizeof raw_nick, "%s%s", us->ntl_client_id, visible_nick);

  bool in_game = g->conn == CONNECTED && g->curr_screen == PLAYING;
  snake *local = in_game ? local_snake(g) : NULL;
  const char *presence_server = local ? us->ipv4 : "_GAME_MENU_";
  float x = local ? local->xx + local->fx : 0.0f;
  float y = local ? local->yy + local->fy : 0.0f;
  int sid = local ? local->id : -1;
  int score = local ? g->data.score : 0;
  int rank = local ? g->data.rank : 0;

  /* Keep one message in flight until a successful response. A newly typed
     message can wait in pending_msg without overwriting the retrying one. */
  if (!S.inflight_msg[0] && S.pending_msg[0]) {
    strncpy(S.inflight_msg, S.pending_msg, sizeof S.inflight_msg - 1);
    S.inflight_msg[sizeof S.inflight_msg - 1] = 0;
    S.pending_msg[0] = 0;
  }

  char nick[192];
  char msg[768];
  char srv[192];
  urlenc(nick, sizeof nick, raw_nick);
  urlenc(msg, sizeof msg, S.inflight_msg);
  urlenc(srv, sizeof srv, presence_server);

  strncpy(S.request_team_id, us->ntl_team_id, sizeof S.request_team_id - 1);
  S.request_team_id[sizeof S.request_team_id - 1] = 0;
  strncpy(S.request_auth_key, us->ntl_auth_key, sizeof S.request_auth_key - 1);
  S.request_auth_key[sizeof S.request_auth_key - 1] = 0;
  strncpy(S.request_client_id, us->ntl_client_id, sizeof S.request_client_id - 1);
  S.request_client_id[sizeof S.request_client_id - 1] = 0;

  snprintf(
      S.request_path, sizeof S.request_path,
      "/slither/ntlplay-mt.php?auth=%s&tid=%s&nick=%s&score=%d"
      "&valx=%.0f&valy=%.0f&bot=false&sos=false&food=false&srv=%s"
      "&sid=%d&msg=%s&rank=%d&dt=Vlither&cs=%d&ver=4.1&tlm=&di=1000",
      us->ntl_auth_key, us->ntl_team_id, nick, score, x, y, srv, sid,
      msg, rank, local ? local->accessory : 0);

  S.request_conn =
      mg_http_connect(&S.mgr, "https://ntl-slither.com", cb, NULL);
  S.request_active = S.request_conn != NULL;
  S.request_started = mg_millis() / 1000.0;
  if (!S.request_active) ntl_schedule_retry(S.request_started);
}
void ntl_team_init(tenv *env) {
  memset(&S, 0, sizeof S);
  mg_mgr_init(&S.mgr);
  S.ready = true;
  if (env && env->usr) {
    user_settings *us = &env->usr->usrs;
    ntl_ensure_client_id(us);
    strncpy(S.last_nickname, us->nickname, sizeof S.last_nickname - 1);
    S.last_nickname[sizeof S.last_nickname - 1] = 0;
    strncpy(S.last_presence_server, "_GAME_MENU_",
            sizeof S.last_presence_server - 1);
  }
  S.chat_open = true;
  S.select_chat_tab = true;
  tuser_data *u = env ? env->usr : NULL;
  if (u && u->usrs.ntl_active_team_profile >= 0 &&
      u->usrs.ntl_active_team_profile < u->usrs.ntl_team_profile_count) {
    ntl_team_profile *profile =
        &u->usrs.ntl_team_profiles[u->usrs.ntl_active_team_profile];
    strncpy(S.profile_name, profile->name, sizeof S.profile_name - 1);
    S.profile_name[sizeof S.profile_name - 1] = 0;
  }
#ifdef ANDROID
  S.players_open = false;
#else
  S.players_open = true;
#endif
}
void ntl_team_update(tenv *env) {
  if (!S.ready || !env || !env->usr) return;

  user_settings *us = &env->usr->usrs;
  game_data *g = &env->usr->gdata;
  ntl_sync_active_credentials(us);
  ntl_ensure_client_id(us);

  /* Publish nickname/server transitions immediately. The stable eight-character
     client prefix lets NTL replace this player's old name instead of creating a
     second entry. */
  bool playing = g->conn == CONNECTED && g->curr_screen == PLAYING;
  const char *presence_server = playing ? us->ipv4 : "_GAME_MENU_";
  if (strcmp(S.last_nickname, us->nickname) != 0 ||
      strcmp(S.last_presence_server, presence_server) != 0 ||
      S.last_presence_playing != playing) {
    strncpy(S.last_nickname, us->nickname, sizeof S.last_nickname - 1);
    S.last_nickname[sizeof S.last_nickname - 1] = 0;
    strncpy(S.last_presence_server, presence_server,
            sizeof S.last_presence_server - 1);
    S.last_presence_server[sizeof S.last_presence_server - 1] = 0;
    S.last_presence_playing = playing;
    S.next_poll = 0.0;
  }

  mg_mgr_poll(&S.mgr, 0);
  double now = mg_millis() / 1000.0;

  if (S.request_active &&
      now - S.request_started > NTL_REQUEST_TIMEOUT_SECONDS) {
    struct mg_connection *timed_out = S.request_conn;
    S.request_active = false;
    S.request_conn = NULL;
    if (timed_out) timed_out->is_closing = true;
    ntl_schedule_retry(now);
  }

  if (!S.request_active && now >= S.next_poll) {
    ntl_poll_request(env);
    if (S.request_active && S.next_poll < now + NTL_POLL_SECONDS)
      S.next_poll = now + NTL_POLL_SECONDS;
  }
}
static void normalize_server(char *out, size_t cap, const char *in) {
  if (!out || cap == 0) return;
  out[0] = 0;
  if (!in) return;

  while (isspace((unsigned char)*in)) in++;
  if (!strncmp(in, "ws://", 5)) in += 5;
  else if (!strncmp(in, "wss://", 6)) in += 6;
  else if (!strncmp(in, "http://", 7)) in += 7;
  else if (!strncmp(in, "https://", 8)) in += 8;

  size_t n = 0;
  while (*in && n + 1 < cap) {
    unsigned char c = (unsigned char)*in++;
    if (c == '/' || c == '?' || c == '#' || isspace(c)) break;
    out[n++] = (char)tolower(c);
  }
  while (n > 0 && (out[n - 1] == '/' || isspace((unsigned char)out[n - 1]))) n--;
  out[n] = 0;
}

static bool same_server(const char *a, const char *b) {
  char na[96], nb[96];
  normalize_server(na, sizeof na, a);
  normalize_server(nb, sizeof nb, b);
  return na[0] && nb[0] && !strcmp(na, nb);
}

static const char *ntl_clean_name(const char *name) {
  if (!name) return "Player";
  if (strlen(name) > 8) {
    bool prefix_is_hex = true;
    for (int i = 0; i < 8; ++i) {
      if (!isxdigit((unsigned char)name[i])) {
        prefix_is_hex = false;
        break;
      }
    }
    if (prefix_is_hex) return name + 8;
  }
  return name[0] ? name : "Player";
}

static void ntl_draw_shape(ImDrawList *dl, ImVec2 p, float radius,
                           int shape, ImU32 fill, ImU32 border) {
  float border_radius = radius + 1.4f;
  if (shape == 1) {
    /* Diamond */
    ImVec2 outer[4] = {{p.x, p.y - border_radius},
                       {p.x + border_radius, p.y},
                       {p.x, p.y + border_radius},
                       {p.x - border_radius, p.y}};
    ImVec2 inner[4] = {{p.x, p.y - radius}, {p.x + radius, p.y},
                       {p.x, p.y + radius}, {p.x - radius, p.y}};
    ImDrawList_AddConvexPolyFilled(dl, outer, 4, border);
    ImDrawList_AddConvexPolyFilled(dl, inner, 4, fill);
  } else if (shape == 2) {
    /* Triangle (legacy -- kept so old saved settings still render) */
    ImVec2 o1 = {p.x, p.y - border_radius};
    ImVec2 o2 = {p.x + border_radius * 0.92f, p.y + border_radius * 0.80f};
    ImVec2 o3 = {p.x - border_radius * 0.92f, p.y + border_radius * 0.80f};
    ImVec2 i1 = {p.x, p.y - radius};
    ImVec2 i2 = {p.x + radius * 0.92f, p.y + radius * 0.80f};
    ImVec2 i3 = {p.x - radius * 0.92f, p.y + radius * 0.80f};
    ImDrawList_AddTriangleFilled(dl, o1, o2, o3, border);
    ImDrawList_AddTriangleFilled(dl, i1, i2, i3, fill);
  } else if (shape == 3) {
    /* Square */
    float bo = border_radius * 0.82f;
    float bi = radius * 0.82f;
    ImDrawList_AddRectFilled(
        dl, (ImVec2){p.x - bo, p.y - bo}, (ImVec2){p.x + bo, p.y + bo},
        border, 0, ImDrawFlags_None);
    ImDrawList_AddRectFilled(
        dl, (ImVec2){p.x - bi, p.y - bi}, (ImVec2){p.x + bi, p.y + bi},
        fill, 0, ImDrawFlags_None);
  } else if (shape == 4) {
    /* Cross */
    float bo_len = border_radius * 1.05f, bo_th = border_radius * 0.42f;
    float bi_len = radius * 1.05f, bi_th = radius * 0.42f;
    ImDrawList_AddRectFilled(
        dl, (ImVec2){p.x - bo_len, p.y - bo_th}, (ImVec2){p.x + bo_len, p.y + bo_th},
        border, 0, ImDrawFlags_None);
    ImDrawList_AddRectFilled(
        dl, (ImVec2){p.x - bo_th, p.y - bo_len}, (ImVec2){p.x + bo_th, p.y + bo_len},
        border, 0, ImDrawFlags_None);
    ImDrawList_AddRectFilled(
        dl, (ImVec2){p.x - bi_len, p.y - bi_th}, (ImVec2){p.x + bi_len, p.y + bi_th},
        fill, 0, ImDrawFlags_None);
    ImDrawList_AddRectFilled(
        dl, (ImVec2){p.x - bi_th, p.y - bi_len}, (ImVec2){p.x + bi_th, p.y + bi_len},
        fill, 0, ImDrawFlags_None);
  } else if (shape == 5) {
    /* 5-point star */
    ImVec2 outer_pts[10], inner_pts[10];
    for (int i = 0; i < 10; i++) {
      float ang = (float)M_PI / 2.0f + i * (float)M_PI / 5.0f;
      float r_o = (i % 2 == 0) ? border_radius * 1.15f : border_radius * 0.45f;
      float r_i = (i % 2 == 0) ? radius * 1.15f : radius * 0.45f;
      outer_pts[i] = (ImVec2){p.x + cosf(ang) * r_o, p.y - sinf(ang) * r_o};
      inner_pts[i] = (ImVec2){p.x + cosf(ang) * r_i, p.y - sinf(ang) * r_i};
    }
    ImDrawList_AddConvexPolyFilled(dl, outer_pts, 10, border);
    ImDrawList_AddConvexPolyFilled(dl, inner_pts, 10, fill);
  } else {
    /* Circle */
    ImDrawList_AddCircleFilled(dl, p, border_radius, border, 16);
    ImDrawList_AddCircleFilled(dl, p, radius, fill, 16);
  }
}

/* ---- colour emoji dots ------------------------------------------------ */

static const unsigned int ntl_emoji_cps[NTL_EMOJI_COUNT] = {0x1F600,0x1F603,0x1F604,0x1F601,0x1F606,0x1F605,0x1F602,0x1F923,0x1F60A,0x1F607,0x1F642,0x1F643,0x1F609,0x1F60C,0x1F60D,0x1F970,0x1F618,0x1F61C,0x1F61D,0x1F911,0x1F917,0x1F914,0x1F928,0x1F610,0x1F611,0x1F636,0x1F644,0x1F60F,0x1F623,0x1F625,0x1F62E,0x1F910,0x1F62F,0x1F62A,0x1F62B,0x1F634,0x1F60E,0x1F913,0x1F9D0,0x1F615,0x1F61F,0x1F641,0x1F632,0x1F633,0x1F97A,0x1F626,0x1F627,0x1F628,0x1F630,0x1F622,0x1F62D,0x1F631,0x1F616,0x1F61E,0x1F613,0x1F629,0x1F624,0x1F621,0x1F620,0x1F92C,0x1F608,0x1F47F,0x1F480,0x2620,0x1F4A9,0x1F921,0x1F47B,0x1F47D,0x1F916,0x1F63A,0x1F638,0x1F63B,0x1F63C,0x1F640,0x1F648,0x1F649,0x1F64A,0x1F4AF,0x1F4A5,0x1F525,0x2728,0x2B50,0x1F31F,0x26A1,0x1F4A2,0x1F4A4,0x1F4A8,0x1F4A3,0x1F389,0x1F3AF,0x1F3C6,0x1F451,0x1F48E,0x1F340,0x1F337,0x1F339,0x1F33A,0x1F335,0x1F30A,0x2764,0x1F9E1,0x1F49B,0x1F49A,0x1F499,0x1F49C,0x1F5A4,0x1F90D,0x1F494,0x1F495,0x1F44D,0x1F44E,0x1F44A,0x270C,0x1F918,0x1F44C,0x1F44B,0x1F4AA,0x1F64F,0x1F440,0x1F40D,0x1F409,0x1F432,0x1F985,0x1F981,0x1F42F,0x1F43A,0x1F98A,0x1F43C,0x1F438,0x1F422,0x1F419,0x1F988,0x1F40A,0x1F577,0x1F982,0x1F47E,0x1F3AE,0x1F680,0x1F6F8,0x2600,0x1F319,0x2744,0x2601,0x1F308,0x1F34E,0x1F355,0x1F354,0x1F366,0x1F37A,0x26BD,0x1F3C0,0x1F3B5,0x1F52B,0x1F5E1,0x1F6E1,0x1F9E8,0x1F3F4,0x1F6A9,0x1F9FF};

static unsigned long long g_emoji_tex = 0;

void ntl_emoji_set_texture(unsigned long long texture_id) {
  g_emoji_tex = texture_id;
}

static void ntl_draw_emoji_image(ImDrawList *dl, ImVec2 a, ImVec2 b, int idx,
                                 ImU32 tint) {
  if (!g_emoji_tex || idx < 0 || idx >= NTL_EMOJI_COUNT) return;
  int col = idx % NTL_EMOJI_COLS;
  int row = idx / NTL_EMOJI_COLS;
  ImVec2 uv0 = {(float)col / NTL_EMOJI_COLS, (float)row / NTL_EMOJI_ROWS};
  ImVec2 uv1 = {(float)(col + 1) / NTL_EMOJI_COLS,
                (float)(row + 1) / NTL_EMOJI_ROWS};
  ImTextureRef tex = {NULL, (ImTextureID)g_emoji_tex};
  ImDrawList_AddImage(dl, tex, a, b, uv0, uv1, tint);
}

/* ---- colour emoji helpers shared with the TEAM CHAT (global_chat.c) ---- */

bool ntl_emoji_ready(void) { return g_emoji_tex != 0; }

int ntl_emoji_index_for_cp(unsigned int cp) {
  for (int i = 0; i < NTL_EMOJI_COUNT; ++i)
    if (ntl_emoji_cps[i] == cp) return i;
  return -1;
}

unsigned int ntl_emoji_cp_for_index(int idx) {
  if (idx < 0 || idx >= NTL_EMOJI_COUNT) return 0;
  return ntl_emoji_cps[idx];
}

void ntl_emoji_draw(ImDrawList *dl, ImVec2 a, ImVec2 b, int idx, ImU32 tint) {
  ntl_draw_emoji_image(dl, a, b, idx, tint);
}

static ImU32 ntl_scale_alpha(ImU32 c, float a) {
  unsigned int al = (c >> 24) & 0xFFu;
  al = (unsigned int)((float)al * a + 0.5f);
  if (al > 255u) al = 255u;
  return (c & 0x00FFFFFFu) | (al << 24);
}

void ntl_draw_marker_pulse(ImDrawList *dl, ImVec2 p, float radius, int shape,
                           ImU32 fill, float pulse_interval) {
  float alpha = 1.0f;
  if (pulse_interval > 0.05f) {
    double t = igGetTime();
    float phase = (float)fmod(t, (double)pulse_interval) / pulse_interval;
    float w = 0.5f - 0.5f * cosf(phase * 2.0f * (float)M_PI);
    radius *= 0.85f + 0.40f * w;
    alpha = 0.55f + 0.45f * w;
  }
  ImU32 border = ntl_scale_alpha(IM_COL32(0, 0, 0, 210), alpha);
  fill = ntl_scale_alpha(fill, alpha);

  if (shape >= NTL_SHAPE_EMOJI_BASE) {
    int idx = shape - NTL_SHAPE_EMOJI_BASE;
    if (idx < NTL_EMOJI_COUNT && g_emoji_tex) {
      /* Emoji are drawn on their own -- no disc or border behind them. */
      float e = radius * 1.6f;
      ntl_draw_emoji_image(dl, (ImVec2){p.x - e, p.y - e},
                           (ImVec2){p.x + e, p.y + e}, idx,
                           IM_COL32(255, 255, 255, (int)(255.0f * alpha)));
      return;
    }
    shape = 0; /* unknown emoji (or sheet missing): plain circle */
  }
  ntl_draw_shape(dl, p, radius, shape, fill, border);
}

void ntl_draw_marker(ImDrawList *dl, ImVec2 p, float radius, int shape,
                     ImU32 fill) {
  ntl_draw_marker_pulse(dl, p, radius, shape, fill, 0.0f);
}

/* ---- one colour + shape/emoji per teammate ------------------------------ */

/* True if another teammate on our server already uses this exact
 * shape/emoji AND colour. A different shape OR a different colour is fine. */
static bool ntl_combo_taken(tenv *env, int shape, const float rgb[3],
                            char *who, size_t who_cap) {
  if (!env || !env->usr) return false;
  global_chat_teammate tm[16];
  int n = global_chat_get_teammates(env, tm, 16);
  const char *own = env->usr->usrs.nickname;
  for (int i = 0; i < n; ++i) {
    const char *clean = ntl_clean_name(tm[i].nickname);
    if (own[0] && (!strcmp(tm[i].nickname, own) || !strcmp(clean, own)))
      continue; /* that's me */
    if (tm[i].shape != shape) continue;
    /* An emoji looks the same whatever colour is picked, so for emoji the
       emoji alone has to be unique. */
    if (shape >= NTL_SHAPE_EMOJI_BASE) {
      if (who && who_cap) {
        strncpy(who, clean, who_cap - 1);
        who[who_cap - 1] = 0;
      }
      return true;
    }
    if (fabsf(tm[i].color[0] - rgb[0]) < 0.025f &&
        fabsf(tm[i].color[1] - rgb[1]) < 0.025f &&
        fabsf(tm[i].color[2] - rgb[2]) < 0.025f) {
      if (who && who_cap) {
        strncpy(who, clean, who_cap - 1);
        who[who_cap - 1] = 0;
      }
      return true;
    }
  }
  return false;
}

static void ntl_flag_taken(const char *who) {
  strncpy(S.dup_name, who && who[0] ? who : "a teammate", sizeof S.dup_name - 1);
  S.dup_name[sizeof S.dup_name - 1] = 0;
  S.dup_until = mg_millis() / 1000.0 + 3.5;
}

/* Apply a new shape + colour unless a teammate already has that exact pair. */
static bool ntl_try_set_marker(tenv *env, user_settings *us, int shape,
                               const float rgb[3]) {
  bool same = us->own_marker_shape == shape &&
              fabsf(us->own_marker_color[0] - rgb[0]) < 0.001f &&
              fabsf(us->own_marker_color[1] - rgb[1]) < 0.001f &&
              fabsf(us->own_marker_color[2] - rgb[2]) < 0.001f;
  if (same) return true;
  char who[40] = {0};
  if (ntl_combo_taken(env, shape, rgb, who, sizeof who)) {
    ntl_flag_taken(who);
    return false;
  }
  us->own_marker_shape = shape;
  us->own_marker_color[0] = rgb[0];
  us->own_marker_color[1] = rgb[1];
  us->own_marker_color[2] = rgb[2];
  return true;
}

/* First codepoint of a UTF-8 string, skipping variation selectors / joiners. */
static unsigned int ntl_first_codepoint(const char *str) {
  const unsigned char *u = (const unsigned char *)str;
  while (*u) {
    unsigned int cp;
    int len;
    if (u[0] < 0x80) { cp = u[0]; len = 1; }
    else if ((u[0] & 0xE0) == 0xC0) { cp = u[0] & 0x1F; len = 2; }
    else if ((u[0] & 0xF0) == 0xE0) { cp = u[0] & 0x0F; len = 3; }
    else if ((u[0] & 0xF8) == 0xF0) { cp = u[0] & 0x07; len = 4; }
    else { u++; continue; }
    for (int i = 1; i < len; ++i) {
      if ((u[i] & 0xC0) != 0x80) return 0;
      cp = (cp << 6) | (u[i] & 0x3F);
    }
    if (cp == 0xFE0F || cp == 0x200D || cp == 0x20E3 || cp == ' ') {
      u += len;
      continue;
    }
    return cp;
  }
  return 0;
}

static int ntl_emoji_lookup(const char *str) {
  unsigned int cp = ntl_first_codepoint(str);
  if (!cp) return -1;
  for (int i = 0; i < NTL_EMOJI_COUNT; ++i)
    if (ntl_emoji_cps[i] == cp) return i;
  return -1;
}


static void ntl_load_profile(user_settings *us, int index) {
  if (!us || index < 0 || index >= us->ntl_team_profile_count) return;
  ntl_team_profile *profile = &us->ntl_team_profiles[index];
  strncpy(us->ntl_team_id, profile->team_id, sizeof us->ntl_team_id - 1);
  us->ntl_team_id[sizeof us->ntl_team_id - 1] = 0;
  strncpy(us->ntl_auth_key, profile->auth_key, sizeof us->ntl_auth_key - 1);
  us->ntl_auth_key[sizeof us->ntl_auth_key - 1] = 0;
  strncpy(S.profile_name, profile->name, sizeof S.profile_name - 1);
  S.profile_name[sizeof S.profile_name - 1] = 0;
  us->ntl_active_team_profile = index;
  ntl_sync_active_credentials(us);
  S.next_poll = 0.0;
}

static int ntl_find_profile(const user_settings *us, const char *name) {
  if (!us || !name || !name[0]) return -1;
  for (int i = 0; i < us->ntl_team_profile_count; ++i)
    if (!strcmp(us->ntl_team_profiles[i].name, name)) return i;
  return -1;
}

static bool ntl_normalize_profile_name(char *name) {
  if (!name) return false;
  char *start = name;
  while (*start && isspace((unsigned char)*start)) start++;
  if (start != name) memmove(name, start, strlen(start) + 1);
  size_t len = strlen(name);
  while (len > 0 && isspace((unsigned char)name[len - 1])) name[--len] = 0;
  return len > 0;
}

static bool ntl_feed_is_fresh(void) {
  double now = mg_millis() / 1000.0;
  return S.last_success > 0.0 &&
         now - S.last_success <= NTL_CONNECTED_GRACE_SECONDS;
}

void ntl_team_draw_minimap(tenv *env, float x, float y, float size) {
  if (!env || !env->usr || size <= 0.0f) return;
  tuser_data *u = env->usr;
  user_settings *us = &u->usrs;
  game_data *g = &u->gdata;
  if (g->conn != CONNECTED || g->data.grd <= 0.0f) return;

  ImDrawList *dl = igGetWindowDrawList();
  if (!dl) return;
  float radius = size * 0.5f;
  float map_radius = radius * 0.90f;
  ImVec2 center = {x + radius, y + radius};

  /* Cover the shader's fixed local marker with the player's chosen marker.
     Normalize against the *current* (possibly shrunk) arena radius,
     flux_grd, not the original static grd -- flux_grd tracks the safe-zone
     shrinking over the course of a match, and using the static grd here
     made the dot drift toward the center relative to the real boundary as
     the match went on. */
  snake *local = local_snake(g);
  if (local) {
    float lx = local->xx + local->fx;
    float ly = local->yy + local->fy;
    float rx = (lx - g->data.grd) / g->data.flux_grd;
    float ry = (ly - g->data.grd) / g->data.flux_grd;
    float dist = sqrtf(rx * rx + ry * ry);
    if (dist > 1.0f) { rx /= dist; ry /= dist; }
    ImVec2 p = {center.x + rx * map_radius, center.y + ry * map_radius};
    ImU32 col = igColorConvertFloat4ToU32((ImVec4){
        us->own_marker_color[0], us->own_marker_color[1],
        us->own_marker_color[2], us->own_marker_color[3]});
    ntl_draw_marker_pulse(dl, p, us->own_marker_size, us->own_marker_shape,
                          col,
                          us->ntl_pulse_enabled ? us->ntl_pulse_interval : 0.0f);
  }
}

void ntl_team_consume_ui_touch(tenv *env) {
  (void)env;
  /* Android input ownership now prevents NTL-window touches from entering
     gameplay at ACTION_DOWN. Preserve any separate trackpad finger. */
}

void ntl_team_draw(tenv *env) {
  // NTL's own floating chat/player HUD widgets are retired --
  // Public/Team Chat (global_chat.c) replaces this entirely.
  // Kept as a no-op (rather than removed) so main.c's existing
  // call site doesn't need touching, and so anyone whose saved
  // settings still have ntl_enabled=true from before this change
  // can no longer get this widget stuck on-screen with no way
  // to turn it off.
  (void)env;
}

void ntl_team_destroy(tenv *env) {
  (void)env;
  if (S.ready) mg_mgr_free(&S.mgr);
  memset(&S, 0, sizeof S);
}

static void ntl_queue_message(const user_settings *us) {
  ntl_normalize_chat_text(S.input, sizeof S.input);
  if (!S.input[0]) return;

  double now = mg_millis() / 1000.0;
  if (!strcmp(S.last_sent_text, S.input) && now - S.last_sent_time < 1.5) {
    S.input[0] = 0;
    return;
  }

  /* Do not insert a local chat echo here. The NTL response is the
     authoritative feed and will add this message once when the server echoes
     it for our persistent client ID. Adding it locally as well made every
     outgoing message appear twice. */
  (void)us;
  strncpy(S.pending_msg, S.input, sizeof S.pending_msg - 1);
  S.pending_msg[sizeof S.pending_msg - 1] = 0;
  strncpy(S.last_sent_text, S.input, sizeof S.last_sent_text - 1);
  S.last_sent_text[sizeof S.last_sent_text - 1] = 0;
  S.last_sent_time = now;
  S.input[0] = 0;
  S.next_poll = 0;
}

void ntl_team_panel(tenv *env) {
  tuser_data *u = env->usr;
  user_settings *us = &u->usrs;
  game_data *g = &u->gdata;
  ImGuiStyle *style = igGetStyle();
  ImVec2 avail;
  igGetContentRegionAvail(&avail);

  igPushFont(u->imgui_data.regular_font[us->ui_font_size],
             u->imgui_data.regular_font[us->ui_font_size]->LegacySize);

  u->r->global.bg_opacity = 0;
  u->r->global.bd_opacity = 0;
  u->r->global.minimap_opacity = 0;

  /* Only the lobby background behind this screen (no game / bot preview). */
  ui_lobby_bg_draw(env);
  crystal_push_theme();

  tcontext *ctx = env->ctx;
  float ui_s = 1.0f;
  {
    float se = ctx->size[0] < ctx->size[1] ? ctx->size[0] : ctx->size[1];
    ui_s = se / 720.0f;
    if (ui_s < 0.75f) ui_s = 0.75f;
    if (ui_s > 1.30f) ui_s = 1.30f;
  }

  /* ---- connection status (text + colour) ---- */
  char status_txt[96];
  ImVec4 status_col = {1.0f, 0.55f, 0.25f, 1.0f};
  if (!us->ntl_enabled) {
    snprintf(status_txt, sizeof status_txt, "NTL chat disabled");
  } else if (strlen(us->ntl_auth_key) < 16 || strlen(us->ntl_team_id) < 16) {
    snprintf(status_txt, sizeof status_txt, "Credentials required");
    status_col = (ImVec4){1.0f, 0.35f, 0.35f, 1.0f};
  } else {
    double now = mg_millis() / 1000.0;
    bool recently_connected = S.last_success > 0 &&
                              now - S.last_success < NTL_CONNECTED_GRACE_SECONDS;
    if (recently_connected) {
      snprintf(status_txt, sizeof status_txt,
               S.consecutive_failures > 0
                   ? "Connected - reconnecting (%d online)"
                   : "Connected - %d online",
               S.count);
      status_col = (ImVec4){0.35f, 1.0f, 0.5f, 1.0f};
    } else if (S.request_active) {
      snprintf(status_txt, sizeof status_txt, "Connecting...");
      status_col = (ImVec4){1.0f, 0.85f, 0.3f, 1.0f};
    } else if (S.consecutive_failures > 0) {
      snprintf(status_txt, sizeof status_txt, "Reconnecting automatically...");
      status_col = (ImVec4){1.0f, 0.68f, 0.25f, 1.0f};
    } else if (!S.last_request_ok && S.last_http_status != 0) {
      snprintf(status_txt, sizeof status_txt, "Server error HTTP %d",
               S.last_http_status);
      status_col = (ImVec4){1.0f, 0.35f, 0.35f, 1.0f};
    } else {
      snprintf(status_txt, sizeof status_txt, "Waiting for NTL server");
    }
  }

  static bool popup_was_open = false;
  static int ntl_tab = 0;
  global_chat_adjust_mode chat_adjust_mode = global_chat_get_adjust_mode();
  bool pos_active = chat_adjust_mode == GLOBAL_CHAT_ADJUST_POSITION;
  bool size_active = chat_adjust_mode == GLOBAL_CHAT_ADJUST_SIZE;

  /* While moving / resizing the chat window, get the big panel out of the way
     (it would cover the screen) and show just a small Confirm bar. */
  if (chat_adjust_mode != GLOBAL_CHAT_ADJUST_NONE) {
    popup_was_open = false; /* reopened automatically after Confirm */
    float bar_w = fminf(460.0f * ui_s, ctx->size[0] - 32.0f);
    igSetNextWindowPos((ImVec2){ctx->size[0] * 0.5f,
                                ctx->size[1] - 16.0f * ui_s},
                       ImGuiCond_Always, (ImVec2){0.5f, 1.0f});
    igPushStyleVar_Float(ImGuiStyleVar_WindowRounding, 16.0f);
    igPushStyleVar_Float(ImGuiStyleVar_WindowBorderSize, 1.5f);
    igPushStyleVar_Vec2(ImGuiStyleVar_WindowPadding,
                        (ImVec2){14.0f * ui_s, 12.0f * ui_s});
    igPushStyleColor_Vec4(ImGuiCol_WindowBg,
                          (ImVec4){0.070f, 0.050f, 0.120f, 0.96f});
    igPushStyleColor_Vec4(ImGuiCol_Border,
                          (ImVec4){0.690f, 0.580f, 0.960f, 0.55f});
    igBegin("##ntl_adjust_bar", NULL,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize);
    igPushTextWrapPos(igGetCursorPosX() + bar_w);
    igTextWrapped("%s", pos_active
                            ? "Drag the chat window to where you want it, then tap Confirm."
                            : "Drag the chat window's edge or corner to resize it, then tap Confirm.");
    igPopTextWrapPos();
    igSpacing();
    igPushStyleColor_Vec4(ImGuiCol_Button,
                          (ImVec4){0.20f, 0.62f, 0.30f, 1.0f});
    if (igButton(pos_active ? "CONFIRM POSITION" : "CONFIRM SIZE",
                 (ImVec2){bar_w, igGetFrameHeight() * 1.6f})) {
      global_chat_set_adjust_mode(env, GLOBAL_CHAT_ADJUST_NONE);
    }
    igPopStyleColor(1);
    igEnd();
    igPopStyleColor(2);
    igPopStyleVar(3);
    crystal_pop_theme();
    igPopFont();
    return;
  }

  /* ---- the panel: same footprint as the Settings / Controls popups ---- */
  if (!popup_was_open) igOpenPopup_Str("NTL Team Chat", 0);
  float footer_h = igGetFrameHeight() * 1.6f + style->ItemSpacing.y * 2.0f;
  crystal_panel_set_layout(0.52f, 0.88f, footer_h);
  if (!crystal_panel_begin(env, "NTL Team Chat", "TEAM CHAT",
                           "Map dots, chat position and online players",
                           480.0f)) {
    if (popup_was_open) { /* closed with the X / by tapping outside = Back */
      popup_was_open = false;
      save_user_settings(us);
      g->curr_screen = TITLE_SCREEN;
    }
    crystal_pop_theme();
    igPopFont();
    return;
  }
  popup_was_open = true;
  ui_touch_scroll(env, 22);

  /* status pill */
  {
    ImVec2 p;
    igGetCursorScreenPos(&p);
    float lh = igGetTextLineHeight();
    ImDrawList *sdl = igGetWindowDrawList();
    ImDrawList_AddCircleFilled(sdl, (ImVec2){p.x + lh * 0.45f, p.y + lh * 0.5f},
                               lh * 0.26f, igColorConvertFloat4ToU32(status_col),
                               14);
    igSetCursorScreenPos((ImVec2){p.x + lh * 1.1f, p.y});
    igTextColored(status_col, "%s", status_txt);
    igSpacing();
  }

  /* tab row (same look as the Settings tabs) */
  {
    const char *tab_names[3] = {"Map dots", "Chat", "Players"};
    ImVec2 tav;
    igGetContentRegionAvail(&tav);
    float tw = (tav.x - style->ItemSpacing.x * 2.0f) / 3.0f;
    float th = igGetFrameHeight() * 1.15f;
    for (int t = 0; t < 3; t++) {
      if (t) igSameLine(0, -1);
      bool sel = t == ntl_tab;
      igPushStyleColor_Vec4(ImGuiCol_Button,
                            sel ? (ImVec4){0.510f, 0.294f, 0.910f, 0.95f}
                                : (ImVec4){0.200f, 0.160f, 0.320f, 0.85f});
      if (igButton(tab_names[t], (ImVec2){tw, th})) ntl_tab = t;
      igPopStyleColor(1);
    }
    igSpacing();
  }

  if (ntl_tab == 0) {
  crystal_section("Map dot customization");
  igCheckbox("Show teammates on minimap", &us->ntl_show_teammates);
  igCheckbox("Show teammate names", &us->ntl_marker_labels);
  igSpacing();

  igText("My dot size");
  igSameLine(0, 8);
  igSetNextItemWidth(-1);
  igSliderFloat("##own_marker_size", &us->own_marker_size, 2.0f, 14.0f,
                "%.1f px", ImGuiSliderFlags_AlwaysClamp);
  igText("Team dots size");
  igSameLine(0, 8);
  igSetNextItemWidth(-1);
  igSliderFloat("##team_marker_size", &us->ntl_marker_size, 2.0f, 14.0f,
                "%.1f px", ImGuiSliderFlags_AlwaysClamp);
  igSpacing();

  /* Pulse: local display option -- the dots on YOUR minimap breathe so they
   * are easier to spot. Interval = seconds per pulse. */
  igCheckbox("Pulse dots", &us->ntl_pulse_enabled);
  if (us->ntl_pulse_enabled) {
    igText("Pulse interval");
    igSameLine(0, 8);
    igSetNextItemWidth(-1);
    igSliderFloat("##ntl_pulse_interval", &us->ntl_pulse_interval, 0.3f, 3.0f,
                  "%.1f s", ImGuiSliderFlags_AlwaysClamp);
  }
  igSpacing();

  /* Live preview of the dot exactly as the minimap draws it. */
  crystal_section("Preview");
  {
    ImVec2 pav;
    igGetContentRegionAvail(&pav);
    float pw = pav.x;
    float ph = igGetFrameHeight() * 2.8f;
    ImVec2 ppos;
    igGetCursorScreenPos(&ppos);
    ImDrawList *pdl = igGetWindowDrawList();
    ImVec2 pend = {ppos.x + pw, ppos.y + ph};
    ImDrawList_AddRectFilled(pdl, ppos, pend, IM_COL32(8, 6, 20, 230), 6.0f,
                             ImDrawFlags_None);
    ImDrawList_AddRect(pdl, ppos, pend, IM_COL32(120, 90, 200, 160), 6.0f,
                       ImDrawFlags_None, 1.0f);
    float interval = us->ntl_pulse_enabled ? us->ntl_pulse_interval : 0.0f;
    ImU32 pcol = igColorConvertFloat4ToU32((ImVec4){
        us->own_marker_color[0], us->own_marker_color[1],
        us->own_marker_color[2], 1.0f});
    float cy = ppos.y + ph * 0.55f;
    float zoom_r = fminf(us->own_marker_size * 2.5f, ph * 0.22f);
    ntl_draw_marker_pulse(pdl, (ImVec2){ppos.x + pw * 0.25f, cy},
                          us->own_marker_size, us->own_marker_shape, pcol,
                          interval);
    ntl_draw_marker_pulse(pdl, (ImVec2){ppos.x + pw * 0.70f, cy}, zoom_r,
                          us->own_marker_shape, pcol, interval);
    ImDrawList_AddText_Vec2(pdl, (ImVec2){ppos.x + pw * 0.25f - 24.0f, ppos.y + 3.0f},
                            IM_COL32(190, 190, 215, 220), "Real size", NULL);
    ImDrawList_AddText_Vec2(pdl, (ImVec2){ppos.x + pw * 0.70f - 20.0f, ppos.y + 3.0f},
                            IM_COL32(190, 190, 215, 220), "Zoomed", NULL);
    if (S.dup_until > mg_millis() / 1000.0) {
      char msg[96];
      snprintf(msg, sizeof msg, "Already used by %s - pick another", S.dup_name);
      ImDrawList_AddText_Vec2(pdl, (ImVec2){ppos.x + 8.0f, pend.y - igGetFrameHeight()},
                              IM_COL32(255, 150, 60, 255), msg, NULL);
    }
    igDummy((ImVec2){pw, ph});
  }
  if (igButton("Reset dot to default", (ImVec2){-1.0f, 0.0f})) {
    us->own_marker_size = 5.5f;
    us->ntl_marker_size = 5.0f;
    us->ntl_pulse_enabled = false;
    us->ntl_pulse_interval = 1.0f;
    S.emoji_input[0] = 0;
    S.emoji_status = 0;
    const float def_col[3] = {1.0f, 0.35f, 0.35f};
    char who[40] = {0};
    if (ntl_combo_taken(env, 0, def_col, who, sizeof who)) {
      /* Default look already belongs to a teammate: keep the current
         shape/colour, everything else is reset. */
      ntl_flag_taken(who);
    } else {
      us->own_marker_shape = 0;
      us->own_marker_color[0] = def_col[0];
      us->own_marker_color[1] = def_col[1];
      us->own_marker_color[2] = def_col[2];
    }
  }
  igSpacing();

  /* Dot colour/shape below describe MY OWN marker. They're sent
   * to teammates over the same NTL/JSR relay that already carries
   * position updates (see jsr_network_send_location), so a
   * teammate who picks e.g. a blue diamond shows up as a blue
   * diamond on everyone else's minimap -- not just their own. */
  crystal_section("Dot colour");
  igTextWrapped("How your dot appears on your teammates' minimaps.");

  static const float ntl_color_presets[6][3] = {
      {0.90f, 0.23f, 0.23f}, {0.20f, 0.80f, 0.35f}, {0.20f, 0.50f, 0.95f},
      {0.95f, 0.85f, 0.20f}, {0.25f, 0.85f, 0.85f}, {0.85f, 0.30f, 0.85f}};
  float swatch = igGetFrameHeight() * 1.2f;
  for (int i = 0; i < 6; i++) {
    igPushID_Int(i);
    bool selected =
        fabsf(us->own_marker_color[0] - ntl_color_presets[i][0]) < 0.01f &&
        fabsf(us->own_marker_color[1] - ntl_color_presets[i][1]) < 0.01f &&
        fabsf(us->own_marker_color[2] - ntl_color_presets[i][2]) < 0.01f;
    ImVec4 c = {ntl_color_presets[i][0], ntl_color_presets[i][1],
                ntl_color_presets[i][2], 1.0f};
    igPushStyleColor_Vec4(ImGuiCol_Button, c);
    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered, c);
    igPushStyleColor_Vec4(ImGuiCol_ButtonActive, c);
    if (selected) {
      igPushStyleColor_Vec4(ImGuiCol_Border, (ImVec4){1.0f, 1.0f, 1.0f, 1.0f});
      igPushStyleVar_Float(ImGuiStyleVar_FrameBorderSize, 2.0f);
    }
    if (igButton("##swatch", (ImVec2){swatch, swatch}))
      ntl_try_set_marker(env, us, us->own_marker_shape, ntl_color_presets[i]);
    if (selected) {
      igPopStyleVar(1);
      igPopStyleColor(1);
    }
    igPopStyleColor(3);
    igPopID();
    igSameLine(0, 6);
  }
  if (igButton("Custom##own_marker_custom", (ImVec2){swatch * 1.8f, swatch}))
    igOpenPopup_Str("##own_marker_custom_popup", 0);
  if (igBeginPopup("##own_marker_custom_popup", 0)) {
    float prev_col[3] = {us->own_marker_color[0], us->own_marker_color[1],
                         us->own_marker_color[2]};
    if (igColorPicker3("##own_marker_custom_picker", us->own_marker_color,
                       ImGuiColorEditFlags_None)) {
      char who[40] = {0};
      if (ntl_combo_taken(env, us->own_marker_shape, us->own_marker_color, who,
                          sizeof who)) {
        us->own_marker_color[0] = prev_col[0];
        us->own_marker_color[1] = prev_col[1];
        us->own_marker_color[2] = prev_col[2];
        ntl_flag_taken(who);
      }
    }
    igEndPopup();
  }
  igSpacing();

  crystal_section("Dot shape");
  static const int ntl_shape_values[5] = {0, 1, 3, 4, 5};
  float shape_btn = igGetFrameHeight() * 1.9f;
  for (int i = 0; i < 5; i++) {
    igPushID_Int(10 + i);
    bool selected = us->own_marker_shape == ntl_shape_values[i];
    if (selected)
      igPushStyleColor_Vec4(ImGuiCol_Button,
                            (ImVec4){0.20f, 0.62f, 0.30f, 1.0f});
    igButton("##shape_btn", (ImVec2){shape_btn, shape_btn});
    if (igIsItemClicked(ImGuiMouseButton_Left))
      ntl_try_set_marker(env, us, ntl_shape_values[i], us->own_marker_color);
    ImVec2 rmin, rmax;
    igGetItemRectMin(&rmin);
    igGetItemRectMax(&rmax);
    ImVec2 center = {(rmin.x + rmax.x) * 0.5f, (rmin.y + rmax.y) * 0.5f};
    ImU32 preview_col = igColorConvertFloat4ToU32((ImVec4){
        us->own_marker_color[0], us->own_marker_color[1],
        us->own_marker_color[2], 1.0f});
    ntl_draw_marker(igGetWindowDrawList(), center, shape_btn * 0.26f,
                    ntl_shape_values[i], preview_col);
    if (selected) igPopStyleColor(1);
    igPopID();
    if (i != 4) igSameLine(0, 6);
  }
  igSpacing();

  crystal_section("Emoji");
  igTextWrapped("Pick an emoji for your dot. Your colour shows as the disc behind it.");
  {
    float eb = shape_btn;
    igBeginChild_Str("##ntl_emoji_grid", (ImVec2){-1.0f, eb * 3.3f},
                     ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
    ImVec2 eav;
    igGetContentRegionAvail(&eav);
    int per_row = (int)(eav.x / (eb + 6.0f));
    if (per_row < 1) per_row = 1;
    for (int i = 0; i < NTL_EMOJI_COUNT; ++i) {
      int sv = NTL_SHAPE_EMOJI_BASE + i;
      bool selected = us->own_marker_shape == sv;
      igPushID_Int(1000 + i);
      if (selected)
        igPushStyleColor_Vec4(ImGuiCol_Button,
                              (ImVec4){0.20f, 0.62f, 0.30f, 1.0f});
      if (igButton("##emoji_btn", (ImVec2){eb, eb})) {
        S.emoji_input[0] = 0;
        S.emoji_status = 0;
        ntl_try_set_marker(env, us, sv, us->own_marker_color);
      }
      ImVec2 rmin, rmax;
      igGetItemRectMin(&rmin);
      igGetItemRectMax(&rmax);
      float inset = eb * 0.12f;
      ntl_draw_emoji_image(igGetWindowDrawList(),
                           (ImVec2){rmin.x + inset, rmin.y + inset},
                           (ImVec2){rmax.x - inset, rmax.y - inset}, i,
                           IM_COL32(255, 255, 255, 255));
      if (selected) igPopStyleColor(1);
      igPopID();
      if ((i % per_row) != per_row - 1 && i != NTL_EMOJI_COUNT - 1)
        igSameLine(0, 6);
    }
    igEndChild();
  }
  igSpacing();

  /* Custom emoji box: paste or type an emoji from your keyboard. Only the
   * emoji in the grid above are available (they are bundled as pictures). */
  igText("Custom emoji");
  igSameLine(0, 8);
  igSetNextItemWidth(-1);
  if (igInputText("##ntl_emoji_input", S.emoji_input, sizeof S.emoji_input,
                  ImGuiInputTextFlags_None, NULL, NULL)) {
    int idx = ntl_emoji_lookup(S.emoji_input);
    if (idx >= 0) {
      S.emoji_status = ntl_try_set_marker(env, us, NTL_SHAPE_EMOJI_BASE + idx,
                                          us->own_marker_color)
                           ? 1
                           : 0;
    } else {
      S.emoji_status = S.emoji_input[0] ? 2 : 0;
    }
  }
  if (S.emoji_status == 1)
    igTextColored((ImVec4){0.35f, 0.95f, 0.45f, 1.0f}, "Emoji applied");
  else if (S.emoji_status == 2)
    igTextColored((ImVec4){1.0f, 0.60f, 0.25f, 1.0f},
                  "That emoji isn't in the list above");

  } else if (ntl_tab == 1) {
  crystal_section("Chat position");

  float chat_btn_h = igGetFrameHeight() * 1.8f;

  if (pos_active)
    igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.20f, 0.62f, 0.30f, 1.0f});
  if (igButton(pos_active ? "CONFIRM POSITION" : "ADJUST POSITION",
              (ImVec2){-1.0f, chat_btn_h})) {
    global_chat_set_adjust_mode(env, pos_active ?
                                GLOBAL_CHAT_ADJUST_NONE :
                                GLOBAL_CHAT_ADJUST_POSITION);
  }
  if (pos_active) igPopStyleColor(1);

  igSpacing();

  if (size_active)
    igPushStyleColor_Vec4(ImGuiCol_Button, (ImVec4){0.20f, 0.62f, 0.30f, 1.0f});
  if (igButton(size_active ? "CONFIRM SIZE" : "ADJUST SIZE",
              (ImVec2){-1.0f, chat_btn_h})) {
    global_chat_set_adjust_mode(env, size_active ?
                                GLOBAL_CHAT_ADJUST_NONE :
                                GLOBAL_CHAT_ADJUST_SIZE);
  }
  if (size_active) igPopStyleColor(1);

  if (chat_adjust_mode != GLOBAL_CHAT_ADJUST_NONE) {
    igSpacing();
    igTextWrapped(
        "%s",
        pos_active ?
            "Drag the chat window by its title bar, then tap "
            "Confirm position." :
            "Drag the chat window's edge or corner, then tap "
            "Confirm size.");
  }

  } else {
  crystal_section("Online players");
  igBeginChild_Str("##ntl_members", (ImVec2){0, 320.0f * ui_s},
                   ImGuiChildFlags_Borders, ImGuiWindowFlags_None);
  jsr_network *jnet = global_chat_get_network();
  int jsr_count = jnet ? jsr_network_roster_count(jnet) : 0;
  if (jsr_count == 0) {
    igTextDisabled("No players online.");
  } else {
    for (int i = 0; i < jsr_count; ++i) {
      char pname[32];
      char powner[32];
      if (!jsr_network_roster_name(jnet, i, pname, sizeof pname)) continue;
      powner[0] = 0;
      jsr_network_roster_owner(jnet, i, powner, sizeof powner);
      if (powner[0]) {
        igTextColored((ImVec4){0.95f, 0.3f, 0.3f, 1.0f}, "%s", powner);
        igSameLine(0, 6);
      }
      igText("%s", pname);
      igSpacing();
    }
  }
  igEndChild(); /* ##ntl_members */
  }

  crystal_panel_end_body();

  /* Pinned footer. Leaving the panel (Save/Back) always locks the chat window
   * back down first. */
  {
    float fb_h = igGetFrameHeight() * 1.6f;
    ImVec2 fav;
    igGetContentRegionAvail(&fav);
    float btn_w = (fav.x - style->ItemSpacing.x) * 0.5f;
    {
      ImVec2 save_pos;
      igGetCursorScreenPos(&save_pos);
      crystal_glow_rect(save_pos, (ImVec2){btn_w, fb_h}, 0.647f, 0.420f, 1.0f);
    }
    igPushStyleColor_Vec4(ImGuiCol_Button,
                          (ImVec4){0.510f, 0.294f, 0.910f, 1.0f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonHovered,
                          (ImVec4){0.569f, 0.353f, 0.960f, 1.0f});
    igPushStyleColor_Vec4(ImGuiCol_ButtonActive,
                          (ImVec4){0.450f, 0.243f, 0.850f, 1.0f});
    if (igButton("Save", (ImVec2){btn_w, fb_h})) {
      ntl_sync_active_credentials(us);
      save_user_settings(us);
      S.next_poll = 0;
    }
    crystal_sheen();
    igPopStyleColor(3);
    igSameLine(0, style->ItemSpacing.x);
    if (igButton("Back", (ImVec2){btn_w, fb_h})) {
      save_user_settings(us);
      popup_was_open = false;
      g->curr_screen = TITLE_SCREEN;
      igCloseCurrentPopup();
    }
    crystal_sheen();
  }

  crystal_panel_end();
  crystal_pop_theme();

  igPopFont();
}
