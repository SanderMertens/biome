#include "../renderer.h"
#include "../shaders/shaders.h"
#include "../../geometry3/geometry3.h"
#include "batches.h"
#include "flecs_engine.h"

typedef struct {
    flecsEngine_primitive_batch_group_t group;
    flecsEngine_batch_t buffers;
    ecs_entity_t self_entity;
} flecsEngine_primitive_ctx_t;

static void* flecsEngine_primitive_createCtx(
    ecs_world_t *world,
    ecs_entity_t self_entity,
    const FlecsMesh3Impl *mesh,
    flecsEngine_batch_flags_t flags,
    ecs_entity_t component,
    flecsEngine_primitive_scale_t scale_callback,
    flecsEngine_primitive_scale_aabb_t scale_aabb)
{
    flecsEngine_primitive_ctx_t *ctx =
        ecs_os_calloc_t(flecsEngine_primitive_ctx_t);
    flecsEngine_batch_init(&ctx->buffers, flags);
    flecsEngine_batch_group_init(&ctx->group.group, mesh, 0);
    ctx->group.group.batch = &ctx->buffers;
    ctx->group.component_size = component
        ? flecsEngine_type_sizeof(world, component) : 0;
    ctx->group.scale_callback = scale_callback;
    ctx->group.scale_aabb = scale_aabb;
    ctx->group.group.scale_component = component;
    ctx->group.group.scale_callback = scale_callback;
    ctx->group.group.scale_aabb = scale_aabb;
    ctx->self_entity = self_entity;
    return ctx;
}

static void flecsEngine_primitive_deleteCtx(void *ptr)
{
    flecsEngine_primitive_ctx_t *ctx = ptr;
    flecsEngine_batch_group_fini(&ctx->group.group);
    flecsEngine_batch_fini(&ctx->buffers);
    ecs_os_free(ctx);
}

static void flecsEngine_primitive_extract(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch)
{
    (void)view_impl;
    flecsEngine_primitive_ctx_t *pctx = batch->ctx;
    flecsEngine_batch_group_t *ctx = &pctx->group.group;
    flecsEngine_batch_t *buf = ctx->batch;

    flecsEngine_batch_dynamicExtract_t dynamic;
    do {
        flecsEngine_batch_dynamicExtract_begin(&dynamic, buf);
        flecsEngine_batch_dynamicExtract_group(
            &dynamic, world, engine, batch, ctx,
            pctx->group.scale_callback, pctx->group.scale_aabb,
            pctx->group.component_size);
    } while (flecsEngine_batch_dynamicExtract_commit(
        &dynamic, engine, buf));

    flecsEngine_batch_staticExtract_t static_extract;
    do {
        flecsEngine_batch_staticExtract_begin(&static_extract, buf);
        flecsEngine_batch_staticExtract_group(
            &static_extract, world, engine, ctx);
    } while (flecsEngine_batch_staticExtract_commit(
        &static_extract, buf));
}

static void flecsEngine_primitive_upload(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch)
{
    (void)world;
    (void)view_impl;
    flecsEngine_primitive_ctx_t *pctx = batch->ctx;
    flecsEngine_batch_upload(engine, &pctx->buffers);
    flecsEngine_batch_group_t *group = &pctx->group.group;
    if (group->static_view.group_idx >= 0) {
        flecsEngine_batch_group_t *groups[] = {group};
        flecsEngine_batch_uploadStatic(
            engine, &pctx->buffers, groups, 1);
    }
}

static void* flecsEngine_primitive_getCullBuf(
    const FlecsRenderBatch *batch)
{
    flecsEngine_primitive_ctx_t *pctx = batch->ctx;
    return &pctx->buffers;
}

static void flecsEngine_primitive_render(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    (void)world;
    (void)view_impl;

    flecsEngine_primitive_ctx_t *pctx = batch->ctx;
    flecsEngine_batch_bindMaterialGroup(
        (FlecsEngineImpl*)engine, pass, &pctx->buffers);
    flecsEngine_batch_bindInstanceGroup(
        (FlecsEngineImpl*)engine, pass, &pctx->buffers);

    flecsEngine_batch_group_draw(engine, pass, &pctx->group.group);

    if (pctx->buffers.static_buffers.group_count > 0) {
        flecsEngine_batch_bindMaterialGroupStatic(
            (FlecsEngineImpl*)engine, pass, &pctx->buffers);
        flecsEngine_batch_bindInstanceGroupStatic(
            (FlecsEngineImpl*)engine, pass, &pctx->buffers);
        flecsEngine_batch_group_drawStatic(
            engine, pass, &pctx->group.group);
    }
}

