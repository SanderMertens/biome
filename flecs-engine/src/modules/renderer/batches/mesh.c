#include "common/common.h"

/* Mesh queries intentionally include entities that already have a
 * FlecsBufferSlot. The static extractor uses the component as an ownership
 * token: slots owned by this batch are skipped, while a slot owned by another
 * batch is transferred only after this batch is ready to replace it. */

/* --- Non-textured mesh batch creation --- */

ecs_entity_t flecsEngine_createBatch_mesh_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbr(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsAABB), .src.id = EcsSelf },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf, .oper = EcsNot },
            { .id = ecs_id(FlecsPbrTextures), .src.id = EcsUp, .trav = EcsIsA, .oper = EcsNot },
            { .id = FlecsAlphaBlend, .src.id = EcsUp, .trav = EcsIsA, .oper = EcsNot },
            { .id = ecs_id(FlecsTransmission), .src.id = EcsUp, .trav = EcsIsA, .oper = EcsNot },
            { .id = FlecsDynamicTransform, .oper = EcsOptional },
            { .id = ecs_id(FlecsActualTint), .src.id = EcsSelf, .oper = EcsNot },
            { .id = ecs_pair(FlecsCustomShader, EcsWildcard),
                .src.id = EcsSelf, .oper = EcsNot },
        },
        .cache_kind = EcsQueryCacheAuto,
        .group_by = EcsIsA,
        .group_by_callback = flecsEngine_mesh_groupByMesh,
        .on_group_create = flecsEngine_mesh_onGroupCreate,
        .on_group_delete = flecsEngine_mesh_onGroupDelete
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsGpuVertexLitUv),
        .extract_callback = flecsEngine_mesh_extract,
        .upload_callback = flecsEngine_mesh_upload,
        .callback = flecsEngine_mesh_render,
        .shadow_callback = flecsEngine_mesh_renderShadow,
        .get_cull_buf = flecsEngine_mesh_getCullBuf,
        .ctx = flecsEngine_mesh_createCtx(FLECS_BATCH_TRACK_STATIC),
        .free_ctx = flecsEngine_mesh_deleteCtx
    });

    return batch;
}

ecs_entity_t flecsEngine_createBatch_mesh_materialData(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbr(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsAABB), .src.id = EcsSelf },
            /* Owning FlecsRgba overrides any inherited material, which
             * makes per-instance tinting of shared models possible */
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf },
            { .id = ecs_id(FlecsPbrMaterial), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsEmissive), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsTransmission), .src.id = EcsSelf, .oper = EcsNot },
            { .id = FlecsDynamicTransform, .oper = EcsOptional },
            { .id = FlecsAlphaBlend, .src.id = EcsSelf, .oper = EcsNot },
            { .id = FlecsVertexColors, .src.id = EcsSelf, .oper = EcsNot },
            { .id = ecs_pair(FlecsCustomShader, EcsWildcard),
                .src.id = EcsSelf, .oper = EcsNot },
        },
        .cache_kind = EcsQueryCacheAuto,
        .group_by = EcsIsA,
        .group_by_callback = flecsEngine_mesh_groupByMesh,
        .on_group_create = flecsEngine_mesh_onGroupCreate,
        .on_group_delete = flecsEngine_mesh_onGroupDelete
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsGpuVertexLitUv),
        .extract_callback = flecsEngine_mesh_extract,
        .upload_callback = flecsEngine_mesh_upload,
        .callback = flecsEngine_mesh_render,
        .shadow_callback = flecsEngine_mesh_renderShadow,
        .get_cull_buf = flecsEngine_mesh_getCullBuf,
        .ctx = flecsEngine_mesh_createCtx(
            FLECS_BATCH_OWNS_MATERIAL | FLECS_BATCH_TRACK_STATIC),
        .free_ctx = flecsEngine_mesh_deleteCtx
    });

    return batch;
}

