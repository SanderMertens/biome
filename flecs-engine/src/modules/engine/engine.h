#include "../../private.h"

#ifndef FLECS_ENGINE_IMPL
#define FLECS_ENGINE_IMPL

int flecsEngine_init(
    ecs_world_t *world,
    ecs_entity_t surface_entity,
    const FlecsSurface *config,
    FlecsSurfaceImpl *impl);

void flecsEngine_surfaceImpl_release(
    FlecsSurfaceImpl *impl);

void flecsEngine_surface_register(
    ecs_world_t *world);

#endif
