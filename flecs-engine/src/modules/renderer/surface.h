#ifndef FLECS_ENGINE_RENDERER_SURFACE_H
#define FLECS_ENGINE_RENDERER_SURFACE_H

typedef struct FlecsEngineSurface {
    WGPUTextureView view_texture;
    WGPUTexture surface_texture;
    bool owns_view_texture;
    WGPUSurfaceGetCurrentTextureStatus surface_status;
    WGPUBuffer readback_buffer;
    uint32_t readback_bytes_per_row;
    uint64_t readback_buffer_size;
} FlecsEngineSurface;

typedef struct FlecsSurface FlecsSurface;
typedef struct FlecsSurfaceImpl FlecsSurfaceImpl;

typedef struct FlecsEngineSurfaceInterface {
    int (*init_instance)(
        FlecsEngineImpl *engine,
        const FlecsSurface *config,
        FlecsSurfaceImpl *impl);
    int (*configure_target)(
        ecs_world_t *world,
        FlecsEngineImpl *engine,
        FlecsSurfaceImpl *impl);
    int (*prepare_frame)(
        ecs_world_t *world,
        FlecsEngineImpl *engine,
        FlecsSurfaceImpl *impl);
    int (*acquire_frame)(
        FlecsEngineImpl *engine,
        FlecsSurfaceImpl *impl,
        FlecsEngineSurface *target);
    int (*encode_frame)(
        ecs_world_t *world,
        FlecsEngineImpl *engine,
        FlecsSurfaceImpl *impl,
        WGPUCommandEncoder encoder,
        FlecsEngineSurface *target);
    int (*submit_frame)(
        ecs_world_t *world,
        FlecsEngineImpl *engine,
        FlecsSurfaceImpl *impl,
        const FlecsEngineSurface *target);
    void (*on_frame_failed)(
        ecs_world_t *world,
        FlecsEngineImpl *engine,
        FlecsSurfaceImpl *impl);
    void (*cleanup)(
        FlecsEngineImpl *engine,
        FlecsSurfaceImpl *impl,
        bool terminate_runtime);
} FlecsEngineSurfaceInterface;

int flecsEngine_surfaceInterface_initInstance(
    FlecsEngineImpl *engine,
    const FlecsSurface *config,
    FlecsSurfaceImpl *impl);

int flecsEngine_surfaceInterface_configureTarget(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl);

int flecsEngine_surfaceInterface_prepareFrame(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl);

int flecsEngine_surfaceInterface_acquireFrame(
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl,
    FlecsEngineSurface *target);

int flecsEngine_surfaceInterface_encodeFrame(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl,
    WGPUCommandEncoder encoder,
    FlecsEngineSurface *target);

int flecsEngine_surfaceInterface_submitFrame(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl,
    const FlecsEngineSurface *target);

void flecsEngine_surfaceInterface_onFrameFailed(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl);

void flecsEngine_surfaceInterface_cleanup(
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl,
    bool terminate_runtime);

/* Write width/height/resolution_scale/sample_count to the FlecsSurface
 * component on `surface_entity` and recompute derived actual_* fields. */
void flecsEngine_surface_set(
    ecs_world_t *world,
    ecs_entity_t surface_entity,
    int32_t width,
    int32_t height,
    int32_t resolution_scale);

int32_t flecsEngine_surface_sampleCount(
    const FlecsSurface *surface);

#include "surfaces/window.h"
#include "surfaces/frame_out.h"

#endif
