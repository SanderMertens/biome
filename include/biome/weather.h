#ifndef FLECS_WEATHER_H
#define FLECS_WEATHER_H

#undef ECS_META_IMPL
#ifndef BIOME_WEATHER_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(Weather, {
    float pressure;
    float gravity;
    float greenhouse_gas;
    float radiative_cooling;
    float stellar_heating;
    float temperature;
    float water_level;
});

void biomeWeatherImport(ecs_world_t *world);

#endif
