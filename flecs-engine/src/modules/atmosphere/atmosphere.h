#include "../../private.h"

#ifndef FLECS_ENGINE_ATMOSPHERE_IMPL
#define FLECS_ENGINE_ATMOSPHERE_IMPL

bool flecsEngine_atmosphere_ensureImpl(
    ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t atmosphere_entity);

bool flecsEngine_atmosphere_renderLuts(
    ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t atmosphere_entity,
    ecs_entity_t view_entity,
    WGPUCommandEncoder encoder);

bool flecsEngine_atmosphere_renderIbl(
    ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t atmosphere_entity,
    WGPUCommandEncoder encoder);

bool flecsEngine_atmosphere_renderCompose(
    ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    ecs_entity_t atmosphere_entity,
    WGPUCommandEncoder encoder,
    WGPUTextureView input_view,
    WGPUTextureView output_view,
    WGPUTextureFormat output_format,
    WGPULoadOp output_load_op);

#endif
