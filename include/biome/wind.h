#ifndef FLECS_WIND_H
#define FLECS_WIND_H

#undef ECS_META_IMPL
#ifndef BIOME_WIND_IMPL
#define ECS_META_IMPL EXTERN
#endif

#define WindParticleCap (1024)

ECS_STRUCT(WindState, {
ECS_PRIVATE
    ecs_entity_t pool;
    uint32_t rand_state;
});

void biomeWindImport(ecs_world_t *world);

#endif