ecs_entity_t flecsEngine_createBatch_mesh_tint(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbr(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsAABB), .src.id = EcsSelf },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsActualTint), .src.id = EcsSelf },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf, .oper = EcsNot },
            { .id = FlecsAlphaBlend, .src.id = EcsUp, .trav = EcsIsA, .oper = EcsNot },
            { .id = ecs_id(FlecsTransmission), .src.id = EcsUp, .trav = EcsIsA, .oper = EcsNot },
            { .id = FlecsDynamicTransform, .oper = EcsOptional },
            { .id = ecs_pair(FlecsCustomShader, EcsWildcard),
                .src.id = EcsSelf, .oper = EcsNot },
        },
        .cache_kind = EcsQueryCacheAuto,
        .group_by = EcsIsA,
        .group_by_callback = flecsEngine_mesh_groupByMesh,
        .on_group_create = flecsEngine_mesh_onGroupCreate,
        .on_group_delete = flecsEngine_mesh_onGroupDelete
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsGpuVertexLitUv),
        .extract_callback = flecsEngine_mesh_extract,
        .upload_callback = flecsEngine_mesh_upload,
        .callback = flecsEngine_mesh_render,
        .shadow_callback = flecsEngine_mesh_renderShadow,
        .get_cull_buf = flecsEngine_mesh_getCullBuf,
        .ctx = flecsEngine_mesh_createCtx(
            FLECS_BATCH_OWNS_MATERIAL | FLECS_BATCH_TRACK_STATIC |
            FLECS_BATCH_TINT),
        .free_ctx = flecsEngine_mesh_deleteCtx
    });

    return batch;
}

/* Vertex-color variant of the materialData batch: meshes with per-vertex
 * colors and an owned FlecsRgba, routed via the FlecsVertexColors tag. Term
 * order matches the materialData batch — the extract callback reads fields
 * by index. */
ecs_entity_t flecsEngine_createBatch_mesh_vertexColor(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbrVertexColor(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsAABB), .src.id = EcsSelf },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf },
            { .id = ecs_id(FlecsPbrMaterial), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsEmissive), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsTransmission), .src.id = EcsSelf, .oper = EcsNot },
            { .id = FlecsDynamicTransform, .oper = EcsOptional },
            { .id = FlecsAlphaBlend, .src.id = EcsSelf, .oper = EcsNot },
            { .id = FlecsVertexColors, .src.id = EcsSelf },
            { .id = ecs_pair(FlecsCustomShader, EcsWildcard),
                .src.id = EcsSelf, .oper = EcsNot },
        },
        .cache_kind = EcsQueryCacheAuto,
        .group_by = EcsIsA,
        .group_by_callback = flecsEngine_mesh_groupByMesh,
        .on_group_create = flecsEngine_mesh_onGroupCreate,
        .on_group_delete = flecsEngine_mesh_onGroupDelete
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsGpuVertexLitUv),
        .extract_callback = flecsEngine_mesh_extract,
        .upload_callback = flecsEngine_mesh_upload,
        .callback = flecsEngine_mesh_render,
        .shadow_callback = flecsEngine_mesh_renderShadow,
        .get_cull_buf = flecsEngine_mesh_getCullBuf,
        .ctx = flecsEngine_mesh_createCtx(
            FLECS_BATCH_OWNS_MATERIAL | FLECS_BATCH_TRACK_STATIC |
            FLECS_BATCH_VERTEX_COLORS),
        .free_ctx = flecsEngine_mesh_deleteCtx
    });

    return batch;
}

/* --- Textured mesh batch --- */

ecs_entity_t flecsEngine_createBatch_textured_mesh(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbr(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsAABB), .src.id = EcsSelf },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsPbrTextures), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf, .oper = EcsNot },
            { .id = FlecsAlphaBlend, .src.id = EcsUp, .trav = EcsIsA, .oper = EcsNot },
            { .id = ecs_id(FlecsTransmission), .src.id = EcsUp, .trav = EcsIsA, .oper = EcsNot },
            { .id = FlecsDynamicTransform, .oper = EcsOptional },
            { .id = ecs_id(FlecsActualTint), .src.id = EcsSelf, .oper = EcsNot },
            { .id = ecs_pair(FlecsCustomShader, EcsWildcard),
                .src.id = EcsSelf, .oper = EcsNot },
        },
        .cache_kind = EcsQueryCacheAuto,
        .group_by = EcsIsA,
        .group_by_callback = flecsEngine_mesh_groupByMesh,
        .on_group_create = flecsEngine_mesh_onGroupCreate,
        .on_group_delete = flecsEngine_mesh_onGroupDelete
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsGpuVertexLitUv),
        .extract_callback = flecsEngine_mesh_extract,
        .upload_callback = flecsEngine_mesh_upload,
        .callback = flecsEngine_mesh_render,
        .shadow_callback = flecsEngine_mesh_renderShadow,
        .get_cull_buf = flecsEngine_mesh_getCullBuf,
        .ctx = flecsEngine_mesh_createCtx(FLECS_BATCH_TRACK_STATIC),
        .free_ctx = flecsEngine_mesh_deleteCtx
    });

    return batch;
}

