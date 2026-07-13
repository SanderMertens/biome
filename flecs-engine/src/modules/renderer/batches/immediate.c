#include "common/common.h"

ECS_COMPONENT_DECLARE(FlecsDrawQueueImpl);

typedef struct {
    ecs_entity_t prefab;
    int32_t first;
    int32_t count;
} flecsEngine_draw_call_t;

typedef struct FlecsDrawQueueImpl {
    ecs_vec_t calls;        /* flecsEngine_draw_call_t */
    ecs_vec_t instances;    /* flecs_draw_instance_t */
    int64_t frame;
} FlecsDrawQueueImpl;

typedef struct {
    ecs_entity_t key;
    FlecsMesh3Impl mesh;
    FlecsWorldTransform3 local;
    FlecsGpuMaterial material;
    int8_t texture_bucket;
    bool transparent;
} flecsEngine_draw_item_t;

typedef struct {
    ecs_entity_t prefab;
    int32_t start;
    int32_t count;
} flecsEngine_draw_range_t;

typedef struct {
    const FlecsRgba *rgba;
    const FlecsPbrMaterial *pbr;
    const FlecsEmissive *emissive;
    const FlecsTint *tint;
    bool has_material;
    bool has_alpha;
} flecsEngine_draw_override_t;

typedef struct {
    flecsEngine_batch_t buffers;
    ecs_vec_t groups;           /* vec<flecsEngine_batch_group_t*> */
    ecs_map_t group_map;        /* item key -> flecsEngine_batch_group_t* */
    ecs_vec_t items;            /* vec<flecsEngine_draw_item_t> */
    ecs_vec_t ranges;           /* vec<flecsEngine_draw_range_t> */
    ecs_entity_t self_entity;
    bool transparent;
} flecsEngine_immediate_ctx_t;

#define FLECS_DRAW_MAX_DEPTH (8)

void flecsEngine_draw(
    ecs_world_t *world,
    ecs_entity_t prefab,
    const flecs_draw_instance_t *instances,
    int32_t count)
{
    if (!prefab || !instances || count <= 0) {
        return;
    }

    FlecsDrawQueueImpl *q = ecs_singleton_ensure(world, FlecsDrawQueueImpl);
    if (!q) {
        return;
    }

    int64_t frame = ecs_get_world_info(world)->frame_count_total;
    if (q->frame != frame) {
        ecs_vec_clear(&q->calls);
        ecs_vec_clear(&q->instances);
        q->frame = frame;
    }

    flecsEngine_draw_call_t *call = ecs_vec_append_t(
        NULL, &q->calls, flecsEngine_draw_call_t);
    call->prefab = prefab;
    call->first = ecs_vec_count(&q->instances);
    call->count = count;

    ecs_os_memcpy_n(ecs_vec_grow_t(NULL, &q->instances,
        flecs_draw_instance_t, count), instances,
        flecs_draw_instance_t, count);
}

/* Get a component from an entity or, if not present, from its IsA chain.
 * Unlike ecs_get() this also reaches components that are not marked
 * (OnInstantiate, Inherit), which is required to resolve transforms and
 * primitive shapes through prefab variants. Optionally returns the entity
 * that owns the component. */
static const void* flecsEngine_draw_getFrom(
    const ecs_world_t *world,
    ecs_entity_t node,
    ecs_id_t id,
    ecs_entity_t *src_out,
    int32_t depth)
{
    if (ecs_owns_id(world, node, id)) {
        if (src_out) {
            *src_out = node;
        }
        return ecs_get_id(world, node, id);
    }
    if (depth > FLECS_DRAW_MAX_DEPTH) {
        return NULL;
    }

    ecs_entity_t tgt;
    int32_t i = 0;
    while ((tgt = ecs_get_target(world, node, EcsIsA, i ++))) {
        const void *ptr = flecsEngine_draw_getFrom(
            world, tgt, id, src_out, depth + 1);
        if (ptr) {
            return ptr;
        }
    }

    return NULL;
}

#define flecsEngine_draw_get(world, node, T) \
    ((const T*)flecsEngine_draw_getFrom(world, node, ecs_id(T), NULL, 0))

