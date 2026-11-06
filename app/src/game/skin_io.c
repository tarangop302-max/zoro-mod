#include "skin_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../user.h"
#include "game_data.h"
#include "user_settings.h"

#ifdef ANDROID
#include "../android_jni.h"
#include "../android_path.h"
#endif

#define SKIN_LIB_FILE "slither_skins.dat"
#define SKIN_LIB_MAGIC 0x31534C53u /* "SLS1" */
#define SKIN_DESKTOP_IN "slither-skins.ntlmod"
#define SKIN_DESKTOP_OUT "slither-skins-export.ntlmod"
#define SKIN_IMPORT_TMP "skin_import.tmp"
#define SKIN_EXPORT_TMP "skin_export.tmp"
#define SKIN_FILE_MAX (64 * 1024 * 1024)

static slither_skin *s_lib = NULL;
static int s_count = 0, s_cap = 0;
static bool s_loaded = false;
static char s_status[160] = "";

static void set_status(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#include <stdarg.h>
static void set_status(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s_status, sizeof s_status, fmt, ap);
  va_end(ap);
}
const char *skin_io_status(void) { return s_status; }

/* ---- paths ---- */
static FILE *open_in_files_dir(const char *name, const char *mode) {
#ifdef ANDROID
  char path[512];
  android_build_path(path, sizeof path, name);
  return fopen(path, mode);
#else
  return fopen(name, mode);
#endif
}
#ifdef ANDROID
static void remove_in_files_dir(const char *name) {
#ifdef ANDROID
  char path[512];
  android_build_path(path, sizeof path, name);
  remove(path);
#else
  remove(name);
#endif
}
#endif

/* ---- library storage ---- */
static void lib_save(void) {
  FILE *f = open_in_files_dir(SKIN_LIB_FILE, "wb");
  if (!f) return;
  uint32_t magic = SKIN_LIB_MAGIC;
  int32_t n = s_count;
  fwrite(&magic, sizeof magic, 1, f);
  fwrite(&n, sizeof n, 1, f);
  if (n > 0) fwrite(s_lib, sizeof(slither_skin), (size_t)n, f);
  fclose(f);
}

static void lib_load(void) {
  if (s_loaded) return;
  s_loaded = true;
  FILE *f = open_in_files_dir(SKIN_LIB_FILE, "rb");
  if (!f) return;
  uint32_t magic = 0;
  int32_t n = 0;
  if (fread(&magic, sizeof magic, 1, f) == 1 && magic == SKIN_LIB_MAGIC &&
      fread(&n, sizeof n, 1, f) == 1 && n > 0 && n <= SKIN_LIB_MAX) {
    s_lib = malloc(sizeof(slither_skin) * (size_t)n);
    if (s_lib) {
      s_cap = n;
      s_count = (int)fread(s_lib, sizeof(slither_skin), (size_t)n, f);
      for (int i = 0; i < s_count; i++) s_lib[i].code[MAX_SKIN_CODE_LEN] = 0;
    }
  }
  fclose(f);
}

int skin_lib_count(void) {
  lib_load();
  return s_count;
}
const slither_skin *skin_lib_get(int i) {
  lib_load();
  return (i >= 0 && i < s_count) ? &s_lib[i] : NULL;
}
void skin_lib_clear(void) {
  lib_load();
  s_count = 0;
  lib_save();
}

static bool lib_has(const char *code) {
  for (int i = 0; i < s_count; i++)
    if (strcmp(s_lib[i].code, code) == 0) return true;
  return false;
}
static bool lib_add(const char *code, uint8_t acc) {
  if (s_count >= SKIN_LIB_MAX || !code[0] || lib_has(code)) return false;
  if (s_count >= s_cap) {
    int nc = s_cap ? s_cap * 2 : 128;
    if (nc > SKIN_LIB_MAX) nc = SKIN_LIB_MAX;
    slither_skin *p = realloc(s_lib, sizeof(slither_skin) * (size_t)nc);
    if (!p) return false;
    s_lib = p;
    s_cap = nc;
  }
  memset(&s_lib[s_count], 0, sizeof(slither_skin));
  strncpy(s_lib[s_count].code, code, MAX_SKIN_CODE_LEN);
  s_lib[s_count].accessory = acc;
  s_count++;
  return true;
}

