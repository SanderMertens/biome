#include "../renderer.h"
#include "../shaders/shaders.h"
#include "../../geometry3/geometry3.h"
#include "../gpu_cull.h"
#include "common/common.h"
#include "batches.h"
#include "flecs_engine.h"

#define FLECS_BEVEL_BOX_QUADS    (6)
#define FLECS_BEVEL_BOX_BEVELS   (12)
#define FLECS_BEVEL_BOX_CORNERS  (8)
#define FLECS_BEVEL_BOX_MAX_SEGMENTS (16)

/* [segments][smooth] where smooth index: 0=flat, 1=smooth */
typedef struct {
    flecsEngine_batch_group_t quad_batch;
    flecsEngine_batch_group_t bevel_batches[FLECS_BEVEL_BOX_MAX_SEGMENTS + 1][2];
    flecsEngine_batch_group_t corner_batches[FLECS_BEVEL_BOX_MAX_SEGMENTS + 1][2];
    flecsEngine_batch_t buffers;
} flecsEngine_bevel_box_batch_t;

static void flecsEngine_bevel_box_setInstance(
    FlecsGpuTransform *out,
    const FlecsWorldTransform3 *wt,
    float lc0_x, float lc0_y, float lc0_z,
    float lc1_x, float lc1_y, float lc1_z,
    float lc2_x, float lc2_y, float lc2_z,
    float lp_x, float lp_y, float lp_z)
{
    out->c0.x = wt->m[0][0]*lc0_x + wt->m[1][0]*lc0_y + wt->m[2][0]*lc0_z;
    out->c0.y = wt->m[0][1]*lc0_x + wt->m[1][1]*lc0_y + wt->m[2][1]*lc0_z;
    out->c0.z = wt->m[0][2]*lc0_x + wt->m[1][2]*lc0_y + wt->m[2][2]*lc0_z;

    out->c1.x = wt->m[0][0]*lc1_x + wt->m[1][0]*lc1_y + wt->m[2][0]*lc1_z;
    out->c1.y = wt->m[0][1]*lc1_x + wt->m[1][1]*lc1_y + wt->m[2][1]*lc1_z;
    out->c1.z = wt->m[0][2]*lc1_x + wt->m[1][2]*lc1_y + wt->m[2][2]*lc1_z;

    out->c2.x = wt->m[0][0]*lc2_x + wt->m[1][0]*lc2_y + wt->m[2][0]*lc2_z;
    out->c2.y = wt->m[0][1]*lc2_x + wt->m[1][1]*lc2_y + wt->m[2][1]*lc2_z;
    out->c2.z = wt->m[0][2]*lc2_x + wt->m[1][2]*lc2_y + wt->m[2][2]*lc2_z;

    out->c3.x = wt->m[0][0]*lp_x + wt->m[1][0]*lp_y + wt->m[2][0]*lp_z + wt->m[3][0];
    out->c3.y = wt->m[0][1]*lp_x + wt->m[1][1]*lp_y + wt->m[2][1]*lp_z + wt->m[3][1];
    out->c3.z = wt->m[0][2]*lp_x + wt->m[1][2]*lp_y + wt->m[2][2]*lp_z + wt->m[3][2];
}

static void flecsEngine_bevel_box_generateQuads(
    FlecsGpuTransform *t,
    int32_t base,
    const FlecsWorldTransform3 *wt,
    float w, float h, float d, float r)
{
    float hw = w * 0.5f, hh = h * 0.5f, hd = d * 0.5f;
    float fw = w - 2*r, fh = h - 2*r, fd = d - 2*r;

    flecsEngine_bevel_box_setInstance(&t[base+0], wt,
        fw,0,0, 0,fh,0, 0,0,1, 0,0,hd);
    flecsEngine_bevel_box_setInstance(&t[base+1], wt,
        -fw,0,0, 0,fh,0, 0,0,-1, 0,0,-hd);
    flecsEngine_bevel_box_setInstance(&t[base+2], wt,
        0,0,-fd, 0,fh,0, 1,0,0, hw,0,0);
    flecsEngine_bevel_box_setInstance(&t[base+3], wt,
        0,0,fd, 0,fh,0, -1,0,0, -hw,0,0);
    flecsEngine_bevel_box_setInstance(&t[base+4], wt,
        fw,0,0, 0,0,-fd, 0,1,0, 0,hh,0);
    flecsEngine_bevel_box_setInstance(&t[base+5], wt,
        fw,0,0, 0,0,fd, 0,-1,0, 0,-hh,0);
}