static void flecsEngine_draw_itemMaterial(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t node,
    const flecsEngine_draw_override_t *ov,
    flecsEngine_draw_item_t *item)
{
    const FlecsTint *tint = ov->tint ?
        ov->tint : ecs_get(world, node, FlecsTint);

    item->texture_bucket = FLECS_ENGINE_BUCKET_UNSET;

    if (ov->has_material) {
        const FlecsPbrMaterial *pbr = ov->pbr ?
            ov->pbr : ecs_get(world, node, FlecsPbrMaterial);
        const FlecsEmissive *emissive = ov->emissive ?
            ov->emissive : ecs_get(world, node, FlecsEmissive);
        item->material = flecsEngine_material_pack(
            engine, ov->rgba, pbr, emissive, NULL, NULL, tint);
        return;
    }

    const FlecsMaterialId *mid = ecs_get(world, node, FlecsMaterialId);
    if (mid) {
        item->material = flecsEngine_material_tintShared(
            engine, mid->value, tint);
        if (mid->value < engine->materials.count) {
            int8_t b = engine->materials.cpu_buckets[mid->value];
            if (b >= 0 && b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT) {
                item->texture_bucket = b;
            }
        }
    } else {
        item->material = flecsEngine_material_pack(engine,
            ecs_get(world, node, FlecsRgba),
            ecs_get(world, node, FlecsPbrMaterial),
            ecs_get(world, node, FlecsEmissive),
            NULL, NULL, tint);
    }
}

static void flecsEngine_draw_emitItem(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t node,
    const flecsEngine_draw_override_t *ov,
    mat4 m,
    ecs_vec_t *items)
{
    typedef struct {
        ecs_id_t component;
        int32_t dims;
        size_t asset;
    } flecsEngine_draw_prim_t;

    ecs_entity_t key = 0;
    const FlecsMesh3Impl *mesh = NULL;
    CGLM_ALIGN_MAT mat4 local;
    glm_mat4_copy(m, local);

    mesh = flecsEngine_draw_getFrom(
        world, node, ecs_id(FlecsMesh3Impl), &key, 0);
    if (!mesh) {
        const flecsEngine_draw_prim_t prims[] = {
            { ecs_id(FlecsBox), 3,
                offsetof(FlecsGeometry3Cache, unit_box_asset) },
            { ecs_id(FlecsQuad), 2,
                offsetof(FlecsGeometry3Cache, unit_quad_asset) },
            { ecs_id(FlecsTriangle), 2,
                offsetof(FlecsGeometry3Cache, unit_triangle_asset) },
            { ecs_id(FlecsRightTriangle), 2,
                offsetof(FlecsGeometry3Cache, unit_right_triangle_asset) },
            { ecs_id(FlecsTrianglePrism), 3,
                offsetof(FlecsGeometry3Cache, unit_triangle_prism_asset) },
            { ecs_id(FlecsRightTrianglePrism), 3,
                offsetof(FlecsGeometry3Cache, unit_right_triangle_prism_asset) }
        };

        const FlecsGeometry3Cache *gc = ecs_singleton_get(
            world, FlecsGeometry3Cache);
        if (!gc) {
            return;
        }

        for (int32_t i = 0; i < 6; i ++) {
            const float *v = flecsEngine_draw_getFrom(
                world, node, prims[i].component, NULL, 0);
            if (!v) {
                continue;
            }

            key = *(const ecs_entity_t*)ECS_OFFSET(gc, prims[i].asset);
            if (!key) {
                return;
            }

            mesh = ecs_get(world, key, FlecsMesh3Impl);

            vec3 s = { v[0], v[1], prims[i].dims == 3 ? v[2] : 1.0f };
            glm_scale(local, s);
            break;
        }
    }

    if (!mesh || !mesh->index_buffer || !mesh->index_count) {
        return;
    }

    flecsEngine_draw_item_t *item = ecs_vec_append_t(
        NULL, items, flecsEngine_draw_item_t);
    item->key = key;
    item->mesh = *mesh;
    glm_mat4_copy(local, item->local.m);
    item->transparent = ov->has_alpha ||
        ecs_has_id(world, node, FlecsAlphaBlend);
    flecsEngine_draw_itemMaterial(world, engine, node, ov, item);
}

