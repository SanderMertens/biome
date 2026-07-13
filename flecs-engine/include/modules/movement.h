#ifndef FLECS_ENGINE_MOVEMENT_H
#define FLECS_ENGINE_MOVEMENT_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_MOVEMENT_IMPL
#define ECS_META_IMPL EXTERN
#endif

typedef flecs_vec3_t FlecsVelocity3;
typedef flecs_vec3_t FlecsAngularVelocity3;

extern ECS_COMPONENT_DECLARE(FlecsVelocity3);
extern ECS_COMPONENT_DECLARE(FlecsAngularVelocity3);

#endif
