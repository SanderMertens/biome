#define FLECS_ENGINE_STARS_IMPL
#include "stars.h"

ECS_COMPONENT_DECLARE(FlecsStars);

ECS_CTOR(FlecsStars, ptr, {
    ptr->density = 0.985f;
    ptr->cells = 160.0f;
    ptr->size = 90.0f;
    ptr->color_variation = 0.5f;
})

void FlecsEngineStarsImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineStars);

    ecs_set_name_prefix(world, "Flecs");

    ECS_META_COMPONENT(world, FlecsStars);

    ecs_set_hooks(world, FlecsStars, {
        .ctor = ecs_ctor(FlecsStars)
    });

    ecs_add_pair(world, ecs_id(FlecsStars), EcsOnInstantiate, EcsInherit);
}
