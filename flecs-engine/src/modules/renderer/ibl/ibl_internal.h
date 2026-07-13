#ifndef FLECS_ENGINE_IBL_INTERNAL_H
#define FLECS_ENGINE_IBL_INTERNAL_H

#include "../renderer.h"
#include "hdri_loader.h"

void flecsEngine_ibl_releaseRuntimeResources(
    FlecsHdriImpl *ibl);

/* Helpers used by the HDRI path and (for atmosphere-driven IBL) by
 * atmosphere.c. Kept minimal — exposes cubemap texture creation, sampler
 * creation, and the preprocess (prefilter + irradiance) passes.
 * flecsIblRunPreprocessPasses now takes an external encoder so per-frame
 * callers can batch it with other work. */
uint32_t flecsIblComputeMipCount(
    uint32_t size);

bool flecsIblCreateCubeTexture(
    const FlecsEngineImpl *engine,
    uint32_t size,
    uint32_t mip_count,
    WGPUTextureFormat format,
    WGPUTexture *out_texture,
    WGPUTextureView *out_cube_view);

bool flecsIblCreateSampler(
    const FlecsEngineImpl *engine,
    FlecsHdriImpl *ibl);

bool flecsIblRunPreprocessPasses(
    ecs_world_t *world,
    const FlecsEngineImpl *engine,
    FlecsHdriImpl *ibl,
    WGPUCommandEncoder encoder,
    uint32_t filter_sample_count,
    uint32_t lut_sample_count);

#endif
