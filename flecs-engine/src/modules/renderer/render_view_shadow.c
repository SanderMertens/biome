#include "renderer.h"
#include "frustum_cull.h"
#include "gpu_timing.h"
#include "../../tracy_hooks.h"
#include "flecs_engine.h"

static void flecsEngine_renderView_computeCascades(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view,
    FlecsRenderViewImpl *view_impl)
{
    (void)engine;
    view_impl->cascade_frustum_valid = false;
    uint32_t shadow_map_size = (uint32_t)view->shadow.map_size;
    if (!shadow_map_size) {
        shadow_map_size = FLECS_ENGINE_SHADOW_MAP_SIZE_DEFAULT;
    }

    if (view->shadow.enabled && view_impl->main_light) {
        flecsEngine_shadow_computeCascades(
            world, view, view_impl->main_light, shadow_map_size,
            view->shadow.max_range,
            view_impl->shadow.current_light_vp,
            view_impl->shadow.cascade_splits);

        for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
            flecsEngine_frustum_extractPlanes(
                view_impl->shadow.current_light_vp[c],
                view_impl->cascade_frustum_planes[c]);
        }
        view_impl->cascade_frustum_valid = true;
    }
}

void flecsEngine_renderView_cullShadowsAll(
    ecs_world_t *world,
    FlecsEngineImpl *engine)
{
    FLECS_TRACY_ZONE_BEGIN("ComputeCascades");
    ecs_iter_t it = ecs_query_iter(world, engine->view_query);
    while (ecs_query_next(&it)) {
        FlecsRenderView *views = ecs_field(&it, FlecsRenderView, 0);
        FlecsRenderViewImpl *viewImpls = ecs_field(&it, FlecsRenderViewImpl, 1);
        for (int32_t i = 0; i < it.count; i ++) {
            flecsEngine_renderView_computeCascades(
                world, engine, &views[i], &viewImpls[i]);
        }
    }
    FLECS_TRACY_ZONE_END;
}

void flecsEngine_renderView_uploadShadowsAll(
    ecs_world_t *world,
    FlecsEngineImpl *engine)
{
    (void)world;
    (void)engine;
}

void flecsEngine_renderView_renderShadow(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view,
    FlecsRenderViewImpl *view_impl,
    WGPUCommandEncoder encoder)
{
    FLECS_TRACY_ZONE_BEGIN("ShadowPass");
    if (!view_impl->shadow.texture_view || !view_impl->main_light) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    const FlecsRenderBatchSet *batch_set = ecs_get(
        world, view_entity, FlecsRenderBatchSet);
    if (!batch_set) {
        return;
    }

    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
        wgpuQueueWriteBuffer(
            engine->queue,
            view_impl->shadow.vp_buffers[c],
            0,
            view_impl->shadow.current_light_vp[c],
            sizeof(mat4));
    }

    /* Render each cascade into its own texture array layer */
    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
        if (!view_impl->shadow.layer_views[c]) {
            continue;
        }

        view_impl->shadow.current_cascade = c;

        /* Begin shadow depth-only render pass for this cascade layer */
        WGPURenderPassDepthStencilAttachment depth_attachment = {
            .view = view_impl->shadow.layer_views[c],
            .depthLoadOp = WGPULoadOp_Clear,
            .depthStoreOp = WGPUStoreOp_Store,
            .depthClearValue = 1.0f,
            .depthReadOnly = false,
            .stencilLoadOp = WGPULoadOp_Undefined,
            .stencilStoreOp = WGPUStoreOp_Undefined,
            .stencilClearValue = 0,
            .stencilReadOnly = true
        };

        WGPURenderPassTimestampWrites ts_writes;
        const char *ts_names[FLECS_ENGINE_SHADOW_CASCADE_COUNT] = {
            "ShadowC0", "ShadowC1", "ShadowC2", "ShadowC3"
        };
        int ts_pair = flecsEngine_gpuTiming_allocPair(engine,
            ts_names[c < FLECS_ENGINE_SHADOW_CASCADE_COUNT ? c : 0]);
        flecsEngine_gpuTiming_renderPassTimestamps(engine, ts_pair, &ts_writes);

        WGPURenderPassDescriptor pass_desc = {
            .colorAttachmentCount = 0,
            .colorAttachments = NULL,
            .depthStencilAttachment = &depth_attachment,
            .timestampWrites = WGPU_TIMESTAMP_WRITES(ts_pair >= 0 ? &ts_writes : NULL)
        };

        WGPURenderPassEncoder shadow_pass = wgpuCommandEncoderBeginRenderPass(
            encoder, &pass_desc);

        wgpuRenderPassEncoderSetViewport(
            shadow_pass,
            0.0f, 0.0f,
            (float)view_impl->shadow.map_size,
            (float)view_impl->shadow.map_size,
            0.0f, 1.0f);

        view_impl->last_pipeline = NULL;

        flecsEngine_renderBatchSet_renderShadow(
            world, engine, view_impl, batch_set, shadow_pass);

        wgpuRenderPassEncoderEnd(shadow_pass);
        wgpuRenderPassEncoderRelease(shadow_pass);
    }

    FLECS_TRACY_ZONE_END;
}