static void flecsEngine_draw_resolveNode(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t node,
    ecs_entity_t root,
    const flecsEngine_draw_override_t *ov,
    mat4 parent,
    ecs_vec_t *items,
    int32_t depth);

/* Iterate the renderable children of a node: its own children (both ChildOf
 * and the non-fragmenting EcsParent kind used by flat gltf hierarchies), plus
 * the children inherited from its IsA chain. An inherited child is shadowed
 * when the node has an own child with the same name, which mirrors how prefab
 * variants override children on instantiation. */
static void flecsEngine_draw_resolveChildren(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t node,
    ecs_entity_t node_of,
    ecs_entity_t root,
    const flecsEngine_draw_override_t *ov,
    mat4 m,
    ecs_vec_t *items,
    int32_t depth)
{
    if (depth > FLECS_DRAW_MAX_DEPTH) {
        return;
    }

    ecs_iter_t it = ecs_children(world, node_of);
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            ecs_entity_t child = it.entities[i];
            if (node != node_of) {
                const char *name = ecs_get_name(world, child);
                if (name && ecs_lookup_child(world, node, name)) {
                    continue;
                }
            }
            flecsEngine_draw_resolveNode(
                world, engine, child, root, ov, m, items, depth + 1);
        }
    }

    ecs_iter_t pit = ecs_each_id(world, ecs_id(EcsParent));
    while (ecs_each_next(&pit)) {
        const EcsParent *p = ecs_field(&pit, EcsParent, 0);
        for (int32_t i = 0; i < pit.count; i ++) {
            if (p[i].value != node_of) {
                continue;
            }
            flecsEngine_draw_resolveNode(
                world, engine, pit.entities[i], root, ov, m, items,
                depth + 1);
        }
    }

    ecs_entity_t tgt;
    int32_t ti = 0;
    while ((tgt = ecs_get_target(world, node_of, EcsIsA, ti ++))) {
        flecsEngine_draw_resolveChildren(
            world, engine, node, tgt, root, ov, m, items, depth + 1);
    }
}

static void flecsEngine_draw_resolveNode(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t node,
    ecs_entity_t root,
    const flecsEngine_draw_override_t *ov,
    mat4 parent,
    ecs_vec_t *items,
    int32_t depth)
{
    if (depth > FLECS_DRAW_MAX_DEPTH) {
        return;
    }

    CGLM_ALIGN_MAT mat4 m;

    /* The submitted instance transform takes the place of the root node's
     * position and rotation; only its scale is preserved. */
    if (node == root) {
        glm_mat4_copy(parent, m);
    } else {
        const FlecsPosition3 *p = flecsEngine_draw_get(
            world, node, FlecsPosition3);
        if (p) {
            vec3 pos = { p->x, p->y, p->z };
            glm_translate_to(parent, pos, m);
        } else {
            glm_mat4_copy(parent, m);
        }

        const FlecsRotation3 *r = flecsEngine_draw_get(
            world, node, FlecsRotation3);
        if (r) {
            vec3 angles = { r->x, r->y, r->z };
            CGLM_ALIGN_MAT mat4 rot;
            glm_euler_xyz(angles, rot);
            glm_mul_rot(m, rot, m);
        }
    }

    const FlecsScale3 *s = flecsEngine_draw_get(world, node, FlecsScale3);
    if (s) {
        vec3 scale = { s->x, s->y, s->z };
        glm_scale(m, scale);
    }

    flecsEngine_draw_emitItem(world, engine, node, ov, m, items);

    flecsEngine_draw_resolveChildren(
        world, engine, node, node, root, ov, m, items, depth);
}

#define flecsEngine_draw_owned(world, e, T) \
    (ecs_owns(world, e, T) ? ecs_get(world, e, T) : NULL)

