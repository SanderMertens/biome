#include <math.h>

#define FLECS_ENGINE_TRANSFORM3_IMPL
#include "transform3.h"
#include "../renderer/render_batch.h"
#include "../../tracy_hooks.h"

ECS_COMPONENT_DECLARE(FlecsPosition3);
ECS_COMPONENT_DECLARE(FlecsRotation3);
ECS_COMPONENT_DECLARE(FlecsScale3);
ECS_COMPONENT_DECLARE(FlecsLookAt);
ECS_TAG_DECLARE(FlecsDynamicTransform);
ECS_TAG_DECLARE(FlecsManualTransform);

typedef struct {
    ecs_query_t *q_childof;
    ecs_query_t *q_parent;
} flecs_transform3_queries_t;

static void flecsEngine_transform3_queriesFree(void *ptr) {
    ecs_os_free(ptr);
}

static void flecsEngine_transform3_rotationAndScale(
    ecs_iter_t *it,
    FlecsWorldTransform3 *t) 
{
    for (int32_t i = 0; i < it->count; i ++) {
        const FlecsRotation3 *r = flecs_transition_get(
            it->world, it->entities[i], ecs_id(FlecsRotation3));
        const FlecsScale3 *s = flecs_transition_get(
            it->world, it->entities[i], ecs_id(FlecsScale3));
        if (r) {
            CGLM_ALIGN_MAT mat4 rot;
            glm_euler_xyz(*(vec3*)r, rot);
            glm_mul_rot(t[i].m, rot, t[i].m);
        }
        if (s) {
            glm_scale(t[i].m, *(vec3*)s);
        }
    }
}

static bool flecsEngine_transform3_childOf(ecs_iter_t *it) {
    bool has_results = false;

    while (ecs_query_next(it)) {
        FlecsWorldTransform3 *t = ecs_field(it, FlecsWorldTransform3, 0);
        FlecsWorldTransform3 *t_parent = ecs_field(it, FlecsWorldTransform3, 4);
        int i;

        for (i = 0; i < it->count; i ++) {
            const FlecsPosition3 *p = flecs_transition_get(
                it->world, it->entities[i], ecs_id(FlecsPosition3));
            if (!t_parent) {
                glm_translate_make(t[i].m, *(vec3*)p);
            } else {
                glm_translate_to(t_parent[0].m, *(vec3*)p, t[i].m);
            }
        }

        flecsEngine_transform3_rotationAndScale(it, t);

        has_results = true;
    }

    return has_results;
}

static bool flecsEngine_transform3_parent(ecs_iter_t *it) {
    bool has_results = false;
    ecs_world_t *world = it->world;

    while (ecs_query_next(it)) {
        FlecsWorldTransform3 *t = ecs_field(it, FlecsWorldTransform3, 0);
        EcsParent *parents = ecs_field(it, EcsParent, 4);
        int i;

        for (i = 0; i < it->count; i ++) {
            ecs_entity_t parent = parents[i].value;
            const FlecsPosition3 *p = flecs_transition_get(
                world, it->entities[i], ecs_id(FlecsPosition3));
            const FlecsWorldTransform3 *t_parent = ecs_get_mut(
                world, parent, FlecsWorldTransform3);

            while (!t_parent) {
                parent = ecs_get_parent(world, parent);
                if (!parent) {
                    break;
                }
                t_parent = ecs_get_mut(world, parent, FlecsWorldTransform3);
            }

            if (!t_parent) {
                glm_translate_make((vec4*)t[i].m, *(vec3*)p);
            } else {
                glm_translate_to(
                    (vec4*)t_parent[0].m, *(vec3*)p, t[i].m);
            }
        }

        flecsEngine_transform3_rotationAndScale(it, t);

        has_results = true;
    }

    return has_results;
}

