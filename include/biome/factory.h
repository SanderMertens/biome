#ifndef FLECS_FACTORY_H
#define FLECS_FACTORY_H

#undef ECS_META_IMPL
#ifndef BIOME_FACTORY_IMPL
#define ECS_META_IMPL EXTERN
#endif

int32_t biome_factory_canAfford(
    const ecs_world_t *world, 
    ecs_entity_t item);

bool biome_factory_purchase(
    const ecs_world_t *world,
    ecs_entity_t item,
    int32_t count);

void biome_factory_refund(
    const ecs_world_t *world,
    ecs_entity_t item,
    int32_t count);

void biomeFactoryImport(ecs_world_t *world);

#endif
