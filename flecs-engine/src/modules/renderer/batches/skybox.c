#include "../renderer.h"
#include "../shaders/shaders.h"
#include "../../geometry3/geometry3.h"
#include "batches.h"
#include "flecs_engine.h"

typedef struct {
    FlecsMesh3Impl mesh;
    bool initialized;
} flecs_engine_skybox_ctx_t;

static flecs_engine_skybox_ctx_t* flecsEngine_skybox_createCtx(void)
{
    return ecs_os_calloc_t(flecs_engine_skybox_ctx_t);
}

static void flecsEngine_skybox_ensureInitialized(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    flecs_engine_skybox_ctx_t *ctx)
{
    (void)engine;
    if (ctx->initialized) {
        return;
    }

    const FlecsMesh3Impl *mesh =
        flecsEngine_quad_getAsset((ecs_world_t*)world);
    if (mesh) {
        ctx->mesh = *mesh;
    }

    ctx->initialized = true;
}

static void flecsEngine_skybox_deleteCtx(
    void *arg)
{
    flecs_engine_skybox_ctx_t *ctx = arg;
    ecs_os_free(ctx);
}

static void flecsEngine_skybox_renderCallback(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch)
{
    (void)view_impl;
    flecs_engine_skybox_ctx_t *ctx = batch->ctx;
    flecsEngine_skybox_ensureInitialized(world, engine, ctx);

    if (!ctx->mesh.vertex_uv_buffer || !ctx->mesh.index_buffer ||
        !ctx->mesh.index_count)
    {
        return;
    }

    wgpuRenderPassEncoderSetVertexBuffer(
        pass, 0, ctx->mesh.vertex_uv_buffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(
        pass, ctx->mesh.index_buffer, WGPUIndexFormat_Uint32, 0,
        WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(pass, ctx->mesh.index_count, 1, 0, 0, 0);
}

void FlecsOnAddSkyBoxBatch(
    ecs_iter_t *it)
{
    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t batch = it->entities[i];
        ecs_entity_t shader = flecsEngine_shader_skybox(it->world);

        ecs_set(it->world, batch, FlecsRenderBatch, {
            .shader = shader,
            .vertex_type = ecs_id(FlecsGpuVertexLitUv),
            .depth_test = WGPUCompareFunction_LessEqual,
            .cull_mode = WGPUCullMode_None,
            .depth_write = false,
            .callback = flecsEngine_skybox_renderCallback,
            .ctx = flecsEngine_skybox_createCtx(),
            .free_ctx = flecsEngine_skybox_deleteCtx
        });
    }
}
