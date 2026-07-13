#define FLECS_ENGINE_MOVEMENT_IMPL
#include "movement.h"
#include "../../tracy_hooks.h"

ECS_COMPONENT_DECLARE(FlecsVelocity3);
ECS_COMPONENT_DECLARE(FlecsAngularVelocity3);

static void FlecsMove3(ecs_iter_t *it) {
    FLECS_TRACY_ZONE_BEGIN("Move3");
    FlecsPosition3 *position = ecs_field(it, FlecsPosition3, 0);
    const FlecsVelocity3 *velocity = ecs_field(it, FlecsVelocity3, 1);
    float delta_time = it->delta_time;

    for (int32_t i = 0; i < it->count; i ++) {
        position[i].x += velocity[i].x * delta_time;
        position[i].y += velocity[i].y * delta_time;
        position[i].z += velocity[i].z * delta_time;
    }
    FLECS_TRACY_ZONE_END;
}

static void FlecsRotate3(ecs_iter_t *it) {
    FLECS_TRACY_ZONE_BEGIN("Rotate3");
    FlecsRotation3 *rotation = ecs_field(it, FlecsRotation3, 0);
    const FlecsAngularVelocity3 *angular_velocity = ecs_field(
        it, FlecsAngularVelocity3, 1);
    float delta_time = it->delta_time;

    for (int32_t i = 0; i < it->count; i ++) {
        rotation[i].x += angular_velocity[i].x * delta_time;
        rotation[i].y += angular_velocity[i].y * delta_time;
        rotation[i].z += angular_velocity[i].z * delta_time;
    }
    FLECS_TRACY_ZONE_END;
}

void FlecsEngineMovementImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineMovement);

    ecs_set_name_prefix(world, "Flecs");

    ECS_COMPONENT_DEFINE(world, FlecsVelocity3);
    ECS_COMPONENT_DEFINE(world, FlecsAngularVelocity3);

    flecsEngine_registerVec3Type(world, ecs_id(FlecsVelocity3));
    flecsEngine_registerVec3Type(world, ecs_id(FlecsAngularVelocity3));

    ecs_add_pair(world,
        ecs_id(FlecsVelocity3), EcsWith,
        ecs_id(FlecsPosition3));
    ecs_add_pair(world,
        ecs_id(FlecsAngularVelocity3), EcsWith,
        ecs_id(FlecsRotation3));
    ecs_add_pair(world, 
        ecs_id(FlecsVelocity3), EcsWith,
        ecs_id(FlecsDynamicTransform));
    ecs_add_pair(world, 
        ecs_id(FlecsAngularVelocity3), EcsWith,
        ecs_id(FlecsDynamicTransform));

    ECS_SYSTEM(world, FlecsMove3, EcsPostUpdate,
        [inout] flecs.engine.transform3.Position3, [in] Velocity3);
    ECS_SYSTEM(world, FlecsRotate3, EcsPostUpdate,
        [inout] flecs.engine.transform3.Rotation3, [in] AngularVelocity3);
}
