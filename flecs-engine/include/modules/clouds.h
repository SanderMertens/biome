#ifndef FLECS_ENGINE_CLOUDS_H
#define FLECS_ENGINE_CLOUDS_H

typedef struct {
    ecs_entity_t atmosphere;     /* atmosphere entity (sun-direction lookup) */
    float low_altitude_km;       /* cloud slab bottom, world altitude in km */
    float high_altitude_km;      /* cloud slab top, world altitude in km */
    float coverage;              /* 0..1 global coverage bias */
    float cloud_type_bias;       /* -0.5..0.5 stratus → cumulonimbus shift */
    float density;               /* extinction scale */
    float detail_strength;       /* 0..1 high-frequency erosion magnitude */
    float anisotropy;            /* 0..1 forward-scatter strength (silver lining) */
    float powder_strength;       /* 0..1 dark-base powder term blend */
    float multi_scatter;         /* 0..1 in-scatter octave attenuation */
    float wind_x;                /* world units per second */
    float wind_z;
    float weather_scale_km;      /* world km per weather-texture tile */
    float noise_scale_km;        /* world km per noise-texture tile */
    uint32_t seed;               /* FBM hash seed; 0 picks a fixed default */
    flecs_rgba_t ambient_top;
    flecs_rgba_t ambient_bottom;
} FlecsCloudsAppearance;

typedef struct {
    float strength;              /* 0 disables ground cloud-shadow */
    float scale_km;              /* world km per shadow-projection tile */
    int32_t size;                /* baked shadow texture resolution (square) */
} FlecsCloudsShadows;

typedef struct {
    /* Resolution divisor for cloud raymarching. 1 = full-res, 2 = half-res
     * in each axis (~4x faster), 4 = quarter-res (~16x faster). Foreground
     * pixels are always taken from the full-res input so geometry edges stay
     * sharp; only sky pixels pay the low-res cost. Values < 1 are clamped. */
    float render_scale;
    int32_t march_steps;         /* primary view march; lower = faster, blockier */
    int32_t light_steps;         /* sun shadow march per sample; lower = faster */
    float max_distance_km;       /* far cull for the cloud march */
} FlecsCloudsPerformance;

ECS_STRUCT(FlecsClouds, {
    FlecsCloudsAppearance appearance;
    FlecsCloudsShadows shadows;
    FlecsCloudsPerformance performance;
});

extern ECS_COMPONENT_DECLARE(FlecsClouds);

void flecsEngine_clouds_register(
    ecs_world_t *world);

#endif
