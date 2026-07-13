#include "common.h"
#include <stdlib.h>

static void flecsEngine_extract_applyScaleAabb(
    FlecsAABB *aabb,
    const float *scale_xyz)
{
    aabb->min[0] *= scale_xyz[0];
    aabb->min[1] *= scale_xyz[1];
    aabb->min[2] *= scale_xyz[2];
    aabb->max[0] *= scale_xyz[0];
    aabb->max[1] *= scale_xyz[1];
    aabb->max[2] *= scale_xyz[2];
}

static void flecsEngine_extract_writeStaticEntry(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *buf,
    flecsEngine_batch_group_t *ctx,
    int32_t slot,
    int32_t i,
    const FlecsWorldTransform3 *wt,
    const void *scale_data,
    ecs_size_t scale_size,
    flecsEngine_primitive_scale_t scale,
    flecsEngine_primitive_scale_aabb_t scale_aabb,
    const FlecsMaterialId *material_id,
    const FlecsRgba *colors,
    const FlecsPbrMaterial *materials,
    const FlecsEmissive *emissives,
    const FlecsTransmission *transmissions,
    const FlecsActualTint *tints)
{
    flecsEngine_batch_buffers_t *bb = &buf->static_buffers;

    FlecsAABB aabb = ctx->mesh.aabb;
    if (scale_aabb) {
        scale_aabb(&aabb,
            scale_data ? ECS_ELEM(scale_data, scale_size, i) : NULL, 1);
    }
    flecsEngine_batch_worldAabb(&wt[i], aabb, &bb->cpu_aabb[slot]);

    bb->cpu_slot_to_group[slot] = (uint32_t)ctx->static_view.group_idx;

    vec3 s = {1, 1, 1};
    if (scale_data) {
        scale(ECS_ELEM(scale_data, scale_size, i), s);
    }
    flecsEngine_batch_transformInstance(
        &bb->cpu_transforms[slot], &wt[i], s[0], s[1], s[2]);

    if (buf->flags & FLECS_BATCH_TINT) {
        bb->cpu_materials[slot] = flecsEngine_material_tintShared(
            engine,
            material_id ? material_id[0].value : 0,
            tints ? &tints[i] : NULL);
    } else if (buf->flags & FLECS_BATCH_OWNS_MATERIAL) {
        bb->cpu_materials[slot] = flecsEngine_material_pack(
            engine,
            colors ? &colors[i] : NULL,
            materials ? &materials[i] : NULL,
            emissives ? &emissives[i] : NULL,
            transmissions ? &transmissions[i] : NULL,
            NULL,
            NULL);
    } else if (material_id) {
        bb->cpu_material_ids[slot] = material_id[0];
    }
}

