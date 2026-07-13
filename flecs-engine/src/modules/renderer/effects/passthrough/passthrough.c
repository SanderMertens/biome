#include "../../renderer.h"
#include "flecs_engine.h"

static const char *kShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "@group(0) @binding(0) var input_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var input_sampler : sampler;\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  return textureSample(input_texture, input_sampler, in.uv);\n"
    "}\n";

int flecsEngine_initPassthrough(
    ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    WGPUShaderModule module = flecsEngine_shader_ensureModule(
        world, "PassthroughShader", kShaderSource);
    if (!module) {
        return -1;
    }

    impl->pipelines.passthrough_sampler =
        flecsEngine_createLinearClampSampler(impl->device);
    if (!impl->pipelines.passthrough_sampler) {
        return -1;
    }

    WGPUBindGroupLayoutEntry layout_entries[2] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = { .type = WGPUSamplerBindingType_Filtering }
        }
    };

    impl->pipelines.passthrough_bind_layout = wgpuDeviceCreateBindGroupLayout(
        impl->device, &(WGPUBindGroupLayoutDescriptor){
            .entries = layout_entries,
            .entryCount = 2
        });
    if (!impl->pipelines.passthrough_bind_layout) {
        return -1;
    }

    WGPUColorTargetState color_target = {
        .format = flecsEngine_getViewTargetFormat(impl),
        .writeMask = WGPUColorWriteMask_All
    };

    impl->pipelines.passthrough_pipeline = flecsEngine_createFullscreenPipeline(
        impl, module, impl->pipelines.passthrough_bind_layout,
        NULL, NULL, &color_target, NULL);

    return impl->pipelines.passthrough_pipeline ? 0 : -1;
}
