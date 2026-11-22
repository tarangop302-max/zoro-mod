#ifndef SKIN_GEN_H
#define SKIN_GEN_H

#include <stdbool.h>
#include <stdint.h>

/* Random skin generator for the Skin Editor.

   Idea taken from Wyrm's "Shed" generator, then extended:
     - every skin only uses the 38 official skin characters,
     - fits the arena (<= 256 beads, <= 146 runs),
     - never puts two beads that LOOK the same next to each other
       (judged by real colour distance, not by character),
     - colour harmony rules (analogous / complement / triad / mono / contrast /
       candy / rainbow), 12 colour themes and 13 pattern styles,
     - "Remix" mode: build new patterns from the colours of the current skin,
     - optional random accessory,
     - a score, so the best-looking candidates come first,
     - a history of canonical codes so the same skin is never shown twice.
   The same recipe + seed always gives the same skins. */

#define SKIN_GEN_MAX_CODE 256
#define SKIN_GEN_MAX_RUNS 146
#define SKIN_GEN_MAX_BATCH 16
#define SKIN_GEN_KEEP_ACC 254 /* result.accessory: leave the accessory alone */

enum {
  SKIN_MOOD_CUTE = 0,
  SKIN_MOOD_DANGER,
  SKIN_MOOD_RANDOM,
  SKIN_MOOD_PATTERN,
  SKIN_MOOD_UNIQUE,
  SKIN_MOOD_COUNT
};

enum {
  SKIN_STYLE_STRIPES = 0,
  SKIN_STYLE_THICKTHIN,
  SKIN_STYLE_RAMP,
  SKIN_STYLE_RAINBOW,
  SKIN_STYLE_PULSE,
  SKIN_STYLE_BAND,
  SKIN_STYLE_DUO,
  SKIN_STYLE_FIB,
  SKIN_STYLE_WEAVE,
  SKIN_STYLE_CHAOS,
  SKIN_STYLE_STARRY,
  SKIN_STYLE_COMET,
  SKIN_STYLE_CHECKER,
  SKIN_STYLE_COUNT
};

enum { SKIN_TONE_AUTO = 0, SKIN_TONE_LIGHT, SKIN_TONE_DARK, SKIN_TONE_MIX, SKIN_TONE_COUNT };
enum { SKIN_SIZE_SHORT = 0, SKIN_SIZE_MEDIUM, SKIN_SIZE_LONG, SKIN_SIZE_COUNT };
enum { SKIN_SYM_AUTO = 0, SKIN_SYM_ON, SKIN_SYM_OFF, SKIN_SYM_COUNT };

#define SKIN_THEME_COUNT 12

typedef struct skin_gen_recipe {
  int mood;       /* SKIN_MOOD_* */
  int tone;       /* SKIN_TONE_* */
  int theme;      /* -1 = none, else 0..SKIN_THEME_COUNT-1 */
  int style;      /* -1 = auto, else SKIN_STYLE_* */
  int size;       /* SKIN_SIZE_* (width of one band) */
  int symmetry;   /* SKIN_SYM_* */
  float wildness; /* 0..1, how many random tweaks are applied */
  int max_len;    /* longest skin wanted, 8..256 */
  bool random_accessory;
  char own[40]; /* "your colours" (skin characters), empty = not used */
  int own_n;
  uint32_t seed;
} skin_gen_recipe;

typedef struct skin_gen_skin {
  char code[SKIN_GEN_MAX_CODE + 1];
  uint64_t key; /* hash of the canonical loop, used for the history */
  int len;
  int runs;
  int style;
  int mood;
  uint32_t seed;
  float score;
  uint8_t accessory; /* SKIN_GEN_KEEP_ACC, NO_ACCESSORY (255) or 0..n-1 */
} skin_gen_skin;

void skin_gen_default_recipe(skin_gen_recipe* r);

/* Fill r->own with the distinct characters used by a skin code. */
void skin_gen_own_from_code(skin_gen_recipe* r, const char* code);

/* One skin from one seed. Returns false when it fails the checks. */
bool skin_gen_make(const skin_gen_recipe* r, uint32_t seed, int num_accessories,
                   skin_gen_skin* out);

/* Up to `want` (<= SKIN_GEN_MAX_BATCH) skins, best first. `history` holds the
   keys of skins already shown; they are only reused when the recipe has run
   out of new ones. Returns how many skins were written to `out`. */
int skin_gen_batch(const skin_gen_recipe* r, int want, int num_accessories,
                   const uint64_t* history, int n_history,
                   skin_gen_skin* out);

/* A fresh seed (time + counter). */
uint32_t skin_gen_new_seed(void);

/* Names for the UI. */
const char* skin_gen_mood_name(int i);
const char* skin_gen_style_name(int i);
const char* skin_gen_theme_name(int i);

/* UI helper: "Cute\0Dangerous\0...\0\0" style lists for igCombo_Str. */
const char* skin_gen_mood_list(void);
const char* skin_gen_style_list(void);  /* first entry is "Auto" */
const char* skin_gen_theme_list(void);  /* first entry is "None" */
const char* skin_gen_tone_list(void);
const char* skin_gen_size_list(void);
const char* skin_gen_sym_list(void);

#endif
