#include <math.h>

#include "../../renderer.h"
#include "../../gpu_timing.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsAutoExposure);
ECS_COMPONENT_DECLARE(FlecsAutoExposureImpl);

#define FLECS_ENGINE_AUTO_EXPOSURE_HISTOGRAM_BINS 256u
#define FLECS_ENGINE_AUTO_EXPOSURE_HISTOGRAM_BYTES \
    (FLECS_ENGINE_AUTO_EXPOSURE_HISTOGRAM_BINS * sizeof(uint32_t))

typedef struct FlecsAutoExposureBuildUniforms {
    float min_log_luma;
    float inv_log_luma_range;
    uint32_t width;
    uint32_t height;
} FlecsAutoExposureBuildUniforms;

typedef struct FlecsAutoExposureReduceUniforms {
    float min_log_luma;
    float log_luma_range;
    float low_percentile;
    float high_percentile;
    float speed_up;
    float speed_down;
    float min_log_brightness;
    float max_log_brightness;
    float dt;
    uint32_t pixel_count;
    uint32_t _pad0;
    uint32_t _pad1;
} FlecsAutoExposureReduceUniforms;

static const char *kBuildShaderSource =
    "struct Uniforms {\n"
    "    min_log_luma : f32,\n"
    "    inv_log_luma_range : f32,\n"
    "    width : u32,\n"
    "    height : u32,\n"
    "};\n"
    "@group(0) @binding(0) var input_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var<storage, read_write> histogram : array<atomic<u32>, 256>;\n"
    "@group(0) @binding(2) var<uniform> u : Uniforms;\n"
    "var<workgroup> local_histogram : array<atomic<u32>, 256>;\n"
    "fn luminance(c : vec3<f32>) -> f32 {\n"
    "    return dot(c, vec3<f32>(0.2126, 0.7152, 0.0722));\n"
    "}\n"
    "@compute @workgroup_size(16, 16)\n"
    "fn cs_main(\n"
    "    @builtin(global_invocation_id) gid : vec3<u32>,\n"
    "    @builtin(local_invocation_index) lid : u32\n"
    ") {\n"
    "    atomicStore(&local_histogram[lid], 0u);\n"
    "    workgroupBarrier();\n"
    "    if (gid.x < u.width && gid.y < u.height) {\n"
    "        let color = textureLoad(input_texture, vec2<i32>(i32(gid.x), i32(gid.y)), 0).rgb;\n"
    "        let luma = max(luminance(color), 0.0);\n"
    "        var bin : u32 = 0u;\n"
    "        if (luma >= 0.0001) {\n"
    "            let t = (log2(luma) - u.min_log_luma) * u.inv_log_luma_range;\n"
    "            bin = u32(clamp(t, 0.0, 1.0) * 254.0 + 1.0);\n"
    "        }\n"
    "        atomicAdd(&local_histogram[bin], 1u);\n"
    "    }\n"
    "    workgroupBarrier();\n"
    "    let count = atomicLoad(&local_histogram[lid]);\n"
    "    if (count > 0u) {\n"
    "        atomicAdd(&histogram[lid], count);\n"
    "    }\n"
    "}\n";

