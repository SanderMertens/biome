#include <string.h>

#include "renderer.h"
#include "hiz.h"
#include "gpu_timing.h"
#include "mip_pyramid.h"
#include "../../tracy_hooks.h"
#include "flecs_engine.h"

typedef struct {
    uint32_t src_w;
    uint32_t src_h;
    uint32_t dst_w;
    uint32_t dst_h;
} flecsEngine_hizDispatchUniforms_t;

static const char *FLECS_ENGINE_HIZ_MIP0_WGSL =
"struct Uniforms {\n"
"    src_w: u32,\n"
"    src_h: u32,\n"
"    dst_w: u32,\n"
"    dst_h: u32,\n"
"};\n"
"\n"
"@group(0) @binding(0) var src_depth: texture_depth_2d;\n"
"@group(0) @binding(1) var dst: texture_storage_2d<r32float, write>;\n"
"@group(0) @binding(2) var<uniform> u: Uniforms;\n"
"\n"
"@compute @workgroup_size(8, 8)\n"
"fn cs_main(@builtin(global_invocation_id) gid: vec3<u32>) {\n"
"    if (gid.x >= u.dst_w || gid.y >= u.dst_h) {\n"
"        return;\n"
"    }\n"
"    let sx = min(gid.x, u.src_w - 1u);\n"
"    let sy = min(gid.y, u.src_h - 1u);\n"
"    let d = textureLoad(src_depth, vec2<i32>(i32(sx), i32(sy)), 0);\n"
"    textureStore(dst, vec2<i32>(i32(gid.x), i32(gid.y)), vec4<f32>(d, 0.0, 0.0, 0.0));\n"
"}\n";

static const char *FLECS_ENGINE_HIZ_REDUCE_WGSL =
"struct Uniforms {\n"
"    src_w: u32,\n"
"    src_h: u32,\n"
"    dst_w: u32,\n"
"    dst_h: u32,\n"
"};\n"
"\n"
"@group(0) @binding(0) var src: texture_2d<f32>;\n"
"@group(0) @binding(1) var dst: texture_storage_2d<r32float, write>;\n"
"@group(0) @binding(2) var<uniform> u: Uniforms;\n"
"\n"
"fn load_clamped(x: i32, y: i32) -> f32 {\n"
"    let cx = clamp(x, 0, i32(u.src_w) - 1);\n"
"    let cy = clamp(y, 0, i32(u.src_h) - 1);\n"
"    return textureLoad(src, vec2<i32>(cx, cy), 0).r;\n"
"}\n"
"\n"
"@compute @workgroup_size(8, 8)\n"
"fn cs_main(@builtin(global_invocation_id) gid: vec3<u32>) {\n"
"    if (gid.x >= u.dst_w || gid.y >= u.dst_h) {\n"
"        return;\n"
"    }\n"
"    let bx = i32(gid.x) * 2;\n"
"    let by = i32(gid.y) * 2;\n"
"    var m = load_clamped(bx, by);\n"
"    m = max(m, load_clamped(bx + 1, by));\n"
"    m = max(m, load_clamped(bx, by + 1));\n"
"    m = max(m, load_clamped(bx + 1, by + 1));\n"
"    let odd_x = (u.src_w & 1u) != 0u && u32(bx + 2) < u.src_w;\n"
"    let odd_y = (u.src_h & 1u) != 0u && u32(by + 2) < u.src_h;\n"
"    if (odd_x) {\n"
"        m = max(m, load_clamped(bx + 2, by));\n"
"        m = max(m, load_clamped(bx + 2, by + 1));\n"
"    }\n"
"    if (odd_y) {\n"
"        m = max(m, load_clamped(bx, by + 2));\n"
"        m = max(m, load_clamped(bx + 1, by + 2));\n"
"    }\n"
"    if (odd_x && odd_y) {\n"
"        m = max(m, load_clamped(bx + 2, by + 2));\n"
"    }\n"
"    textureStore(dst, vec2<i32>(i32(gid.x), i32(gid.y)), vec4<f32>(m, 0.0, 0.0, 0.0));\n"
"}\n";

