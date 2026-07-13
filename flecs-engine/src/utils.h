#ifndef FLECS_ENGINE_UTILS_H
#define FLECS_ENGINE_UTILS_H

#include "types.h"

void flecsEngine_registerVec3Type(
    ecs_world_t *world,
    ecs_entity_t component);

uint64_t flecsEngine_type_sizeof(
    const ecs_world_t *world,
    ecs_entity_t type);

float flecsEngine_colorChannelToFloat(
    uint8_t value);

int32_t flecsEngine_vertexAttrFromType(
    const ecs_world_t *world,
    ecs_entity_t type,
    WGPUVertexAttribute *attrs,
    int32_t attr_count,
    int32_t location_offset);

/* Compute a normalized light ray direction from pitch/yaw rotation.
 * Returns true if the direction is valid, false if degenerate. */
bool flecsEngine_lightDirFromRotation(
    const FlecsRotation3 *rotation,
    float out_ray_dir[3]);

/* Returns the effective HDR color format, falling back to the surface
 * format when no HDR format is configured. */
WGPUTextureFormat flecsEngine_getHdrFormat(
    const FlecsEngineImpl *impl);

WGPUTextureFormat flecsEngine_getViewTargetFormat(
    const FlecsEngineImpl *impl);

ecs_entity_t flecsEngine_vecEntity(
    ecs_world_t *world);

ecs_entity_t flecsEngine_vecI8(
    ecs_world_t *world);

ecs_entity_t flecsEngine_vecVec3(
    ecs_world_t *world);

ecs_entity_t flecsEngine_vecU16(
    ecs_world_t *world);

ecs_entity_t flecsEngine_vecVec2(
    ecs_world_t *world);

ecs_entity_t flecsEngine_vecU32(
    ecs_world_t *world);

ecs_entity_t flecsEngine_vecF32(
    ecs_world_t *world);

ecs_entity_t flecsEngine_vecRgba(
    ecs_world_t *world);

#endif