/* ---- codecs ---- */
static int b64v(int c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+' || c == '-') return 62;
  if (c == '/' || c == '_') return 63;
  return -1;
}
/* Lenient: skips whitespace / other characters, stops at '='. */
static char *b64_decode(const char *in, size_t n, size_t *out_len) {
  char *out = malloc(n / 4 * 3 + 4);
  if (!out) return NULL;
  size_t o = 0;
  uint32_t acc = 0;
  int bits = 0;
  for (size_t i = 0; i < n; i++) {
    if (in[i] == '=') break;
    int v = b64v((unsigned char)in[i]);
    if (v < 0) continue;
    acc = (acc << 6) | (uint32_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out[o++] = (char)((acc >> bits) & 0xFF);
    }
  }
  out[o] = 0;
  *out_len = o;
  return out;
}
static int hexv(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
static void url_decode_inplace(char *s) {
  char *w = s;
  for (char *r = s; *r; r++) {
    if (*r == '%' && hexv(r[1]) >= 0 && hexv(r[2]) >= 0) {
      *w++ = (char)(hexv(r[1]) * 16 + hexv(r[2]));
      r += 2;
    } else {
      *w++ = *r;
    }
  }
  *w = 0;
}

/* ---- tiny JSON scan: array of objects, reads "s" (string/number) and "c" ---- */
static const char *skip_ws(const char *p) {
  while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
  return p;
}
static const char *read_string(const char *p, char *out, size_t cap) {
  size_t o = 0;
  p++; /* opening quote */
  while (*p && *p != '"') {
    char ch = *p;
    if (ch == '\\' && p[1]) {
      p++;
      ch = *p;
      if (ch == 'u') { p += 4; ch = '?'; }
    }
    if (out && o + 1 < cap) out[o++] = ch;
    p++;
  }
  if (out) out[o] = 0;
  return *p == '"' ? p + 1 : p;
}
static const char *skip_value(const char *p) {
  p = skip_ws(p);
  if (*p == '"') return read_string(p, NULL, 0);
  if (*p == '{' || *p == '[') {
    int depth = 0;
    while (*p) {
      if (*p == '"') { p = read_string(p, NULL, 0); continue; }
      if (*p == '{' || *p == '[') depth++;
      else if (*p == '}' || *p == ']') { if (--depth == 0) return p + 1; }
      p++;
    }
    return p;
  }
  while (*p && *p != ',' && *p != '}' && *p != ']') p++;
  return p;
}

/* Converts the NTL/slither skin byte list into a zoro skin code.
   Returns length of code (0 = unusable). */
static int ntl_s_to_code(const game_data *gd, const char *s, char *code) {
  int v[1200], n = 0;
  const char *p = s;
  while (*p && n < 1200) {
    while (*p == ' ' || *p == ',') p++;
    if (*p < '0' || *p > '9') break;
    v[n++] = atoi(p);
    while (*p >= '0' && *p <= '9') p++;
  }
  int len = 0;
  for (int i = 8; i + 1 < n; i += 2) {
    int cnt = v[i], id = v[i + 1];
    if (id < 0 || id >= NUM_COLOR_GROUPS) continue;
    char ch = gd->ntl_cg_map[id];
    if (ch == '*' || ch == 0) continue; /* colours the code can't express */
    for (int k = 0; k < cnt && len < MAX_SKIN_CODE_LEN; k++) code[len++] = ch;
  }
  code[len] = 0;
  return len;
}

static int import_buffer(tenv *env, const char *file, size_t flen) {
  game_data *gd = &env->usr->gdata;
  /* Skip an optional "data:...;base64," prefix. */
  const char *b = file;
  const char *comma = memchr(file, ',', flen < 80 ? flen : 80);
  if (comma && strncmp(file, "data:", 5) == 0) { b = comma + 1; flen -= (size_t)(b - file); }

  size_t dl = 0;
  char *dec = b64_decode(b, flen, &dl);
  if (!dec) return -1;
  /* Plain JSON files are accepted too. */
  char *json = dec;
  if (!strchr(dec, '[') || strchr(dec, '%')) url_decode_inplace(dec);
  const char *p = strchr(json, '[');
  if (!p) { free(dec); return -1; }
  p++;
  int added = 0, seen = 0;
  while (*p) {
    p = skip_ws(p);
    if (*p == ',') { p++; continue; }
    if (*p != '{') break;
    p++;
    char sbuf[4096] = {0};
    int cval = -1;
    while (*p) {
      p = skip_ws(p);
      if (*p == ',') { p++; continue; }
      if (*p == '}') { p++; break; }
      if (*p != '"') { p++; continue; }
      char key[16];
      p = read_string(p, key, sizeof key);
      p = skip_ws(p);
      if (*p == ':') p++;
      p = skip_ws(p);
      if (strcmp(key, "s") == 0) {
        if (*p == '"') p = read_string(p, sbuf, sizeof sbuf);
        else p = skip_value(p);
      } else if (strcmp(key, "c") == 0) {
        cval = atoi(p);
        p = skip_value(p);
      } else {
        p = skip_value(p);
      }
    }
    if (sbuf[0]) {
      seen++;
      char code[MAX_SKIN_CODE_LEN + 1];
      if (ntl_s_to_code(gd, sbuf, code) > 0) {
        uint8_t acc = (cval >= 0 && cval < NUM_ACCESSORIES) ? (uint8_t)cval : NO_ACCESSORY;
        if (lib_add(code, acc)) added++;
      }
    }
  }
  free(dec);
  if (seen == 0) return -1;
  return added;
}

static void report_import(int added) {
  if (added < 0) set_status("Import failed: not a skins .ntlmod file");
  else {
    set_status("Imported %d new skin%s (%d total)", added, added == 1 ? "" : "s", s_count);
    lib_save();
  }
}

/* ---- export ---- */
static void append(char **buf, size_t *len, size_t *cap, const char *s) {
  size_t n = strlen(s);
  if (*len + n + 1 > *cap) {
    while (*len + n + 1 > *cap) *cap *= 2;
    *buf = realloc(*buf, *cap);
  }
  memcpy(*buf + *len, s, n + 1);
  *len += n;
}

static int code_to_s(game_data *gd, const char *code, char *out, size_t cap) {
  int o = snprintf(out, cap, "255,255,255,0,0,0,0,0");
  int n = (int)strlen(code);
  for (int i = 0; i < n;) {
    int j = i;
    while (j < n && code[j] == code[i] && j - i < 255) j++;
    int id = get_cg_id(gd, code[i]);
    if (id >= 0 && o < (int)cap - 12) o += snprintf(out + o, cap - (size_t)o, ",%d,%d", j - i, id);
    i = j;
  }
  return o;
}

/* Builds the full file text (base64 of url-encoded JSON). Caller frees. */
static char *build_export(tenv *env, int *count_out) {
  game_data *gd = &env->usr->gdata;
  user_settings *us = &env->usr->usrs;
  size_t cap = 4096, len = 0;
  char *json = malloc(cap);
  json[0] = 0;
  append(&json, &len, &cap, "[");
  int count = 0;
  char s[2400], item[2600];
  /* seen list to dedupe saved vs library */
  for (int pass = 0; pass < 2; pass++) {
    int n = pass == 0 ? us->saved_skin_count : s_count;
    for (int i = 0; i < n; i++) {
      const char *code = pass == 0 ? us->saved_skins[i].skin_code : s_lib[i].code;
      uint8_t acc = pass == 0 ? us->saved_skins[i].accessory : s_lib[i].accessory;
      if (!code[0]) continue;
      if (pass == 1) {
        bool dup = false;
        for (int k = 0; k < us->saved_skin_count && !dup; k++)
          dup = strcmp(us->saved_skins[k].skin_code, code) == 0;
        if (dup) continue;
      }
      code_to_s(gd, code, s, sizeof s);
      snprintf(item, sizeof item, "%s{\"s\":\"%s\",\"t\":-1,\"c\":%d}", count ? "," : "", s,
               acc < NUM_ACCESSORIES ? (int)acc : -1);
      append(&json, &len, &cap, item);
      count++;
    }
  }
  append(&json, &len, &cap, "]");

  /* encodeURIComponent */
  char *enc = malloc(len * 3 + 1);
  size_t e = 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)json[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        strchr("-_.!~*'()", c))
      enc[e++] = (char)c;
    else
      e += (size_t)sprintf(enc + e, "%%%02X", c);
  }
  enc[e] = 0;
  free(json);

  /* base64 */
  static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char *b64 = malloc(e / 3 * 4 + 8);
  size_t o = 0;
  for (size_t i = 0; i < e; i += 3) {
    uint32_t v = (uint32_t)(unsigned char)enc[i] << 16;
    if (i + 1 < e) v |= (uint32_t)(unsigned char)enc[i + 1] << 8;
    if (i + 2 < e) v |= (uint32_t)(unsigned char)enc[i + 2];
    b64[o++] = T[(v >> 18) & 63];
    b64[o++] = T[(v >> 12) & 63];
    b64[o++] = i + 1 < e ? T[(v >> 6) & 63] : '=';
    b64[o++] = i + 2 < e ? T[v & 63] : '=';
  }
  b64[o] = 0;
  free(enc);
  *count_out = count;
  return b64;
}

