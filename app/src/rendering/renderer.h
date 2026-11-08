#ifndef RENDERER_H
#define RENDERER_H

#include <stdalign.h>

#include "bg_renderer.h"
#include "fd_renderer.h"
#include "bp_renderer.h"
#include "bst_renderer.h"
#include "bd_renderer.h"
#include "mm_renderer.h"
#include "spr_renderer.h"
#include "texture.h"
#include "../game/backgrounds.h"

typedef struct renderer_image_data {
  VkFramebuffer framebuffer;
  VkImage color;
  VkImageView color_view;
  VkImageView color_view_alpha;
  VmaAllocation memory;
} renderer_image_data;

typedef struct renderer {
  struct {
    alignas(8) vec2 viewport;
    alignas(8) vec2 bg_size;
    alignas(16) vec3 bg_color;
    alignas(16) vec3 bd_color;
    alignas(16) vec3 minimap_circ;
    alignas(8) vec2 view;
    alignas(8) vec2 lview;
    alignas(4) float zoom;
    alignas(4) float bg_opacity;
    alignas(4) float bg_scale;
    alignas(4) float bd_opacity;
    alignas(4) float bd_radius;
    alignas(4) float grd;
    alignas(4) float max_minimap_size;
    alignas(4) float minimap_data_size;
    alignas(4) float minimap_opacity;
  } global;

  texture* tex_atlas;
  texture* bg_tex;
  /* In-game background (arena floor), see game/backgrounds.h. bg_tex is the
     texture bound in descriptor binding 2 right now. bg_tex_default is the
     Vlither floor and stays resident. bg_tex_extra is the ONE other floor that
     is loaded at a time (47 textures would be ~200 MB resident); picking a new
     floor loads it, rebinds, and frees the previous one. */
  texture* bg_tex_default;
  texture* bg_tex_extra;
  int bg_extra_style;           /* which BACKGROUNDS entry bg_tex_extra holds */
  bool bg_failed[NUM_BACKGROUNDS]; /* texture file missing/undecodable */
  int bg_style_applied;   /* BACKGROUNDS index currently showing */
  int bg_style_requested; /* what the settings want; applied at the top of
                             renderer_render, before any descriptor is bound */
  /* Preview sheet for the Background picker (one cell per BACKGROUNDS entry).
     Optional: if it fails to load the picker shows plain colour tiles. */
  texture* bg_thumbs_tex;
  VkDescriptorSet bg_thumbs_ds;

  texture* boost_button_tex;
  VkDescriptorSet boost_button_ds;
  /* All nine fixed-colour arrows share one atlas and one ImGui descriptor,
     ported from Vlither's arrow design picker. Optional: a missing/failed
     load leaves these NULL/VK_NULL_HANDLE and the Controls picker falls
     back to plain text buttons instead of thumbnails. */
  texture* arrow_atlas_tex;
  VkDescriptorSet arrow_atlas_ds;

  /* Colour emoji sprite sheet for the team minimap dots (see
     game/ntl_emoji.h). Optional: if it fails to load, emoji dots fall back
     to a plain circle. */
  texture* emoji_atlas_tex;
  VkDescriptorSet emoji_atlas_ds;
  /* NTL tag atlas (game/ntl_tags.h). Optional like the emoji sheet. */
  texture* ntl_tags_tex;
  VkDescriptorSet ntl_tags_ds;

  VkSampler linear_sampler;
  VkSampler nearest_sampler;

  VkRenderPass render_pass;
  VkDescriptorSetLayout global_set_layout;
  VkDescriptorSet* global_set;
  tdbuffer* global_buffer;
  VkPipelineLayout pipeline_layout;
  renderer_image_data* images;
  ivec2 size;
  tbuffer* quad_buffer;

  bg_renderer* bgr;
  fd_renderer* fdr;
  bst_renderer* bsta;
  bp_renderer* bpr;
  bst_renderer* bstb;
  bd_renderer* bdr;
  bp_renderer* astr;
  mm_renderer* mmr;
  spr_renderer* cr;
} renderer;

renderer* renderer_create(tenv* env);
void renderer_render(renderer* r, tcontext* ctx, vec4 clear_color);
void renderer_render_cursor(renderer* r, tcontext* ctx);
void renderer_destroy(renderer* r, tcontext* ctx);
void renderer_resize(renderer* r, tcontext* ctx, ivec2 size);
void renderer_clear_instances(renderer* r);
/* Switches the arena floor to BACKGROUNDS[style]. A floor whose file cannot be
   loaded falls back to Vlither. Rewrites descriptor binding 2, so it must only
   run where this frame's command buffer has not bound the global set yet --
   renderer_render() calls it itself from bg_style_requested, so other code just
   sets bg_style_requested. Returns the style that is actually showing. */
int renderer_set_background(renderer* r, tcontext* ctx, int style);
/* False once a floor's texture failed to load (the settings then fall back). */
bool renderer_background_available(renderer* r, int style);

#endif