static void flecsEngine_transform3_computeSingle(
    ecs_world_t *world,
    ecs_entity_t entity)
{
    FlecsWorldTransform3 *t = ecs_get_mut(world, entity, FlecsWorldTransform3);
    const FlecsPosition3 *p = flecs_transition_get(
        world, entity, ecs_id(FlecsPosition3));
    if (!t || !p) return;

    /* Find parent transform via ChildOf hierarchy */
    const FlecsWorldTransform3 *t_parent = NULL;
    ecs_entity_t parent = ecs_get_parent(world, entity);
    if (parent) {
        t_parent = ecs_get(world, parent, FlecsWorldTransform3);
    }

    /* Check EcsParent component */
    if (!t_parent) {
        const EcsParent *ep = ecs_get(world, entity, EcsParent);
        if (ep) {
            parent = ep->value;
            t_parent = ecs_get(world, parent, FlecsWorldTransform3);
            while (!t_parent) {
                parent = ecs_get_parent(world, parent);
                if (!parent) break;
                t_parent = ecs_get(world, parent, FlecsWorldTransform3);
            }
        }
    }

    /* Translation */
    if (!t_parent) {
        glm_translate_make(t->m, *(vec3*)p);
    } else {
        glm_translate_to((vec4*)t_parent->m, *(vec3*)p, t->m);
    }

    /* Rotation */
    const FlecsRotation3 *r = flecs_transition_get(
        world, entity, ecs_id(FlecsRotation3));
    if (r) {
        CGLM_ALIGN_MAT mat4 rot;
        glm_euler_xyz(*(vec3*)r, rot);
        if (!t_parent) {
            glm_vec3_copy(rot[0], t->m[0]);
            glm_vec3_copy(rot[1], t->m[1]);
            glm_vec3_copy(rot[2], t->m[2]);
        } else {
            glm_mul_rot(t->m, rot, t->m);
        }
    }

    /* Scale */
    const FlecsScale3 *s = flecs_transition_get(
        world, entity, ecs_id(FlecsScale3));
    if (s) {
        glm_scale(t->m, *(vec3*)s);
    }
}

static void flecsEngine_transform3_propagateChildren(
    ecs_world_t *world,
    ecs_entity_t entity)
{
    ecs_iter_t child_it = ecs_children(world, entity);
    while (ecs_children_next(&child_it)) {
        for (int i = 0; i < child_it.count; i ++) {
            ecs_entity_t child = child_it.entities[i];
            if (ecs_has(world, child, FlecsWorldTransform3)) {
                flecsEngine_transform3_computeSingle(world, child);
                flecsEngine_bufferSlot_markChanged(world, child);
                flecsEngine_transform3_propagateChildren(world, child);
            }
        }
    }
}

static void FlecsTransform3OnSet(ecs_iter_t *it) {
    ecs_world_t *world = it->world;
    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        flecsEngine_transform3_computeSingle(world, e);
        flecsEngine_bufferSlot_markChanged(world, e);
        flecsEngine_transform3_propagateChildren(world, e);
    }
}

static void flecsEngine_transform3_groupBits(
    ecs_query_t *q,
    uint64_t bits[4])
{
    ecs_map_iter_t kit = ecs_map_iter(ecs_query_get_groups(q));
    while (ecs_map_next(&kit)) {
        uint64_t group_id = ecs_map_key(&kit);
        if (group_id < 256) {
            bits[group_id >> 6] |= 1llu << (group_id & 63);
        }
    }
}

static void FlecsTransform3(ecs_iter_t *it) {
    FLECS_TRACY_ZONE_BEGIN("Transform3");
    ecs_world_t *world = it->world;
    flecs_transform3_queries_t *ctx = it->ctx;

    /* Depth groups must be processed in order (parents before children),
     * but iterating all 256 possible depths costs two iterator setups per
     * empty depth. Only visit depths that are populated in either query. */
    uint64_t childof_bits[4] = {0};
    uint64_t parent_bits[4] = {0};
    flecsEngine_transform3_groupBits(ctx->q_childof, childof_bits);
    flecsEngine_transform3_groupBits(ctx->q_parent, parent_bits);

    for (uint64_t depth = 0; depth < 256; depth ++) {
        uint64_t bit = 1llu << (depth & 63);
        if (childof_bits[depth >> 6] & bit) {
            ecs_iter_t qit = ecs_query_iter(world, ctx->q_childof);
            ecs_iter_set_group(&qit, depth);
            flecsEngine_transform3_childOf(&qit);
        }
        if (parent_bits[depth >> 6] & bit) {
            ecs_iter_t qit = ecs_query_iter(world, ctx->q_parent);
            ecs_iter_set_group(&qit, depth);
            flecsEngine_transform3_parent(&qit);
        }
    }

    FLECS_TRACY_ZONE_END;
}

