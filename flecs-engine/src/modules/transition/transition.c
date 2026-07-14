#define FLECS_ENGINE_TRANSITION_IMPL
#include "transition.h"

#include <math.h>

ECS_COMPONENT_DECLARE(FlecsTimingFunction);
ECS_COMPONENT_DECLARE(FlecsTransitionParams);
ECS_COMPONENT_DECLARE(FlecsTransition);
ECS_COMPONENT_DECLARE(FlecsTransitions);
ECS_COMPONENT_DECLARE(FlecsTransitionValue);

static void FlecsTransitions_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    FlecsTransitions *all = ptr;
    for (int32_t i = 0; i < count; i ++) {
        ecs_vec_fini_t(NULL, &all[i].transitions, FlecsTransitionState);
    }
}

static bool flecsEngine_transitionPrimitiveAllowed(
    ecs_primitive_kind_t kind)
{
    switch (kind) {
    case EcsByte:
    case EcsU8:
    case EcsU16:
    case EcsU32:
    case EcsU64:
    case EcsI8:
    case EcsI16:
    case EcsI32:
    case EcsI64:
    case EcsF32:
    case EcsF64:
        return true;
    default:
        return false;
    }
}

static bool flecsEngine_transitionTypeInfo(
    const ecs_world_t *world,
    ecs_entity_t type,
    ecs_primitive_kind_t *primitive,
    ecs_size_t *size,
    int32_t *element_count)
{
    const EcsPrimitive *p = ecs_get(world, type, EcsPrimitive);
    if (p) {
        if (!flecsEngine_transitionPrimitiveAllowed(p->kind)) {
            return false;
        }
        const ecs_type_info_t *ti = ecs_get_type_info(world, type);
        if (!ti) {
            return false;
        }
        if (*primitive && *primitive != p->kind) {
            return false;
        }
        *primitive = p->kind;
        *size = ti->size;
        *element_count = 1;
        return true;
    }

    const EcsArray *a = ecs_get(world, type, EcsArray);
    if (a) {
        ecs_size_t elem_size = 0;
        int32_t elem_count = 0;
        if (!flecsEngine_transitionTypeInfo(
            world, a->type, primitive, &elem_size, &elem_count))
        {
            return false;
        }
        *size = elem_size * a->count;
        *element_count = elem_count * a->count;
        const ecs_type_info_t *ti = ecs_get_type_info(world, type);
        return ti && ti->size == *size && *element_count <= 4;
    }

    const EcsStruct *st = ecs_get(world, type, EcsStruct);
    if (!st) {
        return false;
    }

    int32_t count = ecs_vec_count(&st->members);
    const ecs_member_t *members = ecs_vec_first_t(
        &st->members, ecs_member_t);
    ecs_size_t cursor = 0;
    int32_t total_count = 0;
    for (int32_t i = 0; i < count; i ++) {
        ecs_size_t member_size = 0;
        int32_t member_count = 0;
        if (members[i].offset != (int32_t)cursor ||
            !flecsEngine_transitionTypeInfo(
                world, members[i].type, primitive,
                &member_size, &member_count))
        {
            return false;
        }
        int32_t elem_count = members[i].count ? members[i].count : 1;
        cursor += member_size * elem_count;
        total_count += member_count * elem_count;
        if (total_count > 4) {
            return false;
        }
    }

    const ecs_type_info_t *ti = ecs_get_type_info(world, type);
    if (!ti || ti->size != cursor) {
        return false;
    }
    *size = cursor;
    *element_count = total_count;
    return cursor != 0 && total_count != 0;
}

static bool flecsEngine_transitionValidate(
    const ecs_world_t *world,
    ecs_entity_t component,
    ecs_primitive_kind_t *primitive,
    ecs_size_t *size,
    int32_t *element_count)
{
    *primitive = 0;
    *size = 0;
    *element_count = 0;
    if (flecsEngine_transitionTypeInfo(
        world, component, primitive, size, element_count))
    {
        return true;
    }

    char *path = ecs_get_path(world, component);
    ecs_err("transition component '%s' must be a contiguous vector of only "
        "one numeric primitive type with at most 4 values",
        path ? path : "<invalid>");
    ecs_os_free(path);
    return false;
}