/* ---- requests ---- */
static char *read_whole(const char *name, size_t *n) {
  FILE *f = open_in_files_dir(name, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0 || sz > SKIN_FILE_MAX) { fclose(f); return NULL; }
  char *buf = malloc((size_t)sz + 1);
  size_t r = buf ? fread(buf, 1, (size_t)sz, f) : 0;
  fclose(f);
  if (!buf) return NULL;
  buf[r] = 0;
  *n = r;
  return buf;
}

void skin_io_request_import(tenv *env) {
  lib_load();
#ifdef ANDROID
  (void)env;
  set_status("Choose a skins .ntlmod file...");
  android_jni_skinio_import();
#else
  size_t n = 0;
  char *buf = read_whole(SKIN_DESKTOP_IN, &n);
  if (!buf) { set_status("Put %s next to the game, then tap Import", SKIN_DESKTOP_IN); return; }
  report_import(import_buffer(env, buf, n));
  free(buf);
#endif
}

void skin_io_request_export(tenv *env) {
  lib_load();
  int count = 0;
  char *text = build_export(env, &count);
  if (count == 0) { free(text); set_status("No skins to export yet"); return; }
#ifdef ANDROID
  FILE *f = open_in_files_dir(SKIN_EXPORT_TMP, "wb");
  if (!f) { free(text); set_status("Export failed"); return; }
  fwrite(text, 1, strlen(text), f);
  fclose(f);
  free(text);
  set_status("Choose where to save %d skins...", count);
  android_jni_skinio_export();
#else
  FILE *f = fopen(SKIN_DESKTOP_OUT, "wb");
  if (f) {
    fwrite(text, 1, strlen(text), f);
    fclose(f);
    set_status("Exported %d skins to %s", count, SKIN_DESKTOP_OUT);
  } else set_status("Export failed");
  free(text);
#endif
}

void skin_io_update(tenv *env) {
#ifdef ANDROID
  int guard = 0, ev;
  while ((ev = android_jni_poll_skinio_event()) != 0 && guard++ < 8) {
    switch (ev) {
      case 1: { /* picked file copied to skin_import.tmp */
        lib_load();
        size_t n = 0;
        char *buf = read_whole(SKIN_IMPORT_TMP, &n);
        if (!buf) { set_status("Import failed: could not read file"); break; }
        report_import(import_buffer(env, buf, n));
        free(buf);
        remove_in_files_dir(SKIN_IMPORT_TMP);
        break;
      }
      case 2: set_status("Skins exported. Keep that file safe."); remove_in_files_dir(SKIN_EXPORT_TMP); break;
      case 3: set_status("Cancelled."); break;
      default: set_status("Skin file action failed."); break;
    }
  }
#else
  (void)env;
#endif
}
