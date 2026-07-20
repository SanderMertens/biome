#ifndef BIOME_WATER_H
#define BIOME_WATER_H

#undef ECS_META_IMPL
#ifndef BIOME_WATER_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(WaterConfig, {
    float density;
    float flow_rate;
    float min_flow;
});

void biomeWaterImport(ecs_world_t *world);

void biomeWaterConfigureRenderer(
    ecs_world_t *world);

#endif