static void flecsEngine_draw_resolvePrefab(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t prefab,
    ecs_vec_t *items)
{
    /* Appearance components owned by the submitted (variant) prefab itself
     * override the appearance of the whole resolved object. This makes a
     * plain prefab variant with a color/tint set on it sufficient to render
     * a recolored version of the base prefab (e.g. placement ghosts). */
    flecsEngine_draw_override_t ov = {
        .rgba = flecsEngine_draw_owned(world, prefab, FlecsRgba),
        .pbr = flecsEngine_draw_owned(world, prefab, FlecsPbrMaterial),
        .emissive = flecsEngine_draw_owned(world, prefab, FlecsEmissive),
        .tint = flecsEngine_draw_owned(world, prefab, FlecsTint),
        .has_alpha = ecs_owns_id(world, prefab, FlecsAlphaBlend)
    };
    ov.has_material = ov.rgba || ov.pbr || ov.emissive;

    CGLM_ALIGN_MAT mat4 ident;
    glm_mat4_identity(ident);

    flecsEngine_draw_resolveNode(
        world, engine, prefab, prefab, &ov, ident, items, 0);
}

static const flecsEngine_draw_range_t* flecsEngine_draw_findRange(
    const flecsEngine_immediate_ctx_t *ctx,
    ecs_entity_t prefab)
{
    const flecsEngine_draw_range_t *ranges = ecs_vec_first_t(
        &ctx->ranges, flecsEngine_draw_range_t);
    int32_t count = ecs_vec_count(&ctx->ranges);
    for (int32_t i = 0; i < count; i ++) {
        if (ranges[i].prefab == prefab) {
            return &ranges[i];
        }
    }
    return NULL;
}

static flecsEngine_batch_group_t* flecsEngine_immediate_group(
    flecsEngine_immediate_ctx_t *ctx,
    const flecsEngine_draw_item_t *item)
{
    flecsEngine_batch_group_t *group = ecs_map_get_deref(
        &ctx->group_map, flecsEngine_batch_group_t, (ecs_map_key_t)item->key);
    if (!group) {
        group = flecsEngine_batch_group_create(NULL, (uint64_t)item->key);
        ecs_map_insert_ptr(&ctx->group_map, (ecs_map_key_t)item->key, group);
        ecs_vec_append_t(NULL, &ctx->groups, flecsEngine_batch_group_t*)[0] =
            group;
    }

    group->mesh = item->mesh;
    group->batch = &ctx->buffers;
    return group;
}

