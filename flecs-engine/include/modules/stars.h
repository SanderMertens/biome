#ifndef FLECS_ENGINE_STARS_H
#define FLECS_ENGINE_STARS_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_STARS_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(FlecsStars, {
    float density;           /* Threshold in [0,1]; higher = fewer stars. */
    float cells;             /* Cube-face grid resolution (cells per face
                              * edge). */
    float size;              /* Star spot size coefficient. */
    float color_variation;   /* 0..1. 0 = all stars use FlecsCelestialLight
                              * color; 1 = full red/yellow/white/blue range. */
});

extern ECS_COMPONENT_DECLARE(FlecsStars);

void FlecsEngineStarsImport(
    ecs_world_t *world);

#endif
