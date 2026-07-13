#ifndef FLECS_ENGINE_UI2D_INTERNAL_H
#define FLECS_ENGINE_UI2D_INTERNAL_H

#include "../../private.h"

/* Encodes the queued 2D overlay on top of the finished frame. Called by the
 * renderer after all views have been rendered to the surface target. */
void flecsEngine_ui2d_render(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    WGPUTextureView target,
    WGPUCommandEncoder encoder);

#endif
