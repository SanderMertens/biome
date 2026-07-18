#include "common.h"
#include <stdlib.h>

static void flecsEngine_batch_bindMaterialGroupForSet(
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *buf,
    const flecsEngine_batch_buffers_t *bb)
{
    WGPUBindGroup group = (buf->flags & FLECS_BATCH_OWNS_MATERIAL)
        ? bb->gpu_material_bind_group
        : flecsEngine_materialBind_ensure(engine);

    if (group) {
        wgpuRenderPassEncoderSetBindGroup(pass, 2, group, 0, NULL);
    }
}

static void flecsEngine_batch_bindInstanceGroupForSet(
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_buffers_t *bb,
    uint32_t binding)
{
    if (bb->gpu_instance_bind_group) {
        wgpuRenderPassEncoderSetBindGroup(
            pass, binding, bb->gpu_instance_bind_group, 0, NULL);
    }
}

void flecsEngine_batch_bindMaterialGroup(
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *buf)
{
    flecsEngine_batch_bindMaterialGroupForSet(engine, pass, buf, &buf->buffers);
}

void flecsEngine_batch_bindInstanceGroup(
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *buf)
{
    (void)engine;
    flecsEngine_batch_bindInstanceGroupForSet(pass, &buf->buffers, 3);
}

void flecsEngine_batch_bindInstanceGroupShadow(
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *buf)
{
    (void)engine;
    flecsEngine_batch_bindInstanceGroupForSet(pass, &buf->buffers, 1);
}

void flecsEngine_batch_bindMaterialGroupStatic(
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *buf)
{
    flecsEngine_batch_bindMaterialGroupForSet(
        engine, pass, buf, &buf->static_buffers);
}

void flecsEngine_batch_bindInstanceGroupStatic(
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *buf)
{
    (void)engine;
    flecsEngine_batch_bindInstanceGroupForSet(pass, &buf->static_buffers, 3);
}

void flecsEngine_batch_bindInstanceGroupShadowStatic(
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *buf)
{
    (void)engine;
    flecsEngine_batch_bindInstanceGroupForSet(pass, &buf->static_buffers, 1);
}

static void flecsEngine_batch_group_drawViewForSet(
    const flecsEngine_batch_group_t *ctx,
    const flecsEngine_batch_buffers_t *bb,
    const flecsEngine_batch_view_t *view,
    const WGPURenderPassEncoder pass,
    int view_idx,
    WGPUBuffer vertex_buffer,
    WGPUBuffer color_buffer)
{
    int32_t src_count = view->count;
    if (!src_count || view->group_idx < 0) {
        return;
    }

    if (!bb->gpu_visible_slots || !bb->gpu_indirect_args) {
        return;
    }

    if (!vertex_buffer || !ctx->mesh.index_buffer || !ctx->mesh.index_count) {
        return;
    }

    int32_t capacity = bb->capacity;
    int32_t group_count = bb->group_count;

    uint64_t slot_offset =
        ((uint64_t)view_idx * (uint64_t)capacity
            + (uint64_t)view->offset) * sizeof(uint32_t);
    uint64_t slot_size = (uint64_t)src_count * sizeof(uint32_t);

    uint64_t args_offset =
        ((uint64_t)view_idx * (uint64_t)group_count
            + (uint64_t)view->group_idx)
        * sizeof(flecsEngine_gpuDrawArgs_t);

    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 0, vertex_buffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 1, bb->gpu_visible_slots, slot_offset, slot_size);
    if (color_buffer) {
        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 2, color_buffer, 0, WGPU_WHOLE_SIZE);
    }

    wgpuRenderPassEncoderSetIndexBuffer(
        pass, ctx->mesh.index_buffer, WGPUIndexFormat_Uint32, 0,
        WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexedIndirect(
        pass, bb->gpu_indirect_args, args_offset);
}

/* The main pass of a VERTEX_COLORS batch requires the group mesh's color
 * buffer; groups without one are skipped (the pipeline declares the extra
 * vertex buffer). Shadow passes use the position-only pipeline and never
 * bind colors. */
static WGPUBuffer flecsEngine_batch_group_colorBuffer(
    const flecsEngine_batch_group_t *ctx,
    bool *skip)
{
    const flecsEngine_batch_t *buf = ctx->batch;
    if (!buf || !(buf->flags & FLECS_BATCH_VERTEX_COLORS)) {
        *skip = false;
        return NULL;
    }
    *skip = ctx->mesh.vertex_color_buffer == NULL;
    return ctx->mesh.vertex_color_buffer;
}

static void flecsEngine_batch_group_drawView(
    const flecsEngine_batch_group_t *ctx,
    const WGPURenderPassEncoder pass,
    int view_idx,
    WGPUBuffer vertex_buffer,
    WGPUBuffer color_buffer)
{
    const flecsEngine_batch_t *buf = ctx->batch;
    if (!buf) return;
    flecsEngine_batch_group_drawViewForSet(
        ctx, &buf->buffers, &ctx->view, pass, view_idx, vertex_buffer,
        color_buffer);
}

