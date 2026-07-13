#include "../renderer.h"
#include "flecs_engine.h"

WGPUBindGroupLayout flecsEngine_materialBind_ensureLayout(
    FlecsEngineImpl *impl)
{
    if (impl->materials.bind_layout) {
        return impl->materials.bind_layout;
    }

    WGPUBindGroupLayoutEntry entry = {
        .binding = 0,
        .visibility = WGPUShaderStage_Fragment,
        .buffer = {
            .type = WGPUBufferBindingType_ReadOnlyStorage,
            .minBindingSize = sizeof(FlecsGpuMaterial)
        }
    };

    impl->materials.bind_layout = wgpuDeviceCreateBindGroupLayout(
        impl->device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = 1,
            .entries = &entry
        });

    return impl->materials.bind_layout;
}

WGPUBindGroup flecsEngine_materialBind_createGroup(
    const FlecsEngineImpl *engine,
    WGPUBuffer buffer,
    uint64_t size)
{
    if (!engine->materials.bind_layout || !buffer || !size) {
        return NULL;
    }

    return wgpuDeviceCreateBindGroup(
        engine->device,
        &(WGPUBindGroupDescriptor){
            .layout = engine->materials.bind_layout,
            .entryCount = 1,
            .entries = (WGPUBindGroupEntry[1]){
                {
                    .binding = 0,
                    .buffer = buffer,
                    .size = size
                }
            }
        });
}

WGPUBindGroup flecsEngine_materialBind_ensure(
    FlecsEngineImpl *impl)
{
    flecsEngine_material_ensureBuffer(impl);
    if (!impl->materials.buffer) {
        return NULL;
    }

    flecsEngine_materialBind_ensureLayout(impl);
    if (!impl->materials.bind_layout) {
        return NULL;
    }

    if (impl->materials.bind_group &&
        impl->materials.bind_version == impl->scene_bind_version)
    {
        return impl->materials.bind_group;
    }

    FLECS_WGPU_RELEASE(impl->materials.bind_group, wgpuBindGroupRelease);

    uint64_t size =
        (uint64_t)impl->materials.buffer_capacity * sizeof(FlecsGpuMaterial);

    impl->materials.bind_group = flecsEngine_materialBind_createGroup(
        impl, impl->materials.buffer, size);
    impl->materials.bind_version = impl->scene_bind_version;

    return impl->materials.bind_group;
}

void flecsEngine_materialBind_release(
    FlecsEngineImpl *impl)
{
    FLECS_WGPU_RELEASE(impl->materials.bind_group, wgpuBindGroupRelease);
    FLECS_WGPU_RELEASE(impl->materials.bind_layout, wgpuBindGroupLayoutRelease);
    impl->materials.bind_version = 0;
}
