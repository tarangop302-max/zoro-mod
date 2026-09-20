#ifndef PROFILE_EMOJI_H
#define PROFILE_EMOJI_H

/* Shared profile-emoji table. Every entry here is a glyph confirmed
   present in app/res/fonts/regular_regular.ttf (Liberation Sans) --
   checked against the font's own character map, not guessed. True
   color emoji (U+1F300+, e.g. 😀🔥🎉) do NOT exist in that font at all
   and can't be added without bundling a separate emoji font, so this
   is the full available set: the pictograph-style symbols Liberation
   Sans actually ships (smileys, sun, zodiac/planet glyphs, card suits,
   music notes, a house). Nothing outside this set will render --
   everything else in that Unicode territory (stars, checkmarks,
   snowflakes, hearts-as-emoji, sparkles, etc.) is absent from the font
   and falls back to a missing-glyph box.

   Index zero intentionally means "no badge" -- callers should treat
   emoji_id 0 as "don't draw anything" rather than looking it up here.

   PROFILE_EMOJI_GLYPH_RANGES must be passed to ImFontAtlas_AddFontFromFileTTF
   (or the Android LOAD_FONT_RET equivalent) for every text font these
   might be drawn with -- see imgui_setup.c / imgui_setup_android.c.
   Without it, ImGui's default glyph range (Basic Latin only) never
   bakes these into the atlas and they render as missing-glyph boxes
   even though the font file itself contains them. */
static const char *PROFILE_EMOJIS[] = {
    "",   /* 0: none */
    "☺",  /* 1: white smiling face */
    "☻",  /* 2: black smiling face */
    "☼",  /* 3: sun with rays */
    "⌂",  /* 4: house */
    "☿",  /* 5: mercury */
    "♀",  /* 6: female sign */
    "♁",  /* 7: earth */
    "♂",  /* 8: male sign */
    "♃",  /* 9: jupiter */
    "♄",  /* 10: saturn */
    "♅",  /* 11: uranus */
    "♆",  /* 12: neptune */
    "♇",  /* 13: pluto */
    "♠",  /* 14: black spade suit */
    "♣",  /* 15: black club suit */
    "♥",  /* 16: black heart suit */
    "♦",  /* 17: black diamond suit */
    "♩",  /* 18: quarter note */
    "♪",  /* 19: eighth note */
    "♫",  /* 20: beamed eighth notes */
    "♬",  /* 21: beamed sixteenth notes */
    "♯",  /* 22: music sharp sign */
};

#define PROFILE_EMOJI_COUNT \
  ((int)(sizeof PROFILE_EMOJIS / sizeof PROFILE_EMOJIS[0]))

/* Clamped lookup -- always returns a valid pointer, "" for an
   out-of-range or zero id. */
static inline const char *profile_emoji_at(int id) {
  if (id <= 0 || id >= PROFILE_EMOJI_COUNT) return "";
  return PROFILE_EMOJIS[id];
}

/* Glyph ranges covering every codepoint PROFILE_EMOJIS uses (plus a
   couple of adjacent codepoints the font doesn't have -- harmless,
   ImGui just skips glyphs the font can't supply). Pass this as the
   glyph_ranges argument wherever a text font is loaded that might
   display a profile emoji, so it's actually baked into the atlas
   instead of silently falling back to a missing-glyph box. */
static const unsigned short PROFILE_EMOJI_GLYPH_RANGES[] = {
    0x2302, 0x2302, /* house */
    0x263A, 0x2647, /* smileys, sun, zodiac/planet glyphs */
    0x2660, 0x2666, /* card suits */
    0x2669, 0x266F, /* music notes */
    0,
};

#endif