static void flecsEngine_batch_group_drawViewStatic(
    const flecsEngine_batch_group_t *ctx,
    const WGPURenderPassEncoder pass,
    int view_idx,
    WGPUBuffer vertex_buffer,
    WGPUBuffer color_buffer)
{
    const flecsEngine_batch_t *buf = ctx->batch;
    if (!buf) return;
    flecsEngine_batch_group_drawViewForSet(
        ctx, &buf->static_buffers, &ctx->static_view,
        pass, view_idx, vertex_buffer, color_buffer);
}

void flecsEngine_batch_group_draw(
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_group_t *ctx)
{
    (void)engine;
    bool skip;
    WGPUBuffer color_buffer = flecsEngine_batch_group_colorBuffer(ctx, &skip);
    if (skip) return;
    flecsEngine_batch_group_drawView(
        ctx, pass, 0, ctx->mesh.vertex_uv_buffer, color_buffer);
}

void flecsEngine_batch_group_drawShadow(
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_group_t *ctx)
{
    (void)engine;
    int cascade = view_impl->shadow.current_cascade;
    flecsEngine_batch_group_drawView(
        ctx, pass, 1 + cascade, ctx->mesh.vertex_buffer, NULL);
}

void flecsEngine_batch_group_drawStatic(
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_group_t *ctx)
{
    (void)engine;
    bool skip;
    WGPUBuffer color_buffer = flecsEngine_batch_group_colorBuffer(ctx, &skip);
    if (skip) return;
    flecsEngine_batch_group_drawViewStatic(
        ctx, pass, 0, ctx->mesh.vertex_uv_buffer, color_buffer);
}

void flecsEngine_batch_group_drawShadowStatic(
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_group_t *ctx)
{
    (void)engine;
    int cascade = view_impl->shadow.current_cascade;
    flecsEngine_batch_group_drawViewStatic(
        ctx, pass, 1 + cascade, ctx->mesh.vertex_buffer, NULL);
}

bool flecsEngine_batch_bindGroupTextures(
    const FlecsEngineImpl *engine,
    const flecsEngine_batch_t *buf,
    const flecsEngine_batch_group_t *ctx,
    WGPURenderPassEncoder pass,
    int8_t *last_bucket)
{
    if (!buf->uses_textures) return true;
    if (ctx->texture_bucket == FLECS_ENGINE_BUCKET_INVALID) return false;

    int8_t b = ctx->texture_bucket;
    WGPUBindGroup bg;
    if (b >= 0 && b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT) {
        bg = engine->textures.bucket_bind_groups[b];
        if (!bg) bg = engine->textures.fallback_bind_group;
    } else {
        bg = engine->textures.fallback_bind_group;
        b = -1;
    }
    if (!bg) return false;

    if (b != *last_bucket) {
        wgpuRenderPassEncoderSetBindGroup(pass, 1, bg, 0, NULL);
        *last_bucket = b;
    }
    return true;
}

typedef struct {
    flecsEngine_batch_group_t *group;
    int32_t slot;
    int32_t visible_index;
    float distance_sq;
    bool is_static;
} flecsEngine_sorted_slot_t;

static int flecsEngine_batch_sortSlotByDistance(
    const void *a,
    const void *b)
{
    const flecsEngine_sorted_slot_t *ia = a;
    const flecsEngine_sorted_slot_t *ib = b;
    /* Back-to-front: farthest first. Break ties by group to cluster
     * same-mesh instances together and reduce GPU state switches. */
    if (ia->distance_sq < ib->distance_sq) return 1;
    if (ia->distance_sq > ib->distance_sq) return -1;
    if (ia->group < ib->group) return -1;
    if (ia->group > ib->group) return 1;
    return 0;
}

