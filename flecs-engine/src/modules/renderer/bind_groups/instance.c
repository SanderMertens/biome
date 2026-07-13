#include "../renderer.h"
#include "flecs_engine.h"

WGPUBindGroupLayout flecsEngine_instanceBind_ensureLayout(
    FlecsEngineImpl *impl)
{
    if (impl->instance_bind_layout) {
        return impl->instance_bind_layout;
    }

    WGPUBindGroupLayoutEntry entries[2] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Vertex,
            .buffer = {
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .minBindingSize = sizeof(FlecsGpuTransform)
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Vertex,
            .buffer = {
                .type = WGPUBufferBindingType_ReadOnlyStorage,
                .minBindingSize = sizeof(FlecsMaterialId)
            }
        }
    };

    impl->instance_bind_layout = wgpuDeviceCreateBindGroupLayout(
        impl->device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = 2,
            .entries = entries
        });

    return impl->instance_bind_layout;
}

WGPUBindGroup flecsEngine_instanceBind_createGroup(
    const FlecsEngineImpl *engine,
    WGPUBuffer transforms,
    uint64_t transforms_size,
    WGPUBuffer material_ids,
    uint64_t material_ids_size)
{
    if (!engine->instance_bind_layout ||
        !transforms || !transforms_size ||
        !material_ids || !material_ids_size)
    {
        return NULL;
    }

    return wgpuDeviceCreateBindGroup(
        engine->device,
        &(WGPUBindGroupDescriptor){
            .layout = engine->instance_bind_layout,
            .entryCount = 2,
            .entries = (WGPUBindGroupEntry[2]){
                {
                    .binding = 0,
                    .buffer = transforms,
                    .size = transforms_size
                },
                {
                    .binding = 1,
                    .buffer = material_ids,
                    .size = material_ids_size
                }
            }
        });
}

void flecsEngine_instanceBind_release(
    FlecsEngineImpl *impl)
{
    FLECS_WGPU_RELEASE(impl->instance_bind_layout, wgpuBindGroupLayoutRelease);
}
