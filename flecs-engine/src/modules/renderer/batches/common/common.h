#ifndef FLECS_ENGINE_RENDER_BATCH_COMMON_H
#define FLECS_ENGINE_RENDER_BATCH_COMMON_H

#include "../../renderer.h"
#include "../../shaders/shaders.h"
#include "../../../geometry3/geometry3.h"
#include "../../../../tracy_hooks.h"
#include "../../frustum_cull.h"
#include "../../gpu_cull.h"
#include "../batches.h"
#include "flecs_engine.h"

/* -- Storing data from ECS into CPU & GPU buffers -- */

#define FLECS_ENGINE_BATCH_SLOT_FREE 0xFFFFFFFFu

typedef enum {
    FLECS_BATCH_DEFAULT = 0,
    FLECS_BATCH_OWNS_MATERIAL = 1 << 0,
    FLECS_BATCH_OWNS_TRANSMISSION = 1 << 1,
    /* Skip GPU compute cull. prepareArgs pre-fills instance_count with the
     * full source count (no culling, no atomic increments). Used by batches
     * that render every instance unconditionally (e.g. bevel boxes) or that
     * manage their own visible_slots / draw loop (e.g. transparent sort). */
    FLECS_BATCH_NO_GPU_CULL = 1 << 2,
    FLECS_BATCH_TRACK_STATIC = 1 << 3,
    /* Bind the mesh's per-vertex color buffer as a third vertex buffer
     * (slot 2, location after the instance slot). The batch pipeline is
     * built with the extra buffer layout and groups whose mesh has no
     * color buffer are skipped. */
    FLECS_BATCH_VERTEX_COLORS = 1 << 4,
    FLECS_BATCH_TINT = 1 << 5,
} flecsEngine_batch_flags_t;

typedef void (*flecsEngine_primitive_scale_t)(
    const void *value,
    float *out);

typedef void (*flecsEngine_primitive_scale_aabb_t)(
    FlecsAABB *aabb,
    const void *scale_data,
    int32_t count);

/* Set of CPU/GPU buffers that store extracted data from the ECS for a batch. */
typedef struct {
    FlecsGpuTransform *cpu_transforms;
    FlecsMaterialId *cpu_material_ids;
    FlecsGpuMaterial *cpu_materials;
    flecsEngine_gpuAabb_t *cpu_aabb;
    uint32_t *cpu_slot_to_group;

    flecsEngine_gpuCullGroupInfo_t *cpu_group_info;
    flecsEngine_gpuDrawArgs_t *cpu_indirect_args;

    WGPUBuffer gpu_transforms;
    WGPUBuffer gpu_material_ids;
    WGPUBuffer gpu_materials;
    WGPUBindGroup gpu_material_bind_group;
    WGPUBindGroup gpu_instance_bind_group;

    WGPUBuffer gpu_aabb;
    WGPUBuffer gpu_slot_to_group;
    WGPUBuffer gpu_group_info;
    WGPUBuffer gpu_indirect_args;
    WGPUBuffer gpu_visible_slots;        /* 5 * capacity slots: main + 4 cascades */
    WGPUBuffer gpu_batch_info;           /* uniform: count, capacity, group_count */
    WGPUBindGroup gpu_cull_bind_group;   /* cached group 1 for compute cull */

    int32_t count;
    int32_t capacity;

    int32_t group_count;
    int32_t group_capacity;

    bool needs_full_upload;
} flecsEngine_batch_buffers_t;

/* Batch: render data for all entities matching the batch query. */
typedef struct flecsEngine_batch_t {
    flecsEngine_batch_buffers_t buffers;
    flecsEngine_batch_buffers_t static_buffers;
    ecs_vec_t free_slots;
    ecs_flags32_t flags;
    bool uses_textures;

    /* Stable static group indices. The value stored in a slot's
     * slot_to_group and the index into group_info/indirect_args must remain
     * valid for the lifetime of the group, because static slots are written
     * once and not revisited. Query group iteration order is NOT stable
     * (adding a group reshuffles the rest), so instead each group is handed a
     * stable index on first use, drawn from a free list when a group is
     * deleted. static_group_high is the high-water mark (the stride of the
     * group_info/indirect_args arrays). */
    ecs_vec_t static_group_free;
    int32_t static_group_high;
} flecsEngine_batch_t;

/* View on batch buffers. The source slice [offset, offset+count) in the batch
 * buffers belongs to this group. GPU cull writes visible slot indices into a
 * per-view slice of gpu_visible_slots, packed via atomic from src_offset. */
typedef struct {
    int32_t count;
    int32_t offset;
    int32_t group_idx;
} flecsEngine_batch_view_t;

/* Batch group: each batch can have multiple batch groups, where a group is
 * associated with a single mesh. */