static WGPUBindGroupLayout flecsEngine_hiz_createMip0Layout(
    WGPUDevice device)
{
    WGPUBindGroupLayoutEntry entries[3] = {
        { .binding = 0, .visibility = WGPUShaderStage_Compute,
          .texture = {
              .sampleType = WGPUTextureSampleType_Depth,
              .viewDimension = WGPUTextureViewDimension_2D,
              .multisampled = false } },
        { .binding = 1, .visibility = WGPUShaderStage_Compute,
          .storageTexture = {
              .access = WGPUStorageTextureAccess_WriteOnly,
              .format = WGPUTextureFormat_R32Float,
              .viewDimension = WGPUTextureViewDimension_2D } },
        { .binding = 2, .visibility = WGPUShaderStage_Compute,
          .buffer = {
              .type = WGPUBufferBindingType_Uniform,
              .minBindingSize = sizeof(flecsEngine_hizDispatchUniforms_t) } },
    };
    return wgpuDeviceCreateBindGroupLayout(device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = 3,
            .entries = entries
        });
}

static WGPUBindGroupLayout flecsEngine_hiz_createReduceLayout(
    WGPUDevice device)
{
    WGPUBindGroupLayoutEntry entries[3] = {
        { .binding = 0, .visibility = WGPUShaderStage_Compute,
          .texture = {
              .sampleType = WGPUTextureSampleType_UnfilterableFloat,
              .viewDimension = WGPUTextureViewDimension_2D,
              .multisampled = false } },
        { .binding = 1, .visibility = WGPUShaderStage_Compute,
          .storageTexture = {
              .access = WGPUStorageTextureAccess_WriteOnly,
              .format = WGPUTextureFormat_R32Float,
              .viewDimension = WGPUTextureViewDimension_2D } },
        { .binding = 2, .visibility = WGPUShaderStage_Compute,
          .buffer = {
              .type = WGPUBufferBindingType_Uniform,
              .minBindingSize = sizeof(flecsEngine_hizDispatchUniforms_t) } },
    };
    return wgpuDeviceCreateBindGroupLayout(device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = 3,
            .entries = entries
        });
}

static WGPUComputePipeline flecsEngine_hiz_createPipeline(
    WGPUDevice device,
    WGPUShaderModule module,
    WGPUBindGroupLayout layout)
{
    WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(
        device, &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &layout
        });
    if (!pl) {
        return NULL;
    }
    WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(
        device, &(WGPUComputePipelineDescriptor){
            .layout = pl,
            .compute = {
                .module = module,
                .entryPoint = WGPU_STR("cs_main")
            }
        });
    wgpuPipelineLayoutRelease(pl);
    return pipeline;
}

