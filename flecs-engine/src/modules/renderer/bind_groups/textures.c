#include "../renderer.h"
#include "flecs_engine.h"

WGPUBindGroupLayout flecsEngine_textures_ensureBindLayout(
    FlecsEngineImpl *impl)
{
    if (impl->textures.pbr_bind_layout) {
        return impl->textures.pbr_bind_layout;
    }

    WGPUBindGroupLayoutEntry entries[6] = {0};
    for (uint32_t i = 0; i < 4; i++) {
        entries[i].binding = i;
        entries[i].visibility = WGPUShaderStage_Fragment;
        entries[i].texture.sampleType = WGPUTextureSampleType_Float;
        entries[i].texture.viewDimension = WGPUTextureViewDimension_2DArray;
        entries[i].texture.multisampled = false;
    }
    entries[4].binding = 4;
    entries[4].visibility = WGPUShaderStage_Fragment;
    entries[4].sampler.type = WGPUSamplerBindingType_Filtering;
    entries[5].binding = 5;
    entries[5].visibility = WGPUShaderStage_Fragment;
    entries[5].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor layout_desc = {
        .entries = entries,
        .entryCount = 6
    };

    impl->textures.pbr_bind_layout = wgpuDeviceCreateBindGroupLayout(
        impl->device, &layout_desc);

    return impl->textures.pbr_bind_layout;
}
