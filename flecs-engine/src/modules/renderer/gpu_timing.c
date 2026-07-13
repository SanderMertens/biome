#include <string.h>
#include <stdio.h>

#include "renderer.h"
#include "gpu_timing.h"
#include "flecs_engine.h"

#define FLECS_TS_QUERY_COUNT (FLECS_GPU_TIMING_MAX_PAIRS * 2)
#define FLECS_TS_BYTES (FLECS_TS_QUERY_COUNT * sizeof(uint64_t))

int flecsEngine_gpuTiming_init(FlecsEngineImpl *engine)
{
    flecsEngine_gpuTiming_t *t = &engine->gpu_timing;
    ecs_os_zeromem(t);

    /* The timestamp-query feature is optional and is not requested on the web
     * (Emscripten) device. Creating a timestamp query set without it is a
     * validation error, so only proceed when the device supports it. */
    if (!wgpuDeviceHasFeature(engine->device, WGPUFeatureName_TimestampQuery)) {
        ecs_warn("timestamp queries not available — GPU timing disabled");
        return 0;
    }

    t->query_set = wgpuDeviceCreateQuerySet(engine->device,
        &(WGPUQuerySetDescriptor){
            .type = WGPUQueryType_Timestamp,
            .count = FLECS_TS_QUERY_COUNT
        });
    if (!t->query_set) {
        ecs_warn("timestamp queries not available — GPU timing disabled");
        return 0;
    }

    t->resolve_buffer = wgpuDeviceCreateBuffer(engine->device,
        &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_QueryResolve
                   | WGPUBufferUsage_CopySrc,
            .size = FLECS_TS_BYTES
        });

    for (int i = 0; i < FLECS_GPU_TIMING_RING; i ++) {
        t->slots[i].readback_buffer = wgpuDeviceCreateBuffer(engine->device,
            &(WGPUBufferDescriptor){
                .usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead,
                .size = FLECS_TS_BYTES
            });
        t->slots[i].state = FLECS_GPU_TIMING_IDLE;
    }

    t->capable = true;
    t->cur_slot = -1;
    t->next_slot = 0;
    return 0;
}

void flecsEngine_gpuTiming_fini(FlecsEngineImpl *engine)
{
    flecsEngine_gpuTiming_t *t = &engine->gpu_timing;
    for (int i = 0; i < FLECS_GPU_TIMING_RING; i ++) {
        FLECS_WGPU_RELEASE(t->slots[i].readback_buffer, wgpuBufferRelease);
    }
    FLECS_WGPU_RELEASE(t->resolve_buffer, wgpuBufferRelease);
    if (t->query_set) {
        wgpuQuerySetRelease(t->query_set);
        t->query_set = NULL;
    }
}

bool flecsEngine_gpuTiming_beginFrame(FlecsEngineImpl *engine, bool wanted)
{
    flecsEngine_gpuTiming_t *t = &engine->gpu_timing;
    if (!t->capable || !wanted) {
        t->cur_slot = -1;
        return false;
    }

    int s = t->next_slot;
    if (t->slots[s].state != FLECS_GPU_TIMING_IDLE) {
        t->cur_slot = -1;
        return false;
    }

    t->slots[s].pair_count = 0;
    t->slots[s].frame_id = t->frame_counter ++;
    t->cur_slot = s;
    t->next_slot = (s + 1) % FLECS_GPU_TIMING_RING;
    return true;
}

int flecsEngine_gpuTiming_allocPair(
    FlecsEngineImpl *engine,
    const char *name)
{
    flecsEngine_gpuTiming_t *t = &engine->gpu_timing;
    if (t->cur_slot < 0) {
        return -1;
    }
    flecsEngine_gpuTimingSlot_t *slot = &t->slots[t->cur_slot];
    if (slot->pair_count >= FLECS_GPU_TIMING_MAX_PAIRS) {
        return -1;
    }
    int idx = slot->pair_count ++;
    strncpy(slot->pairs[idx].name, name ? name : "?",
        sizeof(slot->pairs[idx].name) - 1);
    slot->pairs[idx].name[sizeof(slot->pairs[idx].name) - 1] = 0;
    return idx;
}

void flecsEngine_gpuTiming_computePassTimestamps(
    FlecsEngineImpl *engine,
    int pair_idx,
    WGPUComputePassTimestampWrites *out)
{
    flecsEngine_gpuTiming_t *t = &engine->gpu_timing;
    out->querySet = (t->capable && pair_idx >= 0) ? t->query_set : NULL;
    out->beginningOfPassWriteIndex = (uint32_t)(pair_idx * 2);
    out->endOfPassWriteIndex = (uint32_t)(pair_idx * 2 + 1);
}

void flecsEngine_gpuTiming_renderPassTimestamps(
    FlecsEngineImpl *engine,
    int pair_idx,
    WGPURenderPassTimestampWrites *out)
{
    flecsEngine_gpuTiming_t *t = &engine->gpu_timing;
    out->querySet = (t->capable && pair_idx >= 0) ? t->query_set : NULL;
    out->beginningOfPassWriteIndex = (uint32_t)(pair_idx * 2);
    out->endOfPassWriteIndex = (uint32_t)(pair_idx * 2 + 1);
}

