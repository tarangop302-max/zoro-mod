#include "bp_renderer.h"
#include <string.h>

static bp_renderer* _bp_renderer_create(tcontext* ctx, int max_instances,
                                        int max_batches, bool stencil_dedup,
                                        VkPipelineLayout layout,
                                        VkRenderPass pass) {
  bp_renderer* r = malloc(sizeof(bp_renderer));

  VkShaderModule vertex_shader =
      tcontext_create_shader(ctx, "app/res/shaders/bin/bpv.spv");
  VkShaderModule fragment_shader =
      tcontext_create_shader(ctx, "app/res/shaders/bin/bpf.spv");

  /* Everything below matches the plain (non-dedup) pipeline exactly,
   * with one exception: pDepthStencilState. The dedup variant enables
   * a stencil test (compare NOT_EQUAL against reference 1, pass op
   * REPLACE) so that within one bp_renderer_begin_batch/end_batch
   * group, a pixel an earlier instance in that same batch already
   * touched is skipped by later ones instead of blending again on
   * top -- see bp_renderer.h for why. */
  VkPipelineDepthStencilStateCreateInfo plain_depth_stencil = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .depthTestEnable = VK_FALSE,
  };

  VkStencilOpState dedup_stencil_op = {
      .failOp = VK_STENCIL_OP_KEEP,
      .passOp = VK_STENCIL_OP_REPLACE,
      .depthFailOp = VK_STENCIL_OP_KEEP,
      .compareOp = VK_COMPARE_OP_NOT_EQUAL,
      .compareMask = 0xFF,
      .writeMask = 0xFF,
      .reference = 1,
  };

  VkPipelineDepthStencilStateCreateInfo dedup_depth_stencil = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .pNext = NULL,
      .flags = 0,
      .depthTestEnable = VK_FALSE,
      .depthWriteEnable = VK_FALSE,
      .depthCompareOp = VK_COMPARE_OP_ALWAYS,
      .depthBoundsTestEnable = VK_FALSE,
      .stencilTestEnable = VK_TRUE,
      .front = dedup_stencil_op,
      .back = dedup_stencil_op,
      .minDepthBounds = 0.0f,
      .maxDepthBounds = 1.0f,
  };

  vkCreateGraphicsPipelines(
      ctx->device, NULL, 1,
      &(VkGraphicsPipelineCreateInfo){
          .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
          .pNext = NULL,
          .flags = 0,
          .stageCount = 2,
          .pStages =
              (VkPipelineShaderStageCreateInfo[]){
                  {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                   .pNext = NULL,
                   .flags = 0,
                   .stage = VK_SHADER_STAGE_VERTEX_BIT,
                   .module = vertex_shader,
                   .pName = "main",
                   .pSpecializationInfo = NULL},
                  {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                   .pNext = NULL,
                   .flags = 0,
                   .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                   .module = fragment_shader,
                   .pName = "main",
                   .pSpecializationInfo = NULL}},
          .pVertexInputState =
              &(VkPipelineVertexInputStateCreateInfo){
                  .sType =
                      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                  .pNext = NULL,
                  .flags = 0,
                  .vertexBindingDescriptionCount = 2,
                  .pVertexBindingDescriptions =
                      (VkVertexInputBindingDescription[]){
                          {.binding = 0,
                           .stride = 2 * sizeof(float),
                           .inputRate = VK_VERTEX_INPUT_RATE_VERTEX},
                          {.binding = 1,
                           .stride = sizeof(bp_instance),
                           .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE},
                      },
                  .vertexAttributeDescriptionCount = 5,
                  .pVertexAttributeDescriptions =
                      (VkVertexInputAttributeDescription[]){
                          {.location = 0,
                           .binding = 0,
                           .format = VK_FORMAT_R32G32_SFLOAT,
                           .offset = 0},
                          {.location = 1,
                           .binding = 1,
                           .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                           .offset = 0},
                          {.location = 2,
                           .binding = 1,
                           .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                           .offset = offsetof(bp_instance, uv_rect)},
                          {.location = 3,
                           .binding = 1,
                           .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                           .offset = offsetof(bp_instance, color)},
                          {.location = 4,
                           .binding = 1,
                           .format = VK_FORMAT_R32G32_SFLOAT,
                           .offset = offsetof(bp_instance, shape)}}},
          .pInputAssemblyState =
              &(VkPipelineInputAssemblyStateCreateInfo){
                  .sType =
                      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                  .pNext = NULL,
                  .flags = 0,
                  .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
                  .primitiveRestartEnable = VK_FALSE},
          .pTessellationState = NULL,
          .pViewportState =
              &(VkPipelineViewportStateCreateInfo){
                  .sType =
                      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
                  .pNext = NULL,
                  .flags = 0,
                  .viewportCount = 1,
                  .scissorCount = 1},
          .pRasterizationState =
              &(VkPipelineRasterizationStateCreateInfo){
                  .sType =
                      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                  .pNext = NULL,
                  .flags = 0,
                  .depthClampEnable = VK_FALSE,
                  .rasterizerDiscardEnable = VK_FALSE,
                  .polygonMode = VK_POLYGON_MODE_FILL,
                  .cullMode = VK_CULL_MODE_NONE,
                  .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                  .depthBiasEnable = VK_FALSE,
                  .depthBiasConstantFactor = 0.0f,
                  .depthBiasClamp = 0.0f,
                  .depthBiasSlopeFactor = 0.0f,
                  .lineWidth = 1.0f},
          .pMultisampleState =
              &(VkPipelineMultisampleStateCreateInfo){
                  .sType =
                      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                  .pNext = NULL,
                  .flags = 0,
                  .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
                  .sampleShadingEnable = VK_FALSE,
                  .minSampleShading = 0.0f,
                  .pSampleMask = NULL,
                  .alphaToCoverageEnable = VK_FALSE,
                  .alphaToOneEnable = VK_FALSE},
          .pDepthStencilState =
              stencil_dedup ? &dedup_depth_stencil : &plain_depth_stencil,
          .pColorBlendState =
              &(VkPipelineColorBlendStateCreateInfo){
                  .sType =
                      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                  .pNext = NULL,
                  .flags = 0,
                  .logicOpEnable = VK_FALSE,
                  .logicOp = VK_LOGIC_OP_COPY,
                  .attachmentCount = 1,
                  .pAttachments =
                      &(VkPipelineColorBlendAttachmentState){
                          .blendEnable = VK_TRUE,
                          .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                          .dstColorBlendFactor =
                              VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                          .colorBlendOp = VK_BLEND_OP_ADD,
                          .srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                          .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                          .alphaBlendOp = VK_BLEND_OP_ADD,
                          .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                            VK_COLOR_COMPONENT_G_BIT |
                                            VK_COLOR_COMPONENT_B_BIT |
                                            VK_COLOR_COMPONENT_A_BIT},
                  .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f}},
          .pDynamicState =
              &(VkPipelineDynamicStateCreateInfo){
                  .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                  .pNext = NULL,
                  .flags = 0,
                  .dynamicStateCount = 2,
                  .pDynamicStates =
                      (VkDynamicState[]){VK_DYNAMIC_STATE_VIEWPORT,
                                         VK_DYNAMIC_STATE_SCISSOR}},
          .layout = layout,
          .renderPass = pass,
          .subpass = 0,
          .basePipelineHandle = VK_NULL_HANDLE,
          .basePipelineIndex = 0},
      NULL, &r->pipeline);

  vkDestroyShaderModule(ctx->device, fragment_shader, NULL);
	vkDestroyShaderModule(ctx->device, vertex_shader, NULL);

	r->instance_buffer = tdbuffer_create(ctx, NULL, max_instances * sizeof(bp_instance), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
  r->instances = malloc(max_instances * sizeof(bp_instance));
  r->max_instances = max_instances;
  r->num_instances = 0;

  r->batches = max_batches > 0 ? malloc(max_batches * sizeof(bp_batch)) : NULL;
  r->max_batches = max_batches;
  r->num_batches = 0;
  r->in_batch = false;

  return r;
}

