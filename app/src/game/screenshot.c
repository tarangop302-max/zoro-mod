#include "screenshot.h"

#include <stdio.h>
#include <time.h>

#include "../user.h"

#ifdef ANDROID
#include "../android_jni.h"
#endif

static bool s_pending = false;
static int s_pending_kill_number = 0;

void screenshot_request(int kill_number) {
  /* Processing happens on the very next frame (see screenshot.h), so
     there's essentially no window for a second kill to arrive before
     this one is handled. If it somehow does, just keep the first
     request rather than overwriting it -- dropping an occasional extra
     screenshot on a very fast multi-kill is a fine tradeoff for keeping
     this dead simple. */
  if (s_pending) return;
  s_pending = true;
  s_pending_kill_number = kill_number;
}

#ifdef ANDROID
static void capture_and_save(tenv *env, int kill_number) {
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

  /* The swapchain's pixel format is commonly BGRA on Android -- normalize
     to RGBA here so the Kotlin/Bitmap side can always assume plain
     RGBA8888 and never needs to know which one it was. */
  unsigned char *pixels = (unsigned char *)staging_info.pMappedData;
  bool is_bgra = (ctx->surface_format.format == VK_FORMAT_B8G8R8A8_UNORM ||
                 ctx->surface_format.format == VK_FORMAT_B8G8R8A8_SRGB);
  if (is_bgra) {
    size_t pixel_count = (size_t)w * (size_t)h;
    for (size_t i = 0; i < pixel_count; i++) {
      unsigned char *p = pixels + i * 4;
      unsigned char tmp = p[0];
      p[0] = p[2];
      p[2] = tmp;
    }
  }

  char filename[64];
  snprintf(filename, sizeof(filename), "kill_%ld_%d.png", (long)time(NULL),
          kill_number);
  android_jni_save_screenshot(pixels, w, h, filename);

  vmaDestroyBuffer(ctx->allocator, staging_buffer, staging_memory);
}
#endif

void screenshot_process_pending(tenv *env) {
  if (!s_pending) return;
  int kill_number = s_pending_kill_number;
  s_pending = false;

#ifdef ANDROID
  if (env && env->usr && env->ctx) capture_and_save(env, kill_number);
#else
  (void)env;
  (void)kill_number;
#endif
}
