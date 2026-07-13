#include <math.h>

#include "../../renderer.h"
#include "../../mip_pyramid.h"
#include "../../gpu_timing.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsBloom);
ECS_COMPONENT_DECLARE(FlecsBloomImpl);

#define FLECS_ENGINE_BLOOM_PREFERRED_TEXTURE_FORMAT (WGPUTextureFormat_RG11B10Ufloat)
#define FLECS_ENGINE_BLOOM_MAX_MIP_COUNT (12u)
#define FLECS_ENGINE_BLOOM_TARGET_LEAF_SIZE (4u)
#define FLECS_ENGINE_BLOOM_DEFAULT_MAX_MIP_DIMENSION (512u)

typedef struct FlecsBloomUniform {
    float threshold_precomputations[4];
    float viewport[4];
    float final_blend;
    float _padding[3];
} FlecsBloomUniform;

static const char *kPlaceholderShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "@group(0) @binding(0) var input_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var input_sampler : sampler;\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  return textureSample(input_texture, input_sampler, in.uv);\n"
    "}\n";

static const char *kBloomShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "struct BloomUniforms {\n"
    "  threshold_precomputations : vec4<f32>,\n"
    "  viewport : vec4<f32>,\n"
    "  final_blend : f32,\n"
    "};\n"
    "@group(0) @binding(0) var input_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var bloom_sampler : sampler;\n"
    "@group(0) @binding(2) var<uniform> uniforms : BloomUniforms;\n"
    "fn soft_threshold(color : vec3<f32>) -> vec3<f32> {\n"
    "  let brightness = max(color.r, max(color.g, color.b));\n"
    "  var softness = brightness - uniforms.threshold_precomputations.y;\n"
    "  softness = clamp(softness, 0.0, uniforms.threshold_precomputations.z);\n"
    "  softness = softness * softness * uniforms.threshold_precomputations.w;\n"
    "  var contribution = max(brightness - uniforms.threshold_precomputations.x, softness);\n"
    "  contribution /= max(brightness, 0.00001);\n"
    "  return color * contribution;\n"
    "}\n"
    "fn tonemapping_luminance(v : vec3<f32>) -> f32 {\n"
    "  return dot(v, vec3<f32>(0.2126, 0.7152, 0.0722));\n"
    "}\n"
    "fn karis_average(color : vec3<f32>) -> f32 {\n"
    "  let luma = tonemapping_luminance(color) / 4.0;\n"
    "  return 1.0 / (1.0 + luma);\n"
    "}\n"
    "fn sample_13_tap_first(uv : vec2<f32>) -> vec3<f32> {\n"
    "  let a = textureSample(input_texture, bloom_sampler, uv, vec2<i32>(-2,  2)).rgb;\n"
    "  let b = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 0,  2)).rgb;\n"
    "  let c = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 2,  2)).rgb;\n"
    "  let d = textureSample(input_texture, bloom_sampler, uv, vec2<i32>(-2,  0)).rgb;\n"
    "  let e = textureSample(input_texture, bloom_sampler, uv).rgb;\n"
    "  let f = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 2,  0)).rgb;\n"
    "  let g = textureSample(input_texture, bloom_sampler, uv, vec2<i32>(-2, -2)).rgb;\n"
    "  let h = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 0, -2)).rgb;\n"
    "  let i = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 2, -2)).rgb;\n"
    "  let j = textureSample(input_texture, bloom_sampler, uv, vec2<i32>(-1,  1)).rgb;\n"
    "  let k = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 1,  1)).rgb;\n"
    "  let l = textureSample(input_texture, bloom_sampler, uv, vec2<i32>(-1, -1)).rgb;\n"
    "  let m = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 1, -1)).rgb;\n"
    "  var group0 = (a + b + d + e) * (0.125 / 4.0);\n"
    "  var group1 = (b + c + e + f) * (0.125 / 4.0);\n"
    "  var group2 = (d + e + g + h) * (0.125 / 4.0);\n"
    "  var group3 = (e + f + h + i) * (0.125 / 4.0);\n"
    "  var group4 = (j + k + l + m) * (0.5 / 4.0);\n"
    "  group0 *= karis_average(group0);\n"
    "  group1 *= karis_average(group1);\n"
    "  group2 *= karis_average(group2);\n"
    "  group3 *= karis_average(group3);\n"
    "  group4 *= karis_average(group4);\n"
    "  return group0 + group1 + group2 + group3 + group4;\n"
    "}\n"
    "fn sample_13_tap(uv : vec2<f32>) -> vec3<f32> {\n"
    "  let a = textureSample(input_texture, bloom_sampler, uv, vec2<i32>(-2,  2)).rgb;\n"
    "  let b = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 0,  2)).rgb;\n"
    "  let c = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 2,  2)).rgb;\n"
    "  let d = textureSample(input_texture, bloom_sampler, uv, vec2<i32>(-2,  0)).rgb;\n"
    "  let e = textureSample(input_texture, bloom_sampler, uv).rgb;\n"
    "  let f = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 2,  0)).rgb;\n"
    "  let g = textureSample(input_texture, bloom_sampler, uv, vec2<i32>(-2, -2)).rgb;\n"
    "  let h = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 0, -2)).rgb;\n"
    "  let i = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 2, -2)).rgb;\n"
    "  let j = textureSample(input_texture, bloom_sampler, uv, vec2<i32>(-1,  1)).rgb;\n"
    "  let k = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 1,  1)).rgb;\n"
    "  let l = textureSample(input_texture, bloom_sampler, uv, vec2<i32>(-1, -1)).rgb;\n"
    "  let m = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 1, -1)).rgb;\n"
    "  var sample = (a + c + g + i) * 0.03125;\n"
    "  sample += (b + d + f + h) * 0.0625;\n"
    "  sample += (e + j + k + l + m) * 0.125;\n"
    "  return sample;\n"
    "}\n"
    "fn sample_3x3_tent(uv : vec2<f32>) -> vec3<f32> {\n"
    "  let a = textureSample(input_texture, bloom_sampler, uv, vec2<i32>(-1,  1)).rgb;\n"
    "  let b = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 0,  1)).rgb;\n"
    "  let c = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 1,  1)).rgb;\n"
    "  let d = textureSample(input_texture, bloom_sampler, uv, vec2<i32>(-1,  0)).rgb;\n"
    "  let e = textureSample(input_texture, bloom_sampler, uv).rgb;\n"
    "  let f = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 1,  0)).rgb;\n"
    "  let g = textureSample(input_texture, bloom_sampler, uv, vec2<i32>(-1, -1)).rgb;\n"
    "  let h = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 0, -1)).rgb;\n"
    "  let i = textureSample(input_texture, bloom_sampler, uv, vec2<i32>( 1, -1)).rgb;\n"
    "  var sample = e * 0.25;\n"
    "  sample += (b + d + f + h) * 0.125;\n"
    "  sample += (a + c + g + i) * 0.0625;\n"
    "  return sample;\n"
    "}\n"
    "@fragment fn downsample_first(@location(0) output_uv : vec2<f32>) -> @location(0) vec4<f32> {\n"
    "  let sample_uv = uniforms.viewport.xy + output_uv * uniforms.viewport.zw;\n"
    "  var sample = sample_13_tap_first(sample_uv);\n"
    "  sample = clamp(sample, vec3<f32>(0.0001), vec3<f32>(3.40282347e+37));\n"
    "  if (uniforms.threshold_precomputations.x > 0.0 || uniforms.threshold_precomputations.z > 0.0) {\n"
    "    sample = soft_threshold(sample);\n"
    "  }\n"
    "  return vec4<f32>(sample, 1.0);\n"
    "}\n"
    "@fragment fn downsample(@location(0) uv : vec2<f32>) -> @location(0) vec4<f32> {\n"
    "  return vec4<f32>(sample_13_tap(uv), 1.0);\n"
    "}\n"
    "@fragment fn upsample(@location(0) uv : vec2<f32>) -> @location(0) vec4<f32> {\n"
    "  return vec4<f32>(sample_3x3_tent(uv), 1.0);\n"
    "}\n"
    "@group(1) @binding(0) var scene_texture : texture_2d<f32>;\n"
    "@fragment fn composite(@location(0) uv : vec2<f32>) -> @location(0) vec4<f32> {\n"
    "  let scene = textureSample(scene_texture, bloom_sampler, uv).rgb;\n"
    "  let bloom = sample_3x3_tent(uv);\n"
    "  return vec4<f32>(scene + bloom * uniforms.final_blend, 1.0);\n"
    "}\n";

