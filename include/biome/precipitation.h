#ifndef FLECS_PRECIPITATION_H
#define FLECS_PRECIPITATION_H

#undef ECS_META_IMPL
#ifndef BIOME_PRECIPITATION_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(PrecipitationEmitter, {
ECS_PRIVATE
    ecs_entity_t rain_pool;
    ecs_entity_t snow_pool;
    uint32_t rand_state;
});

void biomePrecipitationImport(ecs_world_t *world);

#endif
