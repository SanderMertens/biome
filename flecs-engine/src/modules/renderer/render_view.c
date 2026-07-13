#include <string.h>

#include "renderer.h"
#include "../atmosphere/atmosphere.h"
#include "../particles/particles.h"
#include "frustum_cull.h"
#include "gpu_cull.h"
#include "hiz.h"
#include "gpu_timing.h"
#include "../../tracy_hooks.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(flecs_engine_background_t);
ECS_COMPONENT_DECLARE(flecs_engine_shadow_params_t);
ECS_COMPONENT_DECLARE(flecs_render_view_effect_t);
ECS_COMPONENT_DECLARE(FlecsRenderView);
ECS_COMPONENT_DECLARE(FlecsRenderViewImpl);

ECS_CTOR(FlecsRenderView, ptr, {
    ecs_vec_init_t(NULL, &ptr->effects, flecs_render_view_effect_t, 0);
    ecs_os_zeromem(ptr);
    ptr->ambient_intensity = 1.0f;
    ptr->shadow.enabled = true;
    ptr->shadow.map_size = FLECS_ENGINE_SHADOW_MAP_SIZE_DEFAULT;
    ptr->shadow.bias = 0.0005f;
    ptr->shadow.max_range = 100.0f;
})

ECS_MOVE(FlecsRenderView, dst, src, {
    ecs_vec_fini_t(NULL, &dst->effects, flecs_render_view_effect_t);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_COPY(FlecsRenderView, dst, src, {
    ecs_vec_fini_t(NULL, &dst->effects, flecs_render_view_effect_t);
    dst->camera = src->camera;
    dst->atmosphere = src->atmosphere;
    dst->hdri = src->hdri;
    dst->ambient_intensity = src->ambient_intensity;
    dst->shadow = src->shadow;
    dst->screen_px_threshold = src->screen_px_threshold;
    dst->effects = ecs_vec_copy_t(NULL, &src->effects, flecs_render_view_effect_t);
})

ECS_DTOR(FlecsRenderView, ptr, {
    ecs_vec_fini_t(NULL, &ptr->effects, flecs_render_view_effect_t);
})

ECS_MOVE(flecs_render_view_effect_t, dst, src, {
    *dst = *src;
})

static void flecsEngine_renderView_createDepthResources(
    WGPUDevice device,
    uint32_t width,
    uint32_t height,
    WGPUTexture *texture,
    WGPUTextureView *view)
{
    if (*view) {
        wgpuTextureViewRelease(*view);
        *view = NULL;
    }

    if (*texture) {
        wgpuTextureRelease(*texture);
        *texture = NULL;
    }

    WGPUTextureDescriptor depth_desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = (WGPUExtent3D){
            .width = width,
            .height = height,
            .depthOrArrayLayers = 1
        },
        .format = WGPUTextureFormat_Depth32Float,
        .mipLevelCount = 1,
        .sampleCount = 1
    };

    *texture = wgpuDeviceCreateTexture(device, &depth_desc);
    if (!*texture) {
        ecs_err("Failed to create depth texture\n");
        return;
    }

    *view = wgpuTextureCreateView(*texture, NULL);
    if (!*view) {
        ecs_err("Failed to create depth texture view\n");
        wgpuTextureRelease(*texture);
        *texture = NULL;
    }
}

static int flecsEngine_renderView_ensureDepthResources(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *impl)
{
    const FlecsSurface *surface = ecs_get(world, engine->surface, FlecsSurface);
    uint32_t width = (uint32_t)surface->actual_width;
    uint32_t height = (uint32_t)surface->actual_height;

    if (impl->depth_texture &&
        impl->depth_texture_view &&
        impl->depth_texture_width == width &&
        impl->depth_texture_height == height)
    {
        return 0;
    }

    flecsEngine_renderView_createDepthResources(
        engine->device,
        width,
        height,
        &impl->depth_texture,
        &impl->depth_texture_view);
    if (!impl->depth_texture || !impl->depth_texture_view) {
        impl->depth_texture_width = 0;
        impl->depth_texture_height = 0;
        return -1;
    }

    impl->depth_texture_width = width;
    impl->depth_texture_height = height;
    return 0;
}

static void flecsEngine_renderView_releaseMsaaResources(
    FlecsRenderViewImpl *impl)
{
    FLECS_WGPU_RELEASE(impl->depth_resolve_bind_group, wgpuBindGroupRelease);
    impl->depth_resolve_bind_view = NULL;
    FLECS_WGPU_RELEASE(impl->msaa_color_texture_view, wgpuTextureViewRelease);
    FLECS_WGPU_RELEASE(impl->msaa_color_texture, wgpuTextureRelease);
    FLECS_WGPU_RELEASE(impl->msaa_depth_texture_view, wgpuTextureViewRelease);
    FLECS_WGPU_RELEASE(impl->msaa_depth_texture, wgpuTextureRelease);
    impl->msaa_texture_width = 0;
    impl->msaa_texture_height = 0;
    impl->msaa_texture_sample_count = 0;
    impl->msaa_color_format = WGPUTextureFormat_Undefined;
}

static void flecsEngine_renderView_releaseDepth(
    FlecsRenderViewImpl *impl)
{
    flecsEngine_renderView_releaseMsaaResources(impl);

    FLECS_WGPU_RELEASE(impl->depth_texture_view, wgpuTextureViewRelease);
    FLECS_WGPU_RELEASE(impl->depth_texture, wgpuTextureRelease);
    impl->depth_texture_width = 0;
    impl->depth_texture_height = 0;
}

static int flecsEngine_renderView_ensureMsaaResources(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *impl)
{
    const FlecsSurface *surface = ecs_get(world, engine->surface, FlecsSurface);
    int32_t sc = flecsEngine_surface_sampleCount(surface);
    if (sc < 2) {
        if (impl->msaa_color_texture) {
            flecsEngine_renderView_releaseMsaaResources(impl);
        }
        return 0;
    }

    uint32_t width = (uint32_t)surface->actual_width;
    uint32_t height = (uint32_t)surface->actual_height;
    WGPUTextureFormat color_format = flecsEngine_getHdrFormat(engine);

    if (impl->msaa_color_texture &&
        impl->msaa_depth_texture &&
        impl->msaa_texture_width == width &&
        impl->msaa_texture_height == height &&
        impl->msaa_texture_sample_count == sc &&
        impl->msaa_color_format == color_format)
    {
        return 0;
    }

    flecsEngine_renderView_releaseMsaaResources(impl);

    WGPUTextureDescriptor color_desc = {
        .usage = WGPUTextureUsage_RenderAttachment,
        .dimension = WGPUTextureDimension_2D,
        .size = (WGPUExtent3D){
            .width = width,
            .height = height,
            .depthOrArrayLayers = 1
        },
        .format = color_format,
        .mipLevelCount = 1,
        .sampleCount = (uint32_t)sc
    };

    impl->msaa_color_texture = wgpuDeviceCreateTexture(
        engine->device, &color_desc);
    if (!impl->msaa_color_texture) {
        ecs_err("Failed to create MSAA color texture");
        goto error;
    }

    impl->msaa_color_texture_view = wgpuTextureCreateView(
        impl->msaa_color_texture, NULL);
    if (!impl->msaa_color_texture_view) {
        ecs_err("Failed to create MSAA color texture view");
        goto error;
    }

    WGPUTextureDescriptor depth_desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = (WGPUExtent3D){
            .width = width,
            .height = height,
            .depthOrArrayLayers = 1
        },
        .format = WGPUTextureFormat_Depth32Float,
        .mipLevelCount = 1,
        .sampleCount = (uint32_t)sc
    };

    impl->msaa_depth_texture = wgpuDeviceCreateTexture(
        engine->device, &depth_desc);
    if (!impl->msaa_depth_texture) {
        ecs_err("Failed to create MSAA depth texture");
        goto error;
    }

    impl->msaa_depth_texture_view = wgpuTextureCreateView(
        impl->msaa_depth_texture, NULL);
    if (!impl->msaa_depth_texture_view) {
        ecs_err("Failed to create MSAA depth texture view");
        goto error;
    }

    impl->msaa_texture_width = width;
    impl->msaa_texture_height = height;
    impl->msaa_texture_sample_count = sc;
    impl->msaa_color_format = color_format;
    return 0;

error:
    flecsEngine_renderView_releaseMsaaResources(impl);
    return -1;
}

