#ifndef FLECS_ENGINE_SHADOW_H
#define FLECS_ENGINE_SHADOW_H

#define FLECS_ENGINE_SHADOW_MAP_SIZE_DEFAULT 4096

/* Initialize scene-shared shadow resources (shader, pass bind layout,
 * sampler). Shadow textures and cascade VP buffers are owned per-view. */
int flecsEngine_shadow_initShared(
    ecs_world_t *world,
    FlecsEngineImpl *impl);

void flecsEngine_shadow_cleanupShared(
    FlecsEngineImpl *impl);

/* Initialize per-view shadow resources (texture, layer views, VP buffers,
 * pass bind groups). Requires scene-shared shadow resources to already
 * exist on the engine. */
int flecsEngine_shadow_initView(
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    uint32_t shadow_map_size);

void flecsEngine_shadow_cleanupView(
    FlecsRenderViewImpl *view_impl);

int flecsEngine_shadow_ensureViewSize(
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    uint32_t shadow_map_size);

void flecsEngine_shadow_computeCascades(
    const ecs_world_t *world,
    const FlecsRenderView *view,
    ecs_entity_t light,
    uint32_t shadow_map_size,
    float max_range,
    mat4 out_light_vp[FLECS_ENGINE_SHADOW_CASCADE_COUNT],
    float out_splits[FLECS_ENGINE_SHADOW_CASCADE_COUNT]);

#endif
