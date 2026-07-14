#ifndef FLECS_ENGINE_TRANSITION_H
#define FLECS_ENGINE_TRANSITION_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_TRANSITION_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_ENUM(FlecsTimingFunction, {
    FlecsTimingFunctionLinear,
    FlecsTimingFunctionEase,
    FlecsTimingFunctionEaseIn,
    FlecsTimingFunctionEaseOut,
    FlecsTimingFunctionEaseInOut,
});

ECS_STRUCT(FlecsTransitionParams, {
    float duration;
    float delay;
    FlecsTimingFunction timing_function;
});

/* Reflected map<entity, FlecsTransitionParams>. The entity key identifies the
 * component whose value is transitioned. Values produced by the transition
 * runtime are stored in (FlecsTransitionValue, component) pairs. */
typedef ecs_map_t FlecsTransition;

extern ECS_COMPONENT_DECLARE(FlecsTransition);

ECS_STRUCT(FlecsTransitionValue, {
    float value[4];
});

extern ECS_COMPONENT_DECLARE(FlecsTransitionValue);

typedef struct FlecsTransitionState {
    ecs_entity_t entity;
    ecs_entity_t component;
    ecs_ref_t out;
    FlecsTransitionParams params;
    float elapsed;
    int32_t count;
    FlecsTransitionValue from;
    FlecsTransitionValue to;
} FlecsTransitionState;

typedef struct FlecsTransitions {
    ecs_vec_t transitions; /* vector<FlecsTransitionState> */
} FlecsTransitions;

extern ECS_COMPONENT_DECLARE(FlecsTransitions);

/* Ensure an entry in an entity's transition map. Call ecs_modified(...,
 * FlecsTransition) after assigning the returned parameters. */
FlecsTransitionParams* flecs_transition_ensure(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_entity_t component);

/* Return the renderer-facing value for an f32 component. Transition output is
 * always f32, so integer and f64 components must use
 * flecs_transition_value_get instead. */
const void* flecs_transition_get(
    const ecs_world_t *world,
    ecs_entity_t entity,
    ecs_entity_t component);

/* Return the float transition output for a component, or NULL when the
 * component does not have a TransitionValue pair. */
const FlecsTransitionValue* flecs_transition_value_get(
    const ecs_world_t *world,
    ecs_entity_t entity,
    ecs_entity_t component);

#endif