static void flecsEngine_primitive_renderShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    (void)world;

    flecsEngine_primitive_ctx_t *pctx = batch->ctx;
    flecsEngine_batch_bindInstanceGroupShadow(
        (FlecsEngineImpl*)engine, pass, &pctx->buffers);
    flecsEngine_batch_group_drawShadow(engine, view_impl, pass, &pctx->group.group);

    if (pctx->buffers.static_buffers.group_count > 0) {
        flecsEngine_batch_bindInstanceGroupShadowStatic(
            (FlecsEngineImpl*)engine, pass, &pctx->buffers);
        flecsEngine_batch_group_drawShadowStatic(
            engine, view_impl, pass, &pctx->group.group);
    }
}

static void flecsEngine_primitive_renderTransparent(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    flecsEngine_primitive_ctx_t *pctx = batch->ctx;
    const FlecsRenderBatchImpl *impl = ecs_get(
        world, pctx->self_entity, FlecsRenderBatchImpl);
    flecsEngine_batch_group_t *groups[] = {
        &pctx->group.group
    };
    flecsEngine_batch_renderTransparentSorted(
        engine, view_impl, pass, &pctx->buffers,
        impl ? impl->pipeline_hdr : NULL, groups, 1);
}

static void flecsEngine_box_scale(
    const void *ptr,
    float *scale)
{
    const FlecsBox *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = prim->z;
}

static void flecsEngine_box_scale_aabb(
    FlecsAABB *aabb,
    const void *data,
    int32_t count)
{
    const FlecsBox *boxes = data;
    for (int32_t i = 0; i < count; i ++) {
        aabb[i].min[0] *= boxes[i].x;
        aabb[i].min[1] *= boxes[i].y;
        aabb[i].min[2] *= boxes[i].z;
        aabb[i].max[0] *= boxes[i].x;
        aabb[i].max[1] *= boxes[i].y;
        aabb[i].max[2] *= boxes[i].z;
    }
}

static void flecsEngine_quad_scale(
    const void *ptr,
    float *scale)
{
    const FlecsQuad *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = 1.0f;
}

static void flecsEngine_quad_scale_aabb(
    FlecsAABB *aabb,
    const void *data,
    int32_t count)
{
    const FlecsQuad *quads = data;
    for (int32_t i = 0; i < count; i ++) {
        aabb[i].min[0] *= quads[i].x;
        aabb[i].min[1] *= quads[i].y;
        aabb[i].max[0] *= quads[i].x;
        aabb[i].max[1] *= quads[i].y;
    }
}

static void flecsEngine_triangle_scale(
    const void *ptr,
    float *scale)
{
    const FlecsTriangle *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = 1.0f;
}

static void flecsEngine_triangle_scale_aabb(
    FlecsAABB *aabb,
    const void *data,
    int32_t count)
{
    const FlecsTriangle *tris = data;
    for (int32_t i = 0; i < count; i ++) {
        aabb[i].min[0] *= tris[i].x;
        aabb[i].min[1] *= tris[i].y;
        aabb[i].max[0] *= tris[i].x;
        aabb[i].max[1] *= tris[i].y;
    }
}

static void flecsEngine_right_triangle_scale(
    const void *ptr,
    float *scale)
{
    const FlecsRightTriangle *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = 1.0f;
}

static void flecsEngine_right_triangle_scale_aabb(
    FlecsAABB *aabb,
    const void *data,
    int32_t count)
{
    const FlecsRightTriangle *tris = data;
    for (int32_t i = 0; i < count; i ++) {
        aabb[i].min[0] *= tris[i].x;
        aabb[i].min[1] *= tris[i].y;
        aabb[i].max[0] *= tris[i].x;
        aabb[i].max[1] *= tris[i].y;
    }
}

static void flecsEngine_triangle_prism_scale(
    const void *ptr,
    float *scale)
{
    const FlecsTrianglePrism *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = prim->z;
}

static void flecsEngine_triangle_prism_scale_aabb(
    FlecsAABB *aabb,
    const void *data,
    int32_t count)
{
    const FlecsTrianglePrism *prisms = data;
    for (int32_t i = 0; i < count; i ++) {
        aabb[i].min[0] *= prisms[i].x;
        aabb[i].min[1] *= prisms[i].y;
        aabb[i].min[2] *= prisms[i].z;
        aabb[i].max[0] *= prisms[i].x;
        aabb[i].max[1] *= prisms[i].y;
        aabb[i].max[2] *= prisms[i].z;
    }
}

