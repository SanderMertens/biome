#include <flecs_engine_test.h>

#include <math.h>

typedef struct TransitionV1 {
    float x;
} TransitionV1;

typedef struct TransitionV2 {
    float x, y;
} TransitionV2;

typedef struct TransitionV3 {
    float x, y, z;
} TransitionV3;

typedef struct TransitionD3 {
    double x, y, z;
} TransitionD3;

typedef struct TransitionMixed {
    float x;
    double y;
} TransitionMixed;

typedef struct TransitionI32 {
    int32_t x, y, z, w;
} TransitionI32;

typedef struct TransitionU8 { uint8_t value; } TransitionU8;
typedef struct TransitionU16 { uint16_t value; } TransitionU16;
typedef struct TransitionU32 { uint32_t value; } TransitionU32;
typedef struct TransitionU64 { uint64_t value; } TransitionU64;
typedef struct TransitionI8 { int8_t value; } TransitionI8;
typedef struct TransitionI16 { int16_t value; } TransitionI16;
typedef struct TransitionI64 { int64_t value; } TransitionI64;

typedef struct TransitionV5 {
    float value[5];
} TransitionV5;

static void transition_register_f32(
    ecs_world_t *world,
    ecs_entity_t component,
    int32_t count)
{
    if (count == 1) {
        ecs_struct(world, {
            .entity = component,
            .members = {
                { .name = "x", .type = ecs_id(ecs_f32_t) }
            }
        });
    } else if (count == 2) {
        ecs_struct(world, {
            .entity = component,
            .members = {
                { .name = "x", .type = ecs_id(ecs_f32_t) },
                { .name = "y", .type = ecs_id(ecs_f32_t) }
            }
        });
    } else {
        ecs_struct(world, {
            .entity = component,
            .members = {
                { .name = "x", .type = ecs_id(ecs_f32_t) },
                { .name = "y", .type = ecs_id(ecs_f32_t) },
                { .name = "z", .type = ecs_id(ecs_f32_t) }
            }
        });
    }
}

static void transition_register_primitive(
    ecs_world_t *world,
    ecs_entity_t component,
    ecs_entity_t primitive)
{
    ecs_struct(world, {
        .entity = component,
        .members = {
            { .name = "value", .type = primitive }
        }
    });
}

static void transition_add(
    ecs_world_t *world,
    ecs_entity_t entity,
    ecs_entity_t component,
    float duration,
    float delay,
    FlecsTimingFunction function)
{
    FlecsTransitionParams *p = flecs_transition_ensure(
        world, entity, component);
    *p = (FlecsTransitionParams){
        .duration = duration,
        .delay = delay,
        .timing_function = function
    };
    ecs_modified(world, entity, FlecsTransition);
}

static ecs_entity_t transition_integer_entity(
    ecs_world_t *world,
    ecs_entity_t component,
    ecs_size_t size,
    const void *from,
    const void *to)
{
    ecs_entity_t e = ecs_new(world);
    ecs_set_id(world, e, component, size, from);
    transition_add(world, e, component,
        1.0f, 0, FlecsTimingFunctionLinear);
    ecs_set_id(world, e, component, size, to);
    return e;
}

static float transition_expected(
    FlecsTimingFunction function)
{
    switch (function) {
    case FlecsTimingFunctionEase: return 0.4085f;
    case FlecsTimingFunctionEaseIn: return 0.0935f;
    case FlecsTimingFunctionEaseOut: return 0.3781f;
    case FlecsTimingFunctionEaseInOut: return 0.1292f;
    default: return 0.25f;
    }
}