/* --- Unified transparent mesh batch --- */

typedef struct {
    flecsEngine_batch_t buffers;
    ecs_entity_t self_entity;
} flecsEngine_transparent_mesh_ctx_t;

static flecsEngine_transparent_mesh_ctx_t* flecsEngine_transparent_mesh_createCtx(
    ecs_entity_t self_entity,
    uint32_t extra_flags)
{
    flecsEngine_transparent_mesh_ctx_t *ctx =
        ecs_os_calloc_t(flecsEngine_transparent_mesh_ctx_t);
    flecsEngine_batch_init(&ctx->buffers,
        FLECS_BATCH_NO_GPU_CULL | FLECS_BATCH_TRACK_STATIC | extra_flags);
    ctx->self_entity = self_entity;
    return ctx;
}

static void flecsEngine_transparent_mesh_deleteCtx(void *ptr)
{
    flecsEngine_transparent_mesh_ctx_t *ctx = ptr;
    flecsEngine_batch_fini(&ctx->buffers);
    ecs_os_free(ctx);
}

static void* flecsEngine_transparent_mesh_getCullBuf(
    const FlecsRenderBatch *batch)
{
    flecsEngine_transparent_mesh_ctx_t *tctx = batch->ctx;
    return &tctx->buffers;
}

static void flecsEngine_transparent_mesh_render(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    FLECS_TRACY_ZONE_BEGIN("TransparentMeshRender");

    flecsEngine_transparent_mesh_ctx_t *tctx = batch->ctx;

    const ecs_map_t *groups_map = ecs_query_get_groups(batch->query);
    ecs_assert(groups_map != NULL, ECS_INTERNAL_ERROR, NULL);

    int32_t cap = ecs_map_count(groups_map);
    flecsEngine_batch_group_t **groups =
        ecs_os_malloc_n(flecsEngine_batch_group_t*, cap ? cap : 1);

    int32_t n = 0;
    ecs_map_iter_t git = ecs_map_iter(groups_map);
    while (ecs_map_next(&git)) {
        uint64_t group_id = ecs_map_key(&git);
        if (!group_id) continue;
        flecsEngine_batch_group_t *ctx =
            ecs_query_get_group_ctx(batch->query, group_id);
        if (!ctx || !ctx->view.count) continue;
        groups[n ++] = ctx;
    }

    const FlecsRenderBatchImpl *self_impl =
        ecs_get(world, tctx->self_entity, FlecsRenderBatchImpl);

    flecsEngine_batch_renderTransparentSorted(engine, view_impl, pass,
        &tctx->buffers, self_impl ? self_impl->pipeline_hdr : NULL,
        groups, n);

    ecs_os_free(groups);

    FLECS_TRACY_ZONE_END;
}