ECS_CTOR(FlecsBloom, ptr, {
    ptr->intensity = 0.3f;
    ptr->low_frequency_boost = 0.7f;
    ptr->low_frequency_boost_curvature = 0.95f;
    ptr->high_pass_frequency = 1.0f;
    ptr->prefilter.threshold = 1.0f;
    ptr->prefilter.threshold_softness = 0.0f;
    ptr->max_mip_dimension = FLECS_ENGINE_BLOOM_DEFAULT_MAX_MIP_DIMENSION;
})

static uint32_t flecsEngine_bloom_deriveMipCount(
    uint32_t width,
    uint32_t height)
{
    uint32_t shorter = width < height ? width : height;
    if (shorter < FLECS_ENGINE_BLOOM_TARGET_LEAF_SIZE * 2u) {
        return 2u;
    }
    uint32_t count = 1u;
    uint32_t s = shorter;
    while (s >= FLECS_ENGINE_BLOOM_TARGET_LEAF_SIZE * 2u) {
        count++;
        s >>= 1u;
    }
    if (count > FLECS_ENGINE_BLOOM_MAX_MIP_COUNT) {
        count = FLECS_ENGINE_BLOOM_MAX_MIP_COUNT;
    }
    return count;
}

static void flecsEngine_bloom_computeTextureSize(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsBloom *settings,
    uint32_t *out_width,
    uint32_t *out_height)
{
    const FlecsSurface *surface = ecs_get(world, engine->surface, FlecsSurface);
    if (surface->height <= 0 || surface->width <= 0) {
        *out_width = 1u;
        *out_height = 1u;
        return;
    }

    uint32_t max_dim = settings->max_mip_dimension;
    if (max_dim < FLECS_ENGINE_BLOOM_TARGET_LEAF_SIZE * 2u) {
        max_dim = FLECS_ENGINE_BLOOM_TARGET_LEAF_SIZE * 2u;
    }

    uint32_t sw = (uint32_t)surface->width;
    uint32_t sh = (uint32_t)surface->height;
    uint32_t longer = sw > sh ? sw : sh;
    uint32_t half_longer = longer / 2u;
    uint32_t target_longer = half_longer < max_dim ? half_longer : max_dim;
    if (!target_longer) {
        target_longer = 1u;
    }

    uint64_t scaled_w =
        ((uint64_t)sw * (uint64_t)target_longer + (uint64_t)longer / 2u) /
        (uint64_t)longer;
    uint64_t scaled_h =
        ((uint64_t)sh * (uint64_t)target_longer + (uint64_t)longer / 2u) /
        (uint64_t)longer;
    if (!scaled_w) scaled_w = 1u;
    if (!scaled_h) scaled_h = 1u;

    *out_width = (uint32_t)scaled_w;
    *out_height = (uint32_t)scaled_h;
}

