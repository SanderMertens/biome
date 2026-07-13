#ifndef FLECS_ENGINE_ATMOSPHERE_H
#define FLECS_ENGINE_ATMOSPHERE_H

typedef struct {
    float scale_height_km;
    float scattering_scale;
} FlecsAtmosphereRayleigh;

typedef struct {
    float scale_height_km;
    float anisotropy;
    float scattering_scale;
    flecs_rgba_t tint;
} FlecsAtmosphereMie;

ECS_STRUCT(FlecsAtmosphere, {
    ecs_entity_t sun;          /* directional light entity used as sun
                                * direction/intensity for sky scattering */
    ecs_entity_t moon;         /* optional directional light entity used as
                                * a secondary night-side light source */
    ecs_entity_t stars;        /* optional entity carrying FlecsStars
                                * (shader config) plus FlecsCelestialLight
                                * (color/intensity) and FlecsRotation3
                                * (celestial sphere rotation) */

    float sun_disk_intensity;
    float sun_disk_angular_radius;
    float aerial_perspective_distance_km;

    float aerial_perspective_intensity;

    float sea_level_y;
    float world_units_per_km;
    float ground_altitude_km;

    float planet_radius_km;
    float atmosphere_thickness_km;

    FlecsAtmosphereRayleigh rayleigh;
    FlecsAtmosphereMie mie;

    float ozone_scale;
    float stratospheric_aerosol_scale;
    float haze_absorption;

    flecs_rgba_t ground_albedo;

    flecs_rgba_t night_tint;
    float night_intensity;

    float turbulence;
    float turbulence_scale;
    float turbulence_speed;
});

extern ECS_COMPONENT_DECLARE(FlecsAtmosphere);

void flecsEngine_atmos_sunTransmittance(
    const FlecsAtmosphere *atm,
    float sun_cos_zenith,
    float out_rgb[3]);

void FlecsEngineAtmosphereImport(
    ecs_world_t *world);

#endif
