#ifndef FLECS_ENGINE_SURFACE_H
#define FLECS_ENGINE_SURFACE_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_SURFACE_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_ENUM(FlecsAnisotropy, {
    FlecsAnisotropyDefault = 0,
    FlecsAnisotropyOff     = 1,
    FlecsAnisotropyLow     = 2,
    FlecsAnisotropyMedium  = 4,
    FlecsAnisotropyHigh    = 8,
    FlecsAnisotropyUltra   = 16,
});

ECS_ENUM(FlecsMsaa, {
    FlecsMsaaDefault = 0,
    FlecsMsaaOff     = 1,
    FlecsMsaa2x      = 2,
    FlecsMsaa4x      = 4,
    FlecsMsaa8x      = 8,
});

ECS_ENUM(FlecsTextureQuality, {
    FlecsTextureQualityDefault  = 0,
    FlecsTextureQualityLow      = 512,
    FlecsTextureQualityMedium   = 1024,
    FlecsTextureQualityHigh     = 2048,
    FlecsTextureQualityVeryHigh = 4096,
});

ECS_STRUCT(FlecsSurface, {
    int32_t width;
    int32_t height;
    int32_t resolution_scale;
    int32_t actual_width;
    int32_t actual_height;
    int32_t point_width;
    int32_t point_height;
    FlecsMsaa msaa;
    bool vsync;
    bool gpu_timings;
    FlecsAnisotropy anisotropy;
    FlecsTextureQuality texture_quality;
    const char *title;
    const char *write_to_file;
    int32_t write_nth;
});

#endif