static void flecsEngine_bevel_box_generateBevels(
    FlecsGpuTransform *t,
    int32_t base,
    const FlecsWorldTransform3 *wt,
    float w, float h, float d, float r)
{
    float hw = w*0.5f, hh = h*0.5f, hd = d*0.5f;
    float fw = w - 2*r, fh = h - 2*r, fd = d - 2*r;
    float s = 2 * r;

    flecsEngine_bevel_box_setInstance(&t[base+0], wt,
        fw,0,0, 0,s,0, 0,0,s, 0,hh-r,hd-r);
    flecsEngine_bevel_box_setInstance(&t[base+1], wt,
        -fw,0,0, 0,s,0, 0,0,-s, 0,hh-r,-(hd-r));
    flecsEngine_bevel_box_setInstance(&t[base+2], wt,
        -fw,0,0, 0,-s,0, 0,0,s, 0,-(hh-r),hd-r);
    flecsEngine_bevel_box_setInstance(&t[base+3], wt,
        fw,0,0, 0,-s,0, 0,0,-s, 0,-(hh-r),-(hd-r));

    flecsEngine_bevel_box_setInstance(&t[base+4], wt,
        0,-fh,0, s,0,0, 0,0,s, hw-r,0,hd-r);
    flecsEngine_bevel_box_setInstance(&t[base+5], wt,
        0,fh,0, -s,0,0, 0,0,s, -(hw-r),0,hd-r);
    flecsEngine_bevel_box_setInstance(&t[base+6], wt,
        0,fh,0, s,0,0, 0,0,-s, hw-r,0,-(hd-r));
    flecsEngine_bevel_box_setInstance(&t[base+7], wt,
        0,-fh,0, -s,0,0, 0,0,-s, -(hw-r),0,-(hd-r));

    flecsEngine_bevel_box_setInstance(&t[base+8], wt,
        0,0,-fd, 0,s,0, s,0,0, hw-r,hh-r,0);
    flecsEngine_bevel_box_setInstance(&t[base+9], wt,
        0,0,fd, 0,s,0, -s,0,0, -(hw-r),hh-r,0);
    flecsEngine_bevel_box_setInstance(&t[base+10], wt,
        0,0,fd, 0,-s,0, s,0,0, hw-r,-(hh-r),0);
    flecsEngine_bevel_box_setInstance(&t[base+11], wt,
        0,0,-fd, 0,-s,0, -s,0,0, -(hw-r),-(hh-r),0);
}

static void flecsEngine_bevel_box_generateCorners(
    FlecsGpuTransform *t,
    int32_t base,
    const FlecsWorldTransform3 *wt,
    float w, float h, float d, float r)
{
    float hw = w*0.5f, hh = h*0.5f, hd = d*0.5f;
    float s = 2 * r;

    flecsEngine_bevel_box_setInstance(&t[base+0], wt,
        s,0,0, 0,s,0, 0,0,s, hw-r,hh-r,hd-r);
    flecsEngine_bevel_box_setInstance(&t[base+1], wt,
        s,0,0, 0,-s,0, 0,0,-s, hw-r,-(hh-r),-(hd-r));
    flecsEngine_bevel_box_setInstance(&t[base+2], wt,
        -s,0,0, 0,s,0, 0,0,-s, -(hw-r),hh-r,-(hd-r));
    flecsEngine_bevel_box_setInstance(&t[base+3], wt,
        -s,0,0, 0,-s,0, 0,0,s, -(hw-r),-(hh-r),hd-r);

    flecsEngine_bevel_box_setInstance(&t[base+4], wt,
        0,s,0, s,0,0, 0,0,-s, hw-r,hh-r,-(hd-r));
    flecsEngine_bevel_box_setInstance(&t[base+5], wt,
        0,-s,0, s,0,0, 0,0,s, hw-r,-(hh-r),hd-r);
    flecsEngine_bevel_box_setInstance(&t[base+6], wt,
        0,s,0, -s,0,0, 0,0,s, -(hw-r),hh-r,hd-r);
    flecsEngine_bevel_box_setInstance(&t[base+7], wt,
        0,-s,0, -s,0,0, 0,0,-s, -(hw-r),-(hh-r),-(hd-r));
}

