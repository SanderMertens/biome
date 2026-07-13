#ifndef FLECS_ENGINE_TRANSFORM3_H
#define FLECS_ENGINE_TRANSFORM3_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_TRANSFORM3_IMPL
#define ECS_META_IMPL EXTERN
#endif

typedef flecs_vec3_t FlecsPosition3;
typedef flecs_vec3_t FlecsRotation3;
typedef flecs_vec3_t FlecsScale3;
typedef flecs_vec3_t FlecsLookAt;

extern ECS_COMPONENT_DECLARE(FlecsPosition3);
extern ECS_COMPONENT_DECLARE(FlecsRotation3);
extern ECS_COMPONENT_DECLARE(FlecsScale3);
extern ECS_COMPONENT_DECLARE(FlecsLookAt);

extern ECS_TAG_DECLARE(FlecsDynamicTransform);
extern ECS_TAG_DECLARE(FlecsManualTransform);

ECS_STRUCT(FlecsWorldTransform3, {
    mat4 m;
});

ECS_STRUCT(FlecsAABB, {
    float min[3];
    float max[3];
});

#endif