static void flecsEngine_bloom_releaseTexture(
    FlecsBloomImpl *bloom)
{
    if (bloom->mip_bind_groups) {
        for (uint32_t i = 0; i < bloom->mip_count; i ++) {
            FLECS_WGPU_RELEASE(bloom->mip_bind_groups[i], wgpuBindGroupRelease);
        }
        ecs_os_free(bloom->mip_bind_groups);
        bloom->mip_bind_groups = NULL;
    }
    FLECS_WGPU_RELEASE(bloom->input_bind_group, wgpuBindGroupRelease);
    bloom->input_bind_view = NULL;

    flecsEngine_mipPyramid_release(
        &bloom->texture, &bloom->mip_views, bloom->mip_count);

    bloom->mip_count = 0;
    bloom->texture_width = 0;
    bloom->texture_height = 0;
    bloom->texture_format = WGPUTextureFormat_Undefined;
}

static void flecsEngine_bloom_releaseResources(
    FlecsBloomImpl *bloom)
{
    flecsEngine_bloom_releaseTexture(bloom);
    FLECS_WGPU_RELEASE(bloom->composite_scene_bind_group, wgpuBindGroupRelease);
    bloom->composite_scene_view = NULL;
    FLECS_WGPU_RELEASE(bloom->uniform_buffer, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(bloom->sampler, wgpuSamplerRelease);
    FLECS_WGPU_RELEASE(bloom->composite_hdr_pipeline, wgpuRenderPipelineRelease);
    FLECS_WGPU_RELEASE(bloom->composite_surface_pipeline, wgpuRenderPipelineRelease);
    FLECS_WGPU_RELEASE(bloom->upsample_pipeline, wgpuRenderPipelineRelease);
    FLECS_WGPU_RELEASE(bloom->downsample_pipeline, wgpuRenderPipelineRelease);
    FLECS_WGPU_RELEASE(bloom->downsample_first_pipeline, wgpuRenderPipelineRelease);
    FLECS_WGPU_RELEASE(bloom->composite_scene_layout, wgpuBindGroupLayoutRelease);
    FLECS_WGPU_RELEASE(bloom->bind_layout, wgpuBindGroupLayoutRelease);
}

ECS_DTOR(FlecsBloomImpl, ptr, {
    flecsEngine_bloom_releaseResources(ptr);
})

ECS_MOVE(FlecsBloomImpl, dst, src, {
    flecsEngine_bloom_releaseResources(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

static WGPUBindGroup flecsEngine_bloom_createSourceBindGroup(
    const FlecsEngineImpl *engine,
    const FlecsBloomImpl *bloom,
    WGPUTextureView source_view)
{
    WGPUBindGroupEntry entries[3] = {
        { .binding = 0, .textureView = source_view },
        { .binding = 1, .sampler = bloom->sampler },
        {
            .binding = 2,
            .buffer = bloom->uniform_buffer,
            .offset = 0,
            .size = sizeof(FlecsBloomUniform)
        }
    };
    return wgpuDeviceCreateBindGroup(engine->device,
        &(WGPUBindGroupDescriptor){
            .layout = bloom->bind_layout,
            .entryCount = 3,
            .entries = entries
        });
}

static bool flecsEngine_bloom_createTexture(
    const FlecsEngineImpl *engine,
    FlecsBloomImpl *bloom,
    uint32_t width,
    uint32_t height,
    uint32_t mip_count,
    WGPUTextureFormat format)
{
    if (!flecsEngine_mipPyramid_create(
        engine->device, width, height, mip_count, format,
        WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        &bloom->texture, &bloom->mip_views))
    {
        return false;
    }

    bloom->mip_count = mip_count;
    bloom->texture_width = width;
    bloom->texture_height = height;
    bloom->texture_format = format;

    bloom->mip_bind_groups = ecs_os_calloc_n(WGPUBindGroup, mip_count);
    for (uint32_t i = 0; i < mip_count; i ++) {
        bloom->mip_bind_groups[i] = flecsEngine_bloom_createSourceBindGroup(
            engine, bloom, bloom->mip_views[i]);
        if (!bloom->mip_bind_groups[i]) {
            return false;
        }
    }
    return true;
}

static bool flecsEngine_bloom_ensureTexture(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsBloom *bloom,
    FlecsBloomImpl *impl)
{
    uint32_t width = 0;
    uint32_t height = 0;
    flecsEngine_bloom_computeTextureSize(world, engine, bloom, &width, &height);
    uint32_t mip_count = flecsEngine_bloom_deriveMipCount(width, height);

    /* Clamp to the maximum mip count the texture dimensions support */
    uint32_t max_mips = flecsEngine_mipPyramid_maxMips(width, height);
    if (mip_count > max_mips) {
        mip_count = max_mips;
    }

    if (impl->texture &&
        impl->mip_views &&
        impl->texture_width == width &&
        impl->texture_height == height &&
        impl->mip_count == mip_count &&
        impl->texture_format == flecsEngine_getHdrFormat(engine))
    {
        return true;
    }

    flecsEngine_bloom_releaseTexture(impl);

    WGPUTextureFormat working_format = flecsEngine_getHdrFormat(engine);

    if (flecsEngine_bloom_createTexture(
        engine,
        impl,
        width,
        height,
        mip_count,
        working_format))
    {
        return true;
    }

    return false;
}

static ecs_entity_t flecsEngine_bloom_shader(
    ecs_world_t *world)
{
    return flecsEngine_shader_ensure(world, "BloomEffectPlaceholderShader",
        &(FlecsShader){
            .source = kPlaceholderShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}

static WGPURenderPipeline flecsEngine_bloom_createPipeline(
    const FlecsEngineImpl *engine,
    WGPUShaderModule shader_module,
    WGPUBindGroupLayout bind_layout,
    const char *fragment_entry,
    WGPUTextureFormat color_format,
    const WGPUBlendState *blend_state)
{
    WGPUColorTargetState color_target = {
        .format = color_format,
        .blend = (WGPUBlendState*)blend_state,
        .writeMask = WGPUColorWriteMask_All
    };

    return flecsEngine_createFullscreenPipeline(
        engine, shader_module, bind_layout,
        NULL, fragment_entry, &color_target, NULL);
}

static WGPURenderPipeline flecsEngine_bloom_createCompositePipeline(
    const FlecsEngineImpl *engine,
    WGPUShaderModule shader_module,
    WGPUBindGroupLayout bloom_bind_layout,
    WGPUBindGroupLayout scene_bind_layout,
    WGPUTextureFormat color_format)
{
    WGPUBindGroupLayout layouts[2] = {
        bloom_bind_layout,
        scene_bind_layout
    };

    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
        engine->device, &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 2,
            .bindGroupLayouts = layouts
        });
    if (!pipeline_layout) {
        return NULL;
    }

    WGPUColorTargetState color_target = {
        .format = color_format,
        .blend = NULL,
        .writeMask = WGPUColorWriteMask_All
    };

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(
        engine->device, &(WGPURenderPipelineDescriptor){
            .layout = pipeline_layout,
            .vertex = {
                .module = shader_module,
                .entryPoint = WGPU_STR("vs_main")
            },
            .fragment = &(WGPUFragmentState){
                .module = shader_module,
                .entryPoint = WGPU_STR("composite"),
                .targetCount = 1,
                .targets = &color_target
            },
            .primitive = {
                .topology = WGPUPrimitiveTopology_TriangleList,
                .cullMode = WGPUCullMode_None,
                .frontFace = WGPUFrontFace_CCW
            },
            .multisample = WGPU_MULTISAMPLE_DEFAULT
        });

    wgpuPipelineLayoutRelease(pipeline_layout);
    return pipeline;
}

static WGPUBlendState flecsEngine_bloom_getBlendState(void)
{
    WGPUBlendComponent color = {
        .srcFactor = WGPUBlendFactor_Constant,
        .dstFactor = WGPUBlendFactor_One,
        .operation = WGPUBlendOperation_Add
    };

    WGPUBlendComponent alpha = {
        .srcFactor = WGPUBlendFactor_Zero,
        .dstFactor = WGPUBlendFactor_One,
        .operation = WGPUBlendOperation_Add
    };

    return (WGPUBlendState){
        .color = color,
        .alpha = alpha
    };
}

static bool flecsEngine_bloom_runPass(
    WGPUCommandEncoder encoder,
    WGPURenderPipeline pipeline,
    WGPUBindGroup bind_group,
    WGPUTextureView target_view,
    WGPULoadOp load_op,
    bool use_blend_constant,
    float blend_value,
    const WGPURenderPassTimestampWrites *ts_writes)
{
    WGPURenderPassColorAttachment color_att = {
        .view = target_view,
        WGPU_DEPTH_SLICE
        .loadOp = load_op,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = (WGPUColor){0}
    };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
        encoder, &(WGPURenderPassDescriptor){
            .colorAttachmentCount = 1,
            .colorAttachments = &color_att,
            .timestampWrites = WGPU_TIMESTAMP_WRITES(ts_writes)
        });
    if (!pass) {
        return false;
    }

    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);
    if (use_blend_constant) {
        WGPUColor blend = {
            .r = blend_value,
            .g = blend_value,
            .b = blend_value,
            .a = blend_value
        };
        wgpuRenderPassEncoderSetBlendConstant(pass, &blend);
    }
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    return true;
}

static WGPUBindGroup flecsEngine_bloom_ensureInputBindGroup(
    const FlecsEngineImpl *engine,
    FlecsBloomImpl *bloom,
    WGPUTextureView input_view)
{
    if (bloom->input_bind_group && bloom->input_bind_view == input_view) {
        return bloom->input_bind_group;
    }
    FLECS_WGPU_RELEASE(bloom->input_bind_group, wgpuBindGroupRelease);
    bloom->input_bind_group = flecsEngine_bloom_createSourceBindGroup(
        engine, bloom, input_view);
    bloom->input_bind_view = input_view;
    return bloom->input_bind_group;
}

static WGPUBindGroup flecsEngine_bloom_ensureCompositeSceneBindGroup(
    const FlecsEngineImpl *engine,
    FlecsBloomImpl *bloom,
    WGPUTextureView scene_view)
{
    if (bloom->composite_scene_bind_group &&
        bloom->composite_scene_view == scene_view)
    {
        return bloom->composite_scene_bind_group;
    }
    FLECS_WGPU_RELEASE(bloom->composite_scene_bind_group, wgpuBindGroupRelease);
    WGPUBindGroupEntry entry = {
        .binding = 0,
        .textureView = scene_view
    };
    bloom->composite_scene_bind_group = wgpuDeviceCreateBindGroup(
        engine->device, &(WGPUBindGroupDescriptor){
            .layout = bloom->composite_scene_layout,
            .entryCount = 1,
            .entries = &entry
        });
    bloom->composite_scene_view = scene_view;
    return bloom->composite_scene_bind_group;
}

static bool flecsEngine_bloom_runCompositePass(
    WGPUCommandEncoder encoder,
    WGPURenderPipeline pipeline,
    WGPUBindGroup bloom_bg,
    WGPUBindGroup scene_bg,
    WGPUTextureView target_view,
    WGPULoadOp load_op,
    const WGPURenderPassTimestampWrites *ts_writes)
{
    WGPURenderPassColorAttachment color_att = {
        .view = target_view,
        WGPU_DEPTH_SLICE
        .loadOp = load_op,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = (WGPUColor){0}
    };
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
        encoder, &(WGPURenderPassDescriptor){
            .colorAttachmentCount = 1,
            .colorAttachments = &color_att,
            .timestampWrites = WGPU_TIMESTAMP_WRITES(ts_writes)
        });
    if (!pass) {
        return false;
    }

    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bloom_bg, 0, NULL);
    wgpuRenderPassEncoderSetBindGroup(pass, 1, scene_bg, 0, NULL);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    return true;
}

