#include "common.h"
#include <stdlib.h>

/* --- Shared buffer lifecycle --- */

void flecsEngine_batch_init(
    flecsEngine_batch_t *buf,
    flecsEngine_batch_flags_t flags)
{
    ecs_os_memset_t(buf, 0, flecsEngine_batch_t);
    buf->flags = flags;
    ecs_vec_init_t(NULL, &buf->free_slots, int32_t, 0);
    ecs_vec_init_t(NULL, &buf->static_group_free, int32_t, 0);
}

int32_t flecsEngine_batch_allocStaticGroupIdx(
    flecsEngine_batch_t *buf)
{
    int32_t n = ecs_vec_count(&buf->static_group_free);
    if (n > 0) {
        int32_t *f = ecs_vec_first_t(&buf->static_group_free, int32_t);
        int32_t idx = f[n - 1];
        ecs_vec_remove_last(&buf->static_group_free);
        return idx;
    }
    return buf->static_group_high ++;
}

void flecsEngine_batch_freeStaticGroupIdx(
    flecsEngine_batch_t *buf,
    int32_t idx)
{
    if (idx < 0) {
        return;
    }
    *ecs_vec_append_t(NULL, &buf->static_group_free, int32_t) = idx;

    /* Retire the freed index's group range so a stale reservation can't be
     * uploaded. No live slot references it (all the group's entities are
     * gone), so this is belt-and-suspenders. */
    flecsEngine_batch_buffers_t *bb = &buf->static_buffers;
    if (bb->cpu_group_info && idx < bb->group_capacity) {
        bb->cpu_group_info[idx].src_offset = 0;
        bb->cpu_group_info[idx].src_count = 0;
    }
}

static void flecsEngine_batchBuffers_releaseCullGroup(
    flecsEngine_batch_buffers_t *bb)
{
    FLECS_WGPU_RELEASE(bb->gpu_cull_bind_group, wgpuBindGroupRelease);
}

