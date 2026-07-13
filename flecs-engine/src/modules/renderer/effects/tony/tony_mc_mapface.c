#include "../../renderer.h"
#include "tony_mc_mapface_lut.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsTony);
ECS_COMPONENT_DECLARE(FlecsTonyImpl);

static const char *kShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "struct ExposureBuf { ev : f32, scale : f32 };\n"
    "@group(0) @binding(0) var input_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var input_sampler : sampler;\n"
    "@group(0) @binding(2) var tony_lut : texture_3d<f32>;\n"
    "@group(0) @binding(3) var tony_lut_sampler : sampler;\n"
    "@group(0) @binding(4) var<uniform> exposure_data : ExposureBuf;\n"
    "fn interleaved_gradient_noise(pixel : vec2<f32>) -> f32 {\n"
    "  return fract(52.9829189 * fract(0.06711056 * pixel.x + 0.00583715 * pixel.y));\n"
    "}\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let src = textureSample(input_texture, input_sampler, in.uv);\n"
    "  let exposed = src.rgb * exposure_data.scale;\n"
    "  let encoded = exposed / (exposed + vec3<f32>(1.0));\n"
    "  let dims = vec3<f32>(textureDimensions(tony_lut));\n"
    "  let uv = clamp(encoded * ((dims - vec3<f32>(1.0)) / dims) + (vec3<f32>(0.5) / dims),\n"
    "                 vec3<f32>(0.0), vec3<f32>(1.0));\n"
    "  let mapped = textureSampleLevel(tony_lut, tony_lut_sampler, uv, 0.0).rgb;\n"
    "  let tex_size = vec2<f32>(textureDimensions(input_texture));\n"
    "  let pixel = floor(in.uv * tex_size);\n"
    "  let dither = (interleaved_gradient_noise(pixel) - 0.5) / 255.0;\n"
    "  let dithered = clamp(mapped + vec3<f32>(dither), vec3<f32>(0.0), vec3<f32>(1.0));\n"
    "  return vec4<f32>(dithered, src.a);\n"
    "}\n";