static void flecsEngine_right_triangle_prism_scale(
    const void *ptr,
    float *scale)
{
    const FlecsRightTrianglePrism *prim = ptr;
    scale[0] = prim->x;
    scale[1] = prim->y;
    scale[2] = prim->z;
}

static void flecsEngine_right_triangle_prism_scale_aabb(
    FlecsAABB *aabb,
    const void *data,
    int32_t count)
{
    const FlecsRightTrianglePrism *prisms = data;
    for (int32_t i = 0; i < count; i ++) {
        aabb[i].min[0] *= prisms[i].x;
        aabb[i].min[1] *= prisms[i].y;
        aabb[i].min[2] *= prisms[i].z;
        aabb[i].max[0] *= prisms[i].x;
        aabb[i].max[1] *= prisms[i].y;
        aabb[i].max[2] *= prisms[i].z;
    }
}

static ecs_entity_t flecsEngine_createBatch_primitive_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    const FlecsMesh3Impl *mesh,
    ecs_entity_t component,
    flecsEngine_primitive_scale_t scale_callback,
    flecsEngine_primitive_scale_aabb_t scale_aabb,
    ecs_entity_t exclude)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbr(world);

    ecs_query_desc_t desc = {
        .entity = batch,
        .terms = {
            { .id = component, .src.id = EcsSelf },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsAABB), .src.id = EcsSelf },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf, .oper = EcsNot },
            { .id = ecs_id(FlecsActualTint), .src.id = EcsSelf, .oper = EcsNot },
            { .id = FlecsAlphaBlend, .src.id = EcsUp, .trav = EcsIsA,
                .oper = EcsNot },
            { .id = ecs_id(FlecsTransmission), .src.id = EcsUp,
                .trav = EcsIsA, .oper = EcsNot }
        },
        .cache_kind = EcsQueryCacheAuto
    };

    if (exclude) {
        desc.terms[8] = (ecs_term_t){ .id = exclude, .oper = EcsNot };
    }

    ecs_query_t *q = ecs_query_init(world, &desc);

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsGpuVertexLitUv),
        .extract_callback = flecsEngine_primitive_extract,
        .upload_callback = flecsEngine_primitive_upload,
        .get_cull_buf = flecsEngine_primitive_getCullBuf,
        .callback = flecsEngine_primitive_render,
        .shadow_callback = flecsEngine_primitive_renderShadow,
        .ctx = flecsEngine_primitive_createCtx(
            world, batch, mesh, FLECS_BATCH_TRACK_STATIC, component,
            scale_callback, scale_aabb),
        .free_ctx = flecsEngine_primitive_deleteCtx
    });

    return batch;
}

ecs_entity_t flecsEngine_createBatch_primitive(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    const FlecsMesh3Impl *mesh,
    ecs_entity_t component,
    flecsEngine_primitive_scale_t scale_callback,
    flecsEngine_primitive_scale_aabb_t scale_aabb,
    ecs_entity_t exclude)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbr(world);

    ecs_query_desc_t desc = {
        .entity = batch,
        .terms = {
            { .id = component, .src.id = EcsSelf },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsAABB), .src.id = EcsSelf },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf, .oper = EcsOptional  },
            { .id = ecs_id(FlecsPbrMaterial), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsEmissive), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA, .oper = EcsNot },
            { .id = ecs_id(FlecsTransmission), .src.id = EcsSelf,
                .oper = EcsNot },
            { .id = FlecsAlphaBlend, .src.id = EcsSelf, .oper = EcsNot },
        },
        .cache_kind = EcsQueryCacheAuto
    };

    if (exclude) {
        desc.terms[9] = (ecs_term_t){ .id = exclude, .oper = EcsNot };
    }

    ecs_query_t *q = ecs_query_init(world, &desc);

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsGpuVertexLitUv),
        .extract_callback = flecsEngine_primitive_extract,
        .upload_callback = flecsEngine_primitive_upload,
        .get_cull_buf = flecsEngine_primitive_getCullBuf,
        .callback = flecsEngine_primitive_render,
        .shadow_callback = flecsEngine_primitive_renderShadow,
        .ctx = flecsEngine_primitive_createCtx(
            world, batch, mesh,
            FLECS_BATCH_OWNS_MATERIAL | FLECS_BATCH_TRACK_STATIC,
            component,
            scale_callback, scale_aabb),
        .free_ctx = flecsEngine_primitive_deleteCtx
    });

    return batch;
}