static float flecsEngine_bloom_computeBlendFactor(
    const FlecsBloom *bloom,
    float mip,
    float max_mip)
{
    if (max_mip <= 0.0f) {
        return bloom->intensity;
    }

    float normalized = mip / max_mip;
    float curve = 1.0f / (1.0f - bloom->low_frequency_boost_curvature);
    float lf_boost = (1.0f - powf(1.0f - normalized, curve)) *
        bloom->low_frequency_boost;
    float high_pass_lq = 1.0f -
        glm_clamp(
            (normalized - bloom->high_pass_frequency) /
                bloom->high_pass_frequency,
            0.0f,
            1.0f);

    lf_boost *= 1.0f - bloom->intensity;

    return (bloom->intensity + lf_boost) * high_pass_lq;
}

static void flecsEngine_bloom_fillUniform(
    const FlecsBloom *settings,
    float final_blend,
    FlecsBloomUniform *uniform)
{
    float knee = settings->prefilter.threshold *
        glm_clamp(settings->prefilter.threshold_softness, 0.0f, 1.0f);

    uniform->threshold_precomputations[0] = settings->prefilter.threshold;
    uniform->threshold_precomputations[1] = settings->prefilter.threshold - knee;
    uniform->threshold_precomputations[2] = 2.0f * knee;
    uniform->threshold_precomputations[3] = 0.25f / (knee + 0.00001f);

    uniform->viewport[0] = 0.0f;
    uniform->viewport[1] = 0.0f;
    uniform->viewport[2] = 1.0f;
    uniform->viewport[3] = 1.0f;

    uniform->final_blend = final_blend;
}

