#ifndef FLECS_ENGINE_PARTICLES_INTERNAL_H
#define FLECS_ENGINE_PARTICLES_INTERNAL_H

#include "../../private.h"

/* Gathers all live particles into the instance buffer, sorted back to
 * front for the given view. Returns the number of instances to draw
 * (0 means the render pass can be skipped). Called by the renderer
 * between the main batch pass and the effect chain. */
int32_t flecsEngine_particles_prepare(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl);

/* Encodes the instanced particle draw into an already-open pass that
 * targets the scene color + depth. */
void flecsEngine_particles_draw(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    WGPURenderPassEncoder pass,
    WGPUTextureFormat color_format,
    int32_t sample_count,
    int32_t instance_count);

#endif