static ecs_entity_t flecsEngine_createBatch_primitiveVariant(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    const FlecsMesh3Impl *mesh,
    ecs_entity_t component,
    flecsEngine_primitive_scale_t scale_callback,
    flecsEngine_primitive_scale_aabb_t scale_aabb,
    ecs_entity_t exclude,
    flecsEngine_primitive_batch_kind_t kind)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_query_desc_t desc = {
        .entity = batch,
        .terms = {
            { .id = component, .src.id = EcsSelf },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsAABB), .src.id = EcsSelf }
        },
        .cache_kind = EcsQueryCacheAuto
    };
    flecsEngine_batch_flags_t flags = FLECS_BATCH_TRACK_STATIC;
    ecs_entity_t shader = flecsEngine_shader_pbr(world);
    int32_t term = 3;
    bool transparent = false;
    bool transmission = false;

    if (kind == FlecsPrimitiveBatchTint) {
        desc.terms[term ++] = (ecs_term_t){
            .id = ecs_id(FlecsMaterialId),
            .src.id = EcsUp,
            .trav = EcsIsA
        };
        desc.terms[term ++] = (ecs_term_t){
            .id = ecs_id(FlecsActualTint),
            .src.id = EcsSelf
        };
        desc.terms[term ++] = (ecs_term_t){
            .id = ecs_id(FlecsRgba),
            .src.id = EcsSelf,
            .oper = EcsNot
        };
        desc.terms[term ++] = (ecs_term_t){
            .id = FlecsAlphaBlend,
            .src.id = EcsUp,
            .trav = EcsIsA,
            .oper = EcsNot
        };
        desc.terms[term ++] = (ecs_term_t){
            .id = ecs_id(FlecsTransmission),
            .src.id = EcsUp,
            .trav = EcsIsA,
            .oper = EcsNot
        };
        flags = FLECS_BATCH_OWNS_MATERIAL |
            FLECS_BATCH_TINT | FLECS_BATCH_TRACK_STATIC;
    } else if (kind == FlecsPrimitiveBatchTransmission) {
        desc.terms[term ++] = (ecs_term_t){
            .id = ecs_id(FlecsMaterialId),
            .src.id = EcsUp,
            .trav = EcsIsA
        };
        desc.terms[term ++] = (ecs_term_t){
            .id = ecs_id(FlecsTransmission),
            .src.id = EcsUp,
            .trav = EcsIsA
        };
        desc.terms[term ++] = (ecs_term_t){
            .id = ecs_id(FlecsTransmission),
            .src.id = EcsSelf,
            .oper = EcsNot
        };
        desc.terms[term ++] = (ecs_term_t){
            .id = ecs_id(FlecsRgba),
            .src.id = EcsSelf,
            .oper = EcsNot
        };
        shader = flecsEngine_shader_pbrTransmission(world);
        transmission = true;
    } else if (kind == FlecsPrimitiveBatchTransmissionData) {
        desc.terms[term ++] = (ecs_term_t){
            .id = ecs_id(FlecsRgba),
            .src.id = EcsSelf
        };
        desc.terms[term ++] = (ecs_term_t){
            .id = ecs_id(FlecsPbrMaterial),
            .src.id = EcsSelf,
            .oper = EcsOptional
        };
        desc.terms[term ++] = (ecs_term_t){
            .id = ecs_id(FlecsEmissive),
            .src.id = EcsSelf,
            .oper = EcsOptional
        };
        desc.terms[term ++] = (ecs_term_t){
            .id = ecs_id(FlecsTransmission),
            .src.id = EcsSelf
        };
        desc.terms[term ++] = (ecs_term_t){
            .id = ecs_id(FlecsMaterialId),
            .src.id = EcsUp,
            .trav = EcsIsA,
            .oper = EcsNot
        };
        flags = FLECS_BATCH_OWNS_MATERIAL |
            FLECS_BATCH_OWNS_TRANSMISSION |
            FLECS_BATCH_TRACK_STATIC;
        shader = flecsEngine_shader_pbrTransmission(world);
        transmission = true;
    } else if (kind == FlecsPrimitiveBatchTransparent) {
        desc.terms[term ++] = (ecs_term_t){
            .id = ecs_id(FlecsMaterialId),
            .src.id = EcsUp,
            .trav = EcsIsA
        };
        desc.terms[term ++] = (ecs_term_t){
            .id = FlecsAlphaBlend,
            .src.id = EcsUp,
            .trav = EcsIsA
        };
        desc.terms[term ++] = (ecs_term_t){
            .id = ecs_id(FlecsTransmission),
            .src.id = EcsUp,
            .trav = EcsIsA,
            .oper = EcsNot
        };
        desc.terms[term ++] = (ecs_term_t){
            .id = ecs_id(FlecsRgba),
            .src.id = EcsSelf,
            .oper = EcsNot
        };
        flags |= FLECS_BATCH_NO_GPU_CULL;
        transparent = true;
    } else {
        desc.terms[term ++] = (ecs_term_t){
            .id = ecs_id(FlecsRgba),
            .src.id = EcsSelf
        };
        desc.terms[term ++] = (ecs_term_t){
            .id = ecs_id(FlecsPbrMaterial),
            .src.id = EcsSelf,
            .oper = EcsOptional
        };
        desc.terms[term ++] = (ecs_term_t){
            .id = ecs_id(FlecsEmissive),
            .src.id = EcsSelf,
            .oper = EcsOptional
        };
        desc.terms[term ++] = (ecs_term_t){
            .id = ecs_id(FlecsTransmission),
            .src.id = EcsSelf,
            .oper = EcsNot
        };
        desc.terms[term ++] = (ecs_term_t){
            .id = FlecsAlphaBlend,
            .src.id = EcsSelf
        };
        flags = FLECS_BATCH_OWNS_MATERIAL |
            FLECS_BATCH_NO_GPU_CULL |
            FLECS_BATCH_TRACK_STATIC;
        transparent = true;
    }

    if (exclude) {
        desc.terms[term] = (ecs_term_t){
            .id = exclude,
            .src.id = EcsSelf,
            .oper = EcsNot
        };
    }

    ecs_query_t *q = ecs_query_init(world, &desc);
    FlecsRenderBatch value = {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsGpuVertexLitUv),
        .extract_callback = flecsEngine_primitive_extract,
        .upload_callback = flecsEngine_primitive_upload,
        .get_cull_buf = flecsEngine_primitive_getCullBuf,
        .callback = transparent
            ? flecsEngine_primitive_renderTransparent
            : flecsEngine_primitive_render,
        .shadow_callback = transparent || transmission
            ? NULL : flecsEngine_primitive_renderShadow,
        .ctx = flecsEngine_primitive_createCtx(
            world, batch, mesh, flags, component,
            scale_callback, scale_aabb),
        .free_ctx = flecsEngine_primitive_deleteCtx,
        .render_after_snapshot = transparent || transmission,
        .needs_transmission = transmission
    };
    if (transparent) {
        value.depth_test = WGPUCompareFunction_Less;
        value.cull_mode = WGPUCullMode_None;
        value.blend = (WGPUBlendState){
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
        value.depth_write = false;
    } else if (transmission) {
        value.depth_write = false;
    }
    ecs_set_ptr(world, batch, FlecsRenderBatch, &value);
    return batch;
}

