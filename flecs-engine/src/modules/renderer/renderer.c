#include "renderer.h"
#include "batches/batches.h"
#include "gpu_cull.h"
#include "hiz.h"
#include "gpu_timing.h"
#include "../engine/engine.h"
#include "../ui2d/ui2d.h"
#include "../../tracy_hooks.h"

#define FLECS_ENGINE_RENDERER_IMPL
#define FLECS_ENGINE_RENDERER_IMPL_IMPL
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsGpuVertex);
ECS_COMPONENT_DECLARE(FlecsGpuVertexLitUv);
ECS_COMPONENT_DECLARE(FlecsGpuTransform);
ECS_COMPONENT_DECLARE(FlecsTextureImpl);
extern ECS_COMPONENT_DECLARE(FlecsPbrTextures);
extern ECS_COMPONENT_DECLARE(FlecsRgba);
extern ECS_COMPONENT_DECLARE(FlecsPbrMaterial);
extern ECS_COMPONENT_DECLARE(FlecsEmissive);
extern ECS_COMPONENT_DECLARE(FlecsMaterialId);
ECS_COMPONENT_DECLARE(FlecsGpuUniforms);

static void flecsEngine_releaseFrameTarget(
    FlecsEngineSurface *target)
{
    if (target->owns_view_texture && target->view_texture) {
        wgpuTextureViewRelease(target->view_texture);
    }

    if (target->surface_texture) {
        wgpuTextureRelease(target->surface_texture);
    }

    if (target->readback_buffer) {
        wgpuBufferRelease(target->readback_buffer);
    }

    target->view_texture = NULL;
    target->surface_texture = NULL;
    target->owns_view_texture = false;
    target->surface_status = WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal;
    target->readback_buffer = NULL;
    target->readback_bytes_per_row = 0;
    target->readback_buffer_size = 0;
}

void flecsEngine_renderer_cleanup(
    FlecsEngineImpl *impl)
{
    FLECS_WGPU_RELEASE(impl->textures.pbr_sampler, wgpuSamplerRelease);
    FLECS_WGPU_RELEASE(impl->textures.pbr_low_sampler, wgpuSamplerRelease);
    FLECS_WGPU_RELEASE(impl->pipelines.passthrough_pipeline, wgpuRenderPipelineRelease);
    FLECS_WGPU_RELEASE(impl->pipelines.passthrough_bind_layout, wgpuBindGroupLayoutRelease);
    FLECS_WGPU_RELEASE(impl->pipelines.passthrough_sampler, wgpuSamplerRelease);
    FLECS_WGPU_RELEASE(impl->pipelines.depth_resolve_pipeline, wgpuRenderPipelineRelease);
    FLECS_WGPU_RELEASE(impl->pipelines.depth_resolve_bind_layout, wgpuBindGroupLayoutRelease);
    flecsEngine_gpuCull_fini(impl);
    flecsEngine_hiz_fini(impl);
    flecsEngine_gpuTiming_fini(impl);
}

static void flecsEngine_rebuildBatchPipelines(
    ecs_world_t *world)
{
    ecs_query_t *q = ecs_query(world, {
        .terms = {{ ecs_id(FlecsRenderBatch) }}
    });
    if (!q) {
        return;
    }

    ecs_defer_suspend(world);

    ecs_iter_t it = ecs_query_iter(world, q);
    while (ecs_query_next(&it)) {
        for (int32_t i = 0; i < it.count; i++) {
            ecs_modified_id(world, it.entities[i], ecs_id(FlecsRenderBatch));
        }
    }

    ecs_defer_resume(world);
    ecs_query_fini(q);
}