int flecsEngine_hiz_init(
    ecs_world_t *world,
    FlecsEngineImpl *engine)
{
    engine->hiz.mip0_shader_module = flecsEngine_shader_ensureModule(
        world, "HiZMip0Shader", FLECS_ENGINE_HIZ_MIP0_WGSL);
    engine->hiz.reduce_shader_module = flecsEngine_shader_ensureModule(
        world, "HiZReduceShader", FLECS_ENGINE_HIZ_REDUCE_WGSL);
    if (!engine->hiz.mip0_shader_module || !engine->hiz.reduce_shader_module) {
        return -1;
    }

    engine->hiz.mip0_bind_layout =
        flecsEngine_hiz_createMip0Layout(engine->device);
    engine->hiz.reduce_bind_layout =
        flecsEngine_hiz_createReduceLayout(engine->device);
    if (!engine->hiz.mip0_bind_layout || !engine->hiz.reduce_bind_layout) {
        return -1;
    }

    engine->hiz.mip0_pipeline = flecsEngine_hiz_createPipeline(
        engine->device, engine->hiz.mip0_shader_module,
        engine->hiz.mip0_bind_layout);
    engine->hiz.reduce_pipeline = flecsEngine_hiz_createPipeline(
        engine->device, engine->hiz.reduce_shader_module,
        engine->hiz.reduce_bind_layout);
    if (!engine->hiz.mip0_pipeline || !engine->hiz.reduce_pipeline) {
        return -1;
    }

    engine->hiz.sampler = wgpuDeviceCreateSampler(
        engine->device, &(WGPUSamplerDescriptor){
            .addressModeU = WGPUAddressMode_ClampToEdge,
            .addressModeV = WGPUAddressMode_ClampToEdge,
            .addressModeW = WGPUAddressMode_ClampToEdge,
            .minFilter = WGPUFilterMode_Nearest,
            .magFilter = WGPUFilterMode_Nearest,
            .mipmapFilter = WGPUMipmapFilterMode_Nearest,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 32.0f,
            .maxAnisotropy = 1
        });
    if (!engine->hiz.sampler) {
        return -1;
    }

    return 0;
}

void flecsEngine_hiz_fini(
    FlecsEngineImpl *engine)
{
    FLECS_WGPU_RELEASE(engine->hiz.sampler, wgpuSamplerRelease);
    FLECS_WGPU_RELEASE(engine->hiz.mip0_pipeline, wgpuComputePipelineRelease);
    FLECS_WGPU_RELEASE(engine->hiz.reduce_pipeline, wgpuComputePipelineRelease);
    FLECS_WGPU_RELEASE(engine->hiz.mip0_bind_layout, wgpuBindGroupLayoutRelease);
    FLECS_WGPU_RELEASE(engine->hiz.reduce_bind_layout,
        wgpuBindGroupLayoutRelease);
}