static bool flecsEngine_bloom_setup(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t effect_entity,
    const FlecsRenderEffectKind *kind,
    FlecsRenderEffectImpl *effect_impl,
    WGPUBindGroupLayoutEntry *layout_entries,
    uint32_t *entry_count)
{
    (void)effect_impl;
    (void)layout_entries;

    ecs_assert(kind != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(entry_count != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(*entry_count == 2, ECS_INVALID_PARAMETER, NULL);

    FlecsBloomImpl bloom = {0};

    bloom.sampler = flecsEngine_createLinearClampSampler(engine->device);
    if (!bloom.sampler) {
        return false;
    }

    bloom.uniform_buffer = flecsEngine_createUniformBuffer(
        engine->device, sizeof(FlecsBloomUniform));
    if (!bloom.uniform_buffer) {
        flecsEngine_bloom_releaseResources(&bloom);
        return false;
    }

    WGPUBindGroupLayoutEntry bloom_layout_entries[3] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = {
                .type = WGPUSamplerBindingType_Filtering
            }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(FlecsBloomUniform)
            }
        }
    };

    WGPUBindGroupLayoutDescriptor bloom_bind_layout_desc = {
        .entryCount = 3,
        .entries = bloom_layout_entries
    };

    bloom.bind_layout = wgpuDeviceCreateBindGroupLayout(
        engine->device, &bloom_bind_layout_desc);
    if (!bloom.bind_layout) {
        flecsEngine_bloom_releaseResources(&bloom);
        return false;
    }

    WGPUBindGroupLayoutEntry scene_layout_entry = {
        .binding = 0,
        .visibility = WGPUShaderStage_Fragment,
        .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D,
            .multisampled = false
        }
    };
    bloom.composite_scene_layout = wgpuDeviceCreateBindGroupLayout(
        engine->device, &(WGPUBindGroupLayoutDescriptor){
            .entryCount = 1,
            .entries = &scene_layout_entry
        });
    if (!bloom.composite_scene_layout) {
        flecsEngine_bloom_releaseResources(&bloom);
        return false;
    }

    WGPUShaderModule bloom_shader = flecsEngine_shader_ensureModule(
        (ecs_world_t*)world, "BloomComputeShader", kBloomShaderSource);
    if (!bloom_shader) {
        flecsEngine_bloom_releaseResources(&bloom);
        return false;
    }

    WGPUTextureFormat view_target_format = flecsEngine_getViewTargetFormat(engine);
    WGPUTextureFormat hdr_format = flecsEngine_getHdrFormat(engine);
    WGPUTextureFormat bloom_format = hdr_format;

    WGPUBlendState blend_state = flecsEngine_bloom_getBlendState();

    bloom.downsample_first_pipeline = flecsEngine_bloom_createPipeline(
        engine,
        bloom_shader,
        bloom.bind_layout,
        "downsample_first",
        bloom_format,
        NULL);
    bloom.downsample_pipeline = flecsEngine_bloom_createPipeline(
        engine,
        bloom_shader,
        bloom.bind_layout,
        "downsample",
        bloom_format,
        NULL);
    bloom.upsample_pipeline = flecsEngine_bloom_createPipeline(
        engine,
        bloom_shader,
        bloom.bind_layout,
        "upsample",
        bloom_format,
        &blend_state);
    bloom.composite_surface_pipeline = flecsEngine_bloom_createCompositePipeline(
        engine,
        bloom_shader,
        bloom.bind_layout,
        bloom.composite_scene_layout,
        view_target_format);
    bloom.composite_hdr_pipeline = flecsEngine_bloom_createCompositePipeline(
        engine,
        bloom_shader,
        bloom.bind_layout,
        bloom.composite_scene_layout,
        hdr_format);

    if (!bloom.downsample_first_pipeline ||
        !bloom.downsample_pipeline ||
        !bloom.upsample_pipeline ||
        !bloom.composite_surface_pipeline ||
        !bloom.composite_hdr_pipeline)
    {
        flecsEngine_bloom_releaseResources(&bloom);
        return false;
    }

    ecs_set_ptr((ecs_world_t*)world, effect_entity, FlecsBloomImpl, &bloom);
    return true;
}