typedef struct flecsEngine_batch_group_t {
    flecsEngine_batch_t *batch;
    flecsEngine_batch_view_t view;
    flecsEngine_batch_view_t static_view;

    int32_t slot_count;
    ecs_vec_t changed;
    ecs_vec_t changed_slots;
    ecs_map_t changed_set;

    uint64_t group_id;
    FlecsMesh3Impl mesh;
    ecs_entity_t scale_component;
    flecsEngine_primitive_scale_t scale_callback;
    flecsEngine_primitive_scale_aabb_t scale_aabb;

    int32_t static_stable_idx;

    int8_t texture_bucket;
    uint32_t resolved_bucket_version;
    uint32_t first_material_id;     /* sticky: last seen material_id used to
                                       resolve texture_bucket; lets us re-resolve
                                       the bucket on a bucket_version bump even
                                       when the batch group is empty. */
    bool has_first_material_id;
} flecsEngine_batch_group_t;

/* Primitive batch group: like a batch group for buitin primitive meshes. This
 * type has additional functionality for applying the scale from a primitive
 * component. For example, if an entity has "FlecsBox: {10, 10, 10}", the 
 * scale_callback() function will apply a {10, 10, 10} scale to the builtin
 * Box mesh. This scale is applied on top of an optional FlecsScale component. */
typedef struct {
    flecsEngine_batch_group_t group;
    ecs_size_t component_size;
    flecsEngine_primitive_scale_t scale_callback;
    flecsEngine_primitive_scale_aabb_t scale_aabb;
} flecsEngine_primitive_batch_group_t;

void flecsEngine_batch_init(
    flecsEngine_batch_t *buf,
    flecsEngine_batch_flags_t flags);

void flecsEngine_batch_fini(
    flecsEngine_batch_t *buf);

void flecsEngine_batch_ensureCapacity(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *buf,
    int32_t count);

void flecsEngine_batch_ensureStaticCapacity(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *buf,
    int32_t count);

void flecsEngine_batchBuffers_releaseStaticGpu(
    flecsEngine_batch_buffers_t *bb);

void flecsEngine_batchBuffers_freeStaticCpu(
    flecsEngine_batch_buffers_t *bb);

void flecsEngine_batch_ensureGroupCapacity(
    flecsEngine_batch_t *buf,
    int32_t group_count);

void flecsEngine_batch_group_extract(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    flecsEngine_batch_group_t *ctx,
    flecsEngine_primitive_scale_t scale_callback,
    flecsEngine_primitive_scale_aabb_t scale_aabb,
    ecs_size_t component_size);

typedef struct {
    int32_t cap;
    int32_t group_cap;
    int32_t prev_gc;
    int32_t total;
    int32_t group_idx;
} flecsEngine_batch_dynamicExtract_t;

typedef struct {
    int32_t group_cap;
    int32_t prev_gc;
    int32_t static_offset;
} flecsEngine_batch_staticExtract_t;

/* Allocate/release a stable static group index (see static_group_free). */
int32_t flecsEngine_batch_allocStaticGroupIdx(
    flecsEngine_batch_t *buf);

void flecsEngine_batch_freeStaticGroupIdx(
    flecsEngine_batch_t *buf,
    int32_t idx);

void flecsEngine_batch_dynamicExtract_begin(
    flecsEngine_batch_dynamicExtract_t *s,
    flecsEngine_batch_t *shared);

void flecsEngine_batch_dynamicExtract_group(
    flecsEngine_batch_dynamicExtract_t *s,
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderBatch *batch,
    flecsEngine_batch_group_t *ctx,
    flecsEngine_primitive_scale_t scale,
    flecsEngine_primitive_scale_aabb_t scale_aabb,
    ecs_size_t scale_size);

bool flecsEngine_batch_dynamicExtract_commit(
    flecsEngine_batch_dynamicExtract_t *s,
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *shared);

void flecsEngine_batch_staticExtract_begin(
    flecsEngine_batch_staticExtract_t *s,
    flecsEngine_batch_t *shared);

void flecsEngine_batch_staticExtract_group(
    flecsEngine_batch_staticExtract_t *s,
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    flecsEngine_batch_group_t *ctx);

bool flecsEngine_batch_staticExtract_commit(
    flecsEngine_batch_staticExtract_t *s,
    flecsEngine_batch_t *shared);

/* Fill per-group GPU data: slot_to_group entries, group_info, and zero the
 * per-view atomic counters in the indirect args. No AABB test runs on CPU. */
void flecsEngine_batch_group_prepareArgs(
    flecsEngine_batch_group_t *ctx);

void flecsEngine_batch_group_prepareStaticArgs(
    flecsEngine_batch_group_t *ctx);

void flecsEngine_batch_group_applyChanges(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    flecsEngine_batch_group_t *ctx);

void flecsEngine_batch_uploadStatic(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *buf,
    flecsEngine_batch_group_t **groups,
    int32_t group_count);