ecs_entity_t flecsEngine_createBatchSet_primitives(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    flecsEngine_primitive_batch_kind_t kind)
{
    ecs_entity_t result = ecs_entity(world, {
        .parent = parent,
        .name = name
    });
    FlecsRenderBatchSet set = *ecs_ensure(
        world, result, FlecsRenderBatchSet);

    ecs_vec_append_t(NULL, &set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_primitiveVariant(
            world, result, "Boxes", flecsEngine_box_getAsset(world),
            ecs_id(FlecsBox), flecsEngine_box_scale,
            flecsEngine_box_scale_aabb, ecs_id(FlecsBevel), kind);
    ecs_vec_append_t(NULL, &set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_primitiveVariant(
            world, result, "Quads", flecsEngine_quad_getAsset(world),
            ecs_id(FlecsQuad), flecsEngine_quad_scale,
            flecsEngine_quad_scale_aabb, 0, kind);
    ecs_vec_append_t(NULL, &set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_primitiveVariant(
            world, result, "Triangles", flecsEngine_triangle_getAsset(world),
            ecs_id(FlecsTriangle), flecsEngine_triangle_scale,
            flecsEngine_triangle_scale_aabb, 0, kind);
    ecs_vec_append_t(NULL, &set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_primitiveVariant(
            world, result, "RightTriangles",
            flecsEngine_rightTriangle_getAsset(world),
            ecs_id(FlecsRightTriangle), flecsEngine_right_triangle_scale,
            flecsEngine_right_triangle_scale_aabb, 0, kind);
    ecs_vec_append_t(NULL, &set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_primitiveVariant(
            world, result, "TrianglePrisms",
            flecsEngine_trianglePrism_getAsset(world),
            ecs_id(FlecsTrianglePrism), flecsEngine_triangle_prism_scale,
            flecsEngine_triangle_prism_scale_aabb, 0, kind);
    ecs_vec_append_t(NULL, &set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_primitiveVariant(
            world, result, "RightTrianglePrisms",
            flecsEngine_rightTrianglePrism_getAsset(world),
            ecs_id(FlecsRightTrianglePrism),
            flecsEngine_right_triangle_prism_scale,
            flecsEngine_right_triangle_prism_scale_aabb, 0, kind);

    ecs_set_ptr(world, result, FlecsRenderBatchSet, &set);
    return result;
}

ecs_entity_t flecsEngine_createBatch_boxes(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive(world, parent, name,
        flecsEngine_box_getAsset(world), ecs_id(FlecsBox),
        flecsEngine_box_scale, flecsEngine_box_scale_aabb,
        ecs_id(FlecsBevel));
}

ecs_entity_t flecsEngine_createBatch_quads(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive(world, parent, name,
        flecsEngine_quad_getAsset(world), ecs_id(FlecsQuad),
        flecsEngine_quad_scale, flecsEngine_quad_scale_aabb, 0);
}

ecs_entity_t flecsEngine_createBatch_triangles(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive(world, parent, name,
        flecsEngine_triangle_getAsset(world), ecs_id(FlecsTriangle),
        flecsEngine_triangle_scale, flecsEngine_triangle_scale_aabb, 0);
}

ecs_entity_t flecsEngine_createBatch_right_triangles(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive(world, parent, name,
        flecsEngine_rightTriangle_getAsset(world), ecs_id(FlecsRightTriangle),
        flecsEngine_right_triangle_scale, flecsEngine_right_triangle_scale_aabb,
        0);
}

ecs_entity_t flecsEngine_createBatch_triangle_prisms(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive(world, parent, name,
        flecsEngine_trianglePrism_getAsset(world), ecs_id(FlecsTrianglePrism),
        flecsEngine_triangle_prism_scale, flecsEngine_triangle_prism_scale_aabb,
        0);
}

ecs_entity_t flecsEngine_createBatch_right_triangle_prisms(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive(world, parent, name,
        flecsEngine_rightTrianglePrism_getAsset(world),
        ecs_id(FlecsRightTrianglePrism),
        flecsEngine_right_triangle_prism_scale,
        flecsEngine_right_triangle_prism_scale_aabb, 0);
}

ecs_entity_t flecsEngine_createBatch_boxes_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive_materialIndex(world, parent, name,
        flecsEngine_box_getAsset(world), ecs_id(FlecsBox),
        flecsEngine_box_scale, flecsEngine_box_scale_aabb,
        ecs_id(FlecsBevel));
}

ecs_entity_t flecsEngine_createBatch_quads_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive_materialIndex(world, parent, name,
        flecsEngine_quad_getAsset(world), ecs_id(FlecsQuad),
        flecsEngine_quad_scale, flecsEngine_quad_scale_aabb, 0);
}

ecs_entity_t flecsEngine_createBatch_triangles_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive_materialIndex(world, parent, name,
        flecsEngine_triangle_getAsset(world), ecs_id(FlecsTriangle),
        flecsEngine_triangle_scale, flecsEngine_triangle_scale_aabb, 0);
}

ecs_entity_t flecsEngine_createBatch_right_triangles_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive_materialIndex(world, parent, name,
        flecsEngine_rightTriangle_getAsset(world), ecs_id(FlecsRightTriangle),
        flecsEngine_right_triangle_scale,
        flecsEngine_right_triangle_scale_aabb, 0);
}

ecs_entity_t flecsEngine_createBatch_triangle_prisms_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive_materialIndex(world, parent, name,
        flecsEngine_trianglePrism_getAsset(world), ecs_id(FlecsTrianglePrism),
        flecsEngine_triangle_prism_scale,
        flecsEngine_triangle_prism_scale_aabb, 0);
}

ecs_entity_t flecsEngine_createBatch_right_triangle_prisms_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    return flecsEngine_createBatch_primitive_materialIndex(world, parent, name,
        flecsEngine_rightTrianglePrism_getAsset(world),
        ecs_id(FlecsRightTrianglePrism),
        flecsEngine_right_triangle_prism_scale,
        flecsEngine_right_triangle_prism_scale_aabb, 0);
}
