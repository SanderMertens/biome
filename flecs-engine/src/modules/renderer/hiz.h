#ifndef FLECS_ENGINE_HIZ_H
#define FLECS_ENGINE_HIZ_H

#include "../../types.h"

int flecsEngine_hiz_init(
    ecs_world_t *world,
    FlecsEngineImpl *engine);

void flecsEngine_hiz_fini(
    FlecsEngineImpl *engine);

int flecsEngine_hiz_ensureView(
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl);

void flecsEngine_hiz_finiView(
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl);

void flecsEngine_hiz_build(
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    WGPUCommandEncoder encoder);

#endif