void Transition_matrix(void) {
    const int32_t component_counts[] = {0, 1, 3};
    const int32_t entity_counts[] = {1, 3};
    const float delays[] = {0.25f, 0.5f, 0.75f};
    const FlecsTimingFunction functions[] = {
        FlecsTimingFunctionLinear,
        FlecsTimingFunctionEase,
        FlecsTimingFunctionEaseIn,
        FlecsTimingFunctionEaseOut,
        FlecsTimingFunctionEaseInOut
    };

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);
    ECS_COMPONENT(world, TransitionV1);
    ECS_COMPONENT(world, TransitionV2);
    ECS_COMPONENT(world, TransitionV3);
    transition_register_f32(world, ecs_id(TransitionV1), 1);
    transition_register_f32(world, ecs_id(TransitionV2), 2);
    transition_register_f32(world, ecs_id(TransitionV3), 3);

    for (int32_t cc = 0; cc < 3; cc ++) {
        for (int32_t ec = 0; ec < 2; ec ++) {
            for (int32_t d = 0; d < 3; d ++) {
                for (int32_t f = 0; f < 5; f ++) {
                    ecs_entity_t entities[3] = {0};
                    for (int32_t e = 0; e < entity_counts[ec]; e ++) {
                        entities[e] = ecs_new(world);
                        ecs_set(world, entities[e], TransitionV1, {0});
                        ecs_set(world, entities[e], TransitionV2, {0, 0});
                        ecs_set(world, entities[e], TransitionV3, {0, 0, 0});
                        ecs_entity_t components[] = {
                            ecs_id(TransitionV1),
                            ecs_id(TransitionV2),
                            ecs_id(TransitionV3)
                        };
                        for (int32_t c = 0; c < component_counts[cc]; c ++) {
                            transition_add(world, entities[e], components[c],
                                1.0f, delays[d], functions[f]);
                        }
                        ecs_set(world, entities[e], TransitionV1, {10});
                        ecs_set(world, entities[e], TransitionV2, {10, 20});
                        ecs_set(world, entities[e], TransitionV3, {10, 20, 30});
                    }

                    ecs_progress(world, 0.5f);
                    float expected = d == 0
                        ? transition_expected(functions[f]) : 0.0f;
                    for (int32_t e = 0; e < entity_counts[ec]; e ++) {
                        const FlecsTransitionValue *v1 =
                            flecs_transition_value_get(
                                world, entities[e], ecs_id(TransitionV1));
                        const FlecsTransitionValue *v2 =
                            flecs_transition_value_get(
                                world, entities[e], ecs_id(TransitionV2));
                        const FlecsTransitionValue *v3 =
                            flecs_transition_value_get(
                                world, entities[e], ecs_id(TransitionV3));
                        if (component_counts[cc] > 0) {
                            test_assert(v1 != NULL);
                            test_assert(fabsf(v1->value[0] -
                                10.0f * expected) < .002f);
                        } else {
                            test_assert(v1 == NULL);
                        }
                        if (component_counts[cc] > 1) {
                            test_assert(v2 != NULL);
                            test_assert(v3 != NULL);
                            test_assert(fabsf(v2->value[0] -
                                10.0f * expected) < .002f);
                            test_assert(fabsf(v2->value[1] -
                                20.0f * expected) < .004f);
                            test_assert(fabsf(v3->value[2] -
                                30.0f * expected) < .006f);
                        } else {
                            test_assert(v2 == NULL);
                            test_assert(v3 == NULL);
                        }
                    }

                    ecs_progress(world, 2.0f);
                    const FlecsTransitions *all = ecs_singleton_get(
                        world, FlecsTransitions);
                    test_assert(all != NULL);
                    test_int(ecs_vec_count(&all->transitions), 0);
                    for (int32_t e = 0; e < entity_counts[ec]; e ++) {
                        ecs_delete(world, entities[e]);
                    }
                }
            }
        }
    }
    ecs_fini(world);
}

void Transition_f64(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);
    ECS_COMPONENT(world, TransitionD3);
    ecs_struct(world, {
        .entity = ecs_id(TransitionD3),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f64_t) },
            { .name = "y", .type = ecs_id(ecs_f64_t) },
            { .name = "z", .type = ecs_id(ecs_f64_t) }
        }
    });

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, TransitionD3, {0, 10, 20});
    transition_add(world, e, ecs_id(TransitionD3),
        1.0f, 0, FlecsTimingFunctionLinear);
    ecs_set(world, e, TransitionD3, {10, 20, 30});
    ecs_progress(world, 0.5f);
    const FlecsTransitionValue *out = flecs_transition_value_get(
        world, e, ecs_id(TransitionD3));
    test_assert(out != NULL);
    test_flt(out->value[0], 5);
    test_flt(out->value[1], 15);
    test_flt(out->value[2], 25);
    ecs_fini(world);
}