static void flecsEngine_immediate_extract(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch)
{
    FLECS_TRACY_ZONE_BEGIN("ImmediateExtract");
    (void)view_impl;

    flecsEngine_immediate_ctx_t *ctx = batch->ctx;
    flecsEngine_batch_t *buf = &ctx->buffers;

    int32_t group_count = ecs_vec_count(&ctx->groups);
    flecsEngine_batch_group_t **groups = ecs_vec_first_t(
        &ctx->groups, flecsEngine_batch_group_t*);
    for (int32_t i = 0; i < group_count; i ++) {
        groups[i]->view.count = 0;
        groups[i]->view.group_idx = -1;
        groups[i]->slot_count = 0;
        groups[i]->texture_bucket = FLECS_ENGINE_BUCKET_UNSET;
    }

    buf->buffers.count = 0;
    buf->buffers.group_count = 0;

    const FlecsDrawQueueImpl *q = ecs_singleton_get(
        world, FlecsDrawQueueImpl);
    if (!q || q->frame != ecs_get_world_info(world)->frame_count_total ||
        !ecs_vec_count(&q->calls))
    {
        FLECS_TRACY_ZONE_END;
        return;
    }

    int32_t call_count = ecs_vec_count(&q->calls);
    const flecsEngine_draw_call_t *calls = ecs_vec_first_t(
        &q->calls, flecsEngine_draw_call_t);
    const flecs_draw_instance_t *instances = ecs_vec_first_t(
        &q->instances, flecs_draw_instance_t);

    ecs_vec_clear(&ctx->items);
    ecs_vec_clear(&ctx->ranges);

    int32_t total = 0;
    for (int32_t c = 0; c < call_count; c ++) {
        if (!flecsEngine_draw_findRange(ctx, calls[c].prefab)) {
            int32_t start = ecs_vec_count(&ctx->items);
            flecsEngine_draw_resolvePrefab(
                world, engine, calls[c].prefab, &ctx->items);
            *ecs_vec_append_t(NULL, &ctx->ranges, flecsEngine_draw_range_t) =
                (flecsEngine_draw_range_t){
                    .prefab = calls[c].prefab,
                    .start = start,
                    .count = ecs_vec_count(&ctx->items) - start
                };
        }

        const flecsEngine_draw_range_t *range = flecsEngine_draw_findRange(
            ctx, calls[c].prefab);
        flecsEngine_draw_item_t *items = ecs_vec_first_t(
            &ctx->items, flecsEngine_draw_item_t);
        for (int32_t i = 0; i < range->count; i ++) {
            flecsEngine_draw_item_t *item = &items[range->start + i];
            if (item->transparent != ctx->transparent) {
                continue;
            }
            flecsEngine_immediate_group(ctx, item)->view.count +=
                calls[c].count;
            total += calls[c].count;
        }
    }

    if (!total) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    group_count = ecs_vec_count(&ctx->groups);
    groups = ecs_vec_first_t(&ctx->groups, flecsEngine_batch_group_t*);

    int32_t offset = 0, group_idx = 0;
    for (int32_t i = 0; i < group_count; i ++) {
        flecsEngine_batch_group_t *group = groups[i];
        if (!group->view.count) {
            continue;
        }
        group->view.offset = offset;
        group->view.group_idx = group_idx ++;
        offset += group->view.count;
    }

    flecsEngine_batch_ensureCapacity(engine, buf, total);
    flecsEngine_batch_ensureGroupCapacity(buf, group_idx);
    buf->buffers.count = total;
    buf->buffers.group_count = group_idx;

    flecsEngine_batch_buffers_t *bb = &buf->buffers;
    flecsEngine_draw_item_t *items = ecs_vec_first_t(
        &ctx->items, flecsEngine_draw_item_t);

    for (int32_t c = 0; c < call_count; c ++) {
        const flecsEngine_draw_range_t *range = flecsEngine_draw_findRange(
            ctx, calls[c].prefab);

        for (int32_t n = 0; n < calls[c].count; n ++) {
            const flecs_draw_instance_t *inst = &instances[calls[c].first + n];

            CGLM_ALIGN_MAT mat4 w;
            vec3 pos = { inst->position.x, inst->position.y,
                inst->position.z };
            glm_translate_make(w, pos);

            if (inst->rotation.x != 0 || inst->rotation.y != 0 ||
                inst->rotation.z != 0)
            {
                vec3 angles = { inst->rotation.x, inst->rotation.y,
                    inst->rotation.z };
                CGLM_ALIGN_MAT mat4 rot;
                glm_euler_xyz(angles, rot);
                glm_mul_rot(w, rot, w);
            }

            if (inst->scale.x != 0 || inst->scale.y != 0 ||
                inst->scale.z != 0)
            {
                vec3 scale = { inst->scale.x, inst->scale.y, inst->scale.z };
                glm_scale(w, scale);
            }

            for (int32_t i = 0; i < range->count; i ++) {
                flecsEngine_draw_item_t *item = &items[range->start + i];
                if (item->transparent != ctx->transparent) {
                    continue;
                }

                flecsEngine_batch_group_t *group =
                    flecsEngine_immediate_group(ctx, item);

                FlecsWorldTransform3 wt;
                glm_mat4_mul(w, item->local.m, wt.m);

                int32_t slot = group->view.offset + group->slot_count ++;
                flecsEngine_batch_transformInstance(
                    &bb->cpu_transforms[slot], &wt, 1.0f, 1.0f, 1.0f);
                flecsEngine_batch_worldAabb(&wt, item->mesh.aabb,
                    &bb->cpu_aabb[slot]);
                bb->cpu_slot_to_group[slot] = (uint32_t)group->view.group_idx;
                bb->cpu_materials[slot] = item->material;

                if (group->texture_bucket == FLECS_ENGINE_BUCKET_UNSET &&
                    item->texture_bucket >= 0)
                {
                    group->texture_bucket = item->texture_bucket;
                }
            }
        }
    }

    for (int32_t i = 0; i < group_count; i ++) {
        if (groups[i]->view.group_idx >= 0) {
            flecsEngine_batch_group_prepareArgs(groups[i]);
        }
    }

    FLECS_TRACY_ZONE_END;
}