void flecsEngine_batch_ensureStaticGroupCapacity(
    flecsEngine_batch_t *buf,
    int32_t group_count);

void flecsEngine_batch_group_draw(
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_group_t *ctx);

void flecsEngine_batch_group_drawShadow(
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_group_t *ctx);

/* Ensure compute-cull bind group is built for this batch. Returns NULL if
 * inputs are missing. */
WGPUBindGroup flecsEngine_batch_ensureCullBindGroup(
    FlecsEngineImpl *engine,
    flecsEngine_batch_t *buf);

WGPUBindGroup flecsEngine_batch_ensureStaticCullBindGroup(
    FlecsEngineImpl *engine,
    flecsEngine_batch_t *buf);

/* Accessor for the single shared buffer owned by a batch. Used by gpu_cull
 * dispatch to iterate batches without knowing their concrete ctx type. */
flecsEngine_batch_t* flecsEngine_batch_getCullBuf(
    const FlecsRenderBatch *batch);

void flecsEngine_batch_bindMaterialGroup(
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *buf);

void flecsEngine_batch_bindInstanceGroup(
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *buf);

void flecsEngine_batch_bindInstanceGroupShadow(
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *buf);

void flecsEngine_batch_bindMaterialGroupStatic(
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *buf);

void flecsEngine_batch_bindInstanceGroupStatic(
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *buf);

void flecsEngine_batch_bindInstanceGroupShadowStatic(
    FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_t *buf);

void flecsEngine_batch_group_drawStatic(
    const FlecsEngineImpl *engine,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_group_t *ctx);

void flecsEngine_batch_group_drawShadowStatic(
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const flecsEngine_batch_group_t *ctx);

void flecsEngine_batch_group_init(
    flecsEngine_batch_group_t* batch,
    const FlecsMesh3Impl *mesh,
    uint64_t group_id);

flecsEngine_batch_group_t* flecsEngine_batch_group_create(
    const FlecsMesh3Impl *mesh,
    uint64_t group_id);

void flecsEngine_batch_group_fini(
    flecsEngine_batch_group_t *ptr);

void flecsEngine_batch_group_delete(
    void *ptr);

void flecsEngine_batch_upload(
    const FlecsEngineImpl *engine,
    const flecsEngine_batch_t *buf);

void flecsEngine_batch_transformInstance(
    FlecsGpuTransform *out,
    const FlecsWorldTransform3 *wt,
    float scale_x,
    float scale_y,
    float scale_z);

/* Compute a world-space AABB from a local AABB + world transform, written in
 * the padded GPU layout (Arvo's method). */
void flecsEngine_batch_worldAabb(
    const FlecsWorldTransform3 *wt,
    const FlecsAABB local,
    flecsEngine_gpuAabb_t *out);

/* Bind the texture bucket for a group at slot 1. Returns false if the group
 * cannot be drawn (unresolvable bucket). */
bool flecsEngine_batch_bindGroupTextures(
    const FlecsEngineImpl *engine,
    const flecsEngine_batch_t *buf,
    const flecsEngine_batch_group_t *ctx,
    WGPURenderPassEncoder pass,
    int8_t *last_bucket);

/* Sort the instances of the given groups back-to-front and render them with
 * one draw per instance. Used by transparent batches. */
void flecsEngine_batch_renderTransparentSorted(
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    WGPURenderPassEncoder pass,
    flecsEngine_batch_t *buf,
    WGPURenderPipeline pipeline,
    flecsEngine_batch_group_t **groups,
    int32_t group_count);

/* -- Meshes -- */

flecsEngine_batch_t* flecsEngine_mesh_createCtx(
    flecsEngine_batch_flags_t flags);

void flecsEngine_mesh_deleteCtx(void *ptr);

uint64_t flecsEngine_mesh_groupByMesh(
    ecs_world_t *world,
    ecs_table_t *table,
    ecs_id_t id,
    void *ctx);

void* flecsEngine_mesh_onGroupCreate(
    ecs_world_t *world,
    uint64_t group_id,
    void *ptr);

void flecsEngine_mesh_onGroupDelete(
    ecs_world_t *world,
    uint64_t group_id,
    void *group_ptr,
    void *ptr);

void flecsEngine_mesh_extract(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch);

void flecsEngine_mesh_upload(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatch *batch);

void* flecsEngine_mesh_getCullBuf(
    const FlecsRenderBatch *batch);

/* Upload identity visible_slots to gpu_visible_slots main-view slice. Used
 * by NO_GPU_CULL batches whose draw path relies on visible_slots as the
 * instance index source. */
void flecsEngine_batch_writeIdentityVisible(
    const FlecsEngineImpl *engine,
    const flecsEngine_batch_t *buf);

void flecsEngine_mesh_render(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch);

void flecsEngine_mesh_renderShadow(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const FlecsRenderBatch *batch);

#endif