static void flecsEngine_transitionReadValue(
    ecs_primitive_kind_t primitive,
    int32_t count,
    const void *ptr,
    FlecsTransitionValue *out)
{
    *out = (FlecsTransitionValue){{0}};
#define FLECS_TRANSITION_READ(T) \
    do { \
        const T *values = ptr; \
        for (int32_t i = 0; i < count; i ++) { \
            out->value[i] = (float)values[i]; \
        } \
    } while (0)
    switch (primitive) {
    case EcsByte:
    case EcsU8:  FLECS_TRANSITION_READ(uint8_t); break;
    case EcsU16: FLECS_TRANSITION_READ(uint16_t); break;
    case EcsU32: FLECS_TRANSITION_READ(uint32_t); break;
    case EcsU64: FLECS_TRANSITION_READ(uint64_t); break;
    case EcsI8:  FLECS_TRANSITION_READ(int8_t); break;
    case EcsI16: FLECS_TRANSITION_READ(int16_t); break;
    case EcsI32: FLECS_TRANSITION_READ(int32_t); break;
    case EcsI64: FLECS_TRANSITION_READ(int64_t); break;
    case EcsF32: FLECS_TRANSITION_READ(float); break;
    case EcsF64: FLECS_TRANSITION_READ(double); break;
    default: break;
    }
#undef FLECS_TRANSITION_READ
}

static FlecsTransitionParams* flecsEngine_transitionMapGet(
    const FlecsTransition *map,
    ecs_entity_t component)
{
    return map && ecs_map_is_init(map)
        ? ecs_map_get_ptr(map, (ecs_map_key_t)component)
        : NULL;
}

static FlecsTransitionState* flecsEngine_transitionFind(
    FlecsTransitions *all,
    ecs_entity_t entity,
    ecs_entity_t component,
    int32_t *index_out)
{
    int32_t count = ecs_vec_count(&all->transitions);
    FlecsTransitionState *states = ecs_vec_first_t(
        &all->transitions, FlecsTransitionState);
    for (int32_t i = 0; i < count; i ++) {
        if (states[i].entity == entity && states[i].component == component) {
            if (index_out) {
                *index_out = i;
            }
            return &states[i];
        }
    }
    return NULL;
}

static void flecsEngine_transitionRemoveState(
    FlecsTransitions *all,
    int32_t index)
{
    int32_t count = ecs_vec_count(&all->transitions);
    FlecsTransitionState *states = ecs_vec_first_t(
        &all->transitions, FlecsTransitionState);
    if (index != count - 1) {
        states[index] = states[count - 1];
    }
    ecs_vec_remove_last(&all->transitions);
}

static void flecsEngine_transitionStop(
    FlecsTransitions *all,
    ecs_entity_t entity,
    ecs_entity_t component)
{
    int32_t index;
    if (flecsEngine_transitionFind(all, entity, component, &index)) {
        flecsEngine_transitionRemoveState(all, index);
    }
}

static void flecsEngine_transitionSetOutput(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_entity_t component,
    const FlecsTransitionValue *value)
{
    ecs_set_id(world, entity,
        ecs_pair(ecs_id(FlecsTransitionValue), component),
        sizeof(FlecsTransitionValue), value);
}

static void flecsEngine_transitionStart(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_entity_t component,
    const FlecsTransitionParams *params,
    const void *target)
{
    ecs_primitive_kind_t primitive = 0;
    ecs_size_t size = 0;
    int32_t count = 0;
    if (!flecsEngine_transitionValidate(
        world, component, &primitive, &size, &count))
    {
        return;
    }

    FlecsTransitionValue target_value;
    flecsEngine_transitionReadValue(
        primitive, count, target, &target_value);

    ecs_id_t out_id = ecs_pair(ecs_id(FlecsTransitionValue), component);
    const FlecsTransitionValue *current = ecs_get_id(
        world, entity, out_id);
    if (!current) {
        flecsEngine_transitionSetOutput(
            world, entity, component, &target_value);
        return;
    }

    FlecsTransitions *all = ecs_singleton_ensure(world, FlecsTransitions);
    FlecsTransitionState *state = flecsEngine_transitionFind(
        all, entity, component, NULL);
    if (!state) {
        state = ecs_vec_append_t(
            NULL, &all->transitions, FlecsTransitionState);
        ecs_os_zeromem(state);
        state->entity = entity;
        state->component = component;
    }

    state->from = *current;
    state->to = target_value;
    state->count = count;
    state->params = *params;
    state->elapsed = 0;

    /* Make the output owned before creating the ref. It can have been
     * inherited from a prefab. */
    flecsEngine_transitionSetOutput(
        world, entity, component, current);
    state->out = ecs_ref_init_id(world, entity, out_id);

    if (params->duration <= 0 && params->delay <= 0) {
        flecsEngine_transitionSetOutput(
            world, entity, component, &target_value);
        flecsEngine_transitionStop(all, entity, component);
    }
}

