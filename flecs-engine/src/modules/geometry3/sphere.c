#include "geometry3.h"
#include "geometry3_math.h"
#include <math.h>
#include <stdio.h>

static flecs_vec3_t flecsEngine_sphere_unitPoint(
    float theta,
    float phi)
{
    float sin_theta = sinf(theta);
    float cos_theta = cosf(theta);
    float sin_phi = sinf(phi);
    float cos_phi = cosf(phi);

    return (flecs_vec3_t){
        sin_theta * cos_phi,
        cos_theta,
        sin_theta * sin_phi
    };
}

static flecs_vec3_t flecsEngine_sphere_point(
    float theta,
    float phi,
    float radius)
{
    flecs_vec3_t p = flecsEngine_sphere_unitPoint(theta, phi);
    return (flecs_vec3_t){p.x * radius, p.y * radius, p.z * radius};
}

static void flecsEngine_sphere_generateSmoothMesh(
    FlecsMesh3 *mesh,
    int32_t segments,
    float radius)
{
    const int32_t rings = segments;
    const int32_t cols = segments;
    const int32_t vert_count = (rings + 1) * (cols + 1);
    const int32_t index_count = rings * cols * 6;

    ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->indices, uint32_t, index_count);

    flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
    uint32_t *idx = ecs_vec_first_t(&mesh->indices, uint32_t);

    int32_t vi = 0;
    for (int32_t y = 0; y <= rings; y ++) {
        float v_t = (float)y / (float)rings;
        float theta = v_t * (float)M_PI;

        for (int32_t x = 0; x <= cols; x ++) {
            float u = (float)x / (float)cols;
            float phi = u * 2.0f * (float)M_PI;
            vn[vi] = flecsEngine_sphere_unitPoint(theta, phi);
            v[vi] = (flecs_vec3_t){
                vn[vi].x * radius,
                vn[vi].y * radius,
                vn[vi].z * radius
            };
            vi ++;
        }
    }

    int32_t ii = 0;
    for (int32_t y = 0; y < rings; y ++) {
        for (int32_t x = 0; x < cols; x ++) {
            uint32_t a = (uint32_t)(y * (cols + 1) + x);
            uint32_t b = (uint32_t)(a + cols + 1);
            uint32_t c = (uint32_t)(b + 1);
            uint32_t d = (uint32_t)(a + 1);

            idx[ii ++] = a;
            idx[ii ++] = d;
            idx[ii ++] = b;

            idx[ii ++] = b;
            idx[ii ++] = d;
            idx[ii ++] = c;
        }
    }
}

static void flecsEngine_sphere_generateFlatMesh(
    FlecsMesh3 *mesh,
    int32_t segments,
    float radius)
{
    const int32_t rings = segments;
    const int32_t cols = segments;
    const int32_t vert_count = rings * cols * 6;
    const int32_t index_count = vert_count;

    ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh->indices, uint32_t, index_count);

    flecs_vec3_t *v = ecs_vec_first_t(&mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *vn = ecs_vec_first_t(&mesh->normals, flecs_vec3_t);
    uint32_t *idx = ecs_vec_first_t(&mesh->indices, uint32_t);

    int32_t vi = 0;
    int32_t ii = 0;
    for (int32_t y = 0; y < rings; y ++) {
        float v0_t = (float)y / (float)rings;
        float v1_t = (float)(y + 1) / (float)rings;
        float theta0 = v0_t * (float)M_PI;
        float theta1 = v1_t * (float)M_PI;

        for (int32_t x = 0; x < cols; x ++) {
            float u0 = (float)x / (float)cols;
            float u1 = (float)(x + 1) / (float)cols;
            float phi0 = u0 * 2.0f * (float)M_PI;
            float phi1 = u1 * 2.0f * (float)M_PI;

            flecs_vec3_t a = flecsEngine_sphere_point(theta0, phi0, radius);
            flecs_vec3_t b = flecsEngine_sphere_point(theta1, phi0, radius);
            flecs_vec3_t c = flecsEngine_sphere_point(theta1, phi1, radius);
            flecs_vec3_t d = flecsEngine_sphere_point(theta0, phi1, radius);

            flecs_vec3_t n0 = flecsEngine_triangleNormal(a, b, d, (flecs_vec3_t){0.0f, 1.0f, 0.0f});
            flecs_vec3_t n1 = flecsEngine_triangleNormal(b, c, d, (flecs_vec3_t){0.0f, 1.0f, 0.0f});

            v[vi] = a;
            vn[vi] = n0;
            idx[ii] = (uint32_t)vi;
            vi ++;
            ii ++;

            v[vi] = d;
            vn[vi] = n0;
            idx[ii] = (uint32_t)vi;
            vi ++;
            ii ++;

            v[vi] = b;
            vn[vi] = n0;
            idx[ii] = (uint32_t)vi;
            vi ++;
            ii ++;

            v[vi] = b;
            vn[vi] = n1;
            idx[ii] = (uint32_t)vi;
            vi ++;
            ii ++;

            v[vi] = d;
            vn[vi] = n1;
            idx[ii] = (uint32_t)vi;
            vi ++;
            ii ++;

            v[vi] = c;
            vn[vi] = n1;
            idx[ii] = (uint32_t)vi;
            vi ++;
            ii ++;
        }
    }
}

