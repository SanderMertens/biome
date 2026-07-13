#ifndef FLECS_ENGINE_CLUSTER_H
#define FLECS_ENGINE_CLUSTER_H

/* Initialize the scene-wide lights buffer. Per-view cluster buffers are
 * initialized lazily by flecsEngine_cluster_initView. */
int flecsEngine_cluster_initLights(
    FlecsEngineImpl *impl);

/* Initialize per-view cluster buffers (info, grid, index) and CPU index
 * storage. */
int flecsEngine_cluster_initView(
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl);

void flecsEngine_cluster_cleanupView(
    FlecsRenderViewImpl *view_impl);

bool flecsEngine_cluster_ensureLights(
    FlecsEngineImpl *engine, int32_t needed);

void flecsEngine_cluster_build(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    const FlecsRenderView *view);

#endif