static const void* flecsEngine_transitionInheritedValue(
    const ecs_world_t *world,
    ecs_entity_t entity,
    ecs_entity_t component)
{
    for (int32_t i = 0; ; i ++) {
        ecs_entity_t base = ecs_get_target(world, entity, EcsIsA, i);
        if (!base) {
            return NULL;
        }
        const void *value = ecs_get_id(world, base, component);
        if (value) {
            return value;
        }
    }
}

static void FlecsTransition_on_component_changed(
    ecs_iter_t *it)
{
    ecs_entity_t component = it->event_id;
    if (!component || component == ecs_id(FlecsTransition) ||
        component == ecs_id(FlecsTransitions) || ECS_IS_PAIR(component))
    {
        return;
    }

    const ecs_type_info_t *ti = ecs_get_type_info(it->world, component);
    if (!ti) {
        return;
    }

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t entity = it->entities[i];
        const FlecsTransition *map = ecs_get(
            it->world, entity, FlecsTransition);
        FlecsTransitionParams *params =
            flecsEngine_transitionMapGet(map, component);
        if (!params) {
            continue;
        }
        const void *target = it->event == EcsOnRemove
            ? flecsEngine_transitionInheritedValue(
                it->world, entity, component)
            : ecs_get_id(it->world, entity, component);
        if (target) {
            flecsEngine_transitionStart(
                it->world, entity, component, params, target);
        } else if (it->event == EcsOnRemove) {
            FlecsTransitions *all = ecs_singleton_ensure(
                it->world, FlecsTransitions);
            flecsEngine_transitionStop(all, entity, component);
            ecs_remove_pair(
                it->world, entity, ecs_id(FlecsTransitionValue), component);
        }
    }
}

static void flecsEngine_transitionSyncEntity(
    ecs_world_t *world,
    ecs_entity_t entity,
    const FlecsTransition *map)
{
    FlecsTransitions *all = ecs_singleton_ensure(world, FlecsTransitions);
    if (ecs_map_is_init(map)) {
        ecs_map_iter_t mit = ecs_map_iter(map);
        while (ecs_map_next(&mit)) {
            ecs_entity_t component = (ecs_entity_t)ecs_map_key(&mit);
            ecs_primitive_kind_t primitive = 0;
            ecs_size_t size = 0;
            int32_t count = 0;
            if (!flecsEngine_transitionValidate(
                world, component, &primitive, &size, &count))
            {
                continue;
            }

            const void *value = ecs_get_id(world, entity, component);
            ecs_id_t out_id = ecs_pair(
                ecs_id(FlecsTransitionValue), component);
            if (value && !ecs_get_id(world, entity, out_id)) {
                FlecsTransitionValue output;
                flecsEngine_transitionReadValue(
                    primitive, count, value, &output);
                flecsEngine_transitionSetOutput(
                    world, entity, component, &output);
            }
        }
    }

    /* Remove output pairs for entries that disappeared from the map. */
    const ecs_type_t *type = ecs_get_type(world, entity);
    int32_t type_count = type ? type->count : 0;
    ecs_vec_t stale;
    ecs_vec_init_t(NULL, &stale, ecs_id_t, 0);
    for (int32_t i = 0; i < type_count; i ++) {
        ecs_id_t id = type->array[i];
        if (!ECS_IS_PAIR(id) ||
            ecs_pair_first(world, id) != ecs_id(FlecsTransitionValue))
        {
            continue;
        }
        ecs_entity_t component = ecs_pair_second(world, id);
        if (!flecsEngine_transitionMapGet(map, component)) {
            *ecs_vec_append_t(NULL, &stale, ecs_id_t) = id;
            flecsEngine_transitionStop(all, entity, component);
        }
    }
    int32_t stale_count = ecs_vec_count(&stale);
    ecs_id_t *ids = ecs_vec_first_t(&stale, ecs_id_t);
    for (int32_t i = 0; i < stale_count; i ++) {
        ecs_remove_id(world, entity, ids[i]);
    }
    ecs_vec_fini_t(NULL, &stale, ecs_id_t);
}