static ecs_entity_t flecsEngine_tony_shader(
    ecs_world_t *world)
{
    return flecsEngine_shader_ensure(world, "TonyMcMapfaceShader",
        &(FlecsShader){
            .source = kShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}

static void flecsEngine_tony_releaseResources(
    FlecsTonyImpl *impl)
{
    FLECS_WGPU_RELEASE(impl->tony_lut_sampler, wgpuSamplerRelease);
    FLECS_WGPU_RELEASE(impl->tony_lut_texture_view, wgpuTextureViewRelease);
    FLECS_WGPU_RELEASE(impl->tony_lut_texture, wgpuTextureRelease);
    FLECS_WGPU_RELEASE(impl->fallback_exposure_buffer, wgpuBufferRelease);
}

ECS_DTOR(FlecsTonyImpl, ptr, {
    flecsEngine_tony_releaseResources(ptr);
})

ECS_MOVE(FlecsTonyImpl, dst, src, {
    flecsEngine_tony_releaseResources(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

static bool flecsEngine_tony_setup(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t effect_entity,
    const FlecsRenderEffectKind *kind,
    FlecsRenderEffectImpl *effect_impl,
    WGPUBindGroupLayoutEntry *layout_entries,
    uint32_t *entry_count)
{
    (void)effect_impl;
    (void)kind;

    ecs_assert(layout_entries != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(entry_count != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(*entry_count == 2, ECS_INVALID_PARAMETER, NULL);

    const FlecsTony *settings = ecs_get(world, effect_entity, FlecsTony);
    ecs_entity_t auto_exposure = settings ? settings->auto_exposure : 0;

    FlecsTonyImpl tony = {0};
    tony.auto_exposure = auto_exposure;

    tony.tony_lut_sampler = flecsEngine_createLinearClampSampler(engine->device);
    if (!tony.tony_lut_sampler) {
        return false;
    }

    WGPUTextureDescriptor lut_desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_3D,
        .size = (WGPUExtent3D){
            .width = FLECS_ENGINE_TONY_MC_MAPFACE_LUT_WIDTH,
            .height = FLECS_ENGINE_TONY_MC_MAPFACE_LUT_HEIGHT,
            .depthOrArrayLayers = FLECS_ENGINE_TONY_MC_MAPFACE_LUT_DEPTH
        },
        .format = WGPUTextureFormat_RGB9E5Ufloat,
        .mipLevelCount = 1,
        .sampleCount = 1
    };

    tony.tony_lut_texture = wgpuDeviceCreateTexture(engine->device, &lut_desc);
    if (!tony.tony_lut_texture) {
        flecsEngine_tony_releaseResources(&tony);
        return false;
    }

    WGPUTextureViewDescriptor lut_view_desc = {
        .format = WGPUTextureFormat_RGB9E5Ufloat,
        .dimension = WGPUTextureViewDimension_3D,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All,
        WGPU_TEXTURE_VIEW_USAGE(WGPUTextureUsage_TextureBinding)
    };

    tony.tony_lut_texture_view = wgpuTextureCreateView(
        tony.tony_lut_texture, &lut_view_desc);
    if (!tony.tony_lut_texture_view) {
        flecsEngine_tony_releaseResources(&tony);
        return false;
    }

    WGPUTexelCopyTextureInfo destination = {
        .texture = tony.tony_lut_texture,
        .mipLevel = 0,
        .origin = {0, 0, 0},
        .aspect = WGPUTextureAspect_All
    };

    WGPUTexelCopyBufferLayout data_layout = {
        .offset = 0,
        .bytesPerRow = FLECS_ENGINE_TONY_MC_MAPFACE_LUT_WIDTH * sizeof(uint32_t),
        .rowsPerImage = FLECS_ENGINE_TONY_MC_MAPFACE_LUT_HEIGHT
    };

    WGPUExtent3D write_size = {
        .width = FLECS_ENGINE_TONY_MC_MAPFACE_LUT_WIDTH,
        .height = FLECS_ENGINE_TONY_MC_MAPFACE_LUT_HEIGHT,
        .depthOrArrayLayers = FLECS_ENGINE_TONY_MC_MAPFACE_LUT_DEPTH
    };

    wgpuQueueWriteTexture(
        engine->queue,
        &destination,
        flecs_engine_tony_mc_mapface_lut,
        sizeof(uint32_t) * FLECS_ENGINE_TONY_MC_MAPFACE_LUT_TEXEL_COUNT,
        &data_layout,
        &write_size);

    tony.fallback_exposure_buffer = wgpuDeviceCreateBuffer(engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = 2 * sizeof(float)
        });
    if (!tony.fallback_exposure_buffer) {
        flecsEngine_tony_releaseResources(&tony);
        return false;
    }
    {
        float init[2] = { 0.0f, 1.0f };
        wgpuQueueWriteBuffer(engine->queue, tony.fallback_exposure_buffer, 0,
            init, sizeof(init));
    }

    layout_entries[2] = (WGPUBindGroupLayoutEntry){
        .binding = 2,
        .visibility = WGPUShaderStage_Fragment,
        .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_3D,
            .multisampled = false
        }
    };

    layout_entries[3] = (WGPUBindGroupLayoutEntry){
        .binding = 3,
        .visibility = WGPUShaderStage_Fragment,
        .sampler = {
            .type = WGPUSamplerBindingType_Filtering
        }
    };

    layout_entries[4] = (WGPUBindGroupLayoutEntry){
        .binding = 4,
        .visibility = WGPUShaderStage_Fragment,
        .buffer = {
            .type = WGPUBufferBindingType_Uniform,
            .minBindingSize = 2 * sizeof(float)
        }
    };

    ecs_set_ptr((ecs_world_t*)world, effect_entity, FlecsTonyImpl, &tony);

    *entry_count = 5;
    return true;
}

static bool flecsEngine_tony_bind(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    ecs_entity_t effect_entity,
    const FlecsRenderEffectKind *kind,
    const FlecsRenderEffectImpl *impl,
    WGPUBindGroupEntry *entries,
    uint32_t *entry_count)
{
    (void)engine;
    (void)view_impl;
    (void)kind;
    (void)impl;

    ecs_assert(entries != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(entry_count != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(*entry_count == 2, ECS_INVALID_PARAMETER, NULL);

    const FlecsTonyImpl *tony = ecs_get(world, effect_entity, FlecsTonyImpl);
    if (!tony) {
        return false;
    }

    WGPUBuffer exposure_buffer = tony->fallback_exposure_buffer;
    if (tony->auto_exposure &&
        !ecs_has_id(world, tony->auto_exposure, EcsDisabled))
    {
        const FlecsAutoExposureImpl *ae_impl = ecs_get(
            world, tony->auto_exposure, FlecsAutoExposureImpl);
        if (ae_impl && ae_impl->exposure_buffer) {
            exposure_buffer = ae_impl->exposure_buffer;
        }
    }

    entries[2] = (WGPUBindGroupEntry){
        .binding = 2,
        .textureView = tony->tony_lut_texture_view
    };

    entries[3] = (WGPUBindGroupEntry){
        .binding = 3,
        .sampler = tony->tony_lut_sampler
    };

    entries[4] = (WGPUBindGroupEntry){
        .binding = 4,
        .buffer = exposure_buffer,
        .offset = 0,
        .size = 2 * sizeof(float)
    };

    *entry_count = 5;
    return true;
}

static void FlecsTony_on_set(
    ecs_iter_t *it)
{
    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        ecs_set(it->world, e, FlecsRenderEffectKind, {
            .shader = flecsEngine_tony_shader(it->world),
            .setup_callback = flecsEngine_tony_setup,
            .bind_callback = flecsEngine_tony_bind
        });
    }
}

void flecsEngine_tonyMcMapFace_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsTony);
    ECS_COMPONENT_DEFINE(world, FlecsTonyImpl);

    ecs_set_hooks(world, FlecsTonyImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsTonyImpl),
        .dtor = ecs_dtor(FlecsTonyImpl)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsTony),
        .members = {
            { .name = "auto_exposure", .type = ecs_id(ecs_entity_t) }
        }
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(FlecsTony) }},
        .events = { EcsOnSet },
        .callback = FlecsTony_on_set
    });
}