static void flecsEngine_bevel_box_fillGpuMaterial(
    flecsEngine_batch_t *buf,
    int32_t base,
    int32_t count,
    const FlecsEngineImpl *engine,
    const FlecsRgba *color,
    const FlecsPbrMaterial *pbr,
    const FlecsEmissive *emissive)
{
    FlecsGpuMaterial gm = flecsEngine_material_pack(
        engine, color, pbr, emissive, NULL, NULL, NULL);

    for (int32_t i = 0; i < count; i ++) {
        buf->buffers.cpu_materials[base + i] = gm;
    }
}

static void flecsEngine_bevel_box_fillMaterialId(
    flecsEngine_batch_t *buf,
    int32_t base,
    int32_t count,
    const FlecsMaterialId *material_id)
{
    FlecsMaterialId mat = material_id[0];
    for (int32_t i = 0; i < count; i ++) {
        buf->buffers.cpu_material_ids[base + i] = mat;
    }
}

static int32_t flecsEngine_bevel_box_clampSegments(
    int32_t segments)
{
    if (segments < 1) return 1;
    if (segments > FLECS_BEVEL_BOX_MAX_SEGMENTS) {
        return FLECS_BEVEL_BOX_MAX_SEGMENTS;
    }
    return segments;
}

static void flecsEngine_bevel_box_extract(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch)
{
    (void)view_impl;
    flecsEngine_bevel_box_batch_t *ctx = batch->ctx;
    flecsEngine_batch_t *buf = &ctx->buffers;
    bool owns_material_data = (buf->flags & FLECS_BATCH_OWNS_MATERIAL) != 0;

    ctx->quad_batch.view.count = 0;
    for (int32_t s = 1; s <= FLECS_BEVEL_BOX_MAX_SEGMENTS; s ++) {
        ctx->bevel_batches[s][0].view.count = 0;
        ctx->bevel_batches[s][1].view.count = 0;
        ctx->corner_batches[s][0].view.count = 0;
        ctx->corner_batches[s][1].view.count = 0;
    }

    ecs_iter_t it = ecs_query_iter(world, batch->query);
    while (ecs_query_next(&it)) {
        const FlecsBevel *bevel = ecs_field(&it, FlecsBevel, 1);
        for (int32_t i = 0; i < it.count; i ++) {
            int32_t seg = flecsEngine_bevel_box_clampSegments(
                bevel[i].segments);
            int32_t sm = bevel[i].smooth ? 1 : 0;
            ctx->quad_batch.view.count += FLECS_BEVEL_BOX_QUADS;
            ctx->bevel_batches[seg][sm].view.count += FLECS_BEVEL_BOX_BEVELS;
            ctx->corner_batches[seg][sm].view.count += FLECS_BEVEL_BOX_CORNERS;
        }
    }

    /* Compute total and assign offsets into shared buffer */
    int32_t total = ctx->quad_batch.view.count;
    for (int32_t s = 1; s <= FLECS_BEVEL_BOX_MAX_SEGMENTS; s ++) {
        for (int32_t sm = 0; sm < 2; sm ++) {
            total += ctx->bevel_batches[s][sm].view.count;
            total += ctx->corner_batches[s][sm].view.count;
        }
    }

    flecsEngine_batch_ensureCapacity(engine, buf, total);

    {
        int32_t offset = 0;
        ctx->quad_batch.view.offset = offset;
        offset += ctx->quad_batch.view.count;
        for (int32_t s = 1; s <= FLECS_BEVEL_BOX_MAX_SEGMENTS; s ++) {
            for (int32_t sm = 0; sm < 2; sm ++) {
                ctx->bevel_batches[s][sm].view.offset = offset;
                offset += ctx->bevel_batches[s][sm].view.count;
                ctx->corner_batches[s][sm].view.offset = offset;
                offset += ctx->corner_batches[s][sm].view.count;
            }
        }
    }

    /* Pass 2: reset counts and fill instances using offsets */
    int32_t quad_fill = 0;
    int32_t bevel_fill[FLECS_BEVEL_BOX_MAX_SEGMENTS + 1][2];
    int32_t corner_fill[FLECS_BEVEL_BOX_MAX_SEGMENTS + 1][2];
    memset(bevel_fill, 0, sizeof(bevel_fill));
    memset(corner_fill, 0, sizeof(corner_fill));

    it = ecs_query_iter(world, batch->query);
    while (ecs_query_next(&it)) {
        const FlecsBox *box = ecs_field(&it, FlecsBox, 0);
        const FlecsBevel *bevel = ecs_field(&it, FlecsBevel, 1);
        const FlecsWorldTransform3 *wt =
            ecs_field(&it, FlecsWorldTransform3, 2);

        for (int32_t i = 0; i < it.count; i ++) {
            const FlecsBox *actual_box = flecs_transition_get(
                world, it.entities[i], ecs_id(FlecsBox));
            const FlecsBevel *actual_bevel = flecs_transition_get(
                world, it.entities[i], ecs_id(FlecsBevel));
            const FlecsWorldTransform3 *actual_wt = flecs_transition_get(
                world, it.entities[i], ecs_id(FlecsWorldTransform3));
            if (!actual_box) actual_box = &box[i];
            if (!actual_bevel) actual_bevel = &bevel[i];
            if (!actual_wt) actual_wt = &wt[i];
            float w = actual_box->x, h = actual_box->y, d = actual_box->z;
            float r = actual_bevel->radius;
            int32_t seg = flecsEngine_bevel_box_clampSegments(
                actual_bevel->segments);
            int32_t sm = actual_bevel->smooth ? 1 : 0;

            int32_t qi = ctx->quad_batch.view.offset + quad_fill;
            int32_t bi = ctx->bevel_batches[seg][sm].view.offset +
                bevel_fill[seg][sm];
            int32_t ci = ctx->corner_batches[seg][sm].view.offset +
                corner_fill[seg][sm];

            flecsEngine_bevel_box_generateQuads(
                buf->buffers.cpu_transforms, qi, actual_wt, w, h, d, r);
            flecsEngine_bevel_box_generateBevels(
                buf->buffers.cpu_transforms, bi, actual_wt, w, h, d, r);
            flecsEngine_bevel_box_generateCorners(
                buf->buffers.cpu_transforms, ci, actual_wt, w, h, d, r);

            if (owns_material_data) {
                const FlecsRgba *colors =
                    ecs_field(&it, FlecsRgba, 3);
                const FlecsPbrMaterial *pbrs =
                    ecs_field(&it, FlecsPbrMaterial, 4);
                const FlecsEmissive *emissives =
                    ecs_field(&it, FlecsEmissive, 5);

                FlecsRgba color_storage;
                const FlecsRgba *c = flecsEngine_material_resolveRgba(
                    world, it.entities[i],
                    colors ? &colors[i] : NULL, &color_storage);
                const FlecsPbrMaterial *p = flecs_transition_get(
                    world, it.entities[i], ecs_id(FlecsPbrMaterial));
                const FlecsEmissive *e = flecs_transition_get(
                    world, it.entities[i], ecs_id(FlecsEmissive));
                if (!p && pbrs) p = &pbrs[i];
                if (!e && emissives) e = &emissives[i];

                flecsEngine_bevel_box_fillGpuMaterial(
                    buf, qi, FLECS_BEVEL_BOX_QUADS, engine, c, p, e);
                flecsEngine_bevel_box_fillGpuMaterial(
                    buf, bi, FLECS_BEVEL_BOX_BEVELS, engine, c, p, e);
                flecsEngine_bevel_box_fillGpuMaterial(
                    buf, ci, FLECS_BEVEL_BOX_CORNERS, engine, c, p, e);
            } else {
                const FlecsMaterialId *mat =
                    ecs_field(&it, FlecsMaterialId, 3);

                flecsEngine_bevel_box_fillMaterialId(
                    buf, qi, FLECS_BEVEL_BOX_QUADS, mat);
                flecsEngine_bevel_box_fillMaterialId(
                    buf, bi, FLECS_BEVEL_BOX_BEVELS, mat);
                flecsEngine_bevel_box_fillMaterialId(
                    buf, ci, FLECS_BEVEL_BOX_CORNERS, mat);
            }

            quad_fill += FLECS_BEVEL_BOX_QUADS;
            bevel_fill[seg][sm] += FLECS_BEVEL_BOX_BEVELS;
            corner_fill[seg][sm] += FLECS_BEVEL_BOX_CORNERS;
        }
    }

    buf->buffers.count = total;
}