static void FlecsTransition_on_set(
    ecs_iter_t *it)
{
    FlecsTransition *maps = ecs_field(it, FlecsTransition, 0);
    for (int32_t i = 0; i < it->count; i ++) {
        flecsEngine_transitionSyncEntity(
            it->world, it->entities[i], &maps[i]);
    }
}

static void FlecsTransition_on_remove(
    ecs_iter_t *it)
{
    const ecs_world_t *world = ecs_get_world(it->world);
    if (ecs_is_fini(world)) {
        return;
    }

    FlecsTransitions *all = ecs_singleton_get_mut(
        it->world, FlecsTransitions);
    if (!all) {
        return;
    }

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t entity = it->entities[i];
        int32_t s = 0;
        while (s < ecs_vec_count(&all->transitions)) {
            FlecsTransitionState *states = ecs_vec_first_t(
                &all->transitions, FlecsTransitionState);
            if (states[s].entity == entity) {
                flecsEngine_transitionRemoveState(all, s);
            } else {
                s ++;
            }
        }

        const ecs_type_t *type = ecs_get_type(it->world, entity);
        int32_t type_count = type ? type->count : 0;
        ecs_vec_t ids;
        ecs_vec_init_t(NULL, &ids, ecs_id_t, 0);
        for (int32_t t = 0; t < type_count; t ++) {
            ecs_id_t id = type->array[t];
            if (ECS_IS_PAIR(id) &&
                ecs_pair_first(it->world, id) ==
                    ecs_id(FlecsTransitionValue))
            {
                *ecs_vec_append_t(NULL, &ids, ecs_id_t) = id;
            }
        }
        int32_t id_count = ecs_vec_count(&ids);
        ecs_id_t *remove_ids = ecs_vec_first_t(&ids, ecs_id_t);
        for (int32_t t = 0; t < id_count; t ++) {
            ecs_remove_id(it->world, entity, remove_ids[t]);
        }
        ecs_vec_fini_t(NULL, &ids, ecs_id_t);
    }
}

static float flecsEngine_transitionBezierCoord(
    float t,
    float p1,
    float p2)
{
    float u = 1.0f - t;
    return 3.0f * u * u * t * p1 +
        3.0f * u * t * t * p2 + t * t * t;
}

static float flecsEngine_transitionBezier(
    float x,
    float x1,
    float y1,
    float x2,
    float y2)
{
    float lo = 0.0f, hi = 1.0f;
    for (int32_t i = 0; i < 16; i ++) {
        float t = (lo + hi) * 0.5f;
        if (flecsEngine_transitionBezierCoord(t, x1, x2) < x) {
            lo = t;
        } else {
            hi = t;
        }
    }
    return flecsEngine_transitionBezierCoord((lo + hi) * 0.5f, y1, y2);
}

static float flecsEngine_transitionEase(
    FlecsTimingFunction function,
    float t)
{
    switch (function) {
    case FlecsTimingFunctionEase:
        return flecsEngine_transitionBezier(t, .25f, .1f, .25f, 1.0f);
    case FlecsTimingFunctionEaseIn:
        return flecsEngine_transitionBezier(t, .42f, 0, 1.0f, 1.0f);
    case FlecsTimingFunctionEaseOut:
        return flecsEngine_transitionBezier(t, 0, 0, .58f, 1.0f);
    case FlecsTimingFunctionEaseInOut:
        return flecsEngine_transitionBezier(t, .42f, 0, .58f, 1.0f);
    default:
        return t;
    }
}