bp_renderer* bp_renderer_create(tcontext* ctx, int max_instances,
                                VkPipelineLayout layout, VkRenderPass pass) {
  return _bp_renderer_create(ctx, max_instances, 0, false, layout, pass);
}

bp_renderer* bp_renderer_create_dedup(tcontext* ctx, int max_instances,
                                      int max_batches,
                                      VkPipelineLayout layout,
                                      VkRenderPass pass) {
  return _bp_renderer_create(ctx, max_instances, max_batches, true, layout,
                             pass);
}

void bp_renderer_push(bp_renderer* r, const bp_instance* instance) {
  if (r->num_instances >= r->max_instances) {
    return;
  }
  r->instances[r->num_instances++] = *instance;
}

void bp_renderer_begin_batch(bp_renderer* r) {
  if (r->batches == NULL || r->num_batches >= r->max_batches) return;
  r->batches[r->num_batches].start = r->num_instances;
  r->in_batch = true;
}

void bp_renderer_end_batch(bp_renderer* r) {
  if (r->batches == NULL || !r->in_batch || r->num_batches >= r->max_batches)
    return;
  r->batches[r->num_batches].count =
      r->num_instances - r->batches[r->num_batches].start;
  r->num_batches++;
  r->in_batch = false;
}

void bp_renderer_render(bp_renderer* r, tcontext* ctx) {
  tcontext_frame* fr = ctx->frames + ctx->current_frame;
  memcpy(r->instance_buffer[ctx->current_frame].data, r->instances, r->num_instances * sizeof(bp_instance));
  vkCmdBindPipeline(fr->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline);
  vkCmdBindVertexBuffers(fr->cmd, 1, 1, &r->instance_buffer[ctx->current_frame].handle, (VkDeviceSize[]){0});
  vkCmdDraw(fr->cmd, 4, r->num_instances, 0, 0);
}