static void FlecsRotationFromLookAt(
    ecs_iter_t *it)
{
    const FlecsPosition3 *p = ecs_field(it, FlecsPosition3, 0);
    const FlecsLookAt *lookat = ecs_field(it, FlecsLookAt, 1);
    FlecsRotation3 *r = ecs_field(it, FlecsRotation3, 2);

    for (int32_t i = 0; i < it->count; i ++) {
        vec3 forward = {
            lookat[i].x - p[i].x,
            lookat[i].y - p[i].y,
            lookat[i].z - p[i].z
        };

        float len = glm_vec3_norm(forward);
        if (len > 0.0f) {
            glm_vec3_scale(forward, 1.0f / len, forward);
            r[i].x = asinf(glm_clamp(forward[1], -1.0f, 1.0f));
            r[i].y = atan2f(forward[0], forward[2]);
            r[i].z = 0.0f;
        }
    }
}

static void PropagateDynamicTransform(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_add_id(world, it->entities[i], FlecsDynamicTransform);
    }
}

void FlecsEngineTransform3Import(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineTransform3);

    ecs_set_name_prefix(world, "Flecs");
    
    ECS_COMPONENT_DEFINE(world, FlecsPosition3);
    ECS_COMPONENT_DEFINE(world, FlecsRotation3);
    ECS_COMPONENT_DEFINE(world, FlecsScale3);
    ECS_COMPONENT_DEFINE(world, FlecsLookAt);
    ECS_META_COMPONENT(world, FlecsWorldTransform3);
    ECS_META_COMPONENT(world, FlecsAABB);
    ECS_TAG_DEFINE(world, FlecsDynamicTransform);
    ECS_TAG_DEFINE(world, FlecsManualTransform);

    flecsEngine_registerVec3Type(world, ecs_id(FlecsPosition3));
    flecsEngine_registerVec3Type(world, ecs_id(FlecsRotation3));
    flecsEngine_registerVec3Type(world, ecs_id(FlecsScale3));
    flecsEngine_registerVec3Type(world, ecs_id(FlecsLookAt));

    ecs_add_pair(world, ecs_id(FlecsPosition3), EcsWith, ecs_id(FlecsWorldTransform3));
    ecs_add_pair(world, ecs_id(FlecsRotation3), EcsWith, ecs_id(FlecsWorldTransform3));
    ecs_add_pair(world, ecs_id(FlecsScale3),    EcsWith, ecs_id(FlecsWorldTransform3));
    ecs_add_pair(world, ecs_id(FlecsRotation3), EcsWith, ecs_id(FlecsPosition3));
    ecs_add_pair(world, ecs_id(FlecsScale3),    EcsWith, ecs_id(FlecsPosition3));
    ecs_add_pair(world, ecs_id(FlecsLookAt),    EcsWith, ecs_id(FlecsRotation3));
    ecs_add_pair(world, ecs_id(FlecsWorldTransform3), EcsWith, ecs_id(FlecsAABB));

    ecs_query_desc_t q_childof = {
        .entity = ecs_entity(world, { .name = "Transform3ChildOf" }),
        .terms = {{
            .id = ecs_id(FlecsWorldTransform3),
            .inout = EcsOut,
        }, {
            .id = ecs_id(FlecsPosition3),
            .inout = EcsIn
        }, {
            .id = ecs_id(FlecsRotation3),
            .inout = EcsIn,
            .oper = EcsOptional
        }, {
            .id = ecs_id(FlecsScale3),
            .inout = EcsIn,
            .oper = EcsOptional
        }, {
            .id = ecs_id(FlecsWorldTransform3),
            .inout = EcsIn,
            .oper = EcsOptional,
            .src.id = EcsCascade
        }, {
            .id = ecs_id(EcsParent),
            .oper = EcsNot
        }, {
            .id = ecs_id(FlecsDynamicTransform),
        }, {
            .id = ecs_id(FlecsManualTransform),
            .oper = EcsNot
        }},
        .cache_kind = EcsQueryCacheAuto
    };

    ecs_query_desc_t q_parent = {
        .entity = ecs_entity(world, { .name = "Transform3Parent" }),
        .terms = {{ 
            .id = ecs_id(FlecsWorldTransform3),
            .inout = EcsOut,
        }, {
            .id = ecs_id(FlecsPosition3),
            .inout = EcsIn
        }, {
            .id = ecs_id(FlecsRotation3),
            .inout = EcsIn,
            .oper = EcsOptional
        }, {
            .id = ecs_id(FlecsScale3),
            .inout = EcsIn,
            .oper = EcsOptional
        }, {
            .id = ecs_id(EcsParent),
            .inout = EcsIn
        }, {
            .id = ecs_id(FlecsDynamicTransform),
        }, {
            .id = ecs_id(FlecsManualTransform),
            .oper = EcsNot
        }},
        .group_by = EcsParentDepth,
        .cache_kind = EcsQueryCacheAuto
    };

    q_parent.group_by = EcsParentDepth;

    flecs_transform3_queries_t *ctx = ecs_os_malloc_t(flecs_transform3_queries_t);
    ctx->q_childof = ecs_query_init(world, &q_childof);
    ctx->q_parent = ecs_query_init(world, &q_parent);

    ECS_SYSTEM(world, FlecsRotationFromLookAt, EcsPostUpdate,
        [in]     flecs.engine.transform3.Position3,
        [in]     flecs.engine.transform3.LookAt,
        [out]    flecs.engine.transform3.Rotation3);

    ecs_system(world, {
        .entity = ecs_entity(world, {
            .name = "Transform3",
        }),
        .phase = EcsPreStore,
        .run = FlecsTransform3,
        .ctx = ctx,
        .ctx_free = flecsEngine_transform3_queriesFree
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "PropagateDynamicTransform" }),
        .query.terms = {
            { .id = FlecsDynamicTransform, .src.id = EcsUp },
            { .id = FlecsDynamicTransform, .oper = EcsNot }
        },
        .events = {EcsOnAdd},
        .callback = PropagateDynamicTransform,
        .yield_existing = true
    });

    /* Observers for static (non-dynamic) entities. Recompute WorldTransform
     * and propagate to children whenever an input component is set. */
    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "Transform3OnSetPosition" }),
        .query.terms = {
            { .id = ecs_id(FlecsPosition3), .src.id = EcsSelf },
            { .id = FlecsDynamicTransform, .oper = EcsNot },
            { .id = FlecsManualTransform, .oper = EcsNot }
        },
        .events = { EcsOnSet },
        .yield_existing = true,
        .callback = FlecsTransform3OnSet
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "Transform3OnSetRotation" }),
        .query.terms = {
            { .id = ecs_id(FlecsRotation3), .src.id = EcsSelf },
            { .id = FlecsDynamicTransform, .oper = EcsNot },
            { .id = FlecsManualTransform, .oper = EcsNot }
        },
        .events = { EcsOnSet },
        .yield_existing = true,
        .callback = FlecsTransform3OnSet
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "Transform3OnSetScale" }),
        .query.terms = {
            { .id = ecs_id(FlecsScale3), .src.id = EcsSelf },
            { .id = FlecsDynamicTransform, .oper = EcsNot },
            { .id = FlecsManualTransform, .oper = EcsNot }
        },
        .events = { EcsOnSet },
        .yield_existing = true,
        .callback = FlecsTransform3OnSet
    });

    /* Transition outputs change independently from their front-facing
     * components. Keep static world transforms in sync with those values. */
    ecs_observer(world, {
        .entity = ecs_entity(world, {
            .name = "Transform3OnTransitionPosition" }),
        .query.terms = {
            { .id = ecs_pair(ecs_id(FlecsTransitionValue),
                ecs_id(FlecsPosition3)), .src.id = EcsSelf },
            { .id = FlecsDynamicTransform, .oper = EcsNot },
            { .id = FlecsManualTransform, .oper = EcsNot }
        },
        .events = { EcsOnSet },
        .callback = FlecsTransform3OnSet
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, {
            .name = "Transform3OnTransitionRotation" }),
        .query.terms = {
            { .id = ecs_pair(ecs_id(FlecsTransitionValue),
                ecs_id(FlecsRotation3)), .src.id = EcsSelf },
            { .id = FlecsDynamicTransform, .oper = EcsNot },
            { .id = FlecsManualTransform, .oper = EcsNot }
        },
        .events = { EcsOnSet },
        .callback = FlecsTransform3OnSet
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, {
            .name = "Transform3OnTransitionScale" }),
        .query.terms = {
            { .id = ecs_pair(ecs_id(FlecsTransitionValue),
                ecs_id(FlecsScale3)), .src.id = EcsSelf },
            { .id = FlecsDynamicTransform, .oper = EcsNot },
            { .id = FlecsManualTransform, .oper = EcsNot }
        },
        .events = { EcsOnSet },
        .callback = FlecsTransform3OnSet
    });
}
