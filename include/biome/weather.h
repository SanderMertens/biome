#ifndef FLECS_WEATHER_H
#define FLECS_WEATHER_H

#undef ECS_META_IMPL
#ifndef BIOME_WEATHER_IMPL
#define ECS_META_IMPL EXTERN
#endif

/* All amounts are in KG */

/* Ground attributes */
ECS_STRUCT(WeatherGroundTile, {
    float temperature;              /* Exchanges with neighboring tiles */
    float moisture_amount;          /* Water in the ground */
});

/* Water, if tile has standing water (amount of water determines height of water mesh) */
ECS_STRUCT(WeatherWaterTile, {
    float temperature;              /* Exchanges with neighboring tiles */
    float water_amount;             /* Amount of standing water on tile */
});

/* Air attributes */
ECS_STRUCT(WeatherAirTile, {
    float temperature;              /* Exchanges with neighboring tiles */
    float ghg_amount;               /* Amount of green house gass */
    float o2_amount;                /* Amount of oxygen */
    float vapor_amount;             /* Water vapor in air */
    float water;                    /* Water in clouds */
    flecs_vec3_t wind_velocity;     /* Transports air contents to neighboring tiles */

    /* Pressure is computed from the sum of the gasses + temperature */
});

/* Static weather configuration */
ECS_STRUCT(WeatherConfig, {
    ecs_entity_t sun;               /* Reference to sun entity */
    ecs_entity_t atmosphere;        /* Reference to atmosphere entity */

    WeatherGroundTile seed_ground;
    WeatherWaterTile seed_water;
    WeatherAirTile seed_air;
    float seed_variation;
    float ocean_level;
});

ECS_STRUCT(WeatherInfiltration, {
    bool enabled;
    float max_infiltration_rate;
    float ground_moisture_capacity;
});

ECS_STRUCT(WeatherThermalExchange, {
    bool enabled;
    float rate;
});

ECS_STRUCT(WeatherEvaporation, {
    bool enabled;
    float surface_water_rate;
    float ground_moisture_rate;
    float evaporative_cooling;
});

ECS_STRUCT(WeatherRadiativeBalance, {
    bool enabled;
    float radiative_cooling;
    float stellar_heating;
    float greenhouse_effect;
});

ECS_STRUCT(Weather, {
    float stellar_intensity;
    WeatherInfiltration infiltration;
    WeatherThermalExchange thermal_exchange;
    WeatherEvaporation evaporation;
    WeatherRadiativeBalance radiative_balance;
});

/* Contains buffers for double-buffering weather updates. These have the same
 * layout as the layer vectors in the Terrain, and will be swapped each frame. */
ECS_STRUCT(WeatherBuffers, {
ECS_PRIVATE
    ecs_vec_t ground_buffer;
    ecs_vec_t water_buffer;
    ecs_vec_t air_buffer;
    int32_t width;
    int32_t depth;
});

void biomeWeatherImport(ecs_world_t *world);

#endif