const WGPURenderPassTimestampWrites *flecsEngine_gpuTiming_renderPassBegin(
    FlecsEngineImpl *engine,
    int pair_idx,
    WGPURenderPassTimestampWrites *out)
{
    if (pair_idx < 0) {
        return NULL;
    }
    out->querySet = engine->gpu_timing.query_set;
    out->beginningOfPassWriteIndex = (uint32_t)(pair_idx * 2);
    out->endOfPassWriteIndex = WGPU_QUERY_SET_INDEX_UNDEFINED;
    return out;
}

const WGPURenderPassTimestampWrites *flecsEngine_gpuTiming_renderPassEnd(
    FlecsEngineImpl *engine,
    int pair_idx,
    WGPURenderPassTimestampWrites *out)
{
    if (pair_idx < 0) {
        return NULL;
    }
    out->querySet = engine->gpu_timing.query_set;
    out->beginningOfPassWriteIndex = WGPU_QUERY_SET_INDEX_UNDEFINED;
    out->endOfPassWriteIndex = (uint32_t)(pair_idx * 2 + 1);
    return out;
}

void flecsEngine_gpuTiming_endFrame(
    FlecsEngineImpl *engine,
    WGPUCommandEncoder encoder)
{
    flecsEngine_gpuTiming_t *t = &engine->gpu_timing;
    if (t->cur_slot < 0) {
        return;
    }
    flecsEngine_gpuTimingSlot_t *slot = &t->slots[t->cur_slot];
    if (!slot->pair_count) {
        return;
    }

    uint32_t q = (uint32_t)(slot->pair_count * 2);
    wgpuCommandEncoderResolveQuerySet(
        encoder, t->query_set, 0, q, t->resolve_buffer, 0);
    wgpuCommandEncoderCopyBufferToBuffer(
        encoder, t->resolve_buffer, 0,
        slot->readback_buffer, 0, (uint64_t)q * sizeof(uint64_t));
}

typedef struct {
    FlecsEngineImpl *engine;
    int slot_idx;
} flecsEngine_gpuTimingMapCtx_t;

static void flecsEngine_gpuTiming_onMapped(
    WGPUMapAsyncStatus status,
    const char *message,
    void *userdata)
{
    (void)message;
    flecsEngine_gpuTimingMapCtx_t *ctx = userdata;
    if (status == WGPUMapAsyncStatus_Success) {
        ctx->engine->gpu_timing.slots[ctx->slot_idx].state =
            FLECS_GPU_TIMING_READY;
    } else {
        ctx->engine->gpu_timing.slots[ctx->slot_idx].state =
            FLECS_GPU_TIMING_IDLE;
    }
    ecs_os_free(ctx);
}

void flecsEngine_gpuTiming_afterSubmit(FlecsEngineImpl *engine)
{
    flecsEngine_gpuTiming_t *t = &engine->gpu_timing;
    if (t->cur_slot < 0) return;

    flecsEngine_gpuTimingSlot_t *slot = &t->slots[t->cur_slot];
    if (!slot->pair_count) return;

    slot->state = FLECS_GPU_TIMING_PENDING;

    flecsEngine_gpuTimingMapCtx_t *ctx =
        ecs_os_malloc_t(flecsEngine_gpuTimingMapCtx_t);
    ctx->engine = engine;
    ctx->slot_idx = t->cur_slot;

    size_t size = (size_t)(slot->pair_count * 2 * sizeof(uint64_t));
    flecsEngine_bufferMapAsync(
        slot->readback_buffer, WGPUMapMode_Read, 0, size,
        flecsEngine_gpuTiming_onMapped, ctx);
}

void flecsEngine_gpuTiming_logIfReady(FlecsEngineImpl *engine)
{
    flecsEngine_gpuTiming_t *t = &engine->gpu_timing;
    if (!t->capable) {
        return;
    }
    for (int i = 0; i < FLECS_GPU_TIMING_RING; i ++) {
        flecsEngine_gpuTimingSlot_t *slot = &t->slots[i];
        if (slot->state != FLECS_GPU_TIMING_READY) continue;

        size_t size = (size_t)(slot->pair_count * 2 * sizeof(uint64_t));
        const uint64_t *data = wgpuBufferGetConstMappedRange(
            slot->readback_buffer, 0, size);
        if (!data) {
            wgpuBufferUnmap(slot->readback_buffer);
            slot->state = FLECS_GPU_TIMING_IDLE;
            continue;
        }

        printf("[gpu-timing frame %llu]",
            (unsigned long long)slot->frame_id);
        uint64_t min_begin = UINT64_MAX;
        uint64_t max_end = 0;
        for (int p = 0; p < slot->pair_count; p ++) {
            uint64_t t0 = data[p * 2];
            uint64_t t1 = data[p * 2 + 1];
            uint64_t dt = (t1 >= t0) ? (t1 - t0) : 0;
            if (t0 && t0 < min_begin) min_begin = t0;
            if (t1 > max_end) max_end = t1;
            printf("  %s=%.3fms", slot->pairs[p].name,
                (double)dt / 1.0e6);
        }
        uint64_t span_ns = (max_end > min_begin && min_begin != UINT64_MAX)
            ? (max_end - min_begin) : 0;
        printf("  frame=%.3fms\n", (double)span_ns / 1.0e6);

        wgpuBufferUnmap(slot->readback_buffer);
        slot->state = FLECS_GPU_TIMING_IDLE;
    }
}