static const char *kReduceShaderSource =
    "struct Uniforms {\n"
    "    min_log_luma : f32,\n"
    "    log_luma_range : f32,\n"
    "    low_percentile : f32,\n"
    "    high_percentile : f32,\n"
    "    speed_up : f32,\n"
    "    speed_down : f32,\n"
    "    min_log_brightness : f32,\n"
    "    max_log_brightness : f32,\n"
    "    dt : f32,\n"
    "    pixel_count : u32,\n"
    "    _pad0 : u32,\n"
    "    _pad1 : u32,\n"
    "};\n"
    "@group(0) @binding(0) var<storage, read_write> histogram : array<atomic<u32>, 256>;\n"
    "@group(0) @binding(1) var<storage, read_write> exposure : array<f32, 2>;\n"
    "@group(0) @binding(2) var<uniform> u : Uniforms;\n"
    "var<workgroup> shared_bins : array<u32, 256>;\n"
    "@compute @workgroup_size(256)\n"
    "fn cs_main(@builtin(local_invocation_index) idx : u32) {\n"
    "    let c = atomicLoad(&histogram[idx]);\n"
    "    shared_bins[idx] = c;\n"
    "    atomicStore(&histogram[idx], 0u);\n"
    "    workgroupBarrier();\n"
    "    if (idx != 0u) { return; }\n"
    "    var total : u32 = 0u;\n"
    "    for (var i : u32 = 1u; i < 256u; i = i + 1u) {\n"
    "        total = total + shared_bins[i];\n"
    "    }\n"
    "    /* Note: do NOT early-return when total == 0; the loop and divides\n"
    "     * below are guarded, and the bootstrap at the bottom needs to run\n"
    "     * so exposure[1] gets initialized to a non-zero value on the first\n"
    "     * frame. Early-returning here is fine if exposure is already set,\n"
    "     * but freezes a cold-start exposure[1] at 0 if the very first\n"
    "     * histogram is empty (observed on Dawn/web when ae reads an\n"
    "     * intermediate effect target). */\n"
    "    let low_t = u32(f32(total) * u.low_percentile);\n"
    "    let high_t = u32(f32(total) * u.high_percentile);\n"
    "    var running : u32 = 0u;\n"
    "    var weight_sum : f32 = 0.0;\n"
    "    var sum : f32 = 0.0;\n"
    "    for (var i : u32 = 1u; i < 256u; i = i + 1u) {\n"
    "        let bin_c = shared_bins[i];\n"
    "        let prev = running;\n"
    "        running = running + bin_c;\n"
    "        let lo_i = max(i32(low_t), i32(prev)) - i32(prev);\n"
    "        let hi_i = min(i32(high_t), i32(running)) - i32(prev);\n"
    "        let contrib = max(0, hi_i - lo_i);\n"
    "        if (contrib > 0) {\n"
    "            let bin_t = (f32(i) - 1.0) / 254.0;\n"
    "            let log_luma = u.min_log_luma + bin_t * u.log_luma_range;\n"
    "            sum = sum + log_luma * f32(contrib);\n"
    "            weight_sum = weight_sum + f32(contrib);\n"
    "        }\n"
    "    }\n"
    "    var target_log_luma : f32 = 0.0;\n"
    "    if (weight_sum > 0.0) {\n"
    "        target_log_luma = sum / weight_sum;\n"
    "    }\n"
    "    var target_ev : f32 = 0.0;\n"
    "    if (target_log_luma < u.min_log_brightness) {\n"
    "        target_ev = u.min_log_brightness - target_log_luma;\n"
    "    } else if (target_log_luma > u.max_log_brightness) {\n"
    "        target_ev = u.max_log_brightness - target_log_luma;\n"
    "    }\n"
    "    let prev_ev = exposure[0];\n"
    "    let going_up = target_ev > prev_ev;\n"
    "    let speed = select(u.speed_down, u.speed_up, going_up);\n"
    "    let alpha = 1.0 - exp(-u.dt * speed);\n"
    "    var new_ev = mix(prev_ev, target_ev, clamp(alpha, 0.0, 1.0));\n"
    "    if (exposure[1] <= 0.0) {\n"
    "        new_ev = target_ev;\n"
    "    }\n"
    "    exposure[0] = new_ev;\n"
    "    exposure[1] = exp2(new_ev);\n"
    "}\n";

static const char *kPassthroughShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "@group(0) @binding(0) var input_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var input_sampler : sampler;\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "    return textureSample(input_texture, input_sampler, in.uv);\n"
    "}\n";

ECS_CTOR(FlecsAutoExposure, ptr, {
    ptr->min_brightness = 0.1f;
    ptr->max_brightness = 0.3f;
    ptr->min_log_luma = -8.0f;
    ptr->max_log_luma = 4.0f;
    ptr->speed_up = 3.0f;
    ptr->speed_down = 1.0f;
    ptr->low_percentile = 0.5f;
    ptr->high_percentile = 0.95f;
})