void flecsEngine_batch_renderTransparentSorted(
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    WGPURenderPassEncoder pass,
    flecsEngine_batch_t *buf,
    WGPURenderPipeline pipeline,
    flecsEngine_batch_group_t **groups,
    int32_t group_count)
{
    FLECS_TRACY_ZONE_BEGIN("TransparentSortedRender");

    int32_t total = 0;
    for (int32_t i = 0; i < group_count; i ++) {
        if (groups[i]->view.count > 0) {
            total += groups[i]->view.count;
        }
        if (groups[i]->static_view.count > 0) {
            total += groups[i]->static_view.count;
        }
    }

    if (!total || !pipeline) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    flecsEngine_sorted_slot_t *sorted =
        ecs_os_malloc_n(flecsEngine_sorted_slot_t, total);

    float cam_x = view_impl->camera_pos[0];
    float cam_y = view_impl->camera_pos[1];
    float cam_z = view_impl->camera_pos[2];

    int32_t si = 0;
    for (int32_t i = 0; i < group_count; i ++) {
        flecsEngine_batch_group_t *group = groups[i];
        for (int32_t j = 0; j < group->view.count; j ++) {
            int32_t slot = group->view.offset + j;
            const FlecsGpuTransform *t = &buf->buffers.cpu_transforms[slot];
            float dx = t->c3.x - cam_x;
            float dy = t->c3.y - cam_y;
            float dz = t->c3.z - cam_z;
            sorted[si].group = group;
            sorted[si].slot = slot;
            sorted[si].distance_sq = dx * dx + dy * dy + dz * dz;
            sorted[si].is_static = false;
            si ++;
        }
        for (int32_t j = 0; j < group->static_view.count; j ++) {
            int32_t slot = group->static_view.offset + j;
            const FlecsGpuTransform *t =
                &buf->static_buffers.cpu_transforms[slot];
            float dx = t->c3.x - cam_x;
            float dy = t->c3.y - cam_y;
            float dz = t->c3.z - cam_z;
            sorted[si].group = group;
            sorted[si].slot = slot;
            sorted[si].distance_sq = dx * dx + dy * dy + dz * dz;
            sorted[si].is_static = true;
            si ++;
        }
    }

    qsort(sorted, (size_t)total, sizeof(flecsEngine_sorted_slot_t),
        flecsEngine_batch_sortSlotByDistance);

    int32_t dynamic_count = 0;
    int32_t static_count = 0;
    uint32_t *dynamic_slots = ecs_os_malloc_n(uint32_t, total);
    uint32_t *static_slots = ecs_os_malloc_n(uint32_t, total);
    for (int32_t i = 0; i < total; i ++) {
        if (sorted[i].is_static) {
            sorted[i].visible_index = static_count;
            static_slots[static_count ++] = (uint32_t)sorted[i].slot;
        } else {
            sorted[i].visible_index = dynamic_count;
            dynamic_slots[dynamic_count ++] = (uint32_t)sorted[i].slot;
        }
    }
    if (dynamic_count) {
        wgpuQueueWriteBuffer(engine->queue,
            buf->buffers.gpu_visible_slots, 0,
            dynamic_slots, (uint64_t)dynamic_count * sizeof(uint32_t));
    }
    if (static_count) {
        wgpuQueueWriteBuffer(engine->queue,
            buf->static_buffers.gpu_visible_slots, 0,
            static_slots, (uint64_t)static_count * sizeof(uint32_t));
    }
    ecs_os_free(dynamic_slots);
    ecs_os_free(static_slots);

    wgpuRenderPassEncoderSetPipeline(pass, pipeline);

    int8_t last_bucket = -2;
    flecsEngine_batch_group_t *active = NULL;
    bool active_static = false;
    bool bound = false;

    for (int32_t i = 0; i < total; i ++) {
        flecsEngine_batch_group_t *group = sorted[i].group;
        bool is_static = sorted[i].is_static;

        if (!bound || is_static != active_static) {
            if (is_static) {
                flecsEngine_batch_bindMaterialGroupStatic(
                    (FlecsEngineImpl*)engine, pass, buf);
                flecsEngine_batch_bindInstanceGroupStatic(
                    (FlecsEngineImpl*)engine, pass, buf);
            } else {
                flecsEngine_batch_bindMaterialGroup(
                    (FlecsEngineImpl*)engine, pass, buf);
                flecsEngine_batch_bindInstanceGroup(
                    (FlecsEngineImpl*)engine, pass, buf);
            }
            active = NULL;
            active_static = is_static;
            bound = true;
        }

        if (group != active) {
            active = group;

            if (!group->mesh.vertex_uv_buffer || !group->mesh.index_buffer ||
                !group->mesh.index_count ||
                !flecsEngine_batch_bindGroupTextures(
                    engine, buf, group, pass, &last_bucket))
            {
                active = NULL;
                continue;
            }

            wgpuRenderPassEncoderSetVertexBuffer(
                pass, 0, group->mesh.vertex_uv_buffer, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetIndexBuffer(
                pass, group->mesh.index_buffer, WGPUIndexFormat_Uint32,
                0, WGPU_WHOLE_SIZE);
        }
        if (!active) continue;

        flecsEngine_batch_buffers_t *bb = is_static
            ? &buf->static_buffers : &buf->buffers;
        wgpuRenderPassEncoderSetVertexBuffer(
            pass, 1, bb->gpu_visible_slots,
            (uint64_t)sorted[i].visible_index * sizeof(uint32_t),
            sizeof(uint32_t));

        wgpuRenderPassEncoderDrawIndexed(
            pass, (uint32_t)group->mesh.index_count, 1, 0, 0, 0);
    }

    ecs_os_free(sorted);

    FLECS_TRACY_ZONE_END;
}

flecsEngine_batch_t* flecsEngine_batch_getCullBuf(
    const FlecsRenderBatch *batch)
{
    /* Polymorphic over batch ctx type. Mesh batches store the
     * flecsEngine_batch_t directly as ctx; primitive batches embed it. */
    if (!batch || !batch->ctx) {
        return NULL;
    }
    if (batch->get_cull_buf) {
        return batch->get_cull_buf(batch);
    }
    return NULL;
}
