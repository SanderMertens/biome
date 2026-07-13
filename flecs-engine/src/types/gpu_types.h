#ifndef FLECS_ENGINE_TYPES_GPU_TYPES_H
#define FLECS_ENGINE_TYPES_GPU_TYPES_H

#include "flecs_engine.h"
#include "../vendor.h"

typedef struct {
    flecs_vec3_t p;
} FlecsGpuVertex;

extern ECS_COMPONENT_DECLARE(FlecsGpuVertex);

typedef struct {
    flecs_vec3_t p;
    flecs_vec3_t n;
    flecs_vec2_t uv;
    flecs_vec4_t t;  /* tangent xyz + bitangent sign in w */
} FlecsGpuVertexLitUv;

extern ECS_COMPONENT_DECLARE(FlecsGpuVertexLitUv);

typedef struct {
    flecs_vec4_t c0;
    flecs_vec4_t c1;
    flecs_vec4_t c2;
    flecs_vec4_t c3;
} FlecsGpuTransform;

extern ECS_COMPONENT_DECLARE(FlecsGpuTransform);

typedef struct {
    float position[4];  /* xyz = position, w = range */
    float direction[4]; /* xyz = direction, w = outer_cos (-2 = point light) */
    float color[4];     /* rgb = color * intensity, w = inner_cos */
} FlecsGpuLight;

typedef struct {
    flecs_mat4_t mvp;
    flecs_mat4_t inv_vp;
    flecs_mat4_t light_vp[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    float cascade_splits[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    float light_ray_dir[4];
    float light_color[4];
    float camera_pos[4];
    float shadow_info[4];
    float ambient_light[4];
    /* Cloud-shadow projection: xy = footprint origin (world XZ, lower-left
     * corner), z = strength (0 disables), w = 1 / footprint size. */
    float cloud_shadow_params[4];
    float tint_params[4];
} FlecsGpuUniforms;

extern ECS_COMPONENT_DECLARE(FlecsGpuUniforms);

typedef struct {
    flecs_rgba_t color;
    float metallic;
    float roughness;
    float emissive_strength;
    flecs_rgba_t emissive_color;
    uint32_t layer_albedo;
    uint32_t layer_emissive;
    uint32_t layer_mr;
    uint32_t layer_normal;
    /* Transmission (KHR_materials_transmission + volume + ior) */
    float transmission_factor;
    float ior;
    float thickness_factor;
    float attenuation_distance;
    uint32_t attenuation_color;  /* packed RGBA8 */
    /* UV transform (KHR_texture_transform) */
    float uv_scale_x;
    float uv_scale_y;
    float uv_offset_x;
    float uv_offset_y;
    uint32_t tint;
} FlecsGpuMaterial;

#endif