static void flecsEngine_batch_group_extractTable(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_group_t *ctx,
    flecsEngine_primitive_scale_t scale,
    flecsEngine_primitive_scale_aabb_t scale_aabb,
    ecs_size_t scale_size,
    ecs_iter_t *it)
{
    flecsEngine_batch_t *buf = ctx->batch;

    const void *scale_data = scale ? ecs_field_w_size(it, scale_size, 0) : NULL;
    const FlecsWorldTransform3 *wt = ecs_field(it, FlecsWorldTransform3, 1);
    bool owns_material = (buf->flags & FLECS_BATCH_OWNS_MATERIAL) != 0;
    const FlecsMaterialId *material_id = NULL;
    const FlecsRgba *colors = NULL;
    const FlecsPbrMaterial *materials = NULL;
    const FlecsEmissive *emissives = NULL;
    const FlecsTransmission *transmissions = NULL;
    const FlecsActualTint *tints = NULL;
    if (buf->flags & FLECS_BATCH_TINT) {
        material_id = ecs_field(it, FlecsMaterialId, 3);
        tints = ecs_field(it, FlecsActualTint, 4);
    } else if (owns_material) {
        colors = ecs_field(it, FlecsRgba, 3);
        materials = ecs_field(it, FlecsPbrMaterial, 4);
        emissives = ecs_field(it, FlecsEmissive, 5);
        if (buf->flags & FLECS_BATCH_OWNS_TRANSMISSION) {
            transmissions = ecs_field(it, FlecsTransmission, 6);
        }
    } else {
        material_id = ecs_field(it, FlecsMaterialId, 3);
    }

    if (material_id && ctx->texture_bucket != FLECS_ENGINE_BUCKET_INVALID) {
        uint32_t mid = material_id[0].value;
        if (mid < engine->materials.count) {
            ctx->first_material_id = mid;
            ctx->has_first_material_id = true;
            int8_t b = engine->materials.cpu_buckets[mid];
            if (b >= 0 && b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT) {
                if (ctx->texture_bucket == FLECS_ENGINE_BUCKET_UNSET) {
                    ctx->texture_bucket = b;
                } else if (ctx->texture_bucket != b) {
                    char *gname = ecs_get_path(it->world,
                        (ecs_entity_t)ctx->group_id);
                    ecs_err(
                        "render group '%s' references textures from "
                        "multiple buckets (%d and %d): a single mesh "
                        "must use textures of a consistent size/kind",
                        gname, (int)ctx->texture_bucket, (int)b);
                    ecs_os_free(gname);
                    ctx->texture_bucket = FLECS_ENGINE_BUCKET_INVALID;
                }
            }
        }
    }

    bool is_static = false;
    if (buf->flags & FLECS_BATCH_TRACK_STATIC) {
        is_static = !ecs_table_has_id(
            ecs_get_world(it->world), it->table, FlecsDynamicTransform);
    }

    if (is_static) {
        int32_t nfree = ecs_vec_count(&buf->free_slots);
        int32_t need = buf->static_buffers.count + (it->count - nfree);
        if (need < buf->static_buffers.count) need = buf->static_buffers.count;
        if (need > buf->static_buffers.capacity) {
            flecsEngine_batch_ensureStaticCapacity(engine, buf, need);
        }

        for (int32_t i = 0; i < it->count; i ++) {
            ecs_entity_t e = it->entities[i];
            if (ecs_map_get(&ctx->changed_set, (ecs_map_key_t)e)) {
                continue;
            }
            FlecsBufferSlot *existing = ecs_get_mut(
                it->world, e, FlecsBufferSlot);
            if (existing) {
                if (existing->group == ctx) {
                    continue;
                }

                /* Keep the old slot alive until its destination batch is
                 * extracting the replacement. Releasing and overwriting the
                 * slot here makes the handoff atomic with respect to the
                 * remainder of the render frame. */
                flecsEngine_bufferSlot_release(it->world, e, existing);
            }

            int32_t slot;
            int32_t *fs = ecs_vec_first_t(&buf->free_slots, int32_t);
            if (nfree > 0) {
                slot = fs[nfree - 1];
                ecs_vec_remove_last(&buf->free_slots);
                nfree --;
            } else {
                slot = buf->static_buffers.count ++;
            }

            flecsEngine_extract_writeStaticEntry(
                engine, buf, ctx, slot, i, wt, scale_data, scale_size,
                scale, scale_aabb, material_id,
                colors, materials, emissives, transmissions, tints);

            ctx->slot_count ++;

            ecs_map_ensure(&ctx->changed_set, (ecs_map_key_t)e);
            *ecs_vec_append_t(NULL, &ctx->changed, ecs_entity_t) = e;
            *ecs_vec_append_t(NULL, &ctx->changed_slots, int32_t) = slot;

            if (existing) {
                existing->group = ctx;
                existing->slot = slot;
            } else {
                ecs_set(it->world, e, FlecsBufferSlot, {
                    .group = ctx, .slot = slot
                });
            }
        }
        return;
    }

    int32_t base = ctx->view.offset + ctx->view.count;
    int32_t dst = base;

    FlecsAABB mesh_aabb = ctx->mesh.aabb;

    /* If not enough capacity, count for resize */
    if ((dst + it->count) > buf->buffers.capacity) {
        ctx->view.count += it->count;
        return;
    }

    /* Build world AABBs into the padded GPU buffer. Apply optional primitive
     * scale transformation to a copy of the mesh aabb per entity. */
    FlecsAABB batch_aabb_tmp[256];
    int32_t remaining = it->count;
    int32_t src = 0;
    while (remaining > 0) {
        int32_t n = remaining > 256 ? 256 : remaining;
        for (int32_t i = 0; i < n; i ++) {
            batch_aabb_tmp[i] = mesh_aabb;
        }
        if (scale_aabb) {
            scale_aabb(batch_aabb_tmp,
                scale_data ? ECS_ELEM(scale_data, scale_size, src) : NULL, n);
        }
        for (int32_t i = 0; i < n; i ++) {
            flecsEngine_batch_worldAabb(&wt[src + i], batch_aabb_tmp[i],
                &buf->buffers.cpu_aabb[dst + i]);
        }
        src += n;
        dst += n;
        remaining -= n;
    }

    /* Fill slot_to_group */
    uint32_t g = (uint32_t)ctx->view.group_idx;
    for (int32_t i = 0; i < it->count; i ++) {
        buf->buffers.cpu_slot_to_group[base + i] = g;
    }

    dst = base;
    if (!owns_material) {
        for (int32_t i = 0; i < it->count; i ++) {
            vec3 s = {1, 1, 1};
            if (scale_data) {
                scale(ECS_ELEM(scale_data, scale_size, i), s);
            }

            flecsEngine_batch_transformInstance(
                &buf->buffers.cpu_transforms[dst], &wt[i], s[0], s[1], s[2]);

            buf->buffers.cpu_material_ids[dst] = material_id[0];

            dst ++;
        }
    } else {
        for (int32_t i = 0; i < it->count; i ++) {
            vec3 s = {1, 1, 1};
            if (scale_data) {
                scale(ECS_ELEM(scale_data, scale_size, i), s);
            }

            flecsEngine_batch_transformInstance(
                &buf->buffers.cpu_transforms[dst], &wt[i], s[0], s[1], s[2]);

            if (buf->flags & FLECS_BATCH_TINT) {
                buf->buffers.cpu_materials[dst] =
                    flecsEngine_material_tintShared(
                        engine,
                        material_id ? material_id[0].value : 0,
                        tints ? &tints[i] : NULL);
            } else {
                buf->buffers.cpu_materials[dst] = flecsEngine_material_pack(
                    engine,
                    colors ? &colors[i] : NULL,
                    materials ? &materials[i] : NULL,
                    emissives ? &emissives[i] : NULL,
                    transmissions ? &transmissions[i] : NULL,
                    NULL,
                    NULL);
            }

            dst ++;
        }
    }

    ctx->view.count += (dst - base);
    (void)scale_aabb;
}

