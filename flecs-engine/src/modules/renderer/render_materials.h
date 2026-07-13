#ifndef FLECS_ENGINE_RENDER_MATERIALS_H
#define FLECS_ENGINE_RENDER_MATERIALS_H

#include "renderer.h"

void flecsEngine_material_uploadBuffer(
    const ecs_world_t *world,
    FlecsEngineImpl *impl);

void flecsEngine_material_releaseBuffer(
    FlecsEngineImpl *impl);

WGPUBindGroupLayout flecsEngine_globals_ensureBindLayout(
    FlecsEngineImpl *impl);

bool flecsEngine_globals_createBindGroup(
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    ecs_entity_t hdri_entity,
    FlecsHdriImpl *ibl);

WGPUBindGroupLayout flecsEngine_materialBind_ensureLayout(
    FlecsEngineImpl *impl);

WGPUBindGroup flecsEngine_materialBind_createGroup(
    const FlecsEngineImpl *engine,
    WGPUBuffer buffer,
    uint64_t size);

WGPUBindGroup flecsEngine_materialBind_ensure(
    FlecsEngineImpl *impl);

void flecsEngine_materialBind_release(
    FlecsEngineImpl *impl);

WGPUBindGroupLayout flecsEngine_instanceBind_ensureLayout(
    FlecsEngineImpl *impl);

WGPUBindGroup flecsEngine_instanceBind_createGroup(
    const FlecsEngineImpl *engine,
    WGPUBuffer transforms,
    uint64_t transforms_size,
    WGPUBuffer material_ids,
    uint64_t material_ids_size);

void flecsEngine_instanceBind_release(
    FlecsEngineImpl *impl);

void flecsEngine_material_ensureBuffer(
    FlecsEngineImpl *impl);

FlecsGpuMaterial flecsEngine_material_pack(
    const FlecsEngineImpl *engine,
    const FlecsRgba *color,
    const FlecsPbrMaterial *pbr,
    const FlecsEmissive *emissive,
    const FlecsTransmission *transmission,
    const FlecsTextureTransform *tex_transform,
    const FlecsTint *tint);

FlecsGpuMaterial flecsEngine_material_tintShared(
    const FlecsEngineImpl *engine,
    uint32_t material_id,
    const FlecsTint *tint);

flecsEngine_default_attr_cache_t* flecsEngine_defaultAttrCache_create(void);

WGPUBuffer flecsEngine_material_getIdIdentityBuffer(
    FlecsEngineImpl *engine,
    int32_t count);

void flecsEngine_defaultAttrCache_free(
    flecsEngine_default_attr_cache_t *ptr);

FlecsPbrMaterial* flecsEngine_defaultAttrCache_getMaterial(
    const FlecsEngineImpl *engine,
    int32_t count);

FlecsEmissive* flecsEngine_defaultAttrCache_getEmissive(
    const FlecsEngineImpl *engine,
    int32_t count);

FlecsRgba* flecsEngine_defaultAttrCache_getColor(
    const FlecsEngineImpl *engine,
    int32_t count);

void flecsEngine_setupLights(
    const ecs_world_t *world,
    FlecsEngineImpl *engine);

#endif