static void flecsEngine_renderView_releaseTargets(
    FlecsRenderViewImpl *impl)
{
    if (!impl) {
        return;
    }

    for (int32_t i = 0; i < impl->effect_target_count; i ++) {
        if (impl->effect_target_views) {
            FLECS_WGPU_RELEASE(impl->effect_target_views[i], wgpuTextureViewRelease);
        }
        if (impl->effect_target_textures) {
            FLECS_WGPU_RELEASE(impl->effect_target_textures[i], wgpuTextureRelease);
        }
    }

    if (impl->effect_target_views) {
        ecs_os_free(impl->effect_target_views);
        impl->effect_target_views = NULL;
    }
    if (impl->effect_target_textures) {
        ecs_os_free(impl->effect_target_textures);
        impl->effect_target_textures = NULL;
    }

    FLECS_WGPU_RELEASE(impl->passthrough_bind_group, wgpuBindGroupRelease);
    FLECS_WGPU_RELEASE(impl->upscale_bind_group, wgpuBindGroupRelease);
    impl->upscale_bind_input_view = NULL;
    FLECS_WGPU_RELEASE(impl->scene_target_view, wgpuTextureViewRelease);
    FLECS_WGPU_RELEASE(impl->scene_target_texture, wgpuTextureRelease);

    impl->effect_target_count = 0;
    impl->effect_target_width = 0;
    impl->effect_target_height = 0;
    impl->effect_target_format = WGPUTextureFormat_Undefined;
}

static void flecsEngine_renderView_releaseImpl(
    FlecsRenderViewImpl *impl)
{
    flecsEngine_renderView_releaseTargets(impl);
    flecsEngine_renderView_releaseDepth(impl);
    flecsEngine_shadow_cleanupView(impl);
    flecsEngine_cluster_cleanupView(impl);
    flecsEngine_transmission_releaseView(impl);
    flecsEngine_gpuCull_finiView(impl);
    flecsEngine_hiz_finiView(NULL, impl);

    FLECS_WGPU_RELEASE(impl->scene_bind_group, wgpuBindGroupRelease);
    FLECS_WGPU_RELEASE(impl->frame_uniform_buffer, wgpuBufferRelease);
}

static bool flecsEngine_renderView_ensureSceneTarget(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *impl)
{
    const FlecsSurface *surface = ecs_get(world, engine->surface, FlecsSurface);
    uint32_t w = (uint32_t)surface->actual_width;
    uint32_t h = (uint32_t)surface->actual_height;
    if (impl->scene_target_texture &&
        impl->effect_target_width == w &&
        impl->effect_target_height == h)
    {
        return true;
    }
    FLECS_WGPU_RELEASE(impl->scene_target_view, wgpuTextureViewRelease);
    FLECS_WGPU_RELEASE(impl->scene_target_texture, wgpuTextureRelease);
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_RenderAttachment
               | WGPUTextureUsage_TextureBinding
               | WGPUTextureUsage_CopySrc,
        .dimension = WGPUTextureDimension_2D,
        .size = (WGPUExtent3D){ .width = w, .height = h, .depthOrArrayLayers = 1 },
        .format = impl->effect_target_format,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    impl->scene_target_texture = wgpuDeviceCreateTexture(engine->device, &desc);
    if (!impl->scene_target_texture) return false;
    impl->scene_target_view = wgpuTextureCreateView(
        impl->scene_target_texture, NULL);
    return impl->scene_target_view != NULL;
}