static void flecsEngine_immediate_upload(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch)
{
    (void)world;
    (void)view_impl;

    flecsEngine_immediate_ctx_t *ctx = batch->ctx;
    flecsEngine_batch_upload(engine, &ctx->buffers);
}

static void flecsEngine_immediate_render(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    FLECS_TRACY_ZONE_BEGIN("ImmediateRender");
    (void)world;
    (void)view_impl;

    flecsEngine_immediate_ctx_t *ctx = batch->ctx;
    flecsEngine_batch_t *buf = &ctx->buffers;
    if (!buf->buffers.count) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    flecsEngine_batch_bindMaterialGroup((FlecsEngineImpl*)engine, pass, buf);
    flecsEngine_batch_bindInstanceGroup((FlecsEngineImpl*)engine, pass, buf);

    int8_t last_bucket = -1;
    int32_t group_count = ecs_vec_count(&ctx->groups);
    flecsEngine_batch_group_t **groups = ecs_vec_first_t(
        &ctx->groups, flecsEngine_batch_group_t*);
    for (int32_t i = 0; i < group_count; i ++) {
        flecsEngine_batch_group_t *group = groups[i];
        if (group->view.group_idx < 0) {
            continue;
        }
        if (!flecsEngine_batch_bindGroupTextures(
            engine, buf, group, pass, &last_bucket))
        {
            continue;
        }
        flecsEngine_batch_group_draw(engine, pass, group);
    }

    FLECS_TRACY_ZONE_END;
}

static void flecsEngine_immediate_renderShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    FLECS_TRACY_ZONE_BEGIN("ImmediateRenderShadow");
    (void)world;

    flecsEngine_immediate_ctx_t *ctx = batch->ctx;
    flecsEngine_batch_t *buf = &ctx->buffers;
    if (!buf->buffers.count) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    flecsEngine_batch_bindInstanceGroupShadow(
        (FlecsEngineImpl*)engine, pass, buf);

    int32_t group_count = ecs_vec_count(&ctx->groups);
    flecsEngine_batch_group_t **groups = ecs_vec_first_t(
        &ctx->groups, flecsEngine_batch_group_t*);
    for (int32_t i = 0; i < group_count; i ++) {
        flecsEngine_batch_group_t *group = groups[i];
        if (group->view.group_idx < 0) {
            continue;
        }
        flecsEngine_batch_group_drawShadow(engine, view_impl, pass, group);
    }

    FLECS_TRACY_ZONE_END;
}

static void flecsEngine_immediate_renderTransparent(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    flecsEngine_immediate_ctx_t *ctx = batch->ctx;

    const FlecsRenderBatchImpl *self_impl =
        ecs_get(world, ctx->self_entity, FlecsRenderBatchImpl);

    flecsEngine_batch_renderTransparentSorted(engine, view_impl, pass,
        &ctx->buffers, self_impl ? self_impl->pipeline_hdr : NULL,
        ecs_vec_first_t(&ctx->groups, flecsEngine_batch_group_t*),
        ecs_vec_count(&ctx->groups));
}

static void* flecsEngine_immediate_getCullBuf(
    const FlecsRenderBatch *batch)
{
    flecsEngine_immediate_ctx_t *ctx = batch->ctx;
    return &ctx->buffers;
}