static void flecsEngine_batchBuffers_releaseGpu(
    flecsEngine_batch_buffers_t *bb)
{
    flecsEngine_batchBuffers_releaseCullGroup(bb);
    FLECS_WGPU_RELEASE(bb->gpu_transforms, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(bb->gpu_material_ids, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(bb->gpu_material_bind_group, wgpuBindGroupRelease);
    FLECS_WGPU_RELEASE(bb->gpu_instance_bind_group, wgpuBindGroupRelease);
    FLECS_WGPU_RELEASE(bb->gpu_materials, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(bb->gpu_aabb, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(bb->gpu_slot_to_group, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(bb->gpu_visible_slots, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(bb->gpu_batch_info, wgpuBufferRelease);
}

static void flecsEngine_batchBuffers_releaseGroupGpu(
    flecsEngine_batch_buffers_t *bb)
{
    flecsEngine_batchBuffers_releaseCullGroup(bb);
    FLECS_WGPU_RELEASE(bb->gpu_group_info, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(bb->gpu_indirect_args, wgpuBufferRelease);
}

static void flecsEngine_batchBuffers_freeCpu(
    flecsEngine_batch_buffers_t *bb)
{
    ecs_os_free(bb->cpu_transforms);
    bb->cpu_transforms = NULL;
    ecs_os_free(bb->cpu_material_ids);
    bb->cpu_material_ids = NULL;
    ecs_os_free(bb->cpu_materials);
    bb->cpu_materials = NULL;
    ecs_os_free(bb->cpu_aabb);
    bb->cpu_aabb = NULL;
    ecs_os_free(bb->cpu_slot_to_group);
    bb->cpu_slot_to_group = NULL;
}

static void flecsEngine_batchBuffers_freeGroupCpu(
    flecsEngine_batch_buffers_t *bb)
{
    ecs_os_free(bb->cpu_group_info);
    bb->cpu_group_info = NULL;
    ecs_os_free(bb->cpu_indirect_args);
    bb->cpu_indirect_args = NULL;
}

static void flecsEngine_batch_releaseGpu(
    flecsEngine_batch_t *buf)
{
    flecsEngine_batchBuffers_releaseGpu(&buf->buffers);
}

static void flecsEngine_batch_releaseGroupGpu(
    flecsEngine_batch_t *buf)
{
    flecsEngine_batchBuffers_releaseGroupGpu(&buf->buffers);
}

static void flecsEngine_batch_freeCpu(
    flecsEngine_batch_t *buf)
{
    flecsEngine_batchBuffers_freeCpu(&buf->buffers);
}

static void flecsEngine_batch_freeGroupCpu(
    flecsEngine_batch_t *buf)
{
    flecsEngine_batchBuffers_freeGroupCpu(&buf->buffers);
}

void flecsEngine_batchBuffers_releaseStaticGpu(
    flecsEngine_batch_buffers_t *bb)
{
    flecsEngine_batchBuffers_releaseGpu(bb);
    flecsEngine_batchBuffers_releaseGroupGpu(bb);
}

void flecsEngine_batchBuffers_freeStaticCpu(
    flecsEngine_batch_buffers_t *bb)
{
    flecsEngine_batchBuffers_freeCpu(bb);
    flecsEngine_batchBuffers_freeGroupCpu(bb);
}

void flecsEngine_batch_fini(
    flecsEngine_batch_t *buf)
{
    flecsEngine_batch_releaseGpu(buf);
    flecsEngine_batch_releaseGroupGpu(buf);
    flecsEngine_batch_freeCpu(buf);
    flecsEngine_batch_freeGroupCpu(buf);
    buf->buffers.count = 0;
    buf->buffers.capacity = 0;
    buf->buffers.group_count = 0;
    buf->buffers.group_capacity = 0;

    flecsEngine_batchBuffers_releaseStaticGpu(&buf->static_buffers);
    flecsEngine_batchBuffers_freeStaticCpu(&buf->static_buffers);
    buf->static_buffers.count = 0;
    buf->static_buffers.capacity = 0;

    ecs_vec_fini_t(NULL, &buf->free_slots, int32_t);
    ecs_vec_fini_t(NULL, &buf->static_group_free, int32_t);
}

static WGPUBuffer flecsEngine_makeStorageBuffer(
    WGPUDevice device,
    uint64_t size)
{
    return wgpuDeviceCreateBuffer(device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
            .size = size
        });
}

static void* flecsEngine_reallocPreserve(
    void *old_ptr, int32_t old_count, int32_t new_capacity, ecs_size_t elem_size)
{
    void *new_ptr = ecs_os_malloc(new_capacity * elem_size);
    if (old_ptr && old_count > 0) {
        ecs_os_memcpy(new_ptr, old_ptr, old_count * elem_size);
    }
    ecs_os_free(old_ptr);
    return new_ptr;
}

static void flecsEngine_batchBuffers_ensureCapacity(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_buffers_t *bb,
    ecs_flags32_t flags,
    int32_t count)
{
    if (count <= bb->capacity) {
        return;
    }

    int32_t new_capacity = count;
    if (new_capacity < 64) {
        new_capacity = 64;
    }
    if (new_capacity < bb->capacity * 2) {
        new_capacity = bb->capacity * 2;
    }

    int32_t old_count = bb->count;

    flecsEngine_batchBuffers_releaseGpu(bb);

    WGPUDevice device = engine->device;

    bb->gpu_transforms = flecsEngine_makeStorageBuffer(
        device, (uint64_t)new_capacity * sizeof(FlecsGpuTransform));
    bb->cpu_transforms = flecsEngine_reallocPreserve(
        bb->cpu_transforms, old_count, new_capacity,
        ECS_SIZEOF(FlecsGpuTransform));

    bb->cpu_aabb = flecsEngine_reallocPreserve(
        bb->cpu_aabb, old_count, new_capacity,
        ECS_SIZEOF(flecsEngine_gpuAabb_t));
    bb->gpu_aabb = flecsEngine_makeStorageBuffer(
        device, (uint64_t)new_capacity * sizeof(flecsEngine_gpuAabb_t));

    bb->cpu_slot_to_group = flecsEngine_reallocPreserve(
        bb->cpu_slot_to_group, old_count, new_capacity,
        ECS_SIZEOF(uint32_t));
    bb->gpu_slot_to_group = flecsEngine_makeStorageBuffer(
        device, (uint64_t)new_capacity * sizeof(uint32_t));

    bb->gpu_material_ids = flecsEngine_makeStorageBuffer(
        device, (uint64_t)new_capacity * sizeof(FlecsMaterialId));
    bb->cpu_material_ids = flecsEngine_reallocPreserve(
        bb->cpu_material_ids, old_count, new_capacity,
        ECS_SIZEOF(FlecsMaterialId));

    bb->gpu_visible_slots = wgpuDeviceCreateBuffer(device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Storage
                   | WGPUBufferUsage_Vertex
                   | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * 5ull * sizeof(uint32_t)
        });

    bb->gpu_batch_info = wgpuDeviceCreateBuffer(device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
            .size = 16
        });

    if ((flags & FLECS_BATCH_OWNS_MATERIAL)) {
        uint64_t storage_size =
            (uint64_t)new_capacity * sizeof(FlecsGpuMaterial);
        bb->gpu_materials = wgpuDeviceCreateBuffer(device,
            &(WGPUBufferDescriptor){
                .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
                .size = storage_size
            });
        bb->cpu_materials = flecsEngine_reallocPreserve(
            bb->cpu_materials, old_count, new_capacity,
            ECS_SIZEOF(FlecsGpuMaterial));

        flecsEngine_materialBind_ensureLayout((FlecsEngineImpl*)engine);
        bb->gpu_material_bind_group = flecsEngine_materialBind_createGroup(
            engine, bb->gpu_materials, storage_size);

        for (int32_t i = 0; i < new_capacity; i ++) {
            bb->cpu_material_ids[i].value = (uint32_t)i;
        }
    }

    flecsEngine_instanceBind_ensureLayout((FlecsEngineImpl*)engine);
    bb->gpu_instance_bind_group = flecsEngine_instanceBind_createGroup(
        engine,
        bb->gpu_transforms,
        (uint64_t)new_capacity * sizeof(FlecsGpuTransform),
        bb->gpu_material_ids,
        (uint64_t)new_capacity * sizeof(FlecsMaterialId));

    bb->capacity = new_capacity;
    bb->needs_full_upload = (old_count > 0);
}

void flecsEngine_batch_ensureCapacity(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *buf,
    int32_t count)
{
    flecsEngine_batchBuffers_ensureCapacity(engine, &buf->buffers,
        buf->flags, count);
}

void flecsEngine_batch_ensureStaticCapacity(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *buf,
    int32_t count)
{
    flecsEngine_batchBuffers_ensureCapacity(engine, &buf->static_buffers,
        buf->flags, count);
}

static void flecsEngine_batchBuffers_ensureGroupCapacity(
    flecsEngine_batch_buffers_t *bb,
    int32_t group_count)
{
    if (group_count <= bb->group_capacity) {
        return;
    }

    int32_t new_capacity = group_count;
    if (new_capacity < 4) {
        new_capacity = 4;
    }

    flecsEngine_batchBuffers_releaseGroupGpu(bb);
    flecsEngine_batchBuffers_freeGroupCpu(bb);

    bb->cpu_group_info =
        ecs_os_calloc_n(flecsEngine_gpuCullGroupInfo_t, new_capacity);
    bb->cpu_indirect_args =
        ecs_os_calloc_n(flecsEngine_gpuDrawArgs_t,
            new_capacity * (1 + FLECS_ENGINE_SHADOW_CASCADE_COUNT));
    bb->group_capacity = new_capacity;
}

void flecsEngine_batch_ensureGroupCapacity(
    flecsEngine_batch_t *buf,
    int32_t group_count)
{
    flecsEngine_batchBuffers_ensureGroupCapacity(&buf->buffers, group_count);
}

void flecsEngine_batch_ensureStaticGroupCapacity(
    flecsEngine_batch_t *buf,
    int32_t group_count)
{
    flecsEngine_batchBuffers_ensureGroupCapacity(
        &buf->static_buffers, group_count);
}

static void flecsEngine_batchBuffers_ensureGroupGpu(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_buffers_t *bb)
{
    if (bb->gpu_group_info && bb->gpu_indirect_args) {
        return;
    }

    FLECS_WGPU_RELEASE(bb->gpu_group_info, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(bb->gpu_indirect_args, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(bb->gpu_cull_bind_group, wgpuBindGroupRelease);

    bb->gpu_group_info = flecsEngine_makeStorageBuffer(
        engine->device,
        (uint64_t)bb->group_capacity *
            sizeof(flecsEngine_gpuCullGroupInfo_t));

    bb->gpu_indirect_args = wgpuDeviceCreateBuffer(
        engine->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Storage
                   | WGPUBufferUsage_Indirect
                   | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)bb->group_capacity
                * (1 + FLECS_ENGINE_SHADOW_CASCADE_COUNT)
                * sizeof(flecsEngine_gpuDrawArgs_t)
        });
}

static void flecsEngine_batch_ensureGroupGpu(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *buf)
{
    flecsEngine_batchBuffers_ensureGroupGpu(engine, &buf->buffers);
}

typedef struct {
    uint32_t count;
    uint32_t capacity;
    uint32_t group_count;
    uint32_t _pad;
} flecsEngine_gpuCullBatchInfoUpload_t;

void flecsEngine_batch_upload(
    const FlecsEngineImpl *engine,
    const flecsEngine_batch_t *buf)
{
    FLECS_TRACY_ZONE_BEGIN("BatchUpload");
    int32_t count = buf->buffers.count;
    int32_t group_count = buf->buffers.group_count;
    if (!count || !group_count) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    flecsEngine_batch_ensureGroupGpu(engine, (flecsEngine_batch_t*)buf);

    WGPUQueue queue = engine->queue;

    wgpuQueueWriteBuffer(queue,
        buf->buffers.gpu_transforms, 0,
        buf->buffers.cpu_transforms,
        (uint64_t)count * sizeof(FlecsGpuTransform));

    wgpuQueueWriteBuffer(queue,
        buf->buffers.gpu_material_ids, 0,
        buf->buffers.cpu_material_ids,
        (uint64_t)count * sizeof(FlecsMaterialId));

    wgpuQueueWriteBuffer(queue,
        buf->buffers.gpu_aabb, 0,
        buf->buffers.cpu_aabb,
        (uint64_t)count * sizeof(flecsEngine_gpuAabb_t));

    wgpuQueueWriteBuffer(queue,
        buf->buffers.gpu_slot_to_group, 0,
        buf->buffers.cpu_slot_to_group,
        (uint64_t)count * sizeof(uint32_t));

    wgpuQueueWriteBuffer(queue,
        buf->buffers.gpu_group_info, 0,
        buf->buffers.cpu_group_info,
        (uint64_t)group_count * sizeof(flecsEngine_gpuCullGroupInfo_t));

    wgpuQueueWriteBuffer(queue,
        buf->buffers.gpu_indirect_args, 0,
        buf->buffers.cpu_indirect_args,
        (uint64_t)group_count *
            (1 + FLECS_ENGINE_SHADOW_CASCADE_COUNT) *
            sizeof(flecsEngine_gpuDrawArgs_t));

    flecsEngine_gpuCullBatchInfoUpload_t info = {
        .count = (uint32_t)count,
        .capacity = (uint32_t)buf->buffers.capacity,
        .group_count = (uint32_t)group_count,
        ._pad = 0
    };
    wgpuQueueWriteBuffer(queue,
        buf->buffers.gpu_batch_info, 0, &info, sizeof(info));

    if ((buf->flags & FLECS_BATCH_OWNS_MATERIAL)) {
        wgpuQueueWriteBuffer(queue,
            buf->buffers.gpu_materials, 0,
            buf->buffers.cpu_materials,
            (uint64_t)count * sizeof(FlecsGpuMaterial));
    }

    if ((buf->flags & FLECS_BATCH_NO_GPU_CULL)) {
        flecsEngine_batch_writeIdentityVisible(engine, buf);
    }

    FLECS_TRACY_ZONE_END;
}

static WGPUBindGroup flecsEngine_batchBuffers_ensureCullBindGroup(
    FlecsEngineImpl *engine,
    flecsEngine_batch_buffers_t *bb)
{
    if (bb->gpu_cull_bind_group) {
        return bb->gpu_cull_bind_group;
    }

    if (!bb->gpu_aabb ||
        !bb->gpu_slot_to_group ||
        !bb->gpu_group_info ||
        !bb->gpu_visible_slots ||
        !bb->gpu_indirect_args ||
        !bb->gpu_batch_info)
    {
        return NULL;
    }

    WGPUBindGroupEntry entries[6] = {
        { .binding = 0, .buffer = bb->gpu_aabb,
          .size = (uint64_t)bb->capacity * sizeof(flecsEngine_gpuAabb_t) },
        { .binding = 1, .buffer = bb->gpu_slot_to_group,
          .size = (uint64_t)bb->capacity * sizeof(uint32_t) },
        { .binding = 2, .buffer = bb->gpu_group_info,
          .size = (uint64_t)bb->group_capacity
              * sizeof(flecsEngine_gpuCullGroupInfo_t) },
        { .binding = 3, .buffer = bb->gpu_visible_slots,
          .size = (uint64_t)bb->capacity * 5ull * sizeof(uint32_t) },
        { .binding = 4, .buffer = bb->gpu_indirect_args,
          .size = (uint64_t)bb->group_capacity
              * (1 + FLECS_ENGINE_SHADOW_CASCADE_COUNT)
              * sizeof(flecsEngine_gpuDrawArgs_t) },
        { .binding = 5, .buffer = bb->gpu_batch_info, .size = 16 }
    };

    bb->gpu_cull_bind_group = wgpuDeviceCreateBindGroup(
        engine->device, &(WGPUBindGroupDescriptor){
            .layout = engine->gpu_cull.batch_bind_layout,
            .entryCount = 6,
            .entries = entries
        });

    return bb->gpu_cull_bind_group;
}

WGPUBindGroup flecsEngine_batch_ensureCullBindGroup(
    FlecsEngineImpl *engine,
    flecsEngine_batch_t *buf)
{
    return flecsEngine_batchBuffers_ensureCullBindGroup(engine, &buf->buffers);
}

WGPUBindGroup flecsEngine_batch_ensureStaticCullBindGroup(
    FlecsEngineImpl *engine,
    flecsEngine_batch_t *buf)
{
    return flecsEngine_batchBuffers_ensureCullBindGroup(
        engine, &buf->static_buffers);
}

/* --- Per-group batch lifecycle --- */

void flecsEngine_batch_group_init(
    flecsEngine_batch_group_t* result,
    const FlecsMesh3Impl *mesh,
    uint64_t group_id)
{
    ecs_os_memset_t(result, 0, flecsEngine_batch_group_t);
    if (mesh) {
        result->mesh = *mesh;
    }
    result->group_id = group_id;
    result->static_stable_idx = -1;
    result->texture_bucket = FLECS_ENGINE_BUCKET_UNSET;
    ecs_vec_init_t(NULL, &result->changed, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &result->changed_slots, int32_t, 0);
    ecs_map_init(&result->changed_set, NULL);
}

flecsEngine_batch_group_t* flecsEngine_batch_group_create(
    const FlecsMesh3Impl *mesh,
    uint64_t group_id)
{
    flecsEngine_batch_group_t *result = ecs_os_calloc_t(flecsEngine_batch_group_t);
    flecsEngine_batch_group_init(result, mesh, group_id);
    return result;
}

void flecsEngine_batch_group_fini(
    flecsEngine_batch_group_t *ptr)
{
    ptr->view.count = 0;
    ptr->view.offset = 0;
    ptr->batch = NULL;
    ecs_vec_fini_t(NULL, &ptr->changed, ecs_entity_t);
    ecs_vec_fini_t(NULL, &ptr->changed_slots, int32_t);
    ecs_map_fini(&ptr->changed_set);
}

void flecsEngine_batch_group_delete(
    void *ptr)
{
    flecsEngine_batch_group_fini(ptr);
    ecs_os_free(ptr);
}

/* --- Fill indirect draw args (CPU) --- */

static void flecsEngine_batch_group_prepareArgsForSet(
    flecsEngine_batch_buffers_t *bb,
    const flecsEngine_batch_view_t *view,
    uint32_t index_count,
    bool identity)
{
    int32_t g = view->group_idx;
    if (g < 0) {
        return;
    }
    int32_t gc = bb->group_count;
    uint32_t initial_count = identity ? (uint32_t)view->count : 0u;

    bb->cpu_group_info[g] = (flecsEngine_gpuCullGroupInfo_t){
        .src_offset = (uint32_t)view->offset,
        .src_count = (uint32_t)view->count,
        ._pad0 = 0,
        ._pad1 = 0
    };

    for (int v = 0; v < 1 + FLECS_ENGINE_SHADOW_CASCADE_COUNT; v ++) {
        flecsEngine_gpuDrawArgs_t *a =
            &bb->cpu_indirect_args[v * gc + g];
        a->index_count = index_count;
        a->instance_count = (v == 0) ? initial_count : 0u;
        a->first_index = 0;
        a->base_vertex = 0;
        a->first_instance = 0;
    }
}

void flecsEngine_batch_group_prepareArgs(
    flecsEngine_batch_group_t *ctx)
{
    flecsEngine_batch_t *buf = ctx->batch;
    if (!buf || ctx->view.group_idx < 0) {
        return;
    }
    bool identity = (buf->flags & FLECS_BATCH_NO_GPU_CULL) != 0;
    flecsEngine_batch_group_prepareArgsForSet(
        &buf->buffers, &ctx->view,
        (uint32_t)ctx->mesh.index_count, identity);
}

void flecsEngine_batch_group_prepareStaticArgs(
    flecsEngine_batch_group_t *ctx)
{
    flecsEngine_batch_t *buf = ctx->batch;
    if (!buf || ctx->static_view.group_idx < 0) {
        return;
    }
    flecsEngine_batch_group_prepareArgsForSet(
        &buf->static_buffers, &ctx->static_view,
        (uint32_t)ctx->mesh.index_count, false);
}

/* For identity batches, slot_to_group isn't read but visible_slots for the
 * main view must be filled with identity indices [src_offset..src_offset+count).
 */
static void flecsEngine_batch_fillIdentityVisible(
    flecsEngine_batch_t *buf)
{
    /* Not needed if empty. */
    if (!buf->buffers.count) return;
}

void flecsEngine_batch_writeIdentityVisible(
    const FlecsEngineImpl *engine,
    const flecsEngine_batch_t *buf)
{
    if (!(buf->flags & FLECS_BATCH_NO_GPU_CULL)) {
        return;
    }
    if (!buf->buffers.gpu_visible_slots || !buf->buffers.count) {
        return;
    }

    int32_t count = buf->buffers.count;
    uint32_t *tmp = ecs_os_malloc_n(uint32_t, count);
    for (int32_t i = 0; i < count; i ++) {
        tmp[i] = (uint32_t)i;
    }
    wgpuQueueWriteBuffer(engine->queue,
        buf->buffers.gpu_visible_slots, 0,
        tmp, (uint64_t)count * sizeof(uint32_t));
    ecs_os_free(tmp);
    (void)flecsEngine_batch_fillIdentityVisible;
}

void flecsEngine_batch_worldAabb(
    const FlecsWorldTransform3 *wt,
    const FlecsAABB local,
    flecsEngine_gpuAabb_t *out)
{
    float smin[3] = { local.min[0], local.min[1], local.min[2] };
    float smax[3] = { local.max[0], local.max[1], local.max[2] };
    float mn[3], mx[3];
    for (int i = 0; i < 3; i ++) {
        mn[i] = mx[i] = wt->m[3][i];
        for (int j = 0; j < 3; j ++) {
            float e = wt->m[j][i] * smin[j];
            float f = wt->m[j][i] * smax[j];
            if (e < f) { mn[i] += e; mx[i] += f; }
            else       { mn[i] += f; mx[i] += e; }
        }
    }
    out->min[0] = mn[0]; out->min[1] = mn[1]; out->min[2] = mn[2];
    out->max[0] = mx[0]; out->max[1] = mx[1]; out->max[2] = mx[2];
    out->_pad0 = 0; out->_pad1 = 0;
}

void flecsEngine_batch_group_applyChanges(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    flecsEngine_batch_group_t *ctx)
{
    int32_t n = ecs_vec_count(&ctx->changed);
    if (!n) {
        return;
    }
    ecs_entity_t *es = ecs_vec_first_t(&ctx->changed, ecs_entity_t);
    int32_t *slots = ecs_vec_first_t(&ctx->changed_slots, int32_t);
    flecsEngine_batch_t *buf = ctx->batch;
    flecsEngine_batch_buffers_t *bb = &buf->static_buffers;
    FlecsAABB mesh_aabb = ctx->mesh.aabb;
    uint32_t g = (uint32_t)ctx->static_view.group_idx;
    bool owns_material = (buf->flags & FLECS_BATCH_OWNS_MATERIAL) != 0;

    for (int32_t i = 0; i < n; i ++) {
        ecs_entity_t e = es[i];
        int32_t slot = slots[i];
        /* The changed list isn't pruned when an entity is deleted or migrates
         * to another batch (only the changed_set is). An entry can therefore
         * name a dead entity, or a live entity that no longer owns this slot
         * (its slot was freed and possibly reused by a different building).
         * Writing to a slot the entity no longer owns corrupts the new owner:
         * it renders with the wrong transform/AABB and is culled from most
         * angles. Only touch a slot the entity still holds in this group. */
        if (!ecs_is_alive(world, e)) continue;
        const FlecsBufferSlot *bs = ecs_get(world, e, FlecsBufferSlot);
        if (!bs || bs->group != ctx || bs->slot != slot) continue;
        const FlecsWorldTransform3 *wt =
            flecs_transition_get(world, e, ecs_id(FlecsWorldTransform3));
        if (!wt) continue;
        flecsEngine_batch_worldAabb(wt, mesh_aabb, &bb->cpu_aabb[slot]);
        flecsEngine_batch_transformInstance(
            &bb->cpu_transforms[slot], wt, 1.0f, 1.0f, 1.0f);
        bb->cpu_slot_to_group[slot] = g;

        if (buf->flags & FLECS_BATCH_TINT) {
            const FlecsMaterialId *mid = ecs_get(world, e, FlecsMaterialId);
            bb->cpu_materials[slot] = flecsEngine_material_tintShared(
                engine,
                mid ? mid->value : 0,
                ecs_get(world, e, FlecsActualTint));
        } else if (owns_material) {
            const FlecsTransmission *transmission = NULL;
            if (buf->flags & FLECS_BATCH_OWNS_TRANSMISSION) {
                transmission = flecs_transition_get(
                    world, e, ecs_id(FlecsTransmission));
            }
            FlecsRgba rgba_storage;
            const FlecsRgba *rgba = flecsEngine_material_resolveRgba(
                world, e, ecs_get(world, e, FlecsRgba), &rgba_storage);
            bb->cpu_materials[slot] = flecsEngine_material_pack(
                engine,
                rgba,
                flecs_transition_get(world, e, ecs_id(FlecsPbrMaterial)),
                flecs_transition_get(world, e, ecs_id(FlecsEmissive)),
                transmission,
                NULL,
                NULL);
        }
    }
}

void flecsEngine_batch_uploadStatic(
    const FlecsEngineImpl *engine,
    flecsEngine_batch_t *buf,
    flecsEngine_batch_group_t **groups,
    int32_t group_count)
{
    FLECS_TRACY_ZONE_BEGIN("BatchUploadStatic");
    flecsEngine_batch_buffers_t *bb = &buf->static_buffers;
    if (!bb->count || !bb->group_count) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    flecsEngine_batchBuffers_ensureGroupGpu(engine, bb);

    WGPUQueue queue = engine->queue;
    bool owns_material = (buf->flags & FLECS_BATCH_OWNS_MATERIAL) != 0;

    int32_t total_changed = 0;
    if (!bb->needs_full_upload) {
        for (int32_t g = 0; g < group_count; g ++) {
            total_changed += ecs_vec_count(&groups[g]->changed_slots);
        }
    }

    /* When a large fraction of slots changed, the per-slot path issues many
     * small wgpuQueueWriteBuffer calls (3-4 per slot), each of which has
     * non-trivial CPU overhead. In that regime a single full-buffer upload
     * per buffer is dramatically cheaper. */
    int32_t full_threshold = bb->count / 4;
    if (full_threshold < 16) full_threshold = 16;
    bool do_full = bb->needs_full_upload || total_changed >= full_threshold;

    if (do_full) {
        int32_t n = bb->count;
        wgpuQueueWriteBuffer(queue, bb->gpu_transforms, 0,
            bb->cpu_transforms, (uint64_t)n * sizeof(FlecsGpuTransform));
        wgpuQueueWriteBuffer(queue, bb->gpu_aabb, 0,
            bb->cpu_aabb, (uint64_t)n * sizeof(flecsEngine_gpuAabb_t));
        wgpuQueueWriteBuffer(queue, bb->gpu_material_ids, 0,
            bb->cpu_material_ids, (uint64_t)n * sizeof(FlecsMaterialId));
        if (owns_material) {
            wgpuQueueWriteBuffer(queue, bb->gpu_materials, 0,
                bb->cpu_materials, (uint64_t)n * sizeof(FlecsGpuMaterial));
        }
        bb->needs_full_upload = false;
    } else {
        for (int32_t g = 0; g < group_count; g ++) {
            flecsEngine_batch_group_t *ctx = groups[g];
            int32_t nc = ecs_vec_count(&ctx->changed_slots);
            if (!nc) continue;
            int32_t *slots = ecs_vec_first_t(&ctx->changed_slots, int32_t);
            for (int32_t i = 0; i < nc; i ++) {
                int32_t slot = slots[i];

                wgpuQueueWriteBuffer(queue,
                    bb->gpu_transforms,
                    (uint64_t)slot * sizeof(FlecsGpuTransform),
                    &bb->cpu_transforms[slot], sizeof(FlecsGpuTransform));
                wgpuQueueWriteBuffer(queue,
                    bb->gpu_aabb,
                    (uint64_t)slot * sizeof(flecsEngine_gpuAabb_t),
                    &bb->cpu_aabb[slot], sizeof(flecsEngine_gpuAabb_t));
                wgpuQueueWriteBuffer(queue,
                    bb->gpu_material_ids,
                    (uint64_t)slot * sizeof(FlecsMaterialId),
                    &bb->cpu_material_ids[slot], sizeof(FlecsMaterialId));
                if (owns_material) {
                    wgpuQueueWriteBuffer(queue,
                        bb->gpu_materials,
                        (uint64_t)slot * sizeof(FlecsGpuMaterial),
                        &bb->cpu_materials[slot], sizeof(FlecsGpuMaterial));
                }
            }
        }
    }

    wgpuQueueWriteBuffer(queue, bb->gpu_slot_to_group, 0,
        bb->cpu_slot_to_group, (uint64_t)bb->count * sizeof(uint32_t));
    wgpuQueueWriteBuffer(queue, bb->gpu_group_info, 0,
        bb->cpu_group_info,
        (uint64_t)bb->group_count * sizeof(flecsEngine_gpuCullGroupInfo_t));
    wgpuQueueWriteBuffer(queue, bb->gpu_indirect_args, 0,
        bb->cpu_indirect_args,
        (uint64_t)bb->group_count *
            (1 + FLECS_ENGINE_SHADOW_CASCADE_COUNT) *
            sizeof(flecsEngine_gpuDrawArgs_t));

    flecsEngine_gpuCullBatchInfoUpload_t info = {
        .count = (uint32_t)bb->count,
        .capacity = (uint32_t)bb->capacity,
        .group_count = (uint32_t)bb->group_count,
        ._pad = 0
    };
    wgpuQueueWriteBuffer(queue, bb->gpu_batch_info, 0, &info, sizeof(info));

    for (int32_t g = 0; g < group_count; g ++) {
        flecsEngine_batch_group_t *ctx = groups[g];
        ecs_vec_clear(&ctx->changed);
        ecs_vec_clear(&ctx->changed_slots);
        ecs_map_clear(&ctx->changed_set);
    }

    FLECS_TRACY_ZONE_END;
}

void flecsEngine_batch_transformInstance(
    FlecsGpuTransform *out,
    const FlecsWorldTransform3 *wt,
    float scale_x,
    float scale_y,
    float scale_z)
{
    out->c0.x = wt->m[0][0] * scale_x;
    out->c0.y = wt->m[0][1] * scale_x;
    out->c0.z = wt->m[0][2] * scale_x;

    out->c1.x = wt->m[1][0] * scale_y;
    out->c1.y = wt->m[1][1] * scale_y;
    out->c1.z = wt->m[1][2] * scale_y;

    out->c2.x = wt->m[2][0] * scale_z;
    out->c2.y = wt->m[2][1] * scale_z;
    out->c2.z = wt->m[2][2] * scale_z;

    out->c3.x = wt->m[3][0];
    out->c3.y = wt->m[3][1];
    out->c3.z = wt->m[3][2];
}