static ecs_entity_t flecsEngine_autoExposure_shader(
    ecs_world_t *world)
{
    return flecsEngine_shader_ensure(world, "AutoExposurePassthroughShader",
        &(FlecsShader){
            .source = kPassthroughShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}

static void flecsEngine_autoExposure_releaseResources(
    FlecsAutoExposureImpl *impl)
{
    FLECS_WGPU_RELEASE(impl->histogram_bind_group, wgpuBindGroupRelease);
    impl->histogram_bind_view = NULL;
    FLECS_WGPU_RELEASE(impl->reduce_bind_group, wgpuBindGroupRelease);
    FLECS_WGPU_RELEASE(impl->build_pipeline, wgpuComputePipelineRelease);
    FLECS_WGPU_RELEASE(impl->reduce_pipeline, wgpuComputePipelineRelease);
    FLECS_WGPU_RELEASE(impl->build_bind_layout, wgpuBindGroupLayoutRelease);
    FLECS_WGPU_RELEASE(impl->reduce_bind_layout, wgpuBindGroupLayoutRelease);
    FLECS_WGPU_RELEASE(impl->build_uniform_buffer, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(impl->reduce_uniform_buffer, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(impl->histogram_buffer, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(impl->exposure_buffer, wgpuBufferRelease);
}

ECS_DTOR(FlecsAutoExposureImpl, ptr, {
    flecsEngine_autoExposure_releaseResources(ptr);
})

ECS_MOVE(FlecsAutoExposureImpl, dst, src, {
    flecsEngine_autoExposure_releaseResources(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

static WGPUComputePipeline flecsEngine_autoExposure_createPipeline(
    ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const char *shader_name,
    const char *source,
    WGPUBindGroupLayout bind_layout)
{
    WGPUShaderModule module = flecsEngine_shader_ensureModule(
        world, shader_name, source);
    if (!module) {
        return NULL;
    }

    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
        engine->device, &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &bind_layout
        });
    if (!pipeline_layout) {
        return NULL;
    }

    WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(
        engine->device, &(WGPUComputePipelineDescriptor){
            .layout = pipeline_layout,
            .compute = {
                .module = module,
                .entryPoint = WGPU_STR("cs_main")
            }
        });

    wgpuPipelineLayoutRelease(pipeline_layout);
    return pipeline;
}

static bool flecsEngine_autoExposure_setup(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t effect_entity,
    const FlecsRenderEffectKind *kind,
    FlecsRenderEffectImpl *effect_impl,
    WGPUBindGroupLayoutEntry *layout_entries,
    uint32_t *entry_count)
{
    (void)kind;
    (void)effect_impl;
    (void)layout_entries;
    (void)entry_count;

    FlecsAutoExposureImpl impl = {0};

    impl.histogram_buffer = wgpuDeviceCreateBuffer(engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
            .size = FLECS_ENGINE_AUTO_EXPOSURE_HISTOGRAM_BYTES
        });
    if (!impl.histogram_buffer) {
        return false;
    }
    {
        uint32_t zeros[FLECS_ENGINE_AUTO_EXPOSURE_HISTOGRAM_BINS] = {0};
        wgpuQueueWriteBuffer(engine->queue, impl.histogram_buffer, 0,
            zeros, FLECS_ENGINE_AUTO_EXPOSURE_HISTOGRAM_BYTES);
    }

    impl.exposure_buffer = wgpuDeviceCreateBuffer(engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_Uniform |
                     WGPUBufferUsage_CopyDst,
            .size = 2 * sizeof(float)
        });
    if (!impl.exposure_buffer) {
        flecsEngine_autoExposure_releaseResources(&impl);
        return false;
    }
    {
        float init[2] = { 0.0f, 0.0f };
        wgpuQueueWriteBuffer(engine->queue, impl.exposure_buffer, 0,
            init, sizeof(init));
    }

    impl.build_uniform_buffer = flecsEngine_createUniformBuffer(
        engine->device, sizeof(FlecsAutoExposureBuildUniforms));
    impl.reduce_uniform_buffer = flecsEngine_createUniformBuffer(
        engine->device, sizeof(FlecsAutoExposureReduceUniforms));
    if (!impl.build_uniform_buffer || !impl.reduce_uniform_buffer) {
        flecsEngine_autoExposure_releaseResources(&impl);
        return false;
    }

    WGPUBindGroupLayoutEntry build_entries[3] = {
        { .binding = 0, .visibility = WGPUShaderStage_Compute,
          .texture = {
              .sampleType = WGPUTextureSampleType_UnfilterableFloat,
              .viewDimension = WGPUTextureViewDimension_2D,
              .multisampled = false } },
        { .binding = 1, .visibility = WGPUShaderStage_Compute,
          .buffer = {
              .type = WGPUBufferBindingType_Storage,
              .minBindingSize = FLECS_ENGINE_AUTO_EXPOSURE_HISTOGRAM_BYTES } },
        { .binding = 2, .visibility = WGPUShaderStage_Compute,
          .buffer = {
              .type = WGPUBufferBindingType_Uniform,
              .minBindingSize = sizeof(FlecsAutoExposureBuildUniforms) } }
    };
    impl.build_bind_layout = wgpuDeviceCreateBindGroupLayout(engine->device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = 3,
            .entries = build_entries
        });

    WGPUBindGroupLayoutEntry reduce_entries[3] = {
        { .binding = 0, .visibility = WGPUShaderStage_Compute,
          .buffer = {
              .type = WGPUBufferBindingType_Storage,
              .minBindingSize = FLECS_ENGINE_AUTO_EXPOSURE_HISTOGRAM_BYTES } },
        { .binding = 1, .visibility = WGPUShaderStage_Compute,
          .buffer = {
              .type = WGPUBufferBindingType_Storage,
              .minBindingSize = 2 * sizeof(float) } },
        { .binding = 2, .visibility = WGPUShaderStage_Compute,
          .buffer = {
              .type = WGPUBufferBindingType_Uniform,
              .minBindingSize = sizeof(FlecsAutoExposureReduceUniforms) } }
    };
    impl.reduce_bind_layout = wgpuDeviceCreateBindGroupLayout(engine->device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = 3,
            .entries = reduce_entries
        });

    if (!impl.build_bind_layout || !impl.reduce_bind_layout) {
        flecsEngine_autoExposure_releaseResources(&impl);
        return false;
    }

    impl.build_pipeline = flecsEngine_autoExposure_createPipeline(
        (ecs_world_t*)world, engine, "AutoExposureBuildShader",
        kBuildShaderSource, impl.build_bind_layout);
    impl.reduce_pipeline = flecsEngine_autoExposure_createPipeline(
        (ecs_world_t*)world, engine, "AutoExposureReduceShader",
        kReduceShaderSource, impl.reduce_bind_layout);

    if (!impl.build_pipeline || !impl.reduce_pipeline) {
        flecsEngine_autoExposure_releaseResources(&impl);
        return false;
    }

    WGPUBindGroupEntry reduce_bg_entries[3] = {
        { .binding = 0, .buffer = impl.histogram_buffer, .offset = 0,
          .size = FLECS_ENGINE_AUTO_EXPOSURE_HISTOGRAM_BYTES },
        { .binding = 1, .buffer = impl.exposure_buffer, .offset = 0,
          .size = 2 * sizeof(float) },
        { .binding = 2, .buffer = impl.reduce_uniform_buffer, .offset = 0,
          .size = sizeof(FlecsAutoExposureReduceUniforms) }
    };
    impl.reduce_bind_group = wgpuDeviceCreateBindGroup(engine->device,
        &(WGPUBindGroupDescriptor){
            .layout = impl.reduce_bind_layout,
            .entryCount = 3,
            .entries = reduce_bg_entries
        });
    if (!impl.reduce_bind_group) {
        flecsEngine_autoExposure_releaseResources(&impl);
        return false;
    }

    ecs_set_ptr((ecs_world_t*)world, effect_entity, FlecsAutoExposureImpl,
        &impl);
    return true;
}

static WGPUBindGroup flecsEngine_autoExposure_ensureHistogramBindGroup(
    const FlecsEngineImpl *engine,
    FlecsAutoExposureImpl *impl,
    WGPUTextureView input_view)
{
    if (impl->histogram_bind_group && impl->histogram_bind_view == input_view) {
        return impl->histogram_bind_group;
    }
    FLECS_WGPU_RELEASE(impl->histogram_bind_group, wgpuBindGroupRelease);
    WGPUBindGroupEntry entries[3] = {
        { .binding = 0, .textureView = input_view },
        { .binding = 1, .buffer = impl->histogram_buffer, .offset = 0,
          .size = FLECS_ENGINE_AUTO_EXPOSURE_HISTOGRAM_BYTES },
        { .binding = 2, .buffer = impl->build_uniform_buffer, .offset = 0,
          .size = sizeof(FlecsAutoExposureBuildUniforms) }
    };
    impl->histogram_bind_group = wgpuDeviceCreateBindGroup(engine->device,
        &(WGPUBindGroupDescriptor){
            .layout = impl->build_bind_layout,
            .entryCount = 3,
            .entries = entries
        });
    impl->histogram_bind_view = input_view;
    return impl->histogram_bind_group;
}

static bool flecsEngine_autoExposure_render(
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

    FlecsAutoExposureImpl *impl = ecs_get_mut(
        world, effect_entity, FlecsAutoExposureImpl);
    ecs_assert(impl != NULL, ECS_INVALID_OPERATION, NULL);

    const FlecsAutoExposure *settings = ecs_get(
        world, effect_entity, FlecsAutoExposure);
    ecs_assert(settings != NULL, ECS_INVALID_OPERATION, NULL);

    const char *ts_name = ecs_get_name(world, effect_entity);
    if (!ts_name) ts_name = "AutoExposure";

    uint32_t width = view_impl->effect_target_width;
    uint32_t height = view_impl->effect_target_height;
    if (!width || !height) {
        return flecsEngine_renderEffect_render(
            world, engine, view_impl, encoder,
            output_view, output_load_op, (WGPUColor){0},
            effect_entity, kind, effect_impl,
            input_view, output_format, ts_name, NULL);
    }

    float log_luma_range = settings->max_log_luma - settings->min_log_luma;
    if (log_luma_range <= 0.0f) {
        log_luma_range = 1.0f;
    }

    FlecsAutoExposureBuildUniforms build_u = {
        .min_log_luma = settings->min_log_luma,
        .inv_log_luma_range = 1.0f / log_luma_range,
        .width = width,
        .height = height
    };
    wgpuQueueWriteBuffer(engine->queue, impl->build_uniform_buffer, 0,
        &build_u, sizeof(build_u));

    const ecs_world_info_t *wi = ecs_get_world_info(world);
    float dt = wi ? (float)wi->delta_time : 0.0f;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.25f) dt = 0.25f;

    float min_b = settings->min_brightness;
    float max_b = settings->max_brightness;
    if (min_b < 1e-6f) min_b = 1e-6f;
    if (max_b < min_b) max_b = min_b;

    FlecsAutoExposureReduceUniforms reduce_u = {
        .min_log_luma = settings->min_log_luma,
        .log_luma_range = log_luma_range,
        .low_percentile = settings->low_percentile,
        .high_percentile = settings->high_percentile,
        .speed_up = settings->speed_up,
        .speed_down = settings->speed_down,
        .min_log_brightness = log2f(min_b),
        .max_log_brightness = log2f(max_b),
        .dt = dt,
        .pixel_count = width * height,
        ._pad0 = 0,
        ._pad1 = 0
    };
    wgpuQueueWriteBuffer(engine->queue, impl->reduce_uniform_buffer, 0,
        &reduce_u, sizeof(reduce_u));

    WGPUBindGroup build_bg = flecsEngine_autoExposure_ensureHistogramBindGroup(
        engine, impl, input_view);
    if (!build_bg) {
        return false;
    }

    int ts_pair = flecsEngine_gpuTiming_allocPair(engine, ts_name);
    WGPUComputePassTimestampWrites ts_writes;
    flecsEngine_gpuTiming_computePassTimestamps(engine, ts_pair, &ts_writes);

    WGPUComputePassEncoder cpass = wgpuCommandEncoderBeginComputePass(encoder,
        &(WGPUComputePassDescriptor){
            .timestampWrites = WGPU_TIMESTAMP_WRITES(ts_pair >= 0 ? &ts_writes : NULL)
        });
    if (!cpass) {
        return false;
    }

    wgpuComputePassEncoderSetPipeline(cpass, impl->build_pipeline);
    wgpuComputePassEncoderSetBindGroup(cpass, 0, build_bg, 0, NULL);
    uint32_t wg_x = (width + 15) / 16;
    uint32_t wg_y = (height + 15) / 16;
    wgpuComputePassEncoderDispatchWorkgroups(cpass, wg_x, wg_y, 1);

    wgpuComputePassEncoderSetPipeline(cpass, impl->reduce_pipeline);
    wgpuComputePassEncoderSetBindGroup(cpass, 0, impl->reduce_bind_group, 0,
        NULL);
    wgpuComputePassEncoderDispatchWorkgroups(cpass, 1, 1, 1);

    wgpuComputePassEncoderEnd(cpass);
    wgpuComputePassEncoderRelease(cpass);

    return flecsEngine_renderEffect_render(
        world, engine, view_impl, encoder,
        output_view, output_load_op, (WGPUColor){0},
        effect_entity, kind, effect_impl,
        input_view, output_format, NULL, NULL);
}

static void FlecsAutoExposure_on_set(
    ecs_iter_t *it)
{
    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        ecs_set(it->world, e, FlecsRenderEffectKind, {
            .shader = flecsEngine_autoExposure_shader(it->world),
            .setup_callback = flecsEngine_autoExposure_setup,
            .render_callback = flecsEngine_autoExposure_render
        });
    }
}