void Transition_reject_mixed(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);
    ECS_COMPONENT(world, TransitionMixed);
    ecs_struct(world, {
        .entity = ecs_id(TransitionMixed),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t),
                .offset = offsetof(TransitionMixed, x), .use_offset = true },
            { .name = "y", .type = ecs_id(ecs_f64_t),
                .offset = offsetof(TransitionMixed, y), .use_offset = true }
        }
    });

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, TransitionMixed, {0, 0});
    ecs_log_set_level(-4);
    transition_add(world, e, ecs_id(TransitionMixed),
        1.0f, 0, FlecsTimingFunctionLinear);
    ecs_log_set_level(-1);
    test_assert(!ecs_has_pair(world, e,
        ecs_id(FlecsTransitionValue), ecs_id(TransitionMixed)));
    ecs_fini(world);
}

void Transition_integer(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);
    ECS_COMPONENT(world, TransitionI32);
    ECS_COMPONENT(world, TransitionU8);
    ECS_COMPONENT(world, TransitionU16);
    ECS_COMPONENT(world, TransitionU32);
    ECS_COMPONENT(world, TransitionU64);
    ECS_COMPONENT(world, TransitionI8);
    ECS_COMPONENT(world, TransitionI16);
    ECS_COMPONENT(world, TransitionI64);
    ecs_struct(world, {
        .entity = ecs_id(TransitionI32),
        .members = {
            { .name = "x", .type = ecs_id(ecs_i32_t) },
            { .name = "y", .type = ecs_id(ecs_i32_t) },
            { .name = "z", .type = ecs_id(ecs_i32_t) },
            { .name = "w", .type = ecs_id(ecs_i32_t) }
        }
    });
    transition_register_primitive(
        world, ecs_id(TransitionU8), ecs_id(ecs_u8_t));
    transition_register_primitive(
        world, ecs_id(TransitionU16), ecs_id(ecs_u16_t));
    transition_register_primitive(
        world, ecs_id(TransitionU32), ecs_id(ecs_u32_t));
    transition_register_primitive(
        world, ecs_id(TransitionU64), ecs_id(ecs_u64_t));
    transition_register_primitive(
        world, ecs_id(TransitionI8), ecs_id(ecs_i8_t));
    transition_register_primitive(
        world, ecs_id(TransitionI16), ecs_id(ecs_i16_t));
    transition_register_primitive(
        world, ecs_id(TransitionI64), ecs_id(ecs_i64_t));

    ecs_entity_t rgba_entity = ecs_new(world);
    ecs_set(world, rgba_entity, FlecsRgba, {0, 10, 20, 30});
    transition_add(world, rgba_entity, ecs_id(FlecsRgba),
        1.0f, 0, FlecsTimingFunctionLinear);
    ecs_set(world, rgba_entity, FlecsRgba, {100, 110, 120, 130});

    ecs_entity_t i32_entity = ecs_new(world);
    ecs_set(world, i32_entity, TransitionI32, {-20, 0, 20, 40});
    transition_add(world, i32_entity, ecs_id(TransitionI32),
        1.0f, 0, FlecsTimingFunctionLinear);
    ecs_set(world, i32_entity, TransitionI32, {20, 40, 60, 80});

