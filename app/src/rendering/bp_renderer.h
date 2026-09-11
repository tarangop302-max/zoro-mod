#ifndef BP_RENDERER_H
#define BP_RENDERER_H

#include <thermite.h>

typedef struct bp_instance {
  vec4s circle;
  vec4s uv_rect;
  vec4s color;
  // Optional shape parameters. x = capsule thickness in pixels,
  // y = 1 for a rounded line capsule; zero keeps the normal circle sprite.
  vec2s shape;
} bp_instance;

/* One clear-stencil-then-draw group within a single accumulated
 * instance buffer. Only meaningful for the dedup pipeline variant
 * (see bp_renderer_create_dedup) -- it's what keeps one snake's body
 * from affecting the next one's stencil test. */
typedef struct bp_batch {
  int start;
  int count;
} bp_batch;

typedef struct bp_renderer {
  VkPipeline pipeline;
  tdbuffer* instance_buffer;
  bp_instance* instances;
  int max_instances;
  int num_instances;

  /* Only allocated/used by the dedup pipeline variant; NULL/0 for a
   * normal bp_renderer_create() instance. */
  bp_batch* batches;
  int max_batches;
  int num_batches;
  bool in_batch;
} bp_renderer;

bp_renderer* bp_renderer_create(tcontext* ctx, int max_instances,
                                VkPipelineLayout layout, VkRenderPass pass);

/* Same shader and instance format as bp_renderer_create, but the
 * pipeline enables a stencil test (compare NOT_EQUAL against
 * reference 1, pass op REPLACE) so that within a single batch (see
 * bp_renderer_begin_batch/end_batch below), any pixel a previous
 * instance in that same batch already touched gets skipped instead
 * of blending again on top. That's what stops semi-transparent,
 * overlapping body segments from stacking extra alpha at their
 * seams -- each pixel of the body only ever actually contributes
 * once, no matter how many overlapping shapes cover it.
 *
 * pass must have a depth/stencil attachment at index 1 (renderer.c's
 * r->render_pass has one specifically for this). max_batches bounds
 * how many separate clear+draw groups (bp_renderer_begin_batch calls)
 * bp_renderer_render_batched can replay in one frame -- one per body
 * that needs this treatment, so it only needs to be as large as the
 * number of transparent/force-white snakes visible at once. */
bp_renderer* bp_renderer_create_dedup(tcontext* ctx, int max_instances,
                                      int max_batches,
                                      VkPipelineLayout layout,
                                      VkRenderPass pass);

void bp_renderer_push(bp_renderer* r, const bp_instance* instance);

/* Dedup pipeline only. Call before pushing the instances for one
 * body, then bp_renderer_end_batch() once done with it. Instances
 * pushed outside a begin/end pair are dropped (not drawn) by
 * bp_renderer_render_batched -- this is only meant for accumulating
 * whole bodies, one batch each. */
void bp_renderer_begin_batch(bp_renderer* r);
void bp_renderer_end_batch(bp_renderer* r);

/* Normal (non-dedup) renderers: draws every pushed instance in one
 * single instanced draw call, as before. */
void bp_renderer_render(bp_renderer* r, tcontext* ctx);

/* Dedup pipeline only: replays each batch recorded since the last
 * renderer_clear_instances() as its own stencil-clear-then-draw
 * pair, in the order the batches were begun, so batches can never
 * bleed into each other. size is the active render target's pixel
 * size, used to clear the whole thing each time (simpler and safer
 * than tracking each body's exact on-screen bounds, and cheap at the
 * small number of batches this is meant for). */
void bp_renderer_render_batched(bp_renderer* r, tcontext* ctx, ivec2 size);

void bp_renderer_destroy(bp_renderer* r, tcontext* ctx);

#endif
