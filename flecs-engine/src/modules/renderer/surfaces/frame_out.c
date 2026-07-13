#include "../renderer.h"

#include <stdio.h>
#include <stdlib.h>

static const uint32_t kFrameOutRowPitchAlignment = 256;

typedef struct {
    bool done;
    WGPUMapAsyncStatus status;
} flecs_engine_buffer_map_state_t;

static void flecsEngine_frameOut_onBufferMap(
    WGPUMapAsyncStatus status,
    const char *message,
    void *userdata)
{
    flecs_engine_buffer_map_state_t *state = userdata;
    state->status = status;
    state->done = true;

    if (status != WGPUMapAsyncStatus_Success) {
        if (message) {
            ecs_err("Readback map failed: %s\n", message);
        } else {
            ecs_err("Readback map failed with status=%d\n", (int)status);
        }
    }
}

static int flecsEngine_frameOut_writeFrame(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsSurfaceImpl *impl,
    const uint8_t *rgba,
    uint32_t bytes_per_row)
{
    const FlecsSurface *surface = ecs_get(world, engine->surface, FlecsSurface);

    const char *path = impl->path;
    FILE *file = fopen(path, "wb");
    if (!file) {
        ecs_err("Failed to open frame-output file: %s\n", path);
        return -1;
    }

    if (fprintf(file, "P6\n%u %u\n255\n",
        (uint32_t)surface->width, (uint32_t)surface->height) < 0)
    {
        ecs_err("Failed to write PPM header: %s\n", path);
        fclose(file);
        return -1;
    }

    for (uint32_t y = 0; y < (uint32_t)surface->height; y ++) {
        const uint8_t *row = rgba + ((size_t)y * bytes_per_row);
        for (uint32_t x = 0; x < (uint32_t)surface->width; x ++) {
            if (fwrite(&row[(size_t)x * 4u], 1, 3, file) != 3) {
                ecs_err("Failed to write PPM pixel data: %s\n", path);
                fclose(file);
                return -1;
            }
        }
    }

    fclose(file);
    return 0;
}

static int flecsEngine_frameOut_createResources(
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl,
    uint32_t width,
    uint32_t height)
{
    FLECS_WGPU_RELEASE(impl->offscreen_view, wgpuTextureViewRelease);
    FLECS_WGPU_RELEASE(impl->offscreen_texture, wgpuTextureRelease);

    WGPUTextureDescriptor color_desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc,
        .dimension = WGPUTextureDimension_2D,
        .size = (WGPUExtent3D){
            .width = width,
            .height = height,
            .depthOrArrayLayers = 1
        },
        .format = engine->target_format,
        .mipLevelCount = 1,
        .sampleCount = 1
    };

    impl->offscreen_texture = wgpuDeviceCreateTexture(engine->device, &color_desc);
    if (!impl->offscreen_texture) {
        ecs_err("Failed to create frame-output texture\n");
        return -1;
    }

    impl->offscreen_view = wgpuTextureCreateView(impl->offscreen_texture, NULL);
    if (!impl->offscreen_view) {
        ecs_err("Failed to create frame-output texture view\n");
        wgpuTextureRelease(impl->offscreen_texture);
        impl->offscreen_texture = NULL;
        return -1;
    }

    return 0;
}

static int flecsEngine_frameOut_writeImage(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsSurfaceImpl *impl,
    const FlecsEngineSurface *target)
{
    flecs_engine_buffer_map_state_t map_state = {
        .done = false,
        .status = WGPUMapAsyncStatus_Unknown
    };

    flecsEngine_bufferMapAsync(
        target->readback_buffer,
        WGPUMapMode_Read,
        0,
        (size_t)target->readback_buffer_size,
        flecsEngine_frameOut_onBufferMap,
        &map_state);

    flecsEngine_processEventsUntilDone(engine->instance, &map_state.done);
    if (map_state.status != WGPUMapAsyncStatus_Success) {
        return -1;
    }

    const uint8_t *mapped = wgpuBufferGetConstMappedRange(
        target->readback_buffer, 0, (size_t)target->readback_buffer_size);
    if (!mapped) {
        ecs_err("Failed to access mapped readback buffer\n");
        wgpuBufferUnmap(target->readback_buffer);
        return -1;
    }

    int result = flecsEngine_frameOut_writeFrame(
        world, engine, impl, mapped, target->readback_bytes_per_row);

    wgpuBufferUnmap(target->readback_buffer);
    return result;
}

