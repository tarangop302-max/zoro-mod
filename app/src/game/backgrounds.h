#ifndef BACKGROUNDS_H
#define BACKGROUNDS_H

#include <stddef.h>

/**
 * The arena floors the player can pick from (Settings > General > Background).
 *
 * Each entry is one seamlessly tiling image plus the world-space size of a
 * single tile. The size is not the pixel size and is not optional: bg.slang
 * divides a world position by (tile size * Background scale) to get the
 * sampling coordinate, so a wrong number is a pattern that stretches or seams.
 *
 * Order is the picker's grid order, the stored value (user_settings
 * game_bg_style) and the cell index in vlither_thumbs.png, so entries are only
 * ever appended, never reordered or removed.
 *
 * BACKGROUND_NONE draws no floor at all (the clear colour shows). BACKGROUND_BLACK
 * draws the floor pass at full opacity with no colour, i.e. true black. Neither
 * has an image.
 */
typedef struct background_def {
  const char* id;    /* stable name, never changes */
  const char* label; /* what the player sees */
  const char* path;  /* NULL for the ones that draw no image */
  float tile_w;
  float tile_h;
} background_def;

enum {
  BACKGROUND_VLITHER = 0,
  BACKGROUND_NONE = 1,
  BACKGROUND_BLACK = 22,
};

/* The picker's preview sheet: one BACKGROUND_THUMB_CELL square per entry, in
   table order, BACKGROUND_THUMB_COLS to a row. Built by tools/gen_bg_thumbs.py. */
#define BACKGROUND_THUMBS_PATH "app/res/textures/backgrounds/vlither_thumbs.png"
#define BACKGROUND_THUMB_COLS 7
#define BACKGROUND_THUMB_CELL 128

