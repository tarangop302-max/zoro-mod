#include "screenshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../user.h"

#ifdef ANDROID
#include "../android_jni.h"
#include "../android_path.h"
#endif

/* Kept small and deliberately conservative -- each capture is a
   full-resolution RGBA8 frame held in RAM (e.g. ~10MB on a 1080x2400
   screen), so this caps a single run's memory use to roughly 80MB in the
   worst case. Oldest capture is evicted (freed) to make room for a new
   one past this point, so a long kill streak keeps the most recent
   highlights rather than silently refusing to capture any more. See
   SCREENSHOT_MAX_RUN_CAPTURES in screenshot.h. */
#define MAX_RUN_CAPTURES SCREENSHOT_MAX_RUN_CAPTURES

typedef struct {
  unsigned char *rgba;
  int w, h;
  int kill_number;
  /* True while the pixels are still in the swapchain's native BGRA order.
     The R/B swap over ~2.5M pixels is deliberately NOT done at capture
     time (that would run mid-match on the render thread); it is done once,
     later, the first time the capture is actually read -- i.e. on the
     post-match review screen or when saving. See ensure_rgba(). */
  bool needs_swap;
} run_capture;

static run_capture s_captures[MAX_RUN_CAPTURES];
static int s_capture_count = 0;

static bool s_pending = false;
static int s_pending_kill_number = 0;

/* Capture switch -- OFF by default. Persisted as a one-character file
   ("1" = on) in the app's files dir; a missing/unreadable file means OFF. */
static bool s_enabled = false;
static bool s_enabled_loaded = false;

#ifdef ANDROID
static void enabled_file_path(char *out, int out_size) {
  android_build_path(out, out_size, "kill_screenshot_enabled.txt");
}
#endif

bool screenshot_capture_enabled(void) {
  if (!s_enabled_loaded) {
    s_enabled_loaded = true;
    s_enabled = false;
#ifdef ANDROID
    char path[600];
    enabled_file_path(path, (int)sizeof(path));
    FILE *f = fopen(path, "r");
    if (f) {
      s_enabled = (fgetc(f) == '1');
      fclose(f);
    }
#endif
  }
  return s_enabled;
}

void screenshot_set_capture_enabled(bool enabled) {
  s_enabled = enabled;
  s_enabled_loaded = true;
  if (!enabled) s_pending = false;
#ifdef ANDROID
  char path[600];
  enabled_file_path(path, (int)sizeof(path));
  FILE *f = fopen(path, "w");
  if (f) {
    fputc(enabled ? '1' : '0', f);
    fclose(f);
  }
#endif
}

void screenshot_request(int kill_number) {
  if (!screenshot_capture_enabled()) return;
  /* Processing happens on the very next frame (see screenshot.h), so
     there's essentially no window for a second kill to arrive before
     this one is handled. If it somehow does, just keep the first
     request rather than overwriting it -- dropping an occasional extra
     capture on a very fast multi-kill is a fine tradeoff for keeping
     this dead simple. */
  if (s_pending) return;
  s_pending = true;
  s_pending_kill_number = kill_number;
}

int screenshot_run_count(void) { return s_capture_count; }

static void ensure_rgba(run_capture *c) {
  if (!c->needs_swap || !c->rgba) return;
  size_t pixel_count = (size_t)c->w * (size_t)c->h;
  for (size_t i = 0; i < pixel_count; i++) {
    unsigned char *p = c->rgba + i * 4;
    unsigned char tmp = p[0];
    p[0] = p[2];
    p[2] = tmp;
  }
  c->needs_swap = false;
}

bool screenshot_run_get(int index, const unsigned char **out_rgba,
                        int *out_w, int *out_h, int *out_kill_number) {
  if (index < 0 || index >= s_capture_count) return false;
  run_capture *c = &s_captures[index];
  ensure_rgba(c);
  if (out_rgba) *out_rgba = c->rgba;
  if (out_w) *out_w = c->w;
  if (out_h) *out_h = c->h;
  if (out_kill_number) *out_kill_number = c->kill_number;
  return true;
}

void screenshot_run_reset(void) {
  for (int i = 0; i < s_capture_count; i++) free(s_captures[i].rgba);
  s_capture_count = 0;
  s_pending = false;
}

static void store_capture(unsigned char *rgba, int w, int h,
                          int kill_number, bool needs_swap) {
  if (s_capture_count >= MAX_RUN_CAPTURES) {
    /* Evict the oldest, shift the rest down, keep the newest ones. */
    free(s_captures[0].rgba);
    memmove(&s_captures[0], &s_captures[1],
           sizeof(run_capture) * (MAX_RUN_CAPTURES - 1));
    s_capture_count = MAX_RUN_CAPTURES - 1;
  }
  s_captures[s_capture_count].rgba = rgba;
  s_captures[s_capture_count].w = w;
  s_captures[s_capture_count].h = h;
  s_captures[s_capture_count].kill_number = kill_number;
  s_captures[s_capture_count].needs_swap = needs_swap;
  s_capture_count++;
}

