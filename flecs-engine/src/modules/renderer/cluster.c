#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "renderer.h"
#include "../../tracy_hooks.h"
#include "flecs_engine.h"

#define FLECS_ENGINE_CLUSTER_INITIAL_LIGHTS 32
#define FLECS_ENGINE_CLUSTER_INITIAL_INDICES 4096

/* Grow a storage buffer to at least `needed` elements of `elem_size`.
 * Returns the new capacity via `*capacity`. Replaces *buffer in place. */
static bool flecsEngine_cluster_growBuffer(
    WGPUDevice device,
    WGPUBuffer *buffer,
    int32_t *capacity,
    int32_t needed,
    uint64_t elem_size)
{
    if (needed <= *capacity) {
        return false; /* no growth needed */
    }

    int32_t new_cap = *capacity ? *capacity : 1;
    while (new_cap < needed) {
        new_cap *= 2;
    }

    if (*buffer) {
        wgpuBufferRelease(*buffer);
    }

    WGPUBufferDescriptor desc = {
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        .size = (uint64_t)new_cap * elem_size
    };
    *buffer = wgpuDeviceCreateBuffer(device, &desc);
    *capacity = new_cap;
    return true; /* buffer changed, bind group must be recreated */
}

int flecsEngine_cluster_initLights(
    FlecsEngineImpl *impl)
{
    int32_t init_lights = FLECS_ENGINE_CLUSTER_INITIAL_LIGHTS;

    impl->lighting.cpu_lights = ecs_os_calloc_n(
        FlecsGpuLight, init_lights);
    impl->lighting.light_capacity = init_lights;

    WGPUBufferDescriptor l_desc = {
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        .size = (uint64_t)init_lights * sizeof(FlecsGpuLight)
    };
    impl->lighting.light_buffer = wgpuDeviceCreateBuffer(
        impl->device, &l_desc);

    if (!impl->lighting.light_buffer) {
        ecs_err("failed to create light GPU buffer");
        return -1;
    }

    return 0;
}

int flecsEngine_cluster_initView(
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl)
{
    flecs_view_cluster_t *cl = &view_impl->cluster;

    if (cl->cluster_info_buffer) {
        return 0; /* already initialized */
    }

    int32_t init_indices = FLECS_ENGINE_CLUSTER_INITIAL_INDICES;

    cl->cpu_cluster_indices = ecs_os_calloc_n(uint32_t, init_indices);
    cl->cluster_index_capacity = init_indices;

    cl->cluster_info_buffer = flecsEngine_createUniformBuffer(
        engine->device, sizeof(FlecsClusterInfo));

    WGPUBufferDescriptor grid_desc = {
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        .size = (uint64_t)FLECS_ENGINE_CLUSTER_TOTAL *
            sizeof(FlecsClusterEntry)
    };
    cl->cluster_grid_buffer = wgpuDeviceCreateBuffer(
        engine->device, &grid_desc);

    WGPUBufferDescriptor idx_desc = {
        .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
        .size = (uint64_t)init_indices * sizeof(uint32_t)
    };
    cl->cluster_index_buffer = wgpuDeviceCreateBuffer(
        engine->device, &idx_desc);

    if (!cl->cluster_info_buffer || !cl->cluster_grid_buffer ||
        !cl->cluster_index_buffer)
    {
        ecs_err("failed to create cluster GPU buffers");
        return -1;
    }

    return 0;
}

