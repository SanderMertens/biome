#include "../renderer.h"
#include "flecs_engine.h"

WGPUBindGroupLayout flecsEngine_globals_ensureBindLayout(
    FlecsEngineImpl *impl)
{
    if (impl->scene_bind_layout) {
        return impl->scene_bind_layout;
    }

    flecsEngine_material_ensureBuffer(impl);

    WGPUBindGroupLayoutEntry layout_entries[12] = {
        { /* 0: Frame uniform (FlecsGpuUniforms) */
            .binding = 0,
            .visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(FlecsGpuUniforms)
            }
        },
        { /* 1: IBL prefiltered env cubemap */
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_Cube,
                .multisampled = false
            }
        },
        { /* 2: IBL sampler */
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = {
                .type = WGPUSamplerBindingType_Filtering
            }
        },
        { /* 3: Opaque scene snapshot (for transmission rendering) */
            .binding = 3,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            }
        },
        { /* 4: IBL irradiance cubemap */
            .binding = 4,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_Cube,
                .multisampled = false
            }
        },
        { /* 5: Shadow depth texture array */
            .binding = 5,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2DArray,
                .multisampled = false
            }
        },
        { /* 6: Shadow comparison sampler */
            .binding = 6,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = {
                .type = WGPUSamplerBindingType_Comparison
            }
        },
        { /* 7: Cluster info */
            .binding = 7,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(FlecsClusterInfo)
            }
        },
        { /* 8: Cluster grid */
            .binding = 8,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .minBindingSize = sizeof(FlecsClusterEntry)
            }
        },
        { /* 9: Light indices */
            .binding = 9,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .minBindingSize = sizeof(uint32_t)
            }
        },
        { /* 10: Lights (point + spot) */
            .binding = 10,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .minBindingSize = sizeof(FlecsGpuLight)
            }
        },
        { /* 11: Baked cloud-shadow texture (.r = per-ground-point
            * transmittance, sampled by world XZ in the footprint). Falls
            * back to a 1x1 white texture when no cloud effect is active. */
            .binding = 11,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            }
        }
    };

    impl->scene_bind_layout = wgpuDeviceCreateBindGroupLayout(
        impl->device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = 12,
            .entries = layout_entries
        });

    return impl->scene_bind_layout;
}

bool flecsEngine_globals_createBindGroup(
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    ecs_entity_t hdri_entity,
    FlecsHdriImpl *ibl)
{
    FLECS_WGPU_RELEASE(view_impl->scene_bind_group, wgpuBindGroupRelease);

    WGPUBindGroupLayout bind_layout = engine->scene_bind_layout;
    if (!bind_layout) {
        return false;
    }

    /* Ensure fallback textures exist before we reference them */
    flecsEngine_pbr_texture_ensureFallbacks(engine);

    if (!view_impl->shadow.texture_view || !engine->shadow.sampler) {
        return false;
    }

    flecs_view_cluster_t *cl = &view_impl->cluster;
    if (!cl->cluster_info_buffer || !cl->cluster_grid_buffer ||
        !cl->cluster_index_buffer || !engine->lighting.light_buffer)
    {
        return false;
    }

    if (!view_impl->frame_uniform_buffer) {
        return false;
    }

    WGPUTextureView cloud_shadow_view = engine->clouds.shadow_source_view
        ? engine->clouds.shadow_source_view
        : engine->textures.fallback_white_view;

    view_impl->scene_bind_group = wgpuDeviceCreateBindGroup(
        engine->device,
        &(WGPUBindGroupDescriptor){
            .layout = bind_layout,
            .entryCount = 12,
            .entries = (WGPUBindGroupEntry[12]){
                {
                    .binding = 0,
                    .buffer = view_impl->frame_uniform_buffer,
                    .size = sizeof(FlecsGpuUniforms)
                },
                {
                    .binding = 1,
                    .textureView = ibl->ibl_prefiltered_cubemap_view
                },
                {
                    .binding = 2,
                    .sampler = ibl->ibl_sampler
                },
                {   /* Opaque snapshot for transmission. Uses the snapshot
                     * view if available, otherwise the 2D white fallback. */
                    .binding = 3,
                    .textureView = view_impl->opaque_snapshot.view
                        ? view_impl->opaque_snapshot.view
                        : engine->textures.fallback_white_view
                },
                {
                    .binding = 4,
                    .textureView = ibl->ibl_irradiance_cubemap_view
                },
                {
                    .binding = 5,
                    .textureView = view_impl->shadow.texture_view
                },
                {
                    .binding = 6,
                    .sampler = engine->shadow.sampler
                },
                {
                    .binding = 7,
                    .buffer = cl->cluster_info_buffer,
                    .size = sizeof(FlecsClusterInfo)
                },
                {
                    .binding = 8,
                    .buffer = cl->cluster_grid_buffer,
                    .size = (uint64_t)FLECS_ENGINE_CLUSTER_TOTAL *
                        sizeof(FlecsClusterEntry)
                },
                {
                    .binding = 9,
                    .buffer = cl->cluster_index_buffer,
                    .size = (uint64_t)cl->cluster_index_capacity *
                        sizeof(uint32_t)
                },
                {
                    .binding = 10,
                    .buffer = engine->lighting.light_buffer,
                    .size = (uint64_t)engine->lighting.light_capacity *
                        sizeof(FlecsGpuLight)
                },
                {
                    .binding = 11,
                    .textureView = cloud_shadow_view
                }
            }
        });

    view_impl->scene_bind_hdri = hdri_entity;
    view_impl->scene_bind_version = engine->scene_bind_version;

    return view_impl->scene_bind_group != NULL;
}