void flecsEngine_batch_group_extract(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    flecsEngine_batch_group_t *ctx,
    flecsEngine_primitive_scale_t scale,
    flecsEngine_primitive_scale_aabb_t scale_aabb,
    ecs_size_t scale_size)
{
    FLECS_TRACY_ZONE_BEGIN("ExtractGroup");

    ctx->view.count = 0;

    if (ctx->resolved_bucket_version != engine->textures.bucket_version) {
        ctx->texture_bucket = FLECS_ENGINE_BUCKET_UNSET;
        ctx->resolved_bucket_version = engine->textures.bucket_version;

        /* Re-resolve eagerly from the previously-seen material id so static
         * groups remain valid even when they currently have no entities. */
        if (ctx->has_first_material_id &&
            ctx->first_material_id < engine->materials.count)
        {
            int8_t b =
                engine->materials.cpu_buckets[ctx->first_material_id];
            if (b >= 0 && b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT) {
                ctx->texture_bucket = b;
            }
        }
    }

    ecs_iter_t it = ecs_query_iter(world, batch->query);
    ecs_iter_set_group(&it, ctx->group_id);
    while (ecs_query_next(&it)) {
        flecsEngine_batch_group_extractTable(
            engine, ctx, scale, scale_aabb, scale_size, &it);
    }

    FLECS_TRACY_ZONE_END;
}

