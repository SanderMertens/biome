#ifndef FLECS_ENGINE_TIME_OF_DAY_H
#define FLECS_ENGINE_TIME_OF_DAY_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_TIME_OF_DAY_IMPL
#define ECS_META_IMPL EXTERN
#endif

/* Drives a directional light (and optionally an atmosphere entity) from a
 * simulated time of day, day of year, and geographic latitude. The sun's
 * orientation is computed with a standard NOAA-style solar position
 * approximation and written to the light's Rotation3 each frame. The light's
 * DirectionalLight intensity and Rgba are derived from its FlecsCelestialLight
 * (top-of-atmosphere radiance) attenuated by air mass / transmittance. */
ECS_STRUCT(FlecsTimeOfDay, {
    ecs_entity_t light;       /* directional light to drive (required) */
    ecs_entity_t moon_light;  /* directional light for the moon (optional) */
    ecs_entity_t atmosphere;  /* atmosphere entity to drive (optional) */
    ecs_entity_t stars;       /* starfield entity whose Rotation3 is driven
                               * from the simulated hour (optional) */

    float hour;               /* local solar time, 0..24 */
    float day_of_year;        /* 1..365 (82 ≈ equinox) */
    float latitude;           /* degrees, -90..90 (positive = north) */
    float north_offset;       /* degrees; world-yaw that points north. */
    float time_scale;         /* simulated hours per real second; 0 = paused */

    float moon_position_offset; /* hours added to the moon's hour angle. */
});

#endif