static void flecsEngine_immediate_deleteCtx(void *ptr)
{
    flecsEngine_immediate_ctx_t *ctx = ptr;
    int32_t group_count = ecs_vec_count(&ctx->groups);
    flecsEngine_batch_group_t **groups = ecs_vec_first_t(
        &ctx->groups, flecsEngine_batch_group_t*);
    for (int32_t i = 0; i < group_count; i ++) {
        flecsEngine_batch_group_delete(groups[i]);
    }
    ecs_vec_fini_t(NULL, &ctx->groups, flecsEngine_batch_group_t*);
    ecs_map_fini(&ctx->group_map);
    ecs_vec_fini_t(NULL, &ctx->items, flecsEngine_draw_item_t);
    ecs_vec_fini_t(NULL, &ctx->ranges, flecsEngine_draw_range_t);
    flecsEngine_batch_fini(&ctx->buffers);
    ecs_os_free(ctx);
}

static ecs_entity_t flecsEngine_immediate_createBatch(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    bool transparent)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });

    flecsEngine_immediate_ctx_t *ctx =
        ecs_os_calloc_t(flecsEngine_immediate_ctx_t);
    flecsEngine_batch_init(&ctx->buffers, FLECS_BATCH_OWNS_MATERIAL |
        (transparent ? FLECS_BATCH_NO_GPU_CULL : 0));
    ecs_vec_init_t(NULL, &ctx->groups, flecsEngine_batch_group_t*, 0);
    ecs_map_init(&ctx->group_map, NULL);
    ecs_vec_init_t(NULL, &ctx->items, flecsEngine_draw_item_t, 0);
    ecs_vec_init_t(NULL, &ctx->ranges, flecsEngine_draw_range_t, 0);
    ctx->self_entity = batch;
    ctx->transparent = transparent;

    FlecsRenderBatch desc = {
        .shader = flecsEngine_shader_pbr(world),
        .vertex_type = ecs_id(FlecsGpuVertexLitUv),
        .extract_callback = flecsEngine_immediate_extract,
        .upload_callback = flecsEngine_immediate_upload,
        .get_cull_buf = flecsEngine_immediate_getCullBuf,
        .ctx = ctx,
        .free_ctx = flecsEngine_immediate_deleteCtx
    };

    if (transparent) {
        desc.callback = flecsEngine_immediate_renderTransparent;
        desc.depth_test = WGPUCompareFunction_Less;
        desc.cull_mode = WGPUCullMode_None;
        desc.blend = (WGPUBlendState){
            .color = {
                .operation = WGPUBlendOperation_Add,
                .srcFactor = WGPUBlendFactor_SrcAlpha,
                .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha
            },
            .alpha = {
                .operation = WGPUBlendOperation_Add,
                .srcFactor = WGPUBlendFactor_One,
                .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha
            }
        };
        desc.render_after_snapshot = true;
    } else {
        desc.callback = flecsEngine_immediate_render;
        desc.shadow_callback = flecsEngine_immediate_renderShadow;
    }

    ecs_set_ptr(world, batch, FlecsRenderBatch, &desc);
    return batch;
}

ecs_entity_t flecsEngine_createBatch_immediate(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_immediate_createBatch(world, parent, name, false);
}

ecs_entity_t flecsEngine_createBatch_immediateTransparent(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_immediate_createBatch(world, parent, name, true);
}

ECS_DTOR(FlecsDrawQueueImpl, ptr, {
    ecs_vec_fini_t(NULL, &ptr->calls, flecsEngine_draw_call_t);
    ecs_vec_fini_t(NULL, &ptr->instances, flecs_draw_instance_t);
})

void flecsEngine_draw_register(
    ecs_world_t *world)
{
    ecs_set_name_prefix(world, "Flecs");
    ECS_COMPONENT_DEFINE(world, FlecsDrawQueueImpl);

    ecs_set_hooks(world, FlecsDrawQueueImpl, {
        .ctor = flecs_default_ctor,
        .dtor = ecs_dtor(FlecsDrawQueueImpl)
    });

    ecs_singleton_set(world, FlecsDrawQueueImpl, {0});
    FlecsDrawQueueImpl *q = ecs_singleton_ensure(world, FlecsDrawQueueImpl);
    ecs_vec_init_t(NULL, &q->calls, flecsEngine_draw_call_t, 0);
    ecs_vec_init_t(NULL, &q->instances, flecs_draw_instance_t, 0);
}