#ifdef ANDROID
static void capture_to_memory(tenv *env, int kill_number) {
  tcontext *ctx = env->ctx;

  /* Only a kill (a rare, one-off event) triggers this, so a full
     device-idle stall here is an acceptable trade for keeping the
     synchronization trivially correct: nothing else touches the GPU
     while we read the image back. */
  tcontext_wait_idle(ctx);

  int w = ctx->swapchain_size[0];
  int h = ctx->swapchain_size[1];
  if (w <= 0 || h <= 0) return;
  VkImage src_image = ctx->swapchain_frames[ctx->current_image].image;

  VkDeviceSize buf_size = (VkDeviceSize)w * (VkDeviceSize)h * 4;
  VkBuffer staging_buffer;
  VmaAllocation staging_memory;
  VmaAllocationInfo staging_info;
  VkResult res = vmaCreateBuffer(
      ctx->allocator,
      &(VkBufferCreateInfo){.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                            .pNext = NULL,
                            .flags = 0,
                            .size = buf_size,
                            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                            .sharingMode = VK_SHARING_MODE_EXCLUSIVE},
      &(VmaAllocationCreateInfo){
          .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                   VMA_ALLOCATION_CREATE_MAPPED_BIT,
          .usage = VMA_MEMORY_USAGE_AUTO},
      &staging_buffer, &staging_memory, &staging_info);
  if (res != VK_SUCCESS) return;

  vkResetCommandBuffer(ctx->transfer_cmd, 0);
  vkBeginCommandBuffer(
      ctx->transfer_cmd,
      &(VkCommandBufferBeginInfo){
          .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
          .pNext = NULL,
          .flags = 0,
          .pInheritanceInfo = NULL});

  /* The swapchain image is in PRESENT_SRC_KHR right after being shown
     (see tcontext.c's swapchain render pass finalLayout) -- move it to
     TRANSFER_SRC_OPTIMAL for the copy, then back afterward so it's left
     exactly as the presentation engine expects. */
  vkCmdPipelineBarrier(
      ctx->transfer_cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1,
      &(VkImageMemoryBarrier){
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .pNext = NULL,
          .srcAccessMask = VK_ACCESS_NONE,
          .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
          .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
          .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          .srcQueueFamilyIndex = ctx->queue_family,
          .dstQueueFamilyIndex = ctx->queue_family,
          .image = src_image,
          .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .baseMipLevel = 0,
                               .levelCount = 1,
                               .baseArrayLayer = 0,
                               .layerCount = 1}});

  vkCmdCopyImageToBuffer(
      ctx->transfer_cmd, src_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      staging_buffer, 1,
      &(VkBufferImageCopy){
          .bufferOffset = 0,
          .bufferRowLength = 0,
          .bufferImageHeight = 0,
          .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .mipLevel = 0,
                               .baseArrayLayer = 0,
                               .layerCount = 1},
          .imageOffset = {0, 0, 0},
          .imageExtent = {(uint32_t)w, (uint32_t)h, 1}});

  vkCmdPipelineBarrier(
      ctx->transfer_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1,
      &(VkImageMemoryBarrier){
          .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
          .pNext = NULL,
          .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
          .dstAccessMask = VK_ACCESS_NONE,
          .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
          .srcQueueFamilyIndex = ctx->queue_family,
          .dstQueueFamilyIndex = ctx->queue_family,
          .image = src_image,
          .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                               .baseMipLevel = 0,
                               .levelCount = 1,
                               .baseArrayLayer = 0,
                               .layerCount = 1}});

  vkEndCommandBuffer(ctx->transfer_cmd);

  vkQueueSubmit(ctx->queue, 1,
                &(VkSubmitInfo){.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                .pNext = NULL,
                                .commandBufferCount = 1,
                                .pCommandBuffers = &ctx->transfer_cmd},
                ctx->transfer_fence);
  vkWaitForFences(ctx->device, 1, &ctx->transfer_fence, VK_TRUE, UINT64_MAX);
  vkResetFences(ctx->device, 1, &ctx->transfer_fence);

  /* The swapchain's pixel format is commonly BGRA on Android. Normalising
     it to RGBA is left for later (ensure_rgba(), on the review screen /
     when saving) so this in-match capture stays as cheap as possible:
     just the GPU readback and one plain memcpy. */
  unsigned char *pixels = (unsigned char *)staging_info.pMappedData;
  bool is_bgra = (ctx->surface_format.format == VK_FORMAT_B8G8R8A8_UNORM ||
                 ctx->surface_format.format == VK_FORMAT_B8G8R8A8_SRGB);

  /* Copy out of the (about to be destroyed) staging buffer into a plain
     malloc'd buffer we own for the rest of the run. */
  unsigned char *owned = (unsigned char *)malloc((size_t)buf_size);
  if (owned) {
    memcpy(owned, pixels, (size_t)buf_size);
    store_capture(owned, w, h, kill_number, is_bgra);
  }

  vmaDestroyBuffer(ctx->allocator, staging_buffer, staging_memory);
}
#endif

void screenshot_process_pending(tenv *env) {
  if (!s_pending) return;
  int kill_number = s_pending_kill_number;
  s_pending = false;

#ifdef ANDROID
  if (env && env->usr && env->ctx) capture_to_memory(env, kill_number);
#else
  (void)env;
  (void)kill_number;
#endif
}

void screenshot_run_save(tenv *env, const int *indices, int count) {
#ifdef ANDROID
  (void)env;
  for (int i = 0; i < count; i++) {
    int idx = indices[i];
    if (idx < 0 || idx >= s_capture_count) continue;
    run_capture *c = &s_captures[idx];
    ensure_rgba(c);
    char filename[64];
    snprintf(filename, sizeof(filename), "kill_%ld_%d.png", (long)time(NULL),
            c->kill_number);
    android_jni_save_screenshot(c->rgba, c->w, c->h, filename);
  }
#else
  (void)env;
  (void)indices;
  (void)count;
#endif
}