static void flecsEngine_bevel_box_prepareGroup(
    flecsEngine_bevel_box_batch_t *ctx,
    flecsEngine_batch_group_t *g,
    int32_t *group_idx)
{
    if (!g->view.count) {
        g->view.group_idx = -1;
        return;
    }
    g->view.group_idx = (*group_idx) ++;
    flecsEngine_batch_group_prepareArgs(g);
}

static void flecsEngine_bevel_box_upload(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch)
{
    (void)world;
    (void)view_impl;
    flecsEngine_bevel_box_batch_t *ctx = batch->ctx;
    flecsEngine_batch_t *buf = &ctx->buffers;

    /* Count groups and assign indices. Must happen after extract so
     * view.count is stable. */
    int32_t group_idx = 0;
    int32_t max_groups = 1 + FLECS_BEVEL_BOX_MAX_SEGMENTS * 4;
    flecsEngine_batch_ensureGroupCapacity(buf, max_groups);
    buf->buffers.group_count = 0;

    /* First pass: count so prepareArgs gets correct group_count. */
    int32_t counted = 0;
    if (ctx->quad_batch.view.count) counted ++;
    for (int32_t s = 1; s <= FLECS_BEVEL_BOX_MAX_SEGMENTS; s ++) {
        if (ctx->bevel_batches[s][0].view.count) counted ++;
        if (ctx->bevel_batches[s][1].view.count) counted ++;
        if (ctx->corner_batches[s][0].view.count) counted ++;
        if (ctx->corner_batches[s][1].view.count) counted ++;
    }
    buf->buffers.group_count = counted;

    flecsEngine_bevel_box_prepareGroup(ctx, &ctx->quad_batch, &group_idx);
    for (int32_t s = 1; s <= FLECS_BEVEL_BOX_MAX_SEGMENTS; s ++) {
        for (int32_t sm = 0; sm < 2; sm ++) {
            flecsEngine_bevel_box_prepareGroup(
                ctx, &ctx->bevel_batches[s][sm], &group_idx);
            flecsEngine_bevel_box_prepareGroup(
                ctx, &ctx->corner_batches[s][sm], &group_idx);
        }
    }

    flecsEngine_batch_upload(engine, buf);
}