static bool flecsEngine_bloom_renderPassthrough(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    ecs_entity_t effect_entity,
    const FlecsRenderEffectKind *kind,
    FlecsRenderEffectImpl *effect_impl,
    WGPUCommandEncoder encoder,
    WGPUTextureView input_view,
    WGPUTextureView output_view,
    WGPUTextureFormat output_format,
    WGPULoadOp output_load_op,
    const char *ts_name)
{
    return flecsEngine_renderEffect_render(
        world, engine, view_impl, encoder,
        output_view, output_load_op, (WGPUColor){0},
        effect_entity, kind, effect_impl,
        input_view, output_format, ts_name, NULL);
}

static bool flecsEngine_bloom_render(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    WGPUCommandEncoder encoder,
    ecs_entity_t effect_entity,
    const FlecsRenderEffectKind *kind,
    FlecsRenderEffectImpl *effect_impl,
    WGPUTextureView input_view,
    WGPUTextureFormat input_format,
    WGPUTextureView output_view,
    WGPUTextureFormat output_format,
    WGPULoadOp output_load_op)
{
    (void)input_format;
    FlecsBloomImpl *impl = ecs_get_mut(world, effect_entity, FlecsBloomImpl);
    ecs_assert(impl != NULL, ECS_INVALID_OPERATION, NULL);

    const FlecsBloom *bloom = ecs_get(world, effect_entity, FlecsBloom);
    ecs_assert(bloom != NULL, ECS_INVALID_OPERATION, NULL);

    const char *ts_name = ecs_get_name(world, effect_entity);
    if (!ts_name) ts_name = "Bloom";

    if (bloom->intensity <= 0.0f) {
        return flecsEngine_bloom_renderPassthrough(
            world, engine, view_impl, effect_entity, kind, effect_impl,
            encoder, input_view, output_view, output_format, output_load_op,
            ts_name);
    }

    if (!flecsEngine_bloom_ensureTexture(world, engine, bloom, impl)) {
        return false;
    }

    float max_mip = (float)(impl->mip_count - 1u);
    float final_blend = flecsEngine_bloom_computeBlendFactor(
        bloom, 0.0f, max_mip);

    FlecsBloomUniform uniform = {0};
    flecsEngine_bloom_fillUniform(bloom, final_blend, &uniform);
    wgpuQueueWriteBuffer(
        engine->queue,
        impl->uniform_buffer,
        0,
        &uniform,
        sizeof(uniform));

    WGPUBindGroup input_bg = flecsEngine_bloom_ensureInputBindGroup(
        engine, impl, input_view);
    if (!input_bg) {
        return false;
    }

    WGPUBindGroup scene_bg = flecsEngine_bloom_ensureCompositeSceneBindGroup(
        engine, impl, input_view);
    if (!scene_bg) {
        return false;
    }

    int ts_pair = flecsEngine_gpuTiming_allocPair(engine, ts_name);
    WGPURenderPassTimestampWrites ts_b, ts_e;
    const WGPURenderPassTimestampWrites *ts_begin =
        flecsEngine_gpuTiming_renderPassBegin(engine, ts_pair, &ts_b);
    const WGPURenderPassTimestampWrites *ts_end =
        flecsEngine_gpuTiming_renderPassEnd(engine, ts_pair, &ts_e);

    if (!flecsEngine_bloom_runPass(
        encoder,
        impl->downsample_first_pipeline,
        input_bg,
        impl->mip_views[0],
        WGPULoadOp_Clear,
        false,
        0.0f,
        ts_begin))
    {
        return false;
    }

    for (uint32_t mip = 1; mip < impl->mip_count; mip ++) {
        if (!flecsEngine_bloom_runPass(
            encoder,
            impl->downsample_pipeline,
            impl->mip_bind_groups[mip - 1],
            impl->mip_views[mip],
            WGPULoadOp_Clear,
            false,
            0.0f,
            NULL))
        {
            return false;
        }
    }

    for (uint32_t mip = impl->mip_count - 1u; mip > 0u; mip --) {
        float blend = flecsEngine_bloom_computeBlendFactor(
            bloom, mip, max_mip);
        if (!flecsEngine_bloom_runPass(
            encoder,
            impl->upsample_pipeline,
            impl->mip_bind_groups[mip],
            impl->mip_views[mip - 1u],
            WGPULoadOp_Load,
            true,
            blend,
            NULL))
        {
            return false;
        }
    }

    WGPURenderPipeline composite_pipeline =
        output_format == flecsEngine_getViewTargetFormat(engine)
            ? impl->composite_surface_pipeline
            : impl->composite_hdr_pipeline;

    return flecsEngine_bloom_runCompositePass(
        encoder,
        composite_pipeline,
        impl->mip_bind_groups[0],
        scene_bg,
        output_view,
        output_load_op,
        ts_end);
}

