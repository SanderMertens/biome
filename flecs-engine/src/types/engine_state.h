#ifndef FLECS_ENGINE_TYPES_ENGINE_STATE_H
#define FLECS_ENGINE_TYPES_ENGINE_STATE_H

#include "flecs_engine.h"
#include "../vendor.h"
#include "gpu_types.h"

typedef struct {
    WGPUShaderModule shader_module;
    WGPUBindGroupLayout pass_bind_layout;
    WGPUSampler sampler;
} flecsEngine_shadow_t;

typedef struct {
    WGPUShaderModule shader_module;
    WGPUComputePipeline pipeline;
    WGPUBindGroupLayout view_bind_layout;
    WGPUBindGroupLayout batch_bind_layout;
} flecsEngine_gpuCull_t;

typedef struct {
    WGPUShaderModule mip0_shader_module;
    WGPUShaderModule reduce_shader_module;
    WGPUComputePipeline mip0_pipeline;
    WGPUComputePipeline reduce_pipeline;
    WGPUBindGroupLayout mip0_bind_layout;
    WGPUBindGroupLayout reduce_bind_layout;
    WGPUSampler sampler;
} flecsEngine_hiz_t;

typedef struct {
    FlecsGpuLight *cpu_lights;
    int32_t light_count;
    int32_t light_capacity;
    WGPUBuffer light_buffer;

    ecs_query_t *point_light_query;
    ecs_query_t *spot_light_query;
} flecsEngine_lighting_t;

#define FLECS_ENGINE_TEXTURE_BUCKET_COUNT 6

#define FLECS_ENGINE_BUCKET_UNSET   ((int8_t)-1)
#define FLECS_ENGINE_BUCKET_INVALID ((int8_t)-2)

typedef struct {
    WGPUTexture texture_arrays[4];       /* albedo, emissive, roughness, normal */
    WGPUTextureView texture_array_views[4];
    uint32_t layer_counts[4];
    uint32_t mip_count;
    uint32_t width;
    uint32_t height;
    bool is_bc7;
} flecsEngine_texture_bucket_t;

typedef struct {
    WGPUBuffer buffer;
    FlecsGpuMaterial *cpu_materials;
    int8_t *cpu_buckets;
    uint32_t buffer_capacity;
    uint32_t count;
    ecs_query_t *query;
    uint32_t next_id;

    uint32_t dirty_version;
    uint32_t uploaded_version;

    WGPUBindGroupLayout bind_layout;
    WGPUBindGroup bind_group;
    uint32_t bind_version;

    WGPUBuffer id_identity_buffer;
    int32_t id_identity_capacity;
} flecsEngine_materials_t;

typedef struct {
    ecs_query_t *query;

    flecsEngine_texture_bucket_t buckets[FLECS_ENGINE_TEXTURE_BUCKET_COUNT];
    WGPUBindGroup bucket_bind_groups[FLECS_ENGINE_TEXTURE_BUCKET_COUNT];
    WGPUBindGroup fallback_bind_group;
    uint32_t bucket_version;

    WGPUBindGroupLayout pbr_bind_layout;
    WGPUSampler pbr_sampler;
    WGPUSampler pbr_low_sampler;
    uint16_t applied_max_aniso;
    uint8_t  applied_max_bucket;
    int32_t  applied_texture_quality;

    WGPUTexture fallback_white_tex;
    WGPUTextureView fallback_white_array_view;
    WGPUTextureView fallback_white_view;
    WGPUTexture fallback_normal_tex;
    WGPUTextureView fallback_normal_array_view;

    WGPURenderPipeline blit_pipeline;
    WGPUBindGroupLayout blit_bind_layout;
    WGPUSampler blit_sampler;
    WGPUComputePipeline mipgen_pipeline;
    WGPUBindGroupLayout mipgen_bind_layout;
} flecsEngine_textures_t;

typedef struct {
    WGPURenderPipeline passthrough_pipeline;
    WGPUBindGroupLayout passthrough_bind_layout;
    WGPUSampler passthrough_sampler;

    WGPURenderPipeline depth_resolve_pipeline;
    WGPUBindGroupLayout depth_resolve_bind_layout;

    WGPURenderPipeline opaque_snapshot_downsample_pipeline;
    WGPUBindGroupLayout opaque_snapshot_downsample_layout;
    WGPUSampler opaque_snapshot_sampler;
} flecsEngine_pipelines_t;

typedef struct {
    FlecsRgba *color;
    ecs_size_t color_count;
    FlecsPbrMaterial *material;
    ecs_size_t material_count;
    FlecsEmissive *emissive;
    ecs_size_t emissive_count;
} flecsEngine_default_attr_cache_t;

#define FLECS_GPU_TIMING_MAX_PAIRS 16
#define FLECS_GPU_TIMING_RING 3

typedef enum {
    FLECS_GPU_TIMING_IDLE = 0,
    FLECS_GPU_TIMING_PENDING,
    FLECS_GPU_TIMING_READY
} flecsEngine_gpuTimingState_t;

typedef struct {
    char name[40];
} flecsEngine_gpuTimingPair_t;

typedef struct {
    flecsEngine_gpuTimingState_t state;
    int pair_count;
    flecsEngine_gpuTimingPair_t pairs[FLECS_GPU_TIMING_MAX_PAIRS];
    WGPUBuffer readback_buffer;
    uint64_t frame_id;
} flecsEngine_gpuTimingSlot_t;

typedef struct {
    bool capable;
    WGPUQuerySet query_set;
    WGPUBuffer resolve_buffer;
    flecsEngine_gpuTimingSlot_t slots[FLECS_GPU_TIMING_RING];
    int cur_slot;
    int next_slot;
    uint64_t frame_counter;
} flecsEngine_gpuTiming_t;

typedef struct {
    ecs_entity_t surface;
    ecs_entity_t fallback_hdri;
    ecs_query_t *view_query;

    WGPUInstance instance;
    WGPUAdapter adapter;
    WGPUDevice device;
    WGPUQueue queue;

    WGPUTextureFormat target_format;
    WGPUTextureFormat hdr_color_format;
    
    WGPUBindGroupLayout scene_bind_layout;
    uint32_t scene_bind_version;

    WGPUBindGroupLayout no_texture_bind_layout;
    WGPUBindGroup no_texture_bind_group;

    WGPUBindGroupLayout instance_bind_layout;

    flecsEngine_shadow_t shadow;
    flecsEngine_lighting_t lighting;
    flecsEngine_materials_t materials;
    flecsEngine_textures_t textures;
    flecsEngine_pipelines_t pipelines;
    flecsEngine_gpuCull_t gpu_cull;
    flecsEngine_hiz_t hiz;
    flecsEngine_gpuTiming_t gpu_timing;

    flecsEngine_default_attr_cache_t *default_attr_cache;

    /* Cloud-system shadow projection state. The cloud effect (if present)
     * bakes a per-ground-point transmittance texture covering a world-aligned
     * footprint around the camera and registers it here so PBR can sample it
     * by world XZ. When no cloud effect is active, shadow_source_view stays
     * NULL and PBR uses a fallback white texture (no shadow). */
    struct {
        WGPUTextureView shadow_source_view;
        float origin_x;       /* world-X of footprint lower-left corner */
        float origin_z;       /* world-Z of footprint lower-left corner */
        float strength;
        float inv_footprint;  /* 1 / footprint size in world units */
        uint32_t version;     /* incremented when shadow_source_view changes */
    } clouds;
} FlecsEngineImpl;

extern ECS_COMPONENT_DECLARE(FlecsEngineImpl);

#endif
