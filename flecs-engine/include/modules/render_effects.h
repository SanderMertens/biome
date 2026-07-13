#ifndef FLECS_ENGINE_RENDER_EFFECTS_H
#define FLECS_ENGINE_RENDER_EFFECTS_H

typedef struct {
    float threshold;
    float threshold_softness;
} FlecsBloomPrefilter;

typedef struct {
    float intensity;
    float low_frequency_boost;
    float low_frequency_boost_curvature;
    float high_pass_frequency;
    FlecsBloomPrefilter prefilter;
    uint32_t max_mip_dimension;
} FlecsBloom;

extern ECS_COMPONENT_DECLARE(FlecsBloom);

ECS_STRUCT(FlecsHeightFog, {
    float density;
    float falloff;
    float base_height;
    float max_opacity;
    flecs_rgba_t color;

    /* Set to atmosphere entity to derive fog color from atmosphere */
    ecs_entity_t atmosphere;
});

extern ECS_COMPONENT_DECLARE(FlecsHeightFog);

ECS_STRUCT(FlecsSSAO, {
    float radius;
    float bias;
    float intensity;
});

extern ECS_COMPONENT_DECLARE(FlecsSSAO);

ECS_STRUCT(FlecsSunShafts, {
    float intensity;
    float density;
    float weight;
    float decay;
    float exposure;
    flecs_rgba_t color;

    /* Set to directional light entity to derive shaft color from light color */
    ecs_entity_t light;
});

extern ECS_COMPONENT_DECLARE(FlecsSunShafts);

ECS_STRUCT(FlecsAutoExposure, {
    float min_brightness;
    float max_brightness;
    float min_log_luma;
    float max_log_luma;
    float speed_up;
    float speed_down;
    float low_percentile;
    float high_percentile;
});

extern ECS_COMPONENT_DECLARE(FlecsAutoExposure);

ECS_STRUCT(FlecsTony, {
    ecs_entity_t auto_exposure;
});

extern ECS_COMPONENT_DECLARE(FlecsTony);

typedef struct {
    char _dummy;
} FlecsInvert;

extern ECS_COMPONENT_DECLARE(FlecsInvert);

typedef struct {
    char _dummy;
} FlecsGammaCorrect;

extern ECS_COMPONENT_DECLARE(FlecsGammaCorrect);

#endif