void flecsEngine_batch_dynamicExtract_begin(
    flecsEngine_batch_dynamicExtract_t *s,
    flecsEngine_batch_t *shared)
{
    s->cap = shared->buffers.capacity;
    s->group_cap = shared->buffers.group_capacity;
    s->prev_gc = shared->buffers.group_count;
    s->total = 0;
    s->group_idx = 0;
}

void flecsEngine_batch_dynamicExtract_group(
    flecsEngine_batch_dynamicExtract_t *s,
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    flecsEngine_batch_group_t *ctx,
    flecsEngine_primitive_scale_t scale,
    flecsEngine_primitive_scale_aabb_t scale_aabb,
    ecs_size_t scale_size)
{
    if (!ctx->mesh.index_buffer || !ctx->mesh.index_count) {
        ctx->view.count = 0;
        ctx->view.group_idx = -1;
        ctx->static_view.count = 0;
        ctx->static_view.group_idx = -1;
        ecs_os_zeromem(&ctx->mesh);
        return;
    }

    ctx->view.offset = s->total;
    ctx->view.group_idx = s->group_idx;
    flecsEngine_batch_group_extract(
        world, engine, batch, ctx, scale, scale_aabb, scale_size);

    if (s->group_idx < s->group_cap) {
        flecsEngine_batch_group_prepareArgs(ctx);
    }

    s->total += ctx->view.count;
    s->group_idx ++;
}

bool flecsEngine_batch_dynamicExtract_commit(
    flecsEngine_batch_dynamicExtract_t *s,
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *shared)
{
    bool need_redo = false;
    if (s->total > s->cap) {
        flecsEngine_batch_ensureCapacity(engine, shared, s->total);
        need_redo = true;
    }
    if (s->group_idx > s->group_cap) {
        flecsEngine_batch_ensureGroupCapacity(shared, s->group_idx);
        need_redo = true;
    }
    if (s->group_idx != s->prev_gc) {
        shared->buffers.group_count = s->group_idx;
        need_redo = true;
    }

    if (need_redo) {
        return true;
    }

    shared->buffers.count = s->total;
    return false;
}

void flecsEngine_batch_staticExtract_begin(
    flecsEngine_batch_staticExtract_t *s,
    flecsEngine_batch_t *shared)
{
    s->group_cap = shared->static_buffers.group_capacity;
    s->prev_gc = shared->static_buffers.group_count;
    s->static_offset = 0;
}

void flecsEngine_batch_staticExtract_group(
    flecsEngine_batch_staticExtract_t *s,
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    flecsEngine_batch_group_t *ctx)
{
    int32_t sc = ctx->slot_count;
    if (!sc || !ctx->mesh.index_buffer || !ctx->mesh.index_count) {
        ctx->static_view.count = 0;
        ctx->static_view.offset = 0;
        ctx->static_view.group_idx = -1;
        return;
    }

    /* The stable index is what a slot's slot_to_group stores; allocate it
     * once and keep it for the group's lifetime so already-written slots
     * never point at the wrong group. */
    int32_t g = ctx->static_stable_idx;
    if (g < 0) {
        g = flecsEngine_batch_allocStaticGroupIdx(ctx->batch);
        ctx->static_stable_idx = g;
    }

    ctx->static_view.count = sc;
    ctx->static_view.offset = s->static_offset;
    ctx->static_view.group_idx = g;

    if (g < s->group_cap) {
        flecsEngine_batch_group_applyChanges(world, engine, ctx);
        flecsEngine_batch_group_prepareStaticArgs(ctx);
    }

    s->static_offset += sc;
}

bool flecsEngine_batch_staticExtract_commit(
    flecsEngine_batch_staticExtract_t *s,
    flecsEngine_batch_t *shared)
{
    /* The group_info/indirect_args arrays are indexed by stable group index,
     * so their stride is the high-water mark of assigned indices. */
    int32_t stride = shared->static_group_high;
    bool need_redo = false;
    if (stride > s->group_cap) {
        flecsEngine_batch_ensureStaticGroupCapacity(shared, stride);
        need_redo = true;
    }
    if (stride != s->prev_gc) {
        shared->static_buffers.group_count = stride;
        need_redo = true;
    }
    return need_redo;
}
