#include <string.h>

#include "private.h"
#include "modules/renderer/gpu_timing.h"

WGPUShaderModule flecsEngine_createShaderModule(
    WGPUDevice device,
    const char *wgsl_source)
{
    WGPUShaderSourceWGSL wgsl_desc = {
        .chain = {
            .sType = WGPUSType_ShaderSourceWGSL
        },
        .code = WGPU_SHADER_CODE(wgsl_source)
    };

    return wgpuDeviceCreateShaderModule(
        device, &(WGPUShaderModuleDescriptor){
            .nextInChain = (WGPUChainedStruct *)&wgsl_desc
        });
}

WGPURenderPipeline flecsEngine_createFullscreenPipeline(
    const FlecsEngineImpl *impl,
    WGPUShaderModule module,
    WGPUBindGroupLayout bind_layout,
    const char *vertex_entry,
    const char *fragment_entry,
    const WGPUColorTargetState *color_target,
    const WGPUDepthStencilState *depth_stencil)
{
    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
        impl->device, &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &bind_layout
        });
    if (!pipeline_layout) {
        return NULL;
    }

    if (!vertex_entry) {
        vertex_entry = "vs_main";
    }
    if (!fragment_entry) {
        fragment_entry = "fs_main";
    }

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(
        impl->device, &(WGPURenderPipelineDescriptor){
            .layout = pipeline_layout,
            .vertex = {
                .module = module,
                .entryPoint = WGPU_STR(vertex_entry)
            },
            .fragment = &(WGPUFragmentState){
                .module = module,
                .entryPoint = WGPU_STR(fragment_entry),
                .targetCount = color_target ? 1 : 0,
                .targets = color_target
            },
            .primitive = {
                .topology = WGPUPrimitiveTopology_TriangleList,
                .cullMode = WGPUCullMode_None,
                .frontFace = WGPUFrontFace_CCW
            },
            .depthStencil = depth_stencil,
            .multisample = WGPU_MULTISAMPLE_DEFAULT
        });

    wgpuPipelineLayoutRelease(pipeline_layout);
    return pipeline;
}

WGPURenderPassEncoder flecsEngine_beginColorPass(
    WGPUCommandEncoder encoder,
    WGPUTextureView view,
    WGPULoadOp load_op,
    WGPUColor clear_value)
{
    WGPURenderPassColorAttachment color_att = {
        .view = view,
        WGPU_DEPTH_SLICE
        .loadOp = load_op,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = clear_value
    };

    return wgpuCommandEncoderBeginRenderPass(
        encoder, &(WGPURenderPassDescriptor){
            .colorAttachmentCount = 1,
            .colorAttachments = &color_att
        });
}

WGPUComputePipeline flecsEngine_createComputePipeline(
    const FlecsEngineImpl *impl,
    WGPUShaderModule module,
    WGPUBindGroupLayout bind_layout,
    const char *entry)
{
    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
        impl->device, &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &bind_layout
        });
    if (!pipeline_layout) {
        return NULL;
    }

    if (!entry) {
        entry = "cs_main";
    }

    WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(
        impl->device, &(WGPUComputePipelineDescriptor){
            .layout = pipeline_layout,
            .compute = {
                .module = module,
                .entryPoint = WGPU_STR(entry)
            }
        });

    wgpuPipelineLayoutRelease(pipeline_layout);
    return pipeline;
}

WGPUSampler flecsEngine_createLinearClampSampler(
    WGPUDevice device)
{
    return wgpuDeviceCreateSampler(device, &(WGPUSamplerDescriptor){
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Linear,
        .lodMinClamp = 0.0f,
        .lodMaxClamp = 32.0f,
        .maxAnisotropy = 1
    });
}

WGPUBuffer flecsEngine_createUniformBuffer(
    WGPUDevice device,
    uint64_t size)
{
    return wgpuDeviceCreateBuffer(device, &(WGPUBufferDescriptor){
        .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
        .size = size
    });
}

WGPUBindGroup flecsEngine_bindGroup_ensure(
    flecsEngine_bind_group_t *bg,
    WGPUDevice device,
    WGPUBindGroupLayout layout,
    const WGPUBindGroupEntry *entries,
    uint32_t count)
{
    ecs_assert(count <= FLECS_ENGINE_BIND_GROUP_MAX_ENTRIES,
        ECS_INVALID_PARAMETER, NULL);

    if (bg->handle &&
        bg->entry_count == count &&
        memcmp(bg->entries, entries,
            count * sizeof(WGPUBindGroupEntry)) == 0)
    {
        return bg->handle;
    }

    FLECS_WGPU_RELEASE(bg->handle, wgpuBindGroupRelease);
    bg->handle = wgpuDeviceCreateBindGroup(device,
        &(WGPUBindGroupDescriptor){
            .layout = layout,
            .entryCount = count,
            .entries = entries
        });
    if (bg->handle) {
        memcpy(bg->entries, entries, count * sizeof(WGPUBindGroupEntry));
        bg->entry_count = count;
    } else {
        bg->entry_count = 0;
    }
    return bg->handle;
}

void flecsEngine_bindGroup_release(
    flecsEngine_bind_group_t *bg)
{
    FLECS_WGPU_RELEASE(bg->handle, wgpuBindGroupRelease);
    bg->entry_count = 0;
}

bool flecsEngine_fullscreenPass(
    WGPUCommandEncoder encoder,
    WGPUTextureView view,
    WGPULoadOp load_op,
    WGPUColor clear_value,
    WGPURenderPipeline pipeline,
    WGPUBindGroup bind_group,
    FlecsEngineImpl *engine,
    const char *ts_name,
    const WGPURenderPassTimestampWrites *ts_writes)
{
    WGPURenderPassTimestampWrites ts_local;
    if (!ts_writes && engine && ts_name) {
        int ts_pair = flecsEngine_gpuTiming_allocPair(engine, ts_name);
        if (ts_pair >= 0) {
            flecsEngine_gpuTiming_renderPassTimestamps(
                engine, ts_pair, &ts_local);
            ts_writes = &ts_local;
        }
    }

    WGPURenderPassColorAttachment color_att = {
        .view = view,
        WGPU_DEPTH_SLICE
        .loadOp = load_op,
        .storeOp = WGPUStoreOp_Store,
        .clearValue = clear_value
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
    if (bind_group) {
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);
    }
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    return true;
}