#define TRANSITION_INTEGER_ENTITY(name, T, C, from_value, to_value) \
    C name##_from = (C)(from_value); \
    C name##_to = (C)(to_value); \
    ecs_entity_t name = transition_integer_entity( \
        world, ecs_id(T), sizeof(C), &name##_from, &name##_to)
    TRANSITION_INTEGER_ENTITY(u8_entity, TransitionU8, uint8_t, 10, 30);
    TRANSITION_INTEGER_ENTITY(u16_entity, TransitionU16, uint16_t, 10, 30);
    TRANSITION_INTEGER_ENTITY(u32_entity, TransitionU32, uint32_t, 10, 30);
    TRANSITION_INTEGER_ENTITY(u64_entity, TransitionU64, uint64_t, 10, 30);
    TRANSITION_INTEGER_ENTITY(i8_entity, TransitionI8, int8_t, -10, 30);
    TRANSITION_INTEGER_ENTITY(i16_entity, TransitionI16, int16_t, -10, 30);
    TRANSITION_INTEGER_ENTITY(i64_entity, TransitionI64, int64_t, -10, 30);
#undef TRANSITION_INTEGER_ENTITY

    ecs_progress(world, 0.5f);
    const FlecsTransitionValue *rgba = flecs_transition_value_get(
        world, rgba_entity, ecs_id(FlecsRgba));
    const FlecsTransitionValue *i32 = flecs_transition_value_get(
        world, i32_entity, ecs_id(TransitionI32));
    test_assert(rgba != NULL);
    test_assert(i32 != NULL);
    test_int(sizeof(FlecsTransitionValue), sizeof(float) * 4);
    test_int(sizeof(FlecsTint), sizeof(flecs_rgba_t));
    test_flt(rgba->value[0], 50);
    test_flt(rgba->value[1], 60);
    test_flt(rgba->value[2], 70);
    test_flt(rgba->value[3], 80);
    test_flt(i32->value[0], 0);
    test_flt(i32->value[1], 20);
    test_flt(i32->value[2], 40);
    test_flt(i32->value[3], 60);

#define TRANSITION_INTEGER_EXPECT(entity, T, expected) \
    do { \
        const FlecsTransitionValue *value = flecs_transition_value_get( \
            world, entity, ecs_id(T)); \
        test_assert(value != NULL); \
        test_flt(value->value[0], expected); \
    } while (0)
    TRANSITION_INTEGER_EXPECT(u8_entity, TransitionU8, 20);
    TRANSITION_INTEGER_EXPECT(u16_entity, TransitionU16, 20);
    TRANSITION_INTEGER_EXPECT(u32_entity, TransitionU32, 20);
    TRANSITION_INTEGER_EXPECT(u64_entity, TransitionU64, 20);
    TRANSITION_INTEGER_EXPECT(i8_entity, TransitionI8, 10);
    TRANSITION_INTEGER_EXPECT(i16_entity, TransitionI16, 10);
    TRANSITION_INTEGER_EXPECT(i64_entity, TransitionI64, 10);
#undef TRANSITION_INTEGER_EXPECT
    ecs_fini(world);
}

void Transition_reject_too_many(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);
    ECS_COMPONENT(world, TransitionV5);
    ecs_struct(world, {
        .entity = ecs_id(TransitionV5),
        .members = {
            { .name = "value", .type = ecs_id(ecs_f32_t), .count = 5 }
        }
    });

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, TransitionV5, {{0, 0, 0, 0, 0}});
    ecs_log_set_level(-4);
    transition_add(world, e, ecs_id(TransitionV5),
        1.0f, 0, FlecsTimingFunctionLinear);
    ecs_log_set_level(-1);
    test_assert(!ecs_has_pair(world, e,
        ecs_id(FlecsTransitionValue), ecs_id(TransitionV5)));
    ecs_fini(world);
}

void Transition_immediate_missing_tint(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);

    /* Immediate-mode prefab traversal uses zero to represent a node for which
     * no Tint source was found. This is the lookup that used to abort while
     * rendering a placement ghost whose mesh children do not have Tint. */
    const FlecsTransitionValue *value = flecs_transition_value_get(
        world, 0, ecs_id(FlecsTint));
    test_assert(value == NULL);
    test_assert(flecs_transition_get(
        world, 0, ecs_id(FlecsTint)) == NULL);

    ecs_fini(world);
}

void Transition_static_transform(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, FlecsPosition3, {0, 0, 0});
    transition_add(world, e, ecs_id(FlecsPosition3),
        1.0f, 0, FlecsTimingFunctionLinear);
    ecs_set(world, e, FlecsPosition3, {10, 20, 30});

    ecs_progress(world, 0.5f);
    const FlecsWorldTransform3 *wt = ecs_get(
        world, e, FlecsWorldTransform3);
    test_assert(wt != NULL);
    test_flt(wt->m[3][0], 5);
    test_flt(wt->m[3][1], 10);
    test_flt(wt->m[3][2], 15);
    ecs_fini(world);
}

