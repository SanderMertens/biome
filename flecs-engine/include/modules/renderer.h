#ifndef FLECS_ENGINE_RENDERER_H
#define FLECS_ENGINE_RENDERER_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_RENDERER_IMPL
#define ECS_META_IMPL EXTERN
#endif

#define FLECS_ENGINE_SHADOW_CASCADE_COUNT 4
#define FLECS_ENGINE_CLUSTER_X 16
#define FLECS_ENGINE_CLUSTER_Y 9
#define FLECS_ENGINE_CLUSTER_Z 24
#define FLECS_ENGINE_CLUSTER_TOTAL \
    (FLECS_ENGINE_CLUSTER_X * FLECS_ENGINE_CLUSTER_Y * FLECS_ENGINE_CLUSTER_Z)

typedef struct {
    uint32_t grid_size[4];   /* x, y, z, total */
    float screen_info[4];    /* width, height, near, log(far/near) */
} FlecsClusterInfo;

typedef struct {
    uint32_t light_offset;
    uint32_t light_count;
    uint32_t _pad0;
    uint32_t _pad1;
} FlecsClusterEntry;

typedef struct {
    const char *source;
    const char *vertex_entry;
    const char *fragment_entry;
} FlecsShader;

extern ECS_COMPONENT_DECLARE(FlecsShader);

ECS_STRUCT(FlecsRenderBatchSet, {
    ecs_vec_t batches;
});

ECS_STRUCT(FlecsHdri, {
    const char *file;
    uint32_t filter_sample_count;
    uint32_t lut_sample_count;
});

extern ECS_COMPONENT_DECLARE(FlecsHdri);

ECS_STRUCT(flecs_engine_shadow_params_t, {
    ecs_bool_t enabled;
    int32_t map_size;
    float bias;
    float max_range;
});

/* Fullscreen post-process effect entry. Input uses chain indexing:
 * 0 = batches framebuffer, k > 0 = output of effect[k - 1]. */
ECS_STRUCT(flecs_render_view_effect_t, {
    ecs_bool_t enabled;
    int32_t input;
    ecs_entity_t effect;
});

ECS_STRUCT(FlecsRenderView, {
    ecs_entity_t camera;
    ecs_entity_t atmosphere;   /* entity with FlecsAtmosphere component; when
                                * set, supplies sky + aerial perspective + IBL
                                * and takes precedence over `hdri` */
    ecs_entity_t hdri;
    ecs_f32_t ambient_intensity;
    ecs_f32_t screen_px_threshold;  /* min projected bounding-sphere diameter
                                      * in pixels; instances smaller than this
                                      * are culled. 0 disables. */
    flecs_engine_shadow_params_t shadow;
    ecs_vec_t effects;
});

ecs_entity_t flecsEngine_createHdri(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    const char *file,
    uint32_t filterSampleCount,
    uint32_t lutSampleCount);

#endif