static int flecsEngine_frameOut_initInstance(
    FlecsEngineImpl *engine,
    const FlecsSurface *config,
    FlecsSurfaceImpl *impl)
{
    (void)engine;

    const char *path = config->write_to_file;
    if (!path || !path[0]) {
        ecs_err("FlecsSurface.write_to_file is empty\n");
        return -1;
    }

    impl->path = ecs_os_strdup(path);
    if (!impl->path) {
        ecs_err("Failed to allocate frame-output path\n");
        return -1;
    }

    impl->write_nth = config->write_nth > 0 ? config->write_nth : 0;
    impl->frame_count = 0;

    return 0;
}

static int flecsEngine_frameOut_configureTarget(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl)
{
    engine->target_format = WGPUTextureFormat_RGBA8UnormSrgb;

    const FlecsSurface *surface = ecs_get(world, engine->surface, FlecsSurface);
    return flecsEngine_frameOut_createResources(
        engine, impl, (uint32_t)surface->width, (uint32_t)surface->height);
}

static int flecsEngine_frameOut_prepareFrame(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl)
{
    (void)engine;
    if (impl->done) {
        ecs_quit(world);
        return 1;
    }

    return 0;
}

static int flecsEngine_frameOut_acquireFrame(
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl,
    FlecsEngineSurface *target)
{
    (void)engine;
    target->view_texture = impl->offscreen_view;

    if (!target->view_texture) {
        ecs_err("Failed to acquire frame-output texture view\n");
        return -1;
    }

    return 0;
}

static int flecsEngine_frameOut_encodeFrame(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl,
    WGPUCommandEncoder encoder,
    FlecsEngineSurface *target)
{
    const FlecsSurface *surface = ecs_get(world, engine->surface, FlecsSurface);

    uint32_t bytes_per_row = ECS_ALIGN(
        (uint32_t)surface->width * 4u,
        kFrameOutRowPitchAlignment);
    uint64_t buffer_size = (uint64_t)bytes_per_row * (uint64_t)surface->height;

    WGPUBufferDescriptor readback_desc = {
        .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
        .size = buffer_size,
        .mappedAtCreation = false
    };

    WGPUBuffer readback = wgpuDeviceCreateBuffer(engine->device, &readback_desc);
    if (!readback) {
        ecs_err("Failed to create readback buffer\n");
        return -1;
    }

    WGPUTexelCopyTextureInfo source = {
        .texture = impl->offscreen_texture,
        .mipLevel = 0,
        .origin = {0, 0, 0},
        .aspect = WGPUTextureAspect_All
    };

    WGPUTexelCopyBufferInfo destination = {
        .layout = {
            .offset = 0,
            .bytesPerRow = bytes_per_row,
            .rowsPerImage = (uint32_t)surface->height
        },
        .buffer = readback
    };

    WGPUExtent3D copy_size = {
        .width = (uint32_t)surface->width,
        .height = (uint32_t)surface->height,
        .depthOrArrayLayers = 1
    };

    wgpuCommandEncoderCopyTextureToBuffer(
        encoder,
        &source,
        &destination,
        &copy_size);

    target->readback_buffer = readback;
    target->readback_bytes_per_row = bytes_per_row;
    target->readback_buffer_size = buffer_size;
    return 0;
}

static int flecsEngine_frameOut_submitFrame(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl,
    const FlecsEngineSurface *target)
{
    if (impl->frame_count ++ < impl->write_nth) {
        return 0;
    }

    int result = flecsEngine_frameOut_writeImage(world, engine, impl, target);
    impl->done = true;
    ecs_quit(world);
    return result;
}

static void flecsEngine_frameOut_onFrameFailed(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl)
{
    (void)engine;
    impl->done = true;
    ecs_quit(world);
}

const FlecsEngineSurfaceInterface flecsEngineFrameOutOps = {
    .init_instance = flecsEngine_frameOut_initInstance,
    .configure_target = flecsEngine_frameOut_configureTarget,
    .prepare_frame = flecsEngine_frameOut_prepareFrame,
    .acquire_frame = flecsEngine_frameOut_acquireFrame,
    .encode_frame = flecsEngine_frameOut_encodeFrame,
    .submit_frame = flecsEngine_frameOut_submitFrame,
    .on_frame_failed = flecsEngine_frameOut_onFrameFailed
};