ecs_entity_t flecsEngine_createBatch_mesh_transparent(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, {
        .parent = parent, .name = name });

    ecs_entity_t shader = flecsEngine_shader_pbr(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsAABB), .src.id = EcsSelf },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA },
            { .id = FlecsAlphaBlend, .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsTransmission), .src.id = EcsUp, .trav = EcsIsA,
                .oper = EcsNot },
            { .id = ecs_pair(FlecsCustomShader, EcsWildcard),
                .src.id = EcsSelf, .oper = EcsNot },
        },
        .cache_kind = EcsQueryCacheAuto,
        .group_by = EcsIsA,
        .group_by_callback = flecsEngine_mesh_groupByMesh,
        .on_group_create = flecsEngine_mesh_onGroupCreate,
        .on_group_delete = flecsEngine_mesh_onGroupDelete
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsGpuVertexLitUv),
        .depth_test = WGPUCompareFunction_Less,
        .cull_mode = WGPUCullMode_None,
        .blend = {
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
        },
        .depth_write = false,
        .extract_callback = flecsEngine_mesh_extract,
        .upload_callback = flecsEngine_mesh_upload,
        .callback = flecsEngine_transparent_mesh_render,
        .get_cull_buf = flecsEngine_transparent_mesh_getCullBuf,
        .ctx = flecsEngine_transparent_mesh_createCtx(batch, 0),
        .free_ctx = flecsEngine_transparent_mesh_deleteCtx,
        .render_after_snapshot = true
    });

    return batch;
}

/* Transparent variant of the materialData batch: meshes with an owned
 * FlecsRgba AND an owned FlecsAlphaBlend tag render translucent with the
 * color's alpha. Used for per-instance ghosting of shared models. Term
 * order matches the materialData batch — the extract callback reads
 * fields by index. */
ecs_entity_t flecsEngine_createBatch_mesh_transparentData(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, {
        .parent = parent, .name = name });

    ecs_entity_t shader = flecsEngine_shader_pbr(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsAABB), .src.id = EcsSelf },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf },
            { .id = ecs_id(FlecsPbrMaterial), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsEmissive), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsTransmission), .src.id = EcsSelf, .oper = EcsNot },
            { .id = FlecsDynamicTransform, .oper = EcsOptional },
            { .id = FlecsAlphaBlend, .src.id = EcsSelf },
            { .id = ecs_pair(FlecsCustomShader, EcsWildcard),
                .src.id = EcsSelf, .oper = EcsNot },
        },
        .cache_kind = EcsQueryCacheAuto,
        .group_by = EcsIsA,
        .group_by_callback = flecsEngine_mesh_groupByMesh,
        .on_group_create = flecsEngine_mesh_onGroupCreate,
        .on_group_delete = flecsEngine_mesh_onGroupDelete
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsGpuVertexLitUv),
        .depth_test = WGPUCompareFunction_Less,
        .cull_mode = WGPUCullMode_None,
        .blend = {
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
        },
        .depth_write = false,
        .extract_callback = flecsEngine_mesh_extract,
        .upload_callback = flecsEngine_mesh_upload,
        .callback = flecsEngine_transparent_mesh_render,
        .get_cull_buf = flecsEngine_transparent_mesh_getCullBuf,
        .ctx = flecsEngine_transparent_mesh_createCtx(batch,
            FLECS_BATCH_OWNS_MATERIAL),
        .free_ctx = flecsEngine_transparent_mesh_deleteCtx,
        .render_after_snapshot = true
    });

    return batch;
}

/* --- Transmissive mesh batch --- */

ecs_entity_t flecsEngine_createBatch_mesh_transmission(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbrTransmission(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsAABB), .src.id = EcsSelf },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsTransmission), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsTransmission), .src.id = EcsSelf,
                .oper = EcsNot },
            { .id = ecs_pair(FlecsCustomShader, EcsWildcard),
                .src.id = EcsSelf, .oper = EcsNot },
        },
        .cache_kind = EcsQueryCacheAuto,
        .group_by = EcsIsA,
        .group_by_callback = flecsEngine_mesh_groupByMesh,
        .on_group_create = flecsEngine_mesh_onGroupCreate,
        .on_group_delete = flecsEngine_mesh_onGroupDelete
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsGpuVertexLitUv),
        .depth_write = false,
        .extract_callback = flecsEngine_mesh_extract,
        .upload_callback = flecsEngine_mesh_upload,
        .callback = flecsEngine_mesh_render,
        .get_cull_buf = flecsEngine_mesh_getCullBuf,
        .ctx = flecsEngine_mesh_createCtx(FLECS_BATCH_TRACK_STATIC),
        .free_ctx = flecsEngine_mesh_deleteCtx,
        .render_after_snapshot = true,
        .needs_transmission = true
    });

    return batch;
}

