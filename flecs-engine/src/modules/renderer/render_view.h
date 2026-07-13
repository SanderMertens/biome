#ifndef FLECS_ENGINE_RENDER_VIEW_H
#define FLECS_ENGINE_RENDER_VIEW_H

void flecsEngine_renderView_register(
    ecs_world_t *world);

void flecsEngine_renderView_extractAll(
    ecs_world_t *world,
    FlecsEngineImpl *engine);

void flecsEngine_renderView_renderAll(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    WGPUTextureView view_texture,
    WGPUCommandEncoder encoder);

void flecsEngine_renderView_cullAll(
    ecs_world_t *world,
    FlecsEngineImpl *engine);

void flecsEngine_renderView_cullShadowsAll(
    ecs_world_t *world,
    FlecsEngineImpl *engine);

void flecsEngine_renderView_uploadAll(
    ecs_world_t *world,
    FlecsEngineImpl *engine);

void flecsEngine_renderView_uploadShadowsAll(
    ecs_world_t *world,
    FlecsEngineImpl *engine);

void flecsEngine_renderView_renderShadow(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view,
    FlecsRenderViewImpl *view_impl,
    WGPUCommandEncoder encoder);

static inline ecs_entity_t flecsEngine_renderView_atmosphere(
    const ecs_world_t *world,
    const FlecsRenderView *view)
{
    ecs_entity_t e = view->atmosphere;
    if (e && ecs_has_id(world, e, EcsDisabled)) {
        return 0;
    }
    return e;
}

#endif