void Transition_tint(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);

    ecs_entity_t e = ecs_new(world);
    ecs_set(world, e, FlecsTint, {255, 255, 255, 0});
    transition_add(world, e, ecs_id(FlecsTint),
        1.0f, 0, FlecsTimingFunctionLinear);
    ecs_set(world, e, FlecsTint, {0, 0, 0, 200});

    ecs_progress(world, 0.5f);
    const FlecsTransitionValue *out = flecs_transition_value_get(
        world, e, ecs_id(FlecsTint));
    const FlecsActualTint *actual = ecs_get(world, e, FlecsActualTint);
    test_assert(out != NULL);
    test_assert(actual != NULL);
    test_flt(out->value[0], 127.5f);
    test_flt(out->value[3], 100);
    test_int(actual->r, 128);
    test_int(actual->g, 128);
    test_int(actual->b, 128);
    test_int(actual->a, 100);
    ecs_fini(world);
}

void Transition_childof_tint(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);

    ecs_entity_t base = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, base, FlecsTint, {255, 255, 255, 0});
    transition_add(world, base, ecs_id(FlecsTint),
        0.5f, 0, FlecsTimingFunctionLinear);

    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, base);
    ecs_entity_t mesh_a = ecs_new_w_pair(world, EcsChildOf, instance);
    ecs_entity_t group = ecs_new_w_pair(world, EcsChildOf, instance);
    ecs_entity_t mesh_b = ecs_new_w_pair(world, EcsChildOf, group);
    test_assert(!ecs_owns(world, instance, FlecsTint));

    /* This is the write performed by the power system when the newly placed
     * building is not powered. */
    ecs_set(world, instance, FlecsTint, {0, 0, 0, 230});
    test_assert(ecs_owns(world, instance, FlecsTint));
    test_assert(ecs_owns_id(world, instance,
        ecs_pair(ecs_id(FlecsTransitionValue), ecs_id(FlecsTint))));

    const FlecsTransitions *all = ecs_singleton_get(
        world, FlecsTransitions);
    test_assert(all != NULL);
    test_int(ecs_vec_count(&all->transitions), 1);

    ecs_progress(world, 0.25f);
    const FlecsTransitionValue *out = flecs_transition_value_get(
        world, instance, ecs_id(FlecsTint));
    const FlecsActualTint *actual_a = ecs_get(
        world, mesh_a, FlecsActualTint);
    const FlecsActualTint *actual_b = ecs_get(
        world, mesh_b, FlecsActualTint);
    test_assert(out != NULL);
    test_assert(actual_a != NULL);
    test_assert(actual_b != NULL);
    test_flt(out->value[0], 127.5f);
    test_flt(out->value[3], 115);
    test_int(actual_a->r, 128);
    test_int(actual_a->a, 115);
    test_int(actual_b->r, 128);
    test_int(actual_b->a, 115);
    ecs_fini(world);
}

void Transition_parent_tint(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);

    ecs_entity_t base = ecs_new_w_id(world, EcsPrefab);
    ecs_set(world, base, FlecsTint, {255, 255, 255, 0});
    transition_add(world, base, ecs_id(FlecsTint),
        0.5f, 0, FlecsTimingFunctionLinear);

    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, base);
    ecs_entity_t mesh_a = ecs_new(world);
    ecs_entity_t group = ecs_new(world);
    ecs_entity_t mesh_b = ecs_new(world);
    ecs_set(world, mesh_a, EcsParent, {instance});
    ecs_set(world, group, EcsParent, {instance});
    ecs_set(world, mesh_b, EcsParent, {group});
    test_assert(!ecs_owns(world, instance, FlecsTint));

    ecs_set(world, instance, FlecsTint, {0, 0, 0, 230});
    test_assert(ecs_owns_id(world, instance,
        ecs_pair(ecs_id(FlecsTransitionValue), ecs_id(FlecsTint))));

    ecs_progress(world, 0.25f);
    const FlecsTransitionValue *out = flecs_transition_value_get(
        world, instance, ecs_id(FlecsTint));
    const FlecsActualTint *actual_a = ecs_get(
        world, mesh_a, FlecsActualTint);
    const FlecsActualTint *actual_b = ecs_get(
        world, mesh_b, FlecsActualTint);
    test_assert(out != NULL);
    test_assert(actual_a != NULL);
    test_assert(actual_b != NULL);
    test_flt(out->value[0], 127.5f);
    test_flt(out->value[3], 115);
    test_int(actual_a->r, 128);
    test_int(actual_a->a, 115);
    test_int(actual_b->r, 128);
    test_int(actual_b->a, 115);
    ecs_fini(world);
}
