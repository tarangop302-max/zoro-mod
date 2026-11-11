#include "quick_graphics.h"

#include "ntl_tags.h"

typedef struct {
  bool background, accessories, tags;
  bool shadows, glow, flicker, floaty;
  bool boost, death, outline, crosshair, center_line, uniform_food;
  int food_type;   /* 0 Solid, 1 Rings, 2 Hollow square */
  int render_mode; /* RENDER_MODE_* */
} qg_cfg;

/* Low: everything off, Flat, centre line on, uniform food colour on,
   Rings food. Used for both modes. */
static const qg_cfg k_low = {
    .background = false, .accessories = false, .tags = false,
    .shadows = false, .glow = false, .flicker = false, .floaty = false,
    .boost = false, .death = false, .outline = false, .crosshair = false,
    .center_line = true, .uniform_food = true,
    .food_type = 1, .render_mode = RENDER_MODE_FLAT};

/* Normal (Normal mode): only background, accessories and tags on;
   Vlither textured; Hollow square food. */
static const qg_cfg k_normal = {
    .background = true, .accessories = true, .tags = true,
    .shadows = false, .glow = false, .flicker = false, .floaty = false,
    .boost = false, .death = false, .outline = false, .crosshair = false,
    .center_line = false, .uniform_food = false,
    .food_type = 2, .render_mode = RENDER_MODE_VLITHER_TEXTURED};

/* High (both modes): everything on; Texture; Solid food. */
static const qg_cfg k_high = {
    .background = true, .accessories = true, .tags = true,
    .shadows = true, .glow = true, .flicker = true, .floaty = true,
    .boost = true, .death = true, .outline = true, .crosshair = true,
    .center_line = true, .uniform_food = false,
    .food_type = 0, .render_mode = RENDER_MODE_TEXTURE};

static void apply_cfg(user_settings* usrs, int mode_index, const qg_cfg* c,
                      float food_scale) {
  gameplay_mode* m = usrs->modes + mode_index;
  m->show_background = c->background;
  m->show_accessories = c->accessories;
  ntl_tags_show_set(mode_index, c->tags);
  m->show_shadows = c->shadows;
  usrs->food_glow[mode_index] = c->glow;
  m->food_flicker = c->flicker;
  m->food_float = c->floaty;
  m->show_boost = c->boost;
  if (c->boost) m->boost_type = 0; /* the full "Normal" boost style */
  m->death_effect = c->death;
  m->player_names_outline = c->outline;
  m->show_crosshair = c->crosshair;
  m->center_line = c->center_line;
  m->uniform_food_color = c->uniform_food;
  m->food_type = c->food_type;
  m->render_mode = c->render_mode;
  m->food_scale = food_scale;
  /* Low and Normal want solid-looking snakes; High leaves this choice alone. */
  if (!c->boost && !c->glow) m->transparent_skin = false;
}

void quick_graphics_apply(user_settings* usrs, int level) {
  switch (level) {
    case QG_LOW:
      apply_cfg(usrs, 0, &k_low, 0.40f);
      apply_cfg(usrs, 1, &k_low, 0.30f);
      break;
    case QG_NORMAL:
      apply_cfg(usrs, 0, &k_normal, 0.50f);
      apply_cfg(usrs, 1, &k_low, 0.40f); /* Assist = Low look, food +0.10 */
      break;
    case QG_HIGH:
      apply_cfg(usrs, 0, &k_high, 1.00f);
      apply_cfg(usrs, 1, &k_high, 0.60f);
      /* Performance mode would hide the shadows High turns on. */
      usrs->performance_mode = false;
      break;
    default: break;
  }
}

const char* quick_graphics_name(int level) {
  static const char* n[QG_COUNT] = {"Low graphics", "Normal graphics", "High graphics"};
  return (level >= 0 && level < QG_COUNT) ? n[level] : "";
}