void bp_renderer_render_batched(bp_renderer* r, tcontext* ctx, ivec2 size) {
  if (r->num_batches == 0) return;

  tcontext_frame* fr = ctx->frames + ctx->current_frame;
  memcpy(r->instance_buffer[ctx->current_frame].data, r->instances, r->num_instances * sizeof(bp_instance));
  vkCmdBindPipeline(fr->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline);
  vkCmdBindVertexBuffers(fr->cmd, 1, 1, &r->instance_buffer[ctx->current_frame].handle, (VkDeviceSize[]){0});

  for (int i = 0; i < r->num_batches; i++) {
    bp_batch* b = r->batches + i;
    if (b->count <= 0) continue;

    /* Reset the whole target's stencil before this batch's draw, so
     * this body's coverage can't be affected by (or affect) any
     * other batch's. Clearing the whole target rather than just this
     * body's on-screen bounds is simpler and safe -- this is only
     * ever called for a small number of batches per frame. */
    vkCmdClearAttachments(
        fr->cmd, 1,
        &(VkClearAttachment){
            .aspectMask = VK_IMAGE_ASPECT_STENCIL_BIT,
            .colorAttachment = 0,
            .clearValue = {.depthStencil = {.depth = 0.0f, .stencil = 0}}},
        1,
        &(VkClearRect){
            .rect = {.offset = {0, 0},
                     .extent = {(uint32_t)size[0], (uint32_t)size[1]}},
            .baseArrayLayer = 0,
            .layerCount = 1});

    vkCmdDraw(fr->cmd, 4, b->count, 0, b->start);
  }
}

void bp_renderer_destroy(bp_renderer* r, tcontext* ctx) {
  free(r->instances);
  if (r->batches != NULL) free(r->batches);
  tdbuffer_destroy(ctx, r->instance_buffer);
  vkDestroyPipeline(ctx->device, r->pipeline, NULL);
  free(r);
}
