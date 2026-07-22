#ifndef BIOME_PLANT_H
#define BIOME_PLANT_H

#undef ECS_META_IMPL
#ifndef BIOME_PLANT_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(BiomePlant, {
    float min_temperature;
    float max_temperature;
    float min_moisture;
    float min_fertility;
    int32_t resilience;
});

ECS_STRUCT(BiomePlantState, {
    int32_t stress;
});

void biomePlantImport(ecs_world_t *world);

#endif
