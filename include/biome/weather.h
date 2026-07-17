#ifndef FLECS_WEATHER_H
#define FLECS_WEATHER_H

#undef ECS_META_IMPL
#ifndef BIOME_WEATHER_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(WeatherGroundTile, {
    float temperature;              /* Exchanges with neighboring tiles (ground and air) */
    float moisture;                 /* Evaporates over time */
    float water;                    /* Transforms into moisture over time, travels along slope */
    float frozen;                   /* Fraction of water that is ice (0: water, 1: ice) */
    float albedo;                   /* Albedo of tile, used to determine heat absorption. Computed from frozen water, soil kind, and temperature  */
});

ECS_STRUCT(WeatherAirTile, {
    float temperature;              /* Exchanges with neighboring air tiles and ground tile */
    float vapor;                    /* Water vapor in air */
    float cloud_water;              /* Water in clouds/precipitation */
    float precipitation;            /* Amount of cloud water dropped this tick */

    flecs_vec3_t wind_velocity;     /* Transports hot/cold air, vapor and water to neighboring cells */
});

/* Static weather configuration */
ECS_STRUCT(WeatherConfig, {
    ecs_entity_t sun;               /* Reference to sun entity */
    ecs_entity_t atmosphere;        /* Reference to atmosphere entity */

    WeatherGroundTile seed_ground;
    WeatherAirTile seed_air;
    float seed_variation;
});

/* Changeable weather values. O2, CO2 are not tracked per tile, but are
 * slow-moving values that are set outside of the weather simulation. */
ECS_STRUCT(WeatherAtmosphere, {
    float o2_content;
    float co2_quantity;
    float vapor_content;            /* Set by weather simulation (sum of all air tiles) */
    float temperature;              /* Set by weather simulation: (average of all air tiles) */
    float atmosphere_height;        /* Determines how much atmosphere is above a tile */
});

/* Contains buffers for double-buffering weather updates. These have the same
 * layout as the layer vectors in the Terrain, and will be swapped each frame. */
ECS_STRUCT(WeatherBuffers, {
ECS_PRIVATE
    ecs_vec_t ground_buffer;
    ecs_vec_t air_buffer;
    ecs_vec_t head_buffer;
    int32_t width;
    int32_t depth;
    int32_t frame_skip;
    int32_t debug_frame;
    double update_time_sum;
    int32_t update_time_count;
});

void biomeWeatherImport(ecs_world_t *world);

#endif