static void flecsEngine_bevel_box_render(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    (void)world;
    (void)view_impl;

    flecsEngine_bevel_box_batch_t *ctx = batch->ctx;
    flecsEngine_batch_bindMaterialGroup(
        (FlecsEngineImpl*)engine, pass, &ctx->buffers);
    flecsEngine_batch_bindInstanceGroup(
        (FlecsEngineImpl*)engine, pass, &ctx->buffers);

    flecsEngine_batch_group_draw(engine, pass, &ctx->quad_batch);
    for (int32_t s = 1; s <= FLECS_BEVEL_BOX_MAX_SEGMENTS; s ++) {
        flecsEngine_batch_group_draw(engine, pass, &ctx->bevel_batches[s][0]);
        flecsEngine_batch_group_draw(engine, pass, &ctx->bevel_batches[s][1]);
        flecsEngine_batch_group_draw(engine, pass, &ctx->corner_batches[s][0]);
        flecsEngine_batch_group_draw(engine, pass, &ctx->corner_batches[s][1]);
    }
}

static void* flecsEngine_bevel_box_getCullBuf(
    const FlecsRenderBatch *batch)
{
    flecsEngine_bevel_box_batch_t *ctx = batch->ctx;
    return &ctx->buffers;
}

static void flecsEngine_bevel_box_free(void *ptr)
{
    flecsEngine_bevel_box_batch_t *ctx = ptr;
    flecsEngine_batch_fini(&ctx->buffers);
    ecs_os_free(ctx);
}