static void FlecsTransitionUpdate(
    ecs_iter_t *it)
{
    FlecsTransitions *all = ecs_field(it, FlecsTransitions, 0);
    int32_t i = 0;
    while (i < ecs_vec_count(&all->transitions)) {
        FlecsTransitionState *states = ecs_vec_first_t(
            &all->transitions, FlecsTransitionState);
        FlecsTransitionState *state = &states[i];
        const ecs_type_info_t *ti = ecs_get_type_info(
            it->world, state->component);
        ecs_primitive_kind_t primitive = 0;
        ecs_size_t size = 0;
        int32_t count = 0;
        if (!ti || !ecs_is_alive(it->world, state->entity) ||
            !flecsEngine_transitionTypeInfo(
                it->world, state->component,
                &primitive, &size, &count))
        {
            flecsEngine_transitionRemoveState(all, i);
            continue;
        }

        state->elapsed += it->delta_time;
        float active = state->elapsed - state->params.delay;
        if (active < 0) {
            i ++;
            continue;
        }

        float t = state->params.duration > 0
            ? active / state->params.duration
            : 1.0f;
        if (t > 1.0f) {
            t = 1.0f;
        }
        float u = flecsEngine_transitionEase(
            state->params.timing_function, t);
        FlecsTransitionValue *out = ecs_ref_get_id(
            ecs_get_world(it->world), &state->out,
            ecs_pair(ecs_id(FlecsTransitionValue), state->component));
        if (!out) {
            flecsEngine_transitionRemoveState(all, i);
            continue;
        }

        for (int32_t e = 0; e < state->count; e ++) {
            out->value[e] = state->from.value[e] +
                (state->to.value[e] - state->from.value[e]) * u;
        }
        ecs_modified_id(it->world, state->entity,
            ecs_pair(ecs_id(FlecsTransitionValue), state->component));

        if (t >= 1.0f) {
            flecsEngine_transitionRemoveState(all, i);
        } else {
            i ++;
        }
    }
}

FlecsTransitionParams* flecs_transition_ensure(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_entity_t component)
{
    FlecsTransition *map = ecs_ensure(world, entity, FlecsTransition);
    ecs_map_init_if(map, NULL);
    FlecsTransitionParams *result = ecs_map_ensure_alloc_t(
        map, FlecsTransitionParams, (ecs_map_key_t)component);
    return result;
}

const void* flecs_transition_get(
    const ecs_world_t *world,
    ecs_entity_t entity,
    ecs_entity_t component)
{
    if (!entity || !component) {
        return NULL;
    }
    const FlecsTransitionValue *out = flecs_transition_value_get(
        world, entity, component);
    return out ? out->value : ecs_get_id(world, entity, component);
}

const FlecsTransitionValue* flecs_transition_value_get(
    const ecs_world_t *world,
    ecs_entity_t entity,
    ecs_entity_t component)
{
    if (!entity || !component) {
        return NULL;
    }
    return ecs_get_id(world, entity,
        ecs_pair(ecs_id(FlecsTransitionValue), component));
}

void FlecsEngineTransitionImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineTransition);
    ecs_set_name_prefix(world, "Flecs");

    ECS_META_COMPONENT(world, FlecsTimingFunction);
    ECS_META_COMPONENT(world, FlecsTransitionParams);
    ECS_META_COMPONENT(world, FlecsTransitionValue);
    ECS_COMPONENT_DEFINE(world, FlecsTransitions);

    ecs_id(FlecsTransition) = ecs_map_type(world, {
        .entity = ecs_entity(world, { .name = "Transition" }),
        .key_type = ecs_id(ecs_entity_t),
        .type = ecs_id(FlecsTransitionParams)
    });

    ecs_set_hooks(world, FlecsTransitions, {
        .ctor = flecs_default_ctor,
        .dtor = FlecsTransitions_dtor
    });
    ecs_singleton_add(world, FlecsTransitions);

    ecs_add_pair(world, ecs_id(FlecsTransition),
        EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(FlecsTransitionValue),
        EcsOnInstantiate, EcsInherit);

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TransitionConfigSet" }),
        .query.terms = {{ .id = ecs_id(FlecsTransition) }},
        .events = { EcsOnSet },
        .callback = FlecsTransition_on_set
    });
    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TransitionConfigRemove" }),
        .query.terms = {{ .id = ecs_id(FlecsTransition) }},
        .events = { EcsOnRemove },
        .callback = FlecsTransition_on_remove
    });
    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "TransitionComponentSet" }),
        .query.terms = {{ .id = EcsAny }},
        .events = { EcsOnSet, EcsOnRemove },
        .callback = FlecsTransition_on_component_changed
    });

    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "UpdateTransitions",
            .add = ecs_ids(ecs_dependson(EcsPreStore))
        }),
        .query.terms = {{ .id = ecs_id(FlecsTransitions) }},
        .callback = FlecsTransitionUpdate
    });
}
