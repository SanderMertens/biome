#ifndef FLECS_ENGINE_RENDER_BATCH_H
#define FLECS_ENGINE_RENDER_BATCH_H

struct FlecsRenderBatch;

typedef void (*flecs_render_batch_callback)(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const struct FlecsRenderBatch *batch);

typedef void (*flecs_render_batch_extract_callback)(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const struct FlecsRenderBatch *batch);

typedef void* (*flecs_render_batch_get_buf_fn)(
    const struct FlecsRenderBatch *batch);

// Render entities matching a query with specified shader
ECS_STRUCT(FlecsRenderBatch, {
    ecs_entity_t shader;
    ecs_query_t *query;
    ecs_entity_t vertex_type;
    WGPUCompareFunction depth_test;
    WGPUCullMode cull_mode;
    WGPUBlendState blend;
    bool depth_write;
ECS_PRIVATE
    flecs_render_batch_extract_callback extract_callback;
    flecs_render_batch_extract_callback upload_callback;
    flecs_render_batch_callback callback;
    flecs_render_batch_callback shadow_callback;
    flecs_render_batch_get_buf_fn get_cull_buf;
    void *ctx;
    void (*free_ctx)(void *ctx);
    bool render_after_snapshot;
    bool needs_transmission;
});

void flecsEngine_renderBatch_render(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const FlecsRenderView *view,
    ecs_entity_t batch_entity);

void flecsEngine_renderBatch_renderShadow(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    ecs_entity_t batch_entity);

void flecsEngine_renderBatch_extract(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    FlecsRenderViewImpl *view_impl,
    ecs_entity_t batch_entity);

void flecsEngine_renderBatch_upload(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    FlecsRenderViewImpl *view_impl,
    ecs_entity_t batch_entity);

void flecsEngine_renderBatch_register(
    ecs_world_t *world);

void flecsEngine_bufferSlot_markChanged(
    ecs_world_t *world,
    ecs_entity_t entity);

void flecsEngine_bufferSlot_release(
    ecs_world_t *world,
    ecs_entity_t entity,
    const FlecsBufferSlot *slot);

void flecsEngine_renderBatchSet_register(
    ecs_world_t *world);

bool flecsEngine_renderBatchSet_hasTransmission(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderBatchSet *batch_set);

void flecsEngine_renderBatchSet_extract(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatchSet *batch_set);

void flecsEngine_renderBatchSet_upload(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatchSet *batch_set);

void flecsEngine_renderBatchSet_render(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatchSet *batch_set,
    WGPURenderPassEncoder pass,
    const FlecsRenderView *view,
    int phase);

void flecsEngine_renderBatchSet_renderShadow(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatchSet *batch_set,
    WGPURenderPassEncoder pass);

void flecsEngine_renderView_ensureSceneBindGroup(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    const FlecsRenderView *view);

#endif