static void flecsEngine_sphere_generateMesh(
    FlecsMesh3 *mesh,
    int32_t segments,
    bool smooth,
    float radius)
{
    if (smooth) {
        flecsEngine_sphere_generateSmoothMesh(mesh, segments, radius);
    } else {
        flecsEngine_sphere_generateFlatMesh(mesh, segments, radius);
    }
}

static ecs_entity_t flecsEngine_sphere_getAsset(
    ecs_world_t *world,
    int32_t segments,
    bool smooth,
    float radius)
{
    int32_t normalized_segments = flecsEngine_segmentsNormalize(
        segments, smooth, 3, 254, 104);
    uint64_t key = flecsEngine_cacheKey(
        normalized_segments, smooth, radius);
    FlecsGeometry3Cache *ctx = ecs_singleton_ensure(world, FlecsGeometry3Cache);

    ecs_entity_t asset = flecsEngine_findCachedAsset(&ctx->sphere_cache, key);
    if (asset) {
        return asset;
    }

    char asset_name[64];
    snprintf(
        asset_name,
        sizeof(asset_name),
        "Sphere.sphere%llu", key);

    asset = flecsEngine_geometry3_createAsset(world, ctx, asset_name);

    FlecsMesh3 *mesh = ecs_ensure(world, asset, FlecsMesh3);
    flecsEngine_sphere_generateMesh(mesh, normalized_segments, smooth, radius);
    ecs_modified(world, asset, FlecsMesh3);

    ecs_map_insert(
        &ctx->sphere_cache,
        (ecs_map_key_t)key,
        (ecs_map_val_t)asset);

    return asset;
}

void FlecsSphere_on_replace(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    const FlecsSphere *old = ecs_field(it, FlecsSphere, 0);
    const FlecsSphere *new = ecs_field(it, FlecsSphere, 1);
    const FlecsGeometry3Cache *ctx = ecs_singleton_get(world, FlecsGeometry3Cache);
    ecs_assert(ctx != NULL, ECS_INTERNAL_ERROR, NULL);

    for (int32_t i = 0; i < it->count; i ++) {
        int32_t old_segments = flecsEngine_segmentsNormalize(
            old[i].segments, old[i].smooth, 3, 254, 104);
        int32_t new_segments = flecsEngine_segmentsNormalize(
            new[i].segments, new[i].smooth, 3, 254, 104);
        uint64_t old_key = flecsEngine_cacheKey(
            old_segments, old[i].smooth, old[i].radius);
        uint64_t new_key = flecsEngine_cacheKey(
            new_segments, new[i].smooth, new[i].radius);

        if (old_key == new_key) {
            continue;
        }

        ecs_entity_t old_asset = flecsEngine_findCachedAsset(&ctx->sphere_cache, old_key);
        if (old_asset) {
            ecs_remove_pair(world, it->entities[i], EcsIsA, old_asset);
        }

        ecs_entity_t asset = flecsEngine_sphere_getAsset(
            world, new[i].segments, new[i].smooth, new[i].radius);
        ecs_add_pair(world, it->entities[i], EcsIsA, asset);
    }
}
