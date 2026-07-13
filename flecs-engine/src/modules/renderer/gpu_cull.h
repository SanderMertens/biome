#ifndef FLECS_ENGINE_GPU_CULL_H
#define FLECS_ENGINE_GPU_CULL_H

#include "../../types.h"

/* GPU-side layouts. Must match WGSL struct layouts in gpu_cull.c. */

typedef struct {
    float min[3];
    float _pad0;
    float max[3];
    float _pad1;
} flecsEngine_gpuAabb_t;

typedef struct {
    uint32_t src_offset;
    uint32_t src_count;
    uint32_t _pad0;
    uint32_t _pad1;
} flecsEngine_gpuCullGroupInfo_t;

typedef struct {
    uint32_t index_count;
    uint32_t instance_count; /* atomic — zeroed per frame */
    uint32_t first_index;
    int32_t  base_vertex;
    uint32_t first_instance;
} flecsEngine_gpuDrawArgs_t;

int flecsEngine_gpuCull_init(
    ecs_world_t *world,
    FlecsEngineImpl *engine);

void flecsEngine_gpuCull_fini(
    FlecsEngineImpl *engine);

/* Per-view uniform buffer + bind group. Call from render_view to init/fini. */
int flecsEngine_gpuCull_initView(
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl);

void flecsEngine_gpuCull_finiView(
    FlecsRenderViewImpl *view_impl);

/* Write frustum planes + screen cull state for this view. */
void flecsEngine_gpuCull_writeViewUniforms(
    FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl);

/* Encode a compute dispatch for all batches in a view. */
void flecsEngine_gpuCull_dispatchAll(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    ecs_entity_t view_entity,
    WGPUCommandEncoder encoder);

#endif