static void flecsEngine_hiz_releaseViewResources(
    FlecsRenderViewImpl *view_impl)
{
    if (view_impl->hiz_build_mip0_bg) {
        for (uint32_t i = 0; i < view_impl->hiz_mip_count; i ++) {
            FLECS_WGPU_RELEASE(view_impl->hiz_build_mip0_bg[i],
                wgpuBindGroupRelease);
        }
        ecs_os_free(view_impl->hiz_build_mip0_bg);
        view_impl->hiz_build_mip0_bg = NULL;
    }
    if (view_impl->hiz_build_reduce_bg) {
        for (uint32_t i = 0; i < view_impl->hiz_mip_count; i ++) {
            FLECS_WGPU_RELEASE(view_impl->hiz_build_reduce_bg[i],
                wgpuBindGroupRelease);
        }
        ecs_os_free(view_impl->hiz_build_reduce_bg);
        view_impl->hiz_build_reduce_bg = NULL;
    }
    if (view_impl->hiz_build_reduce_ubs) {
        for (uint32_t i = 0; i < view_impl->hiz_mip_count; i ++) {
            FLECS_WGPU_RELEASE(view_impl->hiz_build_reduce_ubs[i],
                wgpuBufferRelease);
        }
        ecs_os_free(view_impl->hiz_build_reduce_ubs);
        view_impl->hiz_build_reduce_ubs = NULL;
    }
    FLECS_WGPU_RELEASE(view_impl->hiz_build_mip0_ub, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(view_impl->hiz_view_all, wgpuTextureViewRelease);
    flecsEngine_mipPyramid_release(
        &view_impl->hiz_texture,
        &view_impl->hiz_mip_views,
        view_impl->hiz_mip_count);
    view_impl->hiz_mip_count = 0;
    view_impl->hiz_width = 0;
    view_impl->hiz_height = 0;
    view_impl->hiz_valid = false;
}

int flecsEngine_hiz_ensureView(
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl)
{
    uint32_t w = view_impl->depth_texture_width;
    uint32_t h = view_impl->depth_texture_height;
    if (!w || !h || !view_impl->depth_texture_view) {
        flecsEngine_hiz_releaseViewResources(view_impl);
        return 0;
    }

    if (view_impl->hiz_texture &&
        view_impl->hiz_width == w &&
        view_impl->hiz_height == h)
    {
        return 0;
    }

    flecsEngine_hiz_releaseViewResources(view_impl);

    uint32_t mip_count = flecsEngine_mipPyramid_maxMips(w, h);
    if (!flecsEngine_mipPyramid_create(
        engine->device, w, h, mip_count,
        WGPUTextureFormat_R32Float,
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding,
        &view_impl->hiz_texture,
        &view_impl->hiz_mip_views))
    {
        ecs_err("failed to create hi-z pyramid");
        return -1;
    }

    view_impl->hiz_mip_count = mip_count;
    view_impl->hiz_width = w;
    view_impl->hiz_height = h;
    view_impl->hiz_version ++;

    view_impl->hiz_view_all = wgpuTextureCreateView(view_impl->hiz_texture,
        &(WGPUTextureViewDescriptor){
            .format = WGPUTextureFormat_R32Float,
            .dimension = WGPUTextureViewDimension_2D,
            .baseMipLevel = 0,
            .mipLevelCount = mip_count,
            .baseArrayLayer = 0,
            .arrayLayerCount = 1
        });
    if (!view_impl->hiz_view_all) {
        flecsEngine_hiz_releaseViewResources(view_impl);
        return -1;
    }

    view_impl->hiz_build_mip0_ub = wgpuDeviceCreateBuffer(engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = sizeof(flecsEngine_hizDispatchUniforms_t)
        });
    if (!view_impl->hiz_build_mip0_ub) {
        flecsEngine_hiz_releaseViewResources(view_impl);
        return -1;
    }

    view_impl->hiz_build_mip0_bg = ecs_os_calloc_n(WGPUBindGroup, mip_count);
    view_impl->hiz_build_reduce_bg = ecs_os_calloc_n(WGPUBindGroup, mip_count);
    view_impl->hiz_build_reduce_ubs = ecs_os_calloc_n(WGPUBuffer, mip_count);

    WGPUBindGroupEntry mip0_entries[3] = {
        { .binding = 0, .textureView = view_impl->depth_texture_view },
        { .binding = 1, .textureView = view_impl->hiz_mip_views[0] },
        { .binding = 2, .buffer = view_impl->hiz_build_mip0_ub, .offset = 0,
          .size = sizeof(flecsEngine_hizDispatchUniforms_t) }
    };
    view_impl->hiz_build_mip0_bg[0] = wgpuDeviceCreateBindGroup(
        engine->device, &(WGPUBindGroupDescriptor){
            .layout = engine->hiz.mip0_bind_layout,
            .entryCount = 3,
            .entries = mip0_entries
        });
    if (!view_impl->hiz_build_mip0_bg[0]) {
        flecsEngine_hiz_releaseViewResources(view_impl);
        return -1;
    }

    for (uint32_t m = 1; m < mip_count; m ++) {
        view_impl->hiz_build_reduce_ubs[m] = wgpuDeviceCreateBuffer(
            engine->device, &(WGPUBufferDescriptor){
                .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
                .size = sizeof(flecsEngine_hizDispatchUniforms_t)
            });
        if (!view_impl->hiz_build_reduce_ubs[m]) {
            flecsEngine_hiz_releaseViewResources(view_impl);
            return -1;
        }

        WGPUBindGroupEntry r_entries[3] = {
            { .binding = 0, .textureView = view_impl->hiz_mip_views[m - 1] },
            { .binding = 1, .textureView = view_impl->hiz_mip_views[m] },
            { .binding = 2, .buffer = view_impl->hiz_build_reduce_ubs[m],
              .offset = 0,
              .size = sizeof(flecsEngine_hizDispatchUniforms_t) }
        };
        view_impl->hiz_build_reduce_bg[m] = wgpuDeviceCreateBindGroup(
            engine->device, &(WGPUBindGroupDescriptor){
                .layout = engine->hiz.reduce_bind_layout,
                .entryCount = 3,
                .entries = r_entries
            });
        if (!view_impl->hiz_build_reduce_bg[m]) {
            flecsEngine_hiz_releaseViewResources(view_impl);
            return -1;
        }
    }

    view_impl->hiz_valid = false;
    return 0;
}