static void flecsEngine_renderView_releaseSceneTarget(
    FlecsRenderViewImpl *impl)
{
    FLECS_WGPU_RELEASE(impl->scene_target_view, wgpuTextureViewRelease);
    FLECS_WGPU_RELEASE(impl->scene_target_texture, wgpuTextureRelease);
}

ECS_DTOR(FlecsRenderViewImpl, ptr, {
    flecsEngine_renderView_releaseImpl(ptr);
})

ECS_MOVE(FlecsRenderViewImpl, dst, src, {
    flecsEngine_renderView_releaseImpl(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

static void flecsEngine_renderView_implOnAdd(ecs_iter_t *it) {
    FlecsRenderViewImpl *impls = ecs_field(it, FlecsRenderViewImpl, 0);
    for (int32_t i = 0; i < it->count; i ++) {
        if (!impls[i].light_query) {
            impls[i].light_query = ecs_query(it->world, {
                .terms = {{ ecs_id(FlecsDirectionalLight) }},
                .cache_kind = EcsQueryCacheAuto
            });
        }
    }
}

static void flecsEngine_renderView_implOnRemove(ecs_iter_t *it) {
    FlecsRenderViewImpl *impls = ecs_field(it, FlecsRenderViewImpl, 0);
    for (int32_t i = 0; i < it->count; i ++) {
        if (impls[i].light_query) {
            ecs_query_fini(impls[i].light_query);
            impls[i].light_query = NULL;
        }
    }
}

static void flecsEngine_renderView_logErr(
    const ecs_world_t *world,
    ecs_entity_t entity,
    const char *fmt)
{
    char *name = ecs_get_path(world, entity);
    ecs_err(fmt, name);
    ecs_os_free(name);
}

static void flecsEngine_renderView_writeCamera(
    const ecs_world_t *world,
    FlecsGpuUniforms *uniforms,
    ecs_entity_t entity)
{
    uniforms->camera_pos[0] = 0.0f;
    uniforms->camera_pos[1] = 0.0f;
    uniforms->camera_pos[2] = 0.0f;
    uniforms->camera_pos[3] = 1.0f;

    glm_mat4_identity(uniforms->mvp);
    glm_mat4_identity(uniforms->inv_vp);

    const FlecsCameraImpl *camera = ecs_get(
        world, entity, FlecsCameraImpl);
    if (!camera) {
        flecsEngine_renderView_logErr(world, entity,
            "invalid camera '%s' in view");
        return;
    }

    glm_mat4_copy((vec4*)camera->mvp, uniforms->mvp);
    glm_mat4_inv(uniforms->mvp, uniforms->inv_vp);

    const FlecsWorldTransform3 *camera_transform = ecs_get(
        world, entity, FlecsWorldTransform3);
    if (camera_transform) {
        uniforms->camera_pos[0] = camera_transform->m[3][0];
        uniforms->camera_pos[1] = camera_transform->m[3][1];
        uniforms->camera_pos[2] = camera_transform->m[3][2];
    }
}

static void flecsEngine_renderView_writeLight(
    const ecs_world_t *world,
    FlecsGpuUniforms *uniforms,
    ecs_entity_t entity)
{
    uniforms->light_color[0] = 1.0f;
    uniforms->light_color[1] = 1.0f;
    uniforms->light_color[2] = 1.0f;
    uniforms->light_color[3] = 1.0f;

    const FlecsDirectionalLight *light = ecs_get(
        world, entity, FlecsDirectionalLight);
    if (!light) {
        flecsEngine_renderView_logErr(world, entity,
            "invalid directional light '%s'");
        return;
    }

    const FlecsRotation3 *rotation = ecs_get(world, entity, FlecsRotation3);
    if (!rotation) {
        flecsEngine_renderView_logErr(world, entity,
            "directional light '%s' is missing Rotation3");
        return;
    }

    vec3 ray_dir;
    if (!flecsEngine_lightDirFromRotation(rotation, ray_dir)) {
        flecsEngine_renderView_logErr(world, entity,
            "directional light '%s' has invalid Rotation3");
        return;
    }

    uniforms->light_ray_dir[0] = ray_dir[0];
    uniforms->light_ray_dir[1] = ray_dir[1];
    uniforms->light_ray_dir[2] = ray_dir[2];

    FlecsRgba rgb = {255, 255, 255, 255};
    const FlecsRgba *light_rgb = ecs_get(world, entity, FlecsRgba);
    if (light_rgb) {
        rgb = *light_rgb;
    }

    uniforms->light_color[0] =
        flecsEngine_colorChannelToFloat(rgb.r) * light->intensity;
    uniforms->light_color[1] =
        flecsEngine_colorChannelToFloat(rgb.g) * light->intensity;
    uniforms->light_color[2] =
        flecsEngine_colorChannelToFloat(rgb.b) * light->intensity;
}

static void flecsEngine_renderView_writeFrameUniforms(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    const FlecsRenderView *view)
{
    FLECS_TRACY_ZONE_BEGIN("WriteFrameUniforms");
    if (!view_impl->frame_uniform_buffer) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    FlecsGpuUniforms uniforms = {0};
    uniforms.camera_pos[3] = 1.0f;

    if (view->camera) {
        flecsEngine_renderView_writeCamera(world, &uniforms, view->camera);
    }

    if (view_impl->main_light) {
        flecsEngine_renderView_writeLight(
            world, &uniforms, view_impl->main_light);
    }

    for (int i = 0; i < FLECS_ENGINE_SHADOW_CASCADE_COUNT; i++) {
        glm_mat4_copy((vec4*)view_impl->shadow.current_light_vp[i],
            uniforms.light_vp[i]);
    }

    memcpy(uniforms.cascade_splits, view_impl->shadow.cascade_splits,
        sizeof(float) * FLECS_ENGINE_SHADOW_CASCADE_COUNT);

    float bias = view->shadow.bias;
    if (bias <= 0) { bias = 0.0005f; }
    uniforms.shadow_info[1] = bias;

    uniforms.ambient_light[3] = view->ambient_intensity;

    uniforms.cloud_shadow_params[0] = engine->clouds.origin_x;
    uniforms.cloud_shadow_params[1] = engine->clouds.origin_z;
    uniforms.cloud_shadow_params[2] =
        (view_impl->cloud_shadow_available && engine->clouds.shadow_source_view)
            ? engine->clouds.strength : 0.0f;
    uniforms.cloud_shadow_params[3] = engine->clouds.inv_footprint;
    view_impl->cloud_shadow_available = false;

    uniforms.tint_params[0] = 1.0f;

    view_impl->camera_pos[0] = uniforms.camera_pos[0];
    view_impl->camera_pos[1] = uniforms.camera_pos[1];
    view_impl->camera_pos[2] = uniforms.camera_pos[2];

    wgpuQueueWriteBuffer(
        engine->queue,
        view_impl->frame_uniform_buffer,
        0,
        &uniforms,
        sizeof(FlecsGpuUniforms));
    FLECS_TRACY_ZONE_END;
}

static bool flecsEngine_renderView_ensureFrameUniformBuffer(
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl)
{
    if (view_impl->frame_uniform_buffer) {
        return true;
    }
    view_impl->frame_uniform_buffer = flecsEngine_createUniformBuffer(
        engine->device, sizeof(FlecsGpuUniforms));
    return view_impl->frame_uniform_buffer != NULL;
}

static bool flecsEngine_renderView_createTargets(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *impl,
    int32_t effect_count,
    WGPUTextureFormat format)
{
    const FlecsSurface *surface = ecs_get(world, engine->surface, FlecsSurface);
    uint32_t width = (uint32_t)surface->actual_width;
    uint32_t height = (uint32_t)surface->actual_height;

    impl->effect_target_textures = ecs_os_calloc_n(WGPUTexture, effect_count);
    impl->effect_target_views = ecs_os_calloc_n(WGPUTextureView, effect_count);
    if (!impl->effect_target_textures || !impl->effect_target_views) {
        goto error;
    }

    WGPUTextureDescriptor color_desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding
               | WGPUTextureUsage_CopySrc,
        .dimension = WGPUTextureDimension_2D,
        .size = (WGPUExtent3D){
            .width = width,
            .height = height,
            .depthOrArrayLayers = 1
        },
        .format = format,
        .mipLevelCount = 1,
        .sampleCount = 1
    };

    for (int32_t i = 0; i < effect_count; i ++) {
        impl->effect_target_textures[i] = wgpuDeviceCreateTexture(
            engine->device, &color_desc);
        if (!impl->effect_target_textures[i]) {
            goto error;
        }

        impl->effect_target_views[i] = wgpuTextureCreateView(
            impl->effect_target_textures[i], NULL);
        if (!impl->effect_target_views[i]) {
            goto error;
        }
    }

    impl->effect_target_count = effect_count;
    impl->effect_target_width = width;
    impl->effect_target_height = height;
    impl->effect_target_format = format;

    return true;
error:
    flecsEngine_renderView_releaseTargets(impl);
    return false;
}

static int flecsEngine_renderView_ensureTargets(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *impl,
    int32_t effect_count)
{
    if (effect_count <= 0) {
        return 0;
    }

    const FlecsSurface *surface = ecs_get(world, engine->surface, FlecsSurface);
    uint32_t width = (uint32_t)surface->actual_width;
    uint32_t height = (uint32_t)surface->actual_height;
    WGPUTextureFormat surface_format = engine->target_format;
    WGPUTextureFormat desired_format = flecsEngine_getHdrFormat(engine);

    if (impl->effect_target_count >= effect_count) {
        if (impl->effect_target_textures && impl->effect_target_views) {
            if (impl->effect_target_width == width &&
                impl->effect_target_height == height &&
                impl->effect_target_format == desired_format)
            {
                return 0;
            }
        }
    }

    flecsEngine_renderView_releaseTargets(impl);

    if (flecsEngine_renderView_createTargets(
        world, engine, impl, effect_count, desired_format))
    {
        return 0;
    }

    if (desired_format != surface_format &&
        flecsEngine_renderView_createTargets(
            world, engine, impl, effect_count, surface_format))
    {
        engine->hdr_color_format = surface_format;
        ecs_warn("falling back to LDR targets: HDR format unavailable");
        return 0;
    }

    return -1;
}

static WGPURenderPassEncoder flecsEngine_renderView_beginPass(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view,
    const FlecsRenderViewImpl *view_impl,
    WGPUCommandEncoder encoder,
    WGPUTextureView color_view,
    WGPULoadOp color_load_op,
    WGPULoadOp depth_load_op,
    const char *ts_name)
{
    (void)view;

    const FlecsSurface *surface = ecs_get(world, engine->surface, FlecsSurface);
    bool msaa = flecsEngine_surface_sampleCount(surface) > 1
        && view_impl->msaa_color_texture_view;

    WGPURenderPassColorAttachment color_attachment = {
        .view = msaa ? view_impl->msaa_color_texture_view : color_view,
        .resolveTarget = msaa ? color_view : NULL,
        WGPU_DEPTH_SLICE
        .loadOp = color_load_op,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = (WGPUColor){0, 0, 0, 1}
    };

    WGPURenderPassDepthStencilAttachment depth_attachment = {
        .view = msaa ? view_impl->msaa_depth_texture_view : view_impl->depth_texture_view,
        .depthLoadOp = depth_load_op,
        .depthStoreOp = WGPUStoreOp_Store,
        .depthClearValue = 1.0f,
        .depthReadOnly = false,
        .stencilLoadOp = WGPULoadOp_Undefined,
        .stencilStoreOp = WGPUStoreOp_Undefined,
        .stencilClearValue = 0,
        .stencilReadOnly = true
    };

    WGPURenderPassTimestampWrites ts_writes;
    int ts_pair = ts_name
        ? flecsEngine_gpuTiming_allocPair(engine, ts_name) : -1;
    flecsEngine_gpuTiming_renderPassTimestamps(engine, ts_pair, &ts_writes);

    WGPURenderPassDescriptor pass_desc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &color_attachment,
        .depthStencilAttachment = &depth_attachment,
        .timestampWrites = WGPU_TIMESTAMP_WRITES(ts_pair >= 0 ? &ts_writes : NULL)
    };

    return wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
}

static void flecsEngine_renderView_extractBatches(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    const FlecsRenderView *view)
{
    (void)view;
    FLECS_TRACY_ZONE_BEGIN("ExtractBatches");
    const FlecsRenderBatchSet *batch_set = ecs_get(
        world, view_entity, FlecsRenderBatchSet);
    ecs_assert(batch_set != NULL, ECS_INTERNAL_ERROR, NULL);

    flecsEngine_renderBatchSet_extract(world, engine, view_impl, batch_set);
    FLECS_TRACY_ZONE_END;
}

static void flecsEngine_renderView_uploadBatches(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    const FlecsRenderView *view)
{
    (void)view;
    FLECS_TRACY_ZONE_BEGIN("UploadBatches");
    const FlecsRenderBatchSet *batch_set = ecs_get(
        world, view_entity, FlecsRenderBatchSet);
    ecs_assert(batch_set != NULL, ECS_INTERNAL_ERROR, NULL);

    flecsEngine_renderBatchSet_upload(world, engine, view_impl, batch_set);
    FLECS_TRACY_ZONE_END;
}

static void flecsEngine_renderView_renderBatches(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view,
    FlecsRenderViewImpl *viewImpl,
    WGPUCommandEncoder encoder)
{
    FLECS_TRACY_ZONE_BEGIN("RenderBatches");
    const FlecsRenderBatchSet *batch_set = ecs_get(
        world, view_entity, FlecsRenderBatchSet);
    ecs_assert(batch_set != NULL, ECS_INTERNAL_ERROR, NULL);

    ecs_entity_t atmosphere = flecsEngine_renderView_atmosphere(world, view);
    WGPUTextureView batch_target = atmosphere && viewImpl->scene_target_view
        ? viewImpl->scene_target_view
        : viewImpl->effect_target_views[0];
    WGPUTexture batch_target_tex = atmosphere && viewImpl->scene_target_texture
        ? viewImpl->scene_target_texture
        : viewImpl->effect_target_textures[0];

    bool has_transmission = flecsEngine_renderBatchSet_hasTransmission(
        world, engine, batch_set);

    if (has_transmission) {
        /* Pass 1: render opaque batches (render_after_snapshot = false) */
        {
            WGPURenderPassEncoder pass = flecsEngine_renderView_beginPass(
                world, engine, view, viewImpl, encoder, batch_target,
                WGPULoadOp_Clear, WGPULoadOp_Clear, "MainOpaque");
            viewImpl->last_pipeline = NULL;

            flecsEngine_renderBatchSet_render(
                world, engine, viewImpl, batch_set, pass, view, 1);

            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        }

        {
            const FlecsSurface *surface = ecs_get(
                world, engine->surface, FlecsSurface);
            if (flecsEngine_surface_sampleCount(surface) > 1) {
                flecsEngine_depthResolve(engine, viewImpl, encoder);
            }
            flecsEngine_hiz_build(engine, viewImpl, encoder);
        }

        /* Snapshot the opaque result for transmission sampling */
        {
            WGPUTexture src_tex = batch_target_tex;
            if (src_tex) {
                flecsEngine_transmission_updateSnapshot(
                    world, engine, viewImpl, encoder, src_tex,
                    viewImpl->effect_target_width,
                    viewImpl->effect_target_height);
            }
        }

        /* Pass 2: render post-snapshot batches (transmission + transparent).
         * LoadOp_Load preserves both color and depth from pass 1. */
        {
            WGPURenderPassEncoder pass = flecsEngine_renderView_beginPass(
                world, engine, view, viewImpl, encoder, batch_target,
                WGPULoadOp_Load, WGPULoadOp_Load, "MainPostSnap");
            viewImpl->last_pipeline = NULL;

            flecsEngine_renderBatchSet_render(
                world, engine, viewImpl, batch_set, pass, view, 2);

            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        }
    } else {
        /* No transmissive materials — single pass, no snapshot needed */
        WGPURenderPassEncoder pass = flecsEngine_renderView_beginPass(
            world, engine, view, viewImpl, encoder, batch_target,
            WGPULoadOp_Clear, WGPULoadOp_Clear, "Main");
        viewImpl->last_pipeline = NULL;

        flecsEngine_renderBatchSet_render(
            world, engine, viewImpl, batch_set, pass, view, 0);

        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    FLECS_TRACY_ZONE_END;
}

static void flecsEngine_renderView_render(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    ecs_entity_t view_entity,
    const FlecsRenderView *view,
    FlecsRenderViewImpl *impl,
    WGPUCommandEncoder encoder,
    WGPUTextureView view_texture)
{
    FLECS_TRACY_ZONE_BEGIN("RenderView");
    impl->last_pipeline = NULL;

    const FlecsSurface *surface = ecs_get(world, engine->surface, FlecsSurface);

    int32_t effect_count = ecs_vec_count(&view->effects);
    int32_t target_count = effect_count > 0 ? effect_count : 1;
    if (effect_count > 0 && surface->resolution_scale > 1) {
        target_count = effect_count + 1;
    }
    FLECS_TRACY_ZONE_BEGIN_N(ensure_targets_zone, "EnsureTargets");
    bool ensure_err = flecsEngine_renderView_ensureTargets(world, engine, impl, target_count);
    FLECS_TRACY_ZONE_END_N(ensure_targets_zone);
    if (ensure_err) {
        ecs_err("failed to allocate effect render targets");
        FLECS_TRACY_ZONE_END;
        return;
    }

    FLECS_TRACY_ZONE_BEGIN_N(ensure_depth_zone, "EnsureDepth");
    bool depth_err = flecsEngine_renderView_ensureDepthResources(world, engine, impl);
    FLECS_TRACY_ZONE_END_N(ensure_depth_zone);
    if (depth_err) {
        ecs_err("failed to allocate view depth texture");
        FLECS_TRACY_ZONE_END;
        return;
    }

    FLECS_TRACY_ZONE_BEGIN_N(ensure_msaa_zone, "EnsureMsaa");
    bool msaa_err = flecsEngine_renderView_ensureMsaaResources(world, engine, impl);
    FLECS_TRACY_ZONE_END_N(ensure_msaa_zone);
    if (msaa_err) {
        ecs_err("failed to allocate view MSAA targets");
        FLECS_TRACY_ZONE_END;
        return;
    }

    if (!flecsEngine_renderView_ensureFrameUniformBuffer(engine, impl)) {
        ecs_err("failed to create frame uniform buffer");
        FLECS_TRACY_ZONE_END;
        return;
    }

    if (flecsEngine_hiz_ensureView(engine, impl)) {
        ecs_err("failed to create hi-z view resources");
        FLECS_TRACY_ZONE_END;
        return;
    }

    if (flecsEngine_gpuCull_initView(engine, impl)) {
        ecs_err("failed to create gpu cull view resources");
        FLECS_TRACY_ZONE_END;
        return;
    }

    ecs_entity_t atmosphere = flecsEngine_renderView_atmosphere(world, view);
    bool have_atmosphere = atmosphere != 0;
    if (have_atmosphere) {
        FLECS_TRACY_ZONE_BEGIN_N(atm_ensure_zone, "AtmosEnsureImpl");
        bool scene_ok = flecsEngine_renderView_ensureSceneTarget(world, engine, impl);
        bool impl_ok = scene_ok && flecsEngine_atmosphere_ensureImpl(
            world, engine, atmosphere);
        FLECS_TRACY_ZONE_END_N(atm_ensure_zone);
        if (!scene_ok) {
            ecs_err("failed to allocate scene pre-atmosphere target");
            FLECS_TRACY_ZONE_END;
            return;
        }
        if (!impl_ok) {
            ecs_err("failed to initialize atmosphere resources");
            FLECS_TRACY_ZONE_END;
            return;
        }
    } else {
        flecsEngine_renderView_releaseSceneTarget(impl);
    }

    if (!view->shadow.enabled) {
        for (int i = 0; i < FLECS_ENGINE_SHADOW_CASCADE_COUNT; i++) {
            memset(impl->shadow.current_light_vp[i], 0, sizeof(mat4));
            impl->shadow.cascade_splits[i] = 0.0f;
        }
    }

    /* Compute cull runs before any render pass that reads the cull outputs
     * (shadow and main). It writes cascade + main visible_slots + indirect
     * args in a single dispatch per batch. */
    flecsEngine_gpuCull_writeViewUniforms(engine, impl);
    flecsEngine_gpuCull_dispatchAll(
        world, engine, impl, view_entity, encoder);

    if (flecsEngine_shadow_ensureViewSize(
        engine, impl, (uint32_t)view->shadow.map_size))
    {
        ecs_err("failed to resize shadow maps");
    }

    if (view->shadow.enabled) {
        flecsEngine_renderView_renderShadow(
            world, view_entity, engine, view, impl, encoder);
    }

    /* Ensure per-view cluster buffers exist before building. */
    if (flecsEngine_cluster_initView(engine, impl)) {
        ecs_err("failed to initialize view cluster buffers");
        FLECS_TRACY_ZONE_END;
        return;
    }

    flecsEngine_setupLights(world, engine);
    flecsEngine_cluster_build(world, engine, impl, view);
    flecsEngine_renderView_writeFrameUniforms(world, engine, impl, view);

    if (have_atmosphere) {
        if (!flecsEngine_atmosphere_renderLuts(
            world, engine, atmosphere, view_entity, encoder))
        {
            ecs_err("atmosphere LUT pass failed");
        }
        if (!flecsEngine_atmosphere_renderIbl(
            world, engine, atmosphere, encoder))
        {
            ecs_err("atmosphere IBL pass failed");
        }
    }

    flecsEngine_renderView_renderBatches(
        world, view_entity, engine, view, impl, encoder);

    if (flecsEngine_surface_sampleCount(surface) > 1) {
        flecsEngine_depthResolve(engine, impl, encoder);
    }

    {
        const FlecsRenderBatchSet *batch_set = ecs_get(
            world, view_entity, FlecsRenderBatchSet);
        if (!batch_set || !flecsEngine_renderBatchSet_hasTransmission(
            world, engine, batch_set))
        {
            flecsEngine_hiz_build(engine, impl, encoder);
        }
    }

    if (have_atmosphere) {
        if (!flecsEngine_atmosphere_renderCompose(
            world, engine, impl, atmosphere, encoder,
            impl->scene_target_view,
            impl->effect_target_views[0],
            impl->effect_target_format,
            WGPULoadOp_Clear))
        {
            ecs_err("atmosphere compose pass failed");
        }
    }

    {
        int32_t particle_count = flecsEngine_particles_prepare(
            world, engine, impl);
        if (particle_count > 0) {
            WGPURenderPassColorAttachment color_attachment = {
                .view = impl->effect_target_views[0],
                WGPU_DEPTH_SLICE
                .loadOp = WGPULoadOp_Load,
                .storeOp = WGPUStoreOp_Store,
                .clearValue = (WGPUColor){0, 0, 0, 1}
            };

            WGPURenderPassDepthStencilAttachment depth_attachment = {
                .view = impl->depth_texture_view,
                .depthLoadOp = WGPULoadOp_Load,
                .depthStoreOp = WGPUStoreOp_Store,
                .depthClearValue = 1.0f,
                .depthReadOnly = false,
                .stencilLoadOp = WGPULoadOp_Undefined,
                .stencilStoreOp = WGPUStoreOp_Undefined,
                .stencilClearValue = 0,
                .stencilReadOnly = true
            };

            WGPURenderPassTimestampWrites ts_writes;
            int ts_pair = flecsEngine_gpuTiming_allocPair(
                engine, "Particles");
            flecsEngine_gpuTiming_renderPassTimestamps(
                engine, ts_pair, &ts_writes);

            WGPURenderPassDescriptor pass_desc = {
                .colorAttachmentCount = 1,
                .colorAttachments = &color_attachment,
                .depthStencilAttachment = &depth_attachment,
                .timestampWrites = WGPU_TIMESTAMP_WRITES(
                    ts_pair >= 0 ? &ts_writes : NULL)
            };

            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
                encoder, &pass_desc);
            impl->last_pipeline = NULL;
            flecsEngine_particles_draw(world, engine, impl, pass,
                impl->effect_target_format, 1, particle_count);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        }
    }

    flecsEngine_renderView_renderEffects(
        world, view_entity, engine, view, impl, view_texture, encoder);

    FLECS_TRACY_ZONE_END;
}

static void flecsEngine_renderView_extract(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    ecs_entity_t view_entity,
    const FlecsRenderView *view,
    FlecsRenderViewImpl *impl)
{
    FLECS_TRACY_ZONE_BEGIN("ExtractView");
    (void)world;
    (void)engine;
    (void)view;

    flecsEngine_renderView_extractBatches(world, view_entity, engine, impl, view);

    FLECS_TRACY_ZONE_END;
}

void flecsEngine_renderView_extractAll(
    ecs_world_t *world,
    FlecsEngineImpl *engine)
{
    ecs_iter_t it = ecs_query_iter(world, engine->view_query);
    while (ecs_query_next(&it)) {
        FlecsRenderView *views = ecs_field(&it, FlecsRenderView, 0);
        FlecsRenderViewImpl *viewImpls = ecs_field(&it, FlecsRenderViewImpl, 1);
        for (int32_t i = 0; i < it.count; i ++) {
            flecsEngine_renderView_extract(
                world,
                engine,
                it.entities[i],
                &views[i],
                &viewImpls[i]);
        }
    }
}

static void flecsEngine_renderView_pickLight(
    ecs_world_t *world,
    FlecsRenderViewImpl *impl)
{
    if (!impl->light_query) {
        return;
    }

    impl->main_light = 0;
    float best_intensity = -1.0f;
    ecs_iter_t it = ecs_query_iter(world, impl->light_query);
    while (ecs_query_next(&it)) {
        const FlecsDirectionalLight *lights = ecs_field(
            &it, FlecsDirectionalLight, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            if (lights[i].intensity > best_intensity) {
                best_intensity = lights[i].intensity;
                impl->main_light = it.entities[i];
            }
        }
    }
}

static void flecsEngine_renderView_cull(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    ecs_entity_t view_entity,
    const FlecsRenderView *view,
    FlecsRenderViewImpl *impl)
{
    FLECS_TRACY_ZONE_BEGIN("CullView");

    flecsEngine_renderView_pickLight(world, impl);

    impl->frustum_valid = false;
    impl->shadow_frustum_valid = false;
    impl->camera_view_proj_valid = false;

    if (view->camera) {
        const FlecsCameraImpl *camera = ecs_get(
            world, view->camera, FlecsCameraImpl);
        if (camera) {
            flecsEngine_frustum_extractPlanes(
                camera->mvp,
                impl->frustum_planes);
            impl->frustum_valid = true;
            memcpy(impl->camera_view_proj, camera->mvp,
                sizeof(impl->camera_view_proj));
            memcpy(impl->camera_proj, camera->proj,
                sizeof(impl->camera_proj));
            impl->camera_view_proj_valid = true;

            if (view->shadow.enabled && view->shadow.max_range > 0.0f) {
                const FlecsCamera *cam = ecs_get(
                    world, view->camera, FlecsCamera);
                if (cam) {
                    mat4 shadow_proj;
                    glm_perspective(
                        cam->fov,
                        cam->aspect_ratio > 0.0f ? cam->aspect_ratio : 1.0f,
                        cam->near_,
                        view->shadow.max_range,
                        shadow_proj);

                    mat4 shadow_vp;
                    glm_mat4_mul(shadow_proj, (vec4*)camera->view, shadow_vp);

                    flecsEngine_frustum_extractPlanes(
                        shadow_vp,
                        impl->shadow_frustum_planes);
                    impl->shadow_frustum_valid = true;
                }
            }
        }
    }

    impl->screen_cull_valid = false;
    if (view->screen_px_threshold > 0.0f && view->camera) {
        const FlecsCamera *cam = ecs_get(
            world, view->camera, FlecsCamera);
        if (cam && !cam->orthographic && cam->fov > 0.0f) {
            float half_tan = tanf(cam->fov * 0.5f);
            if (half_tan > 1e-6f) {
                const FlecsSurface *surface = ecs_get(
                    world, engine->surface, FlecsSurface);
                float vh = (float)surface->height;
                float f = vh / half_tan;
                impl->screen_cull_factor = f * f;
                impl->screen_cull_threshold = view->screen_px_threshold;
                impl->screen_cull_valid = true;
            }
        }
    }

    (void)view_entity;
    FLECS_TRACY_ZONE_END;
}

void flecsEngine_renderView_cullAll(
    ecs_world_t *world,
    FlecsEngineImpl *engine)
{
    ecs_iter_t it = ecs_query_iter(world, engine->view_query);
    while (ecs_query_next(&it)) {
        FlecsRenderView *views = ecs_field(&it, FlecsRenderView, 0);
        FlecsRenderViewImpl *viewImpls = ecs_field(&it, FlecsRenderViewImpl, 1);
        for (int32_t i = 0; i < it.count; i ++) {
            flecsEngine_renderView_cull(
                world,
                engine,
                it.entities[i],
                &views[i],
                &viewImpls[i]);
        }
    }
}

void flecsEngine_renderView_uploadAll(
    ecs_world_t *world,
    FlecsEngineImpl *engine)
{
    ecs_iter_t it = ecs_query_iter(world, engine->view_query);
    while (ecs_query_next(&it)) {
        FlecsRenderView *views = ecs_field(&it, FlecsRenderView, 0);
        FlecsRenderViewImpl *viewImpls = ecs_field(&it, FlecsRenderViewImpl, 1);
        for (int32_t i = 0; i < it.count; i ++) {
            flecsEngine_renderView_uploadBatches(
                world, it.entities[i], engine, &viewImpls[i], &views[i]);
        }
    }
}

void flecsEngine_renderView_renderAll(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    WGPUTextureView view_texture,
    WGPUCommandEncoder encoder)
{
    ecs_iter_t it = ecs_query_iter(world, engine->view_query);
    while (ecs_query_next(&it)) {
        FlecsRenderView *views = ecs_field(&it, FlecsRenderView, 0);
        FlecsRenderViewImpl *viewImpls = ecs_field(&it, FlecsRenderViewImpl, 1);
        for (int32_t i = 0; i < it.count; i ++) {
            flecsEngine_renderView_render(world, engine, it.entities[i],
                &views[i], &viewImpls[i],
                encoder, view_texture);
        }
    }
}

void flecsEngine_renderView_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, flecs_engine_shadow_params_t);
    ECS_COMPONENT_DEFINE(world, flecs_render_view_effect_t);
    ECS_COMPONENT_DEFINE(world, FlecsRenderView);
    ECS_COMPONENT_DEFINE(world, FlecsRenderViewImpl);

    ecs_set_hooks(world, FlecsRenderView, {
        .ctor = ecs_ctor(FlecsRenderView),
        .move = ecs_move(FlecsRenderView),
        .copy = ecs_copy(FlecsRenderView),
        .dtor = ecs_dtor(FlecsRenderView)
    });

    ecs_set_hooks(world, FlecsRenderViewImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsRenderViewImpl),
        .dtor = ecs_dtor(FlecsRenderViewImpl),
        .on_add = flecsEngine_renderView_implOnAdd,
        .on_remove = flecsEngine_renderView_implOnRemove
    });

    ecs_add_pair(world, ecs_id(FlecsRenderView), EcsWith, ecs_id(FlecsRenderViewImpl));

    ecs_struct(world, {
        .entity = ecs_id(flecs_engine_shadow_params_t),
        .members = {
            { .name = "enabled", .type = ecs_id(ecs_bool_t) },
            { .name = "map_size", .type = ecs_id(ecs_i32_t) },
            { .name = "bias", .type = ecs_id(ecs_f32_t) },
            { .name = "max_range", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(flecs_render_view_effect_t),
        .members = {
            { .name = "enabled", .type = ecs_id(ecs_bool_t) },
            { .name = "input", .type = ecs_id(ecs_i32_t) },
            { .name = "effect", .type = ecs_id(ecs_entity_t) }
        }
    });

    ecs_set_hooks(world, flecs_render_view_effect_t, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(flecs_render_view_effect_t)
    });

    ecs_entity_t vec_view_effect = ecs_vector(world, {
        .entity = ecs_entity(world, {
            .name = "::flecs.engine.types.vecViewEffect",
        }),
        .type = ecs_id(flecs_render_view_effect_t)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsRenderView),
        .members = {
            { .name = "camera", .type = ecs_id(ecs_entity_t) },
            { .name = "atmosphere", .type = ecs_id(ecs_entity_t) },
            { .name = "hdri", .type = ecs_id(ecs_entity_t) },
            { .name = "ambient_intensity", .type = ecs_id(ecs_f32_t) },
            { .name = "screen_px_threshold", .type = ecs_id(ecs_f32_t) },
            { .name = "shadow", .type = ecs_id(flecs_engine_shadow_params_t) },
            { .name = "effects", .type = vec_view_effect }
        }
    });
}