static void FlecsBloom_on_set(
    ecs_iter_t *it)
{
    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        ecs_set(it->world, e, FlecsRenderEffectKind, {
            .shader = flecsEngine_bloom_shader(it->world),
            .setup_callback = flecsEngine_bloom_setup,
            .render_callback = flecsEngine_bloom_render
        });
    }
}

void flecsEngine_bloom_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsBloom);
    ECS_COMPONENT_DEFINE(world, FlecsBloomImpl);

    ecs_set_hooks(world, FlecsBloom, {
        .ctor = ecs_ctor(FlecsBloom)
    });

    ecs_set_hooks(world, FlecsBloomImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsBloomImpl),
        .dtor = ecs_dtor(FlecsBloomImpl)
    });

    ecs_entity_t bloom_prefilter_t = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "FlecsBloomPrefilter" }),
        .members = {
            { .name = "threshold", .type = ecs_id(ecs_f32_t) },
            { .name = "threshold_softness", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsBloom),
        .members = {
            { .name = "intensity", .type = ecs_id(ecs_f32_t) },
            { .name = "low_frequency_boost", .type = ecs_id(ecs_f32_t) },
            { .name = "low_frequency_boost_curvature", .type = ecs_id(ecs_f32_t) },
            { .name = "high_pass_frequency", .type = ecs_id(ecs_f32_t) },
            { .name = "prefilter", .type = bloom_prefilter_t },
            { .name = "max_mip_dimension", .type = ecs_id(ecs_u32_t) }
        }
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(FlecsBloom) }},
        .events = { EcsOnSet },
        .callback = FlecsBloom_on_set
    });
}