void flecsEngine_hiz_finiView(
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl)
{
    (void)engine;
    flecsEngine_hiz_releaseViewResources(view_impl);
}

static uint32_t hiz_mip_dim(uint32_t base, uint32_t mip)
{
    uint32_t d = base >> mip;
    return d > 0 ? d : 1u;
}

void flecsEngine_hiz_build(
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    WGPUCommandEncoder encoder)
{
    if (!view_impl->hiz_texture || !view_impl->hiz_mip_count) {
        return;
    }
    if (!view_impl->depth_texture_view) {
        return;
    }

    FLECS_TRACY_ZONE_BEGIN("HiZBuild");

    WGPUComputePassTimestampWrites ts_writes;
    int ts_pair = flecsEngine_gpuTiming_allocPair(engine, "HiZ");
    flecsEngine_gpuTiming_computePassTimestamps(engine, ts_pair, &ts_writes);

    WGPUComputePassDescriptor desc = {
        .timestampWrites = WGPU_TIMESTAMP_WRITES(ts_pair >= 0 ? &ts_writes : NULL)
    };
    WGPUComputePassEncoder cpass = wgpuCommandEncoderBeginComputePass(
        encoder, &desc);
    if (!cpass) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    uint32_t w = view_impl->hiz_width;
    uint32_t h = view_impl->hiz_height;

    flecsEngine_hizDispatchUniforms_t u0 = {
        .src_w = view_impl->depth_texture_width,
        .src_h = view_impl->depth_texture_height,
        .dst_w = w,
        .dst_h = h
    };
    wgpuQueueWriteBuffer(engine->queue, view_impl->hiz_build_mip0_ub, 0,
        &u0, sizeof(u0));

    wgpuComputePassEncoderSetPipeline(cpass, engine->hiz.mip0_pipeline);
    wgpuComputePassEncoderSetBindGroup(cpass, 0,
        view_impl->hiz_build_mip0_bg[0], 0, NULL);
    uint32_t wg_x = (w + 7) / 8;
    uint32_t wg_y = (h + 7) / 8;
    wgpuComputePassEncoderDispatchWorkgroups(cpass, wg_x, wg_y, 1);

    wgpuComputePassEncoderSetPipeline(cpass, engine->hiz.reduce_pipeline);
    for (uint32_t m = 1; m < view_impl->hiz_mip_count; m ++) {
        uint32_t pw = hiz_mip_dim(w, m - 1);
        uint32_t ph = hiz_mip_dim(h, m - 1);
        uint32_t cw = hiz_mip_dim(w, m);
        uint32_t ch = hiz_mip_dim(h, m);

        flecsEngine_hizDispatchUniforms_t u = {
            .src_w = pw, .src_h = ph, .dst_w = cw, .dst_h = ch
        };
        wgpuQueueWriteBuffer(engine->queue,
            view_impl->hiz_build_reduce_ubs[m], 0, &u, sizeof(u));

        wgpuComputePassEncoderSetBindGroup(cpass, 0,
            view_impl->hiz_build_reduce_bg[m], 0, NULL);
        uint32_t rwg_x = (cw + 7) / 8;
        uint32_t rwg_y = (ch + 7) / 8;
        wgpuComputePassEncoderDispatchWorkgroups(cpass, rwg_x, rwg_y, 1);
    }

    wgpuComputePassEncoderEnd(cpass);
    wgpuComputePassEncoderRelease(cpass);

    view_impl->hiz_valid = true;

    FLECS_TRACY_ZONE_END;
}
