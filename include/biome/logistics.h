#ifndef FLECS_LOGISTICS_H
#define FLECS_LOGISTICS_H

#undef ECS_META_IMPL
#ifndef BIOME_LOGISTICS_IMPL
#define ECS_META_IMPL EXTERN
#endif

void biomeLogisticsImport(ecs_world_t *world);

#endif
