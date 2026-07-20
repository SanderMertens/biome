#define BIOME_WEATHER_IMPL

#include "biome.h"
#include "weather_model.h"

static void WeatherUpdate(ecs_iter_t *it) {
    Weather *weather = ecs_field(it, Weather, 0);

    weather->temperature += biomeWeatherTemperatureChange(
        weather, it->delta_time);
}

void biomeWeatherImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeWeather);

    ecs_set_name_prefix(world, "Weather");
    ECS_META_COMPONENT(world, Weather);

    ecs_add_id(world, ecs_id(Weather), EcsSingleton);

    ECS_SYSTEM(world, WeatherUpdate, EcsPostUpdate,
        [inout] Weather);
}