int flecsEngine_initRenderer(
    ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    impl->hdr_color_format = WGPUTextureFormat_RGBA16Float;

    ecs_entity_t engine_parent = ecs_lookup(world, "flecs.engine");

    impl->view_query = ecs_query(world, {
        .entity = ecs_entity(world, {
            .parent = engine_parent
        }),
        .terms = {{ ecs_id(FlecsRenderView) }, { ecs_id(FlecsRenderViewImpl) }},
        .cache_kind = EcsQueryCacheAuto
    });

    if (flecsEngine_shadow_initShared(world, impl)) {
        goto error;
    }

    if (flecsEngine_cluster_initLights(impl)) {
        goto error;
    }

    impl->materials.query = ecs_query(world, {
        .entity = ecs_entity(world, {
            .parent = engine_parent
        }),
        .terms = {
            { .id = ecs_id(FlecsRgba) },
            { .id = ecs_id(FlecsPbrMaterial), .oper = EcsOptional },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsSelf },
            { .id = ecs_id(FlecsEmissive), .oper = EcsOptional },
            { .id = ecs_id(FlecsTransmission), .oper = EcsOptional },
            { .id = ecs_id(FlecsTextureTransform), .oper = EcsOptional },
            { .id = EcsPrefab, .src.id = EcsSelf }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    impl->textures.query = ecs_query(world, {
        .entity = ecs_entity(world, {
            .parent = engine_parent
        }),
        .terms = {
            { .id = ecs_id(FlecsPbrTextures), .src.id = EcsSelf },
            { .id = ecs_id(FlecsMaterialId), .src.id = EcsSelf },
            { .id = EcsPrefab, .src.id = EcsSelf }
        },
        .cache_kind = EcsQueryCacheAuto
    });

    impl->lighting.point_light_query = ecs_query(world, {
        .entity = ecs_entity(world, {
            .parent = engine_parent
        }),
        .terms = {
            { .id = ecs_id(FlecsPointLight), .src.id = EcsSelf },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf, .oper = EcsOptional },
        },
        .cache_kind = EcsQueryCacheAuto
    });

    impl->lighting.spot_light_query = ecs_query(world, {
        .entity = ecs_entity(world, {
            .parent = engine_parent
        }),
        .terms = {
            { .id = ecs_id(FlecsSpotLight), .src.id = EcsSelf },
            { .id = ecs_id(FlecsWorldTransform3), .src.id = EcsSelf },
            { .id = ecs_id(FlecsRgba), .src.id = EcsSelf, .oper = EcsOptional },
        },
        .cache_kind = EcsQueryCacheAuto
    });

    impl->fallback_hdri = flecsEngine_createHdri(
        world, engine_parent, "FallbackHdri", NULL, 1014, 64);

    if (flecsEngine_initPassthrough(world, impl)) {
        goto error;
    }

    if (flecsEngine_initDepthResolve(world, impl)) {
        goto error;
    }

    if (flecsEngine_gpuCull_init(world, impl)) {
        goto error;
    }

    if (flecsEngine_hiz_init(world, impl)) {
        goto error;
    }

    flecsEngine_textureBlit_init(world, impl);

    flecsEngine_gpuTiming_init(impl);

    return 0;
error:
    return -1;
}

static void FlecsEngineMaterialManager(
    ecs_iter_t *it)
{
    FLECS_TRACY_ZONE_BEGIN("MaterialManager");
    FlecsEngineImpl *impl = ecs_field(it, FlecsEngineImpl, 0);

    if (!impl->device || !impl->queue) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    const FlecsSurface *surface = ecs_get(it->world, impl->surface, FlecsSurface);
    if (!surface) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    FLECS_TRACY_ZONE_BEGIN_N(__matu, "MaterialUploadBuffer");
    flecsEngine_material_uploadBuffer(it->world, impl);
    FLECS_TRACY_ZONE_END_N(__matu);

    if (impl->textures.fallback_bind_group &&
        impl->textures.applied_texture_quality !=
            (int32_t)surface->texture_quality)
    {
        flecsEngine_material_buildTextureArrays(it->world, impl);
    }

    {
        uint16_t desired_aniso = (surface->anisotropy != FlecsAnisotropyDefault)
            ? (uint16_t)surface->anisotropy
            : (uint16_t)FlecsAnisotropyHigh;
        if (impl->textures.applied_max_aniso &&
            impl->textures.applied_max_aniso != desired_aniso &&
            impl->textures.fallback_bind_group)
        {
            flecsEngine_pbr_texture_ensureSamplers(impl, desired_aniso);
            flecsEngine_textureArray_rebuildBindGroups(impl);
        }
    }

    if (!impl->textures.fallback_bind_group) {
        FLECS_TRACY_ZONE_BEGIN_N(__mta, "BuildTextureArrays");
        flecsEngine_material_buildTextureArrays(it->world, impl);
        FLECS_TRACY_ZONE_END_N(__mta);
    }

    FLECS_TRACY_ZONE_END;
}

static void FlecsEngineExtract(
    ecs_iter_t *it)
{
    FLECS_TRACY_ZONE_BEGIN("Extract");
    FlecsEngineImpl *impl = ecs_field(it, FlecsEngineImpl, 0);

    if (!impl->device || !impl->queue) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    flecsEngine_renderView_extractAll(it->world, impl);
    FLECS_TRACY_ZONE_END;
}

static void FlecsEngineCull(
    ecs_iter_t *it)
{
    FLECS_TRACY_ZONE_BEGIN("Cull");
    FlecsEngineImpl *impl = ecs_field(it, FlecsEngineImpl, 0);

    if (!impl->device || !impl->queue) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    flecsEngine_renderView_cullAll(it->world, impl);
    FLECS_TRACY_ZONE_END;
}

static void FlecsEngineCullShadows(
    ecs_iter_t *it)
{
    FLECS_TRACY_ZONE_BEGIN("CullShadows");
    FlecsEngineImpl *impl = ecs_field(it, FlecsEngineImpl, 0);

    if (!impl->device || !impl->queue) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    flecsEngine_renderView_cullShadowsAll(it->world, impl);
    FLECS_TRACY_ZONE_END;
}

static void FlecsEngineUpload(
    ecs_iter_t *it)
{
    FLECS_TRACY_ZONE_BEGIN("Upload");
    FlecsEngineImpl *impl = ecs_field(it, FlecsEngineImpl, 0);

    if (!impl->device || !impl->queue) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    flecsEngine_renderView_uploadAll(it->world, impl);
    flecsEngine_renderView_uploadShadowsAll(it->world, impl);
    FLECS_TRACY_ZONE_END;
}

static void FlecsEngineRender(
    ecs_iter_t *it)
{
    FLECS_TRACY_ZONE_BEGIN("Render");
#ifndef __EMSCRIPTEN__
    FLECS_TRACY_ZONE_BEGIN_N(__dbg, "DebugServerDequeue");
    flecsEngine_debugServer_dequeue(it->delta_time);
    FLECS_TRACY_ZONE_END_N(__dbg);
#endif
    FlecsEngineImpl *impl = ecs_field(it, FlecsEngineImpl, 0);

    FlecsSurfaceImpl *surf = ecs_get_mut(
        it->world, impl->surface, FlecsSurfaceImpl);

    FLECS_TRACY_ZONE_BEGIN_N(__prepare, "PrepareFrame");
    int prep_result = flecsEngine_surfaceInterface_prepareFrame(
        it->world, impl, surf);
    FLECS_TRACY_ZONE_END_N(__prepare);
    if (prep_result > 0) {
        FLECS_TRACY_ZONE_END;
        return;
    }
    if (prep_result < 0) {
        flecsEngine_surfaceInterface_onFrameFailed(
            it->world, impl, surf);
        FLECS_TRACY_ZONE_END;
        return;
    }

    const FlecsSurface *surface = ecs_get(it->world, impl->surface, FlecsSurface);
    if (!surface->width || !surface->height) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    /* Recompute actual dimensions for runtime resolution_scale changes */
    flecsEngine_surface_set(
        it->world,
        impl->surface,
        surface->width,
        surface->height,
        surface->resolution_scale);

    /* Per-view depth/MSAA targets are allocated during flecsEngine_renderView_render.
     * When sample_count changes, rebuild batch pipelines before rendering views. */
    {
        int32_t sc = flecsEngine_surface_sampleCount(surface);
        int32_t cur_sc = sc < 2 ? 0 : sc;
        if (surf->prev_sample_count != cur_sc) {
            FLECS_TRACY_ZONE_BEGIN_N(__rb, "RebuildBatchPipelines");
            flecsEngine_rebuildBatchPipelines(it->world);
            FLECS_TRACY_ZONE_END_N(__rb);
            surf->prev_sample_count = cur_sc;
        }
    }

    bool failed = false;
    FlecsEngineSurface frame_target = {0};
    WGPUCommandEncoder encoder = NULL;
    WGPUCommandBuffer cmd = NULL;

    FLECS_TRACY_ZONE_BEGIN_N(__acquire, "AcquireFrame");
    int target_result = flecsEngine_surfaceInterface_acquireFrame(
        impl, surf, &frame_target);
    FLECS_TRACY_ZONE_END_N(__acquire);
    if (target_result > 0) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    if (target_result < 0) {
        failed = true;
        goto cleanup;
    }

    WGPUCommandEncoderDescriptor encoder_desc = {0};
    encoder = wgpuDeviceCreateCommandEncoder(impl->device, &encoder_desc);
    if (!encoder) {
        ecs_err("Failed to create command encoder\n");
        failed = true;
        goto cleanup;
    }

    flecsEngine_gpuTiming_logIfReady(impl);
    flecsEngine_gpuTiming_beginFrame(impl, surface->gpu_timings);

    // Render all views
    flecsEngine_renderView_renderAll(
        it->world, impl, frame_target.view_texture, encoder);

    // 2D overlay (HUD) on top of the finished frame
    flecsEngine_ui2d_render(
        it->world, impl, frame_target.view_texture, encoder);

    FLECS_TRACY_ZONE_BEGIN_N(__enc, "EncodeFrame");
    bool enc_err = flecsEngine_surfaceInterface_encodeFrame(
        it->world, impl, surf, encoder, &frame_target);
    FLECS_TRACY_ZONE_END_N(__enc);
    if (enc_err) {
        failed = true;
        goto cleanup;
    }

    flecsEngine_gpuTiming_endFrame(impl, encoder);

    WGPUCommandBufferDescriptor cmd_desc = {0};
    FLECS_TRACY_ZONE_BEGIN_N(__finish, "EncoderFinish");
    cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    FLECS_TRACY_ZONE_END_N(__finish);
    if (!cmd) {
        ecs_err("Failed to create command buffer\n");
        failed = true;
        goto cleanup;
    }

    FLECS_TRACY_ZONE_BEGIN_N(__submit, "SubmitFrame");
    wgpuQueueSubmit(impl->queue, 1, &cmd);
    flecsEngine_gpuTiming_afterSubmit(impl);

    if (flecsEngine_surfaceInterface_submitFrame(
        it->world, impl, surf, &frame_target))
    {
        failed = true;
    }
    FLECS_TRACY_ZONE_END_N(__submit);

cleanup:
    if (cmd) {
        wgpuCommandBufferRelease(cmd);
    }
    if (encoder) {
        wgpuCommandEncoderRelease(encoder);
    }

    flecsEngine_releaseFrameTarget(&frame_target);

    if (failed) {
        flecsEngine_surfaceInterface_onFrameFailed(
            it->world, impl, surf);
    }

    FLECS_TRACY_FRAME_MARK;
    FLECS_TRACY_ZONE_END;
}

static void flecsEngine_textureImpl_releaseImpl(
    FlecsTextureImpl *ptr)
{
    FLECS_WGPU_RELEASE(ptr->view, wgpuTextureViewRelease);
    FLECS_WGPU_RELEASE(ptr->texture, wgpuTextureRelease);
}

ECS_DTOR(FlecsTextureImpl, ptr, {
    flecsEngine_textureImpl_releaseImpl(ptr);
})

ECS_MOVE(FlecsTextureImpl, dst, src, {
    flecsEngine_textureImpl_releaseImpl(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

void FlecsEngineRendererImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineRenderer);

    ecs_set_name_prefix(world, "Flecs");

    ECS_COMPONENT_DEFINE(world, FlecsGpuVertex);
    ECS_COMPONENT_DEFINE(world, FlecsGpuVertexLitUv);
    ECS_COMPONENT_DEFINE(world, FlecsGpuTransform);
    ECS_COMPONENT_DEFINE(world, FlecsGpuUniforms);

    ecs_struct(world, {
        .entity = ecs_id(FlecsGpuVertex),
        .members = {
            { .name = "p", .type = ecs_id(flecs_vec3_t) },
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsGpuVertexLitUv),
        .members = {
            { .name = "p", .type = ecs_id(flecs_vec3_t) },
            { .name = "n", .type = ecs_id(flecs_vec3_t) },
            { .name = "uv", .type = ecs_id(flecs_vec2_t) },
            { .name = "t", .type = ecs_id(flecs_vec4_t) }
        }
    });


    ecs_struct(world, {
        .entity = ecs_id(FlecsGpuTransform),
        .members = {
            { .name = "c0", .type = ecs_id(flecs_vec4_t) },
            { .name = "c1", .type = ecs_id(flecs_vec4_t) },
            { .name = "c2", .type = ecs_id(flecs_vec4_t) },
            { .name = "c3", .type = ecs_id(flecs_vec4_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsGpuUniforms),
        .members = {
            { .name = "mvp", .type = ecs_id(flecs_mat4_t) },
            { .name = "inv_vp", .type = ecs_id(flecs_mat4_t) },
            { .name = "light_vp", .type = ecs_id(flecs_mat4_t), .count = FLECS_ENGINE_SHADOW_CASCADE_COUNT },
            { .name = "cascade_splits", .type = ecs_id(ecs_f32_t), .count = FLECS_ENGINE_SHADOW_CASCADE_COUNT },
            { .name = "light_ray_dir", .type = ecs_id(ecs_f32_t), .count = 4 },
            { .name = "light_color", .type = ecs_id(ecs_f32_t), .count = 4 },
            { .name = "camera_pos", .type = ecs_id(ecs_f32_t), .count = 4 },
            { .name = "shadow_info", .type = ecs_id(ecs_f32_t), .count = 4 },
            { .name = "ambient_light", .type = ecs_id(ecs_f32_t), .count = 4 },
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsShader),
        .members = {
            { .name = "source", .type = ecs_id(ecs_string_t) },
            { .name = "vertex_entry", .type = ecs_id(ecs_string_t) },
            { .name = "fragment_entry", .type = ecs_id(ecs_string_t) }
        }
    });

    flecsEngine_shader_register(world);
    flecsEngine_renderBatch_register(world);
    flecsEngine_renderBatchSet_register(world);
    flecsEngine_renderEffect_register(world);
    flecsEngine_renderView_register(world);
    flecsEngine_batchSets_register(world);
    flecsEngine_draw_register(world);
    flecsEngine_ibl_register(world);
    flecsEngine_tonyMcMapFace_register(world);
    flecsEngine_bloom_register(world);
    flecsEngine_heightFog_register(world);
    flecsEngine_ssao_register(world);
    flecsEngine_sunShafts_register(world);
    flecsEngine_clouds_register(world);
    flecsEngine_autoExposure_register(world);
    flecsEngine_invert_register(world);
    flecsEngine_gammaCorrect_register(world);

    /* Register FlecsTextureImpl (renderer-side companion for FlecsTexture) */
    ECS_COMPONENT_DEFINE(world, FlecsTextureImpl);
    ecs_set_hooks(world, FlecsTextureImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsTextureImpl),
        .dtor = ecs_dtor(FlecsTextureImpl)
    });
    ecs_add_pair(world, ecs_id(FlecsTexture), EcsWith, ecs_id(FlecsTextureImpl));

    {
        const ecs_type_info_t *ti;
        ecs_type_hooks_t hooks;

        ti = ecs_get_type_info(world, ecs_id(FlecsTexture));
        hooks = ti->hooks;
        hooks.on_set = flecsEngine_texture_onSet;
        ecs_set_hooks_id(world, ecs_id(FlecsTexture), &hooks);
    }

    ecs_set_name_prefix(world, "FlecsEngine");

    ECS_SYSTEM(world, FlecsEngineMaterialManager, EcsOnStore,
        flecs.engine.EngineImpl);

    ECS_SYSTEM(world, FlecsEngineExtract, EcsOnStore,
        flecs.engine.EngineImpl);

    ECS_SYSTEM(world, FlecsEngineCull, EcsOnStore,
        flecs.engine.EngineImpl);

    ECS_SYSTEM(world, FlecsEngineCullShadows, EcsOnStore,
        flecs.engine.EngineImpl);

    ECS_SYSTEM(world, FlecsEngineUpload, EcsOnStore,
        flecs.engine.EngineImpl);

    ECS_SYSTEM(world, FlecsEngineRender, EcsOnStore,
        flecs.engine.EngineImpl);

    ecs_system_update(world, ecs_id(FlecsEngineRender), &(ecs_system_desc_t){
        .immediate = true
    });

#ifndef __EMSCRIPTEN__
    flecsEngine_debugServer_init(world);
#endif
}