static const background_def BACKGROUNDS[] = {
    {"vlither", "Vlither", "app/res/textures/background_4k.png", 2048.0f, 2048.0f},
    {"none", "None", NULL, 512.0f, 512.0f},
    {"classic", "Classic", "app/res/textures/backgrounds/bgee_classic.png", 599.0f, 519.0f},
    {"bgee2", "Slither", "app/res/textures/backgrounds/bgee2.png", 800.0f, 800.0f},
    {"asanoha", "Asanoha", "app/res/textures/backgrounds/bg_asanoha.png", 600.0f, 520.0f},
    {"seigaiha", "Seigaiha", "app/res/textures/backgrounds/bg_seigaiha.png", 801.0f, 810.0f},
    {"graygrid", "Grey grid", "app/res/textures/backgrounds/bg_graygrid.png", 768.0f, 768.0f},
    {"rizz", "Rizz", "app/res/textures/backgrounds/bg_rizz.png", 800.0f, 794.0f},
    {"usastar", "Stars", "app/res/textures/backgrounds/bg_usastar.png", 599.0f, 519.0f},
    {"circuits", "Circuits", "app/res/textures/backgrounds/bg_circuits.png", 800.0f, 800.0f},
    {"circuits2", "Circuits II", "app/res/textures/backgrounds/bg_circuits2.png", 800.0f, 800.0f},
    {"hexice", "Hex ice", "app/res/textures/backgrounds/bg_hexice.png", 800.0f, 800.0f},
    {"hexb", "Hex", "app/res/textures/backgrounds/bg_hexB.png", 800.0f, 693.0f},
    {"hearts", "Hearts", "app/res/textures/backgrounds/bg_hearts.png", 800.0f, 781.0f},
    {"leaves", "Leaves", "app/res/textures/backgrounds/bg_leaves.png", 800.0f, 800.0f},
    {"paint", "Paint", "app/res/textures/backgrounds/bg_paint.png", 800.0f, 800.0f},
    {"snakey", "Snakey", "app/res/textures/backgrounds/bg_snakey.png", 800.0f, 800.0f},
    {"stainedglass", "Stained glass", "app/res/textures/backgrounds/bg_stainedglass.png", 800.0f, 800.0f},
    {"kitties", "Kitties", "app/res/textures/backgrounds/bg_kitties.png", 800.0f, 786.0f},
    {"bluecube", "Blue cube", "app/res/textures/backgrounds/bg_bluecube.png", 872.0f, 882.0f},
    {"purplecube", "Purple cube", "app/res/textures/backgrounds/bg_purplecube.png", 872.0f, 882.0f},
    {"redcube", "Red cube", "app/res/textures/backgrounds/bg_redcube.png", 872.0f, 882.0f},
    /* Vlither's own floors, round one. The seven tiles' sizes put them at their
       designed world size at the default Background scale (599/4096). */
    {"black", "Black", NULL, 512.0f, 512.0f},
    {"vlither_midnight", "Midnight", "app/res/textures/backgrounds/vlither_midnight.png", 3790.04f, 3282.27f},
    {"vlither_carbon", "Carbon", "app/res/textures/backgrounds/vlither_carbon.png", 2954.04f, 2954.04f},
    {"vlither_abyss", "Abyss", "app/res/textures/backgrounds/vlither_abyss.png", 7002.18f, 7002.18f},
    {"vlither_nebula", "Nebula", "app/res/textures/backgrounds/vlither_nebula.png", 7002.18f, 7002.18f},
    {"vlither_dotgrid", "Dot grid", "app/res/textures/backgrounds/vlither_dotgrid.png", 2735.23f, 2735.23f},
    {"vlither_contours", "Contours", "app/res/textures/backgrounds/vlither_contours.png", 7002.18f, 7002.18f},
    {"vlither_scales", "Scales", "app/res/textures/backgrounds/vlither_scales.png", 3282.27f, 3282.27f},
    /* Round two: nine dark and ten soft mid-tone floors (append only). */
    {"vlither_ember", "Ember", "app/res/textures/backgrounds/vlither_ember.png", 5251.63f, 5251.63f},
    {"vlither_dunes", "Dunes", "app/res/textures/backgrounds/vlither_dunes.png", 7002.18f, 7002.18f},
    {"vlither_moss", "Moss", "app/res/textures/backgrounds/vlither_moss.png", 5251.63f, 5251.63f},
    {"vlither_cells", "Cells", "app/res/textures/backgrounds/vlither_cells.png", 6154.26f, 6154.26f},
    {"vlither_aurora", "Aurora", "app/res/textures/backgrounds/vlither_aurora.png", 7002.18f, 7002.18f},
    {"vlither_facets", "Facets", "app/res/textures/backgrounds/vlither_facets.png", 5470.45f, 5470.45f},
    {"vlither_frost", "Frost", "app/res/textures/backgrounds/vlither_frost.png", 5251.63f, 5251.63f},
    {"vlither_tartan", "Tartan", "app/res/textures/backgrounds/vlither_tartan.png", 3282.27f, 3282.27f},
    {"vlither_reptile", "Reptile", "app/res/textures/backgrounds/vlither_reptile.png", 6154.26f, 6154.26f},
    {"vlither_sage", "Sage", "app/res/textures/backgrounds/vlither_sage.png", 3501.09f, 3501.09f},
    {"vlither_shore", "Shore", "app/res/textures/backgrounds/vlither_shore.png", 7002.18f, 7002.18f},
    {"vlither_terrazzo", "Terrazzo", "app/res/textures/backgrounds/vlither_terrazzo.png", 5251.63f, 5251.63f},
    {"vlither_mist", "Mist", "app/res/textures/backgrounds/vlither_mist.png", 7002.18f, 7002.18f},
    {"vlither_lavender", "Lavender", "app/res/textures/backgrounds/vlither_lavender.png", 3411.04f, 2954.04f},
    {"vlither_clay", "Clay", "app/res/textures/backgrounds/vlither_clay.png", 2625.82f, 2625.82f},
    {"vlither_paper", "Paper", "app/res/textures/backgrounds/vlither_paper.png", 2735.23f, 2735.23f},
    {"vlither_seafoam", "Seafoam", "app/res/textures/backgrounds/vlither_seafoam.png", 3501.09f, 1750.54f},
    {"vlither_marble", "Marble", "app/res/textures/backgrounds/vlither_marble.png", 7002.18f, 7002.18f},
    {"vlither_pebbles", "Pebbles", "app/res/textures/backgrounds/vlither_pebbles.png", 5251.63f, 5251.63f},
};

enum { NUM_BACKGROUNDS = (int)(sizeof(BACKGROUNDS) / sizeof(BACKGROUNDS[0])) };

/** Any stored value that is no longer in the table falls back to Vlither's own. */
static inline int background_clamp(int index) {
  return (index >= 0 && index < NUM_BACKGROUNDS) ? index : BACKGROUND_VLITHER;
}

#endif