WGPUBuffer flecsEngine_autoExposure_getBuffer(
    const ecs_world_t *world,
    ecs_entity_t effect)
{
    if (!effect) return NULL;
    const FlecsAutoExposureImpl *impl = ecs_get(
        world, effect, FlecsAutoExposureImpl);
    return impl ? impl->exposure_buffer : NULL;
}

void flecsEngine_autoExposure_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsAutoExposure);
    ECS_COMPONENT_DEFINE(world, FlecsAutoExposureImpl);

    ecs_set_hooks(world, FlecsAutoExposure, {
        .ctor = ecs_ctor(FlecsAutoExposure)
    });

    ecs_set_hooks(world, FlecsAutoExposureImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsAutoExposureImpl),
        .dtor = ecs_dtor(FlecsAutoExposureImpl)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsAutoExposure),
        .members = {
            { .name = "min_brightness", .type = ecs_id(ecs_f32_t) },
            { .name = "max_brightness", .type = ecs_id(ecs_f32_t) },
            { .name = "min_log_luma", .type = ecs_id(ecs_f32_t) },
            { .name = "max_log_luma", .type = ecs_id(ecs_f32_t) },
            { .name = "speed_up", .type = ecs_id(ecs_f32_t) },
            { .name = "speed_down", .type = ecs_id(ecs_f32_t) },
            { .name = "low_percentile", .type = ecs_id(ecs_f32_t) },
            { .name = "high_percentile", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(FlecsAutoExposure) }},
        .events = { EcsOnSet },
        .callback = FlecsAutoExposure_on_set
    });
}
