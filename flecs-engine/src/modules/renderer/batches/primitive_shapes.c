#include "../renderer.h"
#include "../shaders/shaders.h"
#include "../../geometry3/geometry3.h"
#include "batches.h"
#include "flecs_engine.h"

typedef struct {
    flecsEngine_primitive_batch_group_t group;
    flecsEngine_batch_t buffers;
} flecsEngine_primitive_ctx_t;

static void* flecsEngine_primitive_createCtx(
    ecs_world_t *world,
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

redo:
    ctx->view.offset = 0;
    ctx->view.group_idx = 0;
    flecsEngine_batch_group_extract(world, engine, batch, ctx,
        pctx->group.scale_callback, pctx->group.scale_aabb,
        pctx->group.component_size);

    if (ctx->view.count > buf->buffers.capacity) {
        flecsEngine_batch_ensureCapacity(engine, buf, ctx->view.count);
        goto redo;
    }

    buf->buffers.count = ctx->view.count;
    buf->buffers.group_count = ctx->view.count ? 1 : 0;

    if (buf->buffers.group_count) {
        flecsEngine_batch_ensureGroupCapacity(buf, 1);
        flecsEngine_batch_group_prepareArgs(ctx);
    }
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
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA }
        },
        .cache_kind = EcsQueryCacheAuto
    };

    if (exclude) {
        desc.terms[4] = (ecs_term_t){ .id = exclude, .oper = EcsNot };
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
            world, mesh, FLECS_BATCH_DEFAULT, component,
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
        },
        .cache_kind = EcsQueryCacheAuto
    };

    if (exclude) {
        desc.terms[7] = (ecs_term_t){ .id = exclude, .oper = EcsNot };
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
            world, mesh, FLECS_BATCH_OWNS_MATERIAL, component,
            scale_callback, scale_aabb),
        .free_ctx = flecsEngine_primitive_deleteCtx
    });

    return batch;
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
