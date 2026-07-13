#ifndef FLECS_CLOUD_H
#define FLECS_CLOUD_H

#undef ECS_META_IMPL
#ifndef BIOME_CLOUD_IMPL
#define ECS_META_IMPL EXTERN
#endif

#define CloudBaseHeight (16.0f)

ECS_STRUCT(CloudEmitter, {
ECS_PRIVATE
    ecs_entity_t cloud_pool;
    uint32_t rand_state;
});

void biomeCloudImport(ecs_world_t *world);

#endif