ecs_entity_t flecsEngine_createBatch_mesh_transmissionData(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch = ecs_entity(world, { .parent = parent, .name = name });
    ecs_entity_t shader = flecsEngine_shader_pbrTransmission(world);

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsAABB), .src.id = EcsSelf },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsPbrMaterial), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsEmissive), .src.id = EcsSelf, .oper = EcsOptional },
            { .id = ecs_id(FlecsTransmission), .src.id = EcsSelf },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsUp, .trav = EcsIsA, .oper = EcsNot },
            { .id = ecs_id(FlecsPbrTextures), .src.id = EcsUp, .trav = EcsIsA, .oper = EcsNot },
            { .id = ecs_pair(FlecsCustomShader, EcsWildcard),
                .src.id = EcsSelf, .oper = EcsNot },
        },
        .cache_kind = EcsQueryCacheAuto,
        .group_by = EcsIsA,
        .group_by_callback = flecsEngine_mesh_groupByMesh,
        .on_group_create = flecsEngine_mesh_onGroupCreate,
        .on_group_delete = flecsEngine_mesh_onGroupDelete
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsGpuVertexLitUv),
        .depth_write = false,
        .extract_callback = flecsEngine_mesh_extract,
        .upload_callback = flecsEngine_mesh_upload,
        .callback = flecsEngine_mesh_render,
        .get_cull_buf = flecsEngine_mesh_getCullBuf,
        .ctx = flecsEngine_mesh_createCtx(
            FLECS_BATCH_DEFAULT |
            FLECS_BATCH_OWNS_MATERIAL |
            FLECS_BATCH_OWNS_TRANSMISSION |
            FLECS_BATCH_TRACK_STATIC),
        .free_ctx = flecsEngine_mesh_deleteCtx,
        .render_after_snapshot = true,
        .needs_transmission = true
    });

    return batch;
}

ecs_entity_t flecsEngine_createMeshShaderBatch(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    ecs_entity_t shader,
    bool needs_transmission)
{
    ecs_entity_t batch = ecs_entity(world, {
        .parent = parent,
        .name = name
    });

    ecs_query_t *q = ecs_query(world, {
        .entity = batch,
        .terms = {
            { .id = ecs_id(FlecsMesh3Impl), .src.id = EcsUp, .trav = EcsIsA },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsAABB), .src.id = EcsSelf },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf,
                .oper = EcsOptional },
            { .id = ecs_id(FlecsPbrMaterial), .src.id = EcsSelf,
                .oper = EcsOptional },
            { .id = ecs_id(FlecsEmissive), .src.id = EcsSelf,
                .oper = EcsOptional },
            { .id = ecs_id(FlecsTransmission), .src.id = EcsSelf,
                .oper = EcsOptional },
            { .id = FlecsDynamicTransform, .oper = EcsOptional },
            { .id = ecs_pair(FlecsCustomShader, shader),
                .src.id = EcsSelf }
        },
        .cache_kind = EcsQueryCacheAuto,
        .group_by = EcsIsA,
        .group_by_callback = flecsEngine_mesh_groupByMesh,
        .on_group_create = flecsEngine_mesh_onGroupCreate,
        .on_group_delete = flecsEngine_mesh_onGroupDelete
    });

    ecs_set(world, batch, FlecsRenderBatch, {
        .shader = shader,
        .query = q,
        .vertex_type = ecs_id(FlecsGpuVertexLitUv),
        .depth_test = WGPUCompareFunction_LessEqual,
        .depth_write = !needs_transmission,
        .extract_callback = flecsEngine_mesh_extract,
        .upload_callback = flecsEngine_mesh_upload,
        .callback = flecsEngine_mesh_render,
        .shadow_callback = needs_transmission
            ? NULL
            : flecsEngine_mesh_renderShadow,
        .get_cull_buf = flecsEngine_mesh_getCullBuf,
        .ctx = flecsEngine_mesh_createCtx(
            FLECS_BATCH_OWNS_MATERIAL | FLECS_BATCH_OWNS_TRANSMISSION |
            FLECS_BATCH_TRACK_STATIC),
        .free_ctx = flecsEngine_mesh_deleteCtx,
        .render_after_snapshot = needs_transmission,
        .needs_transmission = needs_transmission
    });

    return batch;
}