void flecsEngine_cluster_cleanupView(
    FlecsRenderViewImpl *view_impl)
{
    flecs_view_cluster_t *cl = &view_impl->cluster;
    FLECS_WGPU_RELEASE(cl->cluster_index_buffer, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(cl->cluster_grid_buffer, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(cl->cluster_info_buffer, wgpuBufferRelease);
    if (cl->cpu_cluster_indices) {
        ecs_os_free(cl->cpu_cluster_indices);
        cl->cpu_cluster_indices = NULL;
    }
    cl->cluster_index_capacity = 0;
}

/* Ensure CPU + GPU light arrays can hold `needed` lights. Returns true
 * if a GPU buffer was reallocated. Bumps the scene bind version so every
 * view rebuilds its group 0 (which binds the light buffer). */
bool flecsEngine_cluster_ensureLights(
    FlecsEngineImpl *engine, int32_t needed)
{
    if (needed <= engine->lighting.light_capacity) {
        return false;
    }

    int32_t new_cap = engine->lighting.light_capacity ? engine->lighting.light_capacity : 1;
    while (new_cap < needed) new_cap *= 2;

    engine->lighting.cpu_lights = ecs_os_realloc_n(
        engine->lighting.cpu_lights, FlecsGpuLight, new_cap);

    flecsEngine_cluster_growBuffer(engine->device,
        &engine->lighting.light_buffer, &engine->lighting.light_capacity,
        new_cap, sizeof(FlecsGpuLight));

    engine->scene_bind_version ++;
    return true;
}

static bool flecsEngine_cluster_ensureIndices(
    FlecsEngineImpl *engine, FlecsRenderViewImpl *view_impl,
    int32_t needed)
{
    flecs_view_cluster_t *cl = &view_impl->cluster;
    if (needed <= cl->cluster_index_capacity) {
        return false;
    }

    int32_t new_cap = cl->cluster_index_capacity
        ? cl->cluster_index_capacity : 1;
    while (new_cap < needed) new_cap *= 2;

    cl->cpu_cluster_indices = ecs_os_realloc_n(
        cl->cpu_cluster_indices, uint32_t, new_cap);

    bool changed = flecsEngine_cluster_growBuffer(engine->device,
        &cl->cluster_index_buffer, &cl->cluster_index_capacity,
        new_cap, sizeof(uint32_t));

    return changed;
}

static inline int flecsEngine_cluster_index(int tx, int ty, int sz) {
    return tx + ty * FLECS_ENGINE_CLUSTER_X
             + sz * FLECS_ENGINE_CLUSTER_X * FLECS_ENGINE_CLUSTER_Y;
}

/* Compute overlapping cluster tile range for a light sphere. */
static bool flecsEngine_cluster_sphereRange(
    const float view_mat[4][4],
    float world_x, float world_y, float world_z,
    float range,
    float near, float far,
    float tan_half_fov, float aspect,
    float log_ratio,
    int *out_tx_min, int *out_tx_max,
    int *out_ty_min, int *out_ty_max,
    int *out_sz_min, int *out_sz_max)
{
    float vx = view_mat[0][0] * world_x + view_mat[1][0] * world_y +
               view_mat[2][0] * world_z + view_mat[3][0];
    float vy = view_mat[0][1] * world_x + view_mat[1][1] * world_y +
               view_mat[2][1] * world_z + view_mat[3][1];
    float vz = view_mat[0][2] * world_x + view_mat[1][2] * world_y +
               view_mat[2][2] * world_z + view_mat[3][2];

    float depth = -vz;

    if (depth + range < near || depth - range > far) {
        return false;
    }

    float z_min = fmaxf(depth - range, near);
    float z_max = fminf(depth + range, far);

    int sz_min = (int)floorf(logf(z_min / near) / log_ratio *
        (float)FLECS_ENGINE_CLUSTER_Z);
    int sz_max = (int)floorf(logf(z_max / near) / log_ratio *
        (float)FLECS_ENGINE_CLUSTER_Z);
    if (sz_min < 0) sz_min = 0;
    if (sz_max >= FLECS_ENGINE_CLUSTER_Z) sz_max = FLECS_ENGINE_CLUSTER_Z - 1;

    int tx_min, tx_max, ty_min, ty_max;
    float dist_sq = vx * vx + vy * vy + vz * vz;
    if (dist_sq <= range * range) {
        /* Camera is inside the light's bounding sphere */
        tx_min = 0; tx_max = FLECS_ENGINE_CLUSTER_X - 1;
        ty_min = 0; ty_max = FLECS_ENGINE_CLUSTER_Y - 1;
    } else {
        float safe_depth = fmaxf(depth, near);
        float half_y = safe_depth * tan_half_fov;
        float half_x = half_y * aspect;
        float ndc_cx = (half_x > 1e-6f) ? (vx / half_x) : 0.0f;
        float ndc_cy = (half_y > 1e-6f) ? (vy / half_y) : 0.0f;

        float min_depth = fmaxf(depth - range, near);
        float half_y_min = min_depth * tan_half_fov;
        float half_x_min = half_y_min * aspect;
        float ndc_rx = (half_x_min > 1e-6f) ? (range / half_x_min) : 100.0f;
        float ndc_ry = (half_y_min > 1e-6f) ? (range / half_y_min) : 100.0f;

        float tile_cx = ( ndc_cx * 0.5f + 0.5f) * (float)FLECS_ENGINE_CLUSTER_X;
        float tile_cy = (-ndc_cy * 0.5f + 0.5f) * (float)FLECS_ENGINE_CLUSTER_Y;
        float tile_rx = ndc_rx * 0.5f * (float)FLECS_ENGINE_CLUSTER_X;
        float tile_ry = ndc_ry * 0.5f * (float)FLECS_ENGINE_CLUSTER_Y;

        tx_min = (int)floorf(tile_cx - tile_rx);
        tx_max = (int)floorf(tile_cx + tile_rx);
        ty_min = (int)floorf(tile_cy - tile_ry);
        ty_max = (int)floorf(tile_cy + tile_ry);
        if (tx_min < 0) tx_min = 0;
        if (tx_max >= FLECS_ENGINE_CLUSTER_X) tx_max = FLECS_ENGINE_CLUSTER_X - 1;
        if (ty_min < 0) ty_min = 0;
        if (ty_max >= FLECS_ENGINE_CLUSTER_Y) ty_max = FLECS_ENGINE_CLUSTER_Y - 1;
    }

    *out_tx_min = tx_min; *out_tx_max = tx_max;
    *out_ty_min = ty_min; *out_ty_max = ty_max;
    *out_sz_min = sz_min; *out_sz_max = sz_max;
    return true;
}

/* Iterate lights and count how many fall into each cluster cell. */
static void flecsEngine_cluster_countLights(
    const float (*view_mat)[4],
    const FlecsGpuLight *lights,
    int32_t light_count,
    float near, float far,
    float tan_half_fov, float aspect,
    float log_ratio,
    uint16_t *cell_counts)
{
    for (int32_t li = 0; li < light_count; li++) {
        const float *pos = lights[li].position;

        int tx0, tx1, ty0, ty1, sz0, sz1;
        if (!flecsEngine_cluster_sphereRange(view_mat,
                pos[0], pos[1], pos[2], pos[3],
                near, far, tan_half_fov, aspect, log_ratio,
                &tx0, &tx1, &ty0, &ty1, &sz0, &sz1)) {
            continue;
        }

        for (int sz = sz0; sz <= sz1; sz++)
            for (int ty = ty0; ty <= ty1; ty++)
                for (int tx = tx0; tx <= tx1; tx++)
                    cell_counts[flecsEngine_cluster_index(tx, ty, sz)]++;
    }
}

/* Iterate lights and fill the index buffer for each cluster cell. */
static void flecsEngine_cluster_fillLights(
    const float (*view_mat)[4],
    const FlecsGpuLight *lights,
    int32_t light_count,
    float near, float far,
    float tan_half_fov, float aspect,
    float log_ratio,
    const uint32_t *cell_offsets,
    uint16_t *fill_offsets,
    uint32_t *index_buf)
{
    for (int32_t li = 0; li < light_count; li++) {
        const float *pos = lights[li].position;

        int tx0, tx1, ty0, ty1, sz0, sz1;
        if (!flecsEngine_cluster_sphereRange(view_mat,
                pos[0], pos[1], pos[2], pos[3],
                near, far, tan_half_fov, aspect, log_ratio,
                &tx0, &tx1, &ty0, &ty1, &sz0, &sz1)) {
            continue;
        }

        for (int sz = sz0; sz <= sz1; sz++)
            for (int ty = ty0; ty <= ty1; ty++)
                for (int tx = tx0; tx <= tx1; tx++) {
                    int ci = flecsEngine_cluster_index(tx, ty, sz);
                    uint32_t dst = cell_offsets[ci] + fill_offsets[ci];
                    index_buf[dst] = (uint32_t)li;
                    fill_offsets[ci]++;
                }
    }
}

void flecsEngine_cluster_build(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    const FlecsRenderView *view)
{
    FLECS_TRACY_ZONE_BEGIN("ClusterBuild");
    flecs_view_cluster_t *cl = &view_impl->cluster;
    if (!cl->cluster_info_buffer || !view->camera) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    const FlecsCamera *cam = ecs_get(world, view->camera, FlecsCamera);
    const FlecsCameraImpl *cam_impl = ecs_get(
        world, view->camera, FlecsCameraImpl);
    if (!cam || !cam_impl) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    float near = cam->near_;
    float far = cam->far_;
    if (near <= 0.0f) near = 0.1f;
    if (far <= near) far = near + 1.0f;
    float tan_half_fov = tanf(cam->fov * 0.5f);
    const FlecsSurface *surface = ecs_get(world, engine->surface, FlecsSurface);
    float aspect = (float)surface->actual_width / (float)surface->actual_height;
    float log_ratio = logf(far / near);
    if (log_ratio < 1e-6f) { FLECS_TRACY_ZONE_END; return; }

    const float (*view_mat)[4] = (const float (*)[4])cam_impl->view;

    int32_t light_count = engine->lighting.light_count;

    /* Upload cluster info */
    FlecsClusterInfo info = {
        .grid_size = { FLECS_ENGINE_CLUSTER_X, FLECS_ENGINE_CLUSTER_Y,
            FLECS_ENGINE_CLUSTER_Z, FLECS_ENGINE_CLUSTER_TOTAL },
        .screen_info = { (float)surface->actual_width, (float)surface->actual_height,
            near, log_ratio }
    };
    wgpuQueueWriteBuffer(engine->queue, cl->cluster_info_buffer,
        0, &info, sizeof(info));

    /* --- Two-pass cluster assignment --- */

    /* Pass 1: count per-cluster lights */
    uint16_t cell_counts[FLECS_ENGINE_CLUSTER_TOTAL];
    memset(cell_counts, 0, sizeof(cell_counts));

    flecsEngine_cluster_countLights(view_mat,
        engine->lighting.cpu_lights, light_count,
        near, far, tan_half_fov, aspect, log_ratio, cell_counts);

    /* Compute offsets via prefix sum */
    FlecsClusterEntry grid[FLECS_ENGINE_CLUSTER_TOTAL];
    uint32_t offset = 0;
    for (int i = 0; i < FLECS_ENGINE_CLUSTER_TOTAL; i++) {
        grid[i].light_offset = offset;
        grid[i].light_count = cell_counts[i];
        grid[i]._pad0 = 0;
        grid[i]._pad1 = 0;
        offset += cell_counts[i];
    }
    uint32_t total_indices = offset;

    /* Grow index buffer if needed — if it resized, invalidate this view's
     * group 0 so it rebuilds with the new buffer. */
    if (flecsEngine_cluster_ensureIndices(
        engine, view_impl, (int32_t)total_indices))
    {
        view_impl->scene_bind_version = 0;
    }

    /* Pass 2: fill index list */
    uint32_t cell_offsets[FLECS_ENGINE_CLUSTER_TOTAL];
    for (int i = 0; i < FLECS_ENGINE_CLUSTER_TOTAL; i++) {
        cell_offsets[i] = grid[i].light_offset;
    }

    uint16_t fill_counts[FLECS_ENGINE_CLUSTER_TOTAL];
    memset(fill_counts, 0, sizeof(fill_counts));

    uint32_t *indices = cl->cpu_cluster_indices;

    flecsEngine_cluster_fillLights(view_mat,
        engine->lighting.cpu_lights, light_count,
        near, far, tan_half_fov, aspect, log_ratio,
        cell_offsets, fill_counts, indices);

    /* Upload everything to GPU */
    wgpuQueueWriteBuffer(engine->queue, cl->cluster_grid_buffer,
        0, grid, sizeof(grid));

    if (total_indices > 0) {
        wgpuQueueWriteBuffer(engine->queue, cl->cluster_index_buffer,
            0, indices, (uint64_t)total_indices * sizeof(uint32_t));
    }

    if (light_count > 0) {
        wgpuQueueWriteBuffer(engine->queue, engine->lighting.light_buffer,
            0, engine->lighting.cpu_lights,
            (uint64_t)light_count * sizeof(FlecsGpuLight));
    }
    FLECS_TRACY_ZONE_END;
}