static flecsEngine_bevel_box_batch_t* flecsEngine_bevel_box_createCtx(
    ecs_world_t *world,
    flecsEngine_batch_flags_t flags)
{
    flecsEngine_bevel_box_batch_t *ctx =
        ecs_os_calloc_t(flecsEngine_bevel_box_batch_t);
    flecsEngine_batch_init(&ctx->buffers, flags);

    flecsEngine_batch_group_init(&ctx->quad_batch,
        flecsEngine_quad_getAsset(world), 0);
    ctx->quad_batch.batch = &ctx->buffers;

    for (int32_t s = 1; s <= FLECS_BEVEL_BOX_MAX_SEGMENTS; s ++) {
        flecsEngine_batch_group_init(&ctx->bevel_batches[s][0],
            flecsEngine_bevel_getAssetImpl(world, s, false), 0);
        ctx->bevel_batches[s][0].batch = &ctx->buffers;

        flecsEngine_batch_group_init(&ctx->bevel_batches[s][1],
            flecsEngine_bevel_getAssetImpl(world, s, true), 0);
        ctx->bevel_batches[s][1].batch = &ctx->buffers;

        flecsEngine_batch_group_init(&ctx->corner_batches[s][0],
            flecsEngine_bevelCorner_getAssetImpl(world, s, false), 0);
        ctx->corner_batches[s][0].batch = &ctx->buffers;

        flecsEngine_batch_group_init(&ctx->corner_batches[s][1],
            flecsEngine_bevelCorner_getAssetImpl(world, s, true), 0);
        ctx->corner_batches[s][1].batch = &ctx->buffers;
    }

    return ctx;
}

ecs_entity_t flecsEngine_createBatch_bevel_boxes(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbr(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsBox), .src.id = EcsSelf },
            { .id = ecs_id(FlecsBevel), .src.id = EcsSelf },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf,
                .oper = EcsOptional },
            { .id = ecs_id(FlecsPbrMaterial), .src.id = EcsSelf,
                .oper = EcsOptional },
            { .id = ecs_id(FlecsEmissive), .src.id = EcsSelf,
                .oper = EcsOptional },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp,
                .trav = EcsIsA, .oper = EcsNot },
        },
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsGpuVertexLitUv),
        .extract_callback = flecsEngine_bevel_box_extract,
        .upload_callback = flecsEngine_bevel_box_upload,
        .callback = flecsEngine_bevel_box_render,
        .get_cull_buf = flecsEngine_bevel_box_getCullBuf,
        .ctx = flecsEngine_bevel_box_createCtx(world,
            FLECS_BATCH_OWNS_MATERIAL | FLECS_BATCH_NO_GPU_CULL),
        .free_ctx = flecsEngine_bevel_box_free
    });

    return batch;
}

ecs_entity_t flecsEngine_createBatch_bevel_boxes_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbr(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsBox), .src.id = EcsSelf },
            { .id = ecs_id(FlecsBevel), .src.id = EcsSelf },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp,
                .trav = EcsIsA },
        },
        .cache_kind = EcsQueryCacheAuto
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsGpuVertexLitUv),
        .extract_callback = flecsEngine_bevel_box_extract,
        .upload_callback = flecsEngine_bevel_box_upload,
        .callback = flecsEngine_bevel_box_render,
        .get_cull_buf = flecsEngine_bevel_box_getCullBuf,
        .ctx = flecsEngine_bevel_box_createCtx(world, FLECS_BATCH_NO_GPU_CULL),
        .free_ctx = flecsEngine_bevel_box_free
    });

    return batch;
}
