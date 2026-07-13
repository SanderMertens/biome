#ifndef FLECS_ENGINE_GEOMETRY3_MATH_H
#define FLECS_ENGINE_GEOMETRY3_MATH_H

#include <math.h>

static inline flecs_vec3_t flecsEngine_vec3_sub(
    flecs_vec3_t a,
    flecs_vec3_t b)
{
    return (flecs_vec3_t){a.x - b.x, a.y - b.y, a.z - b.z};
}

static inline flecs_vec3_t flecsEngine_vec3_cross(
    flecs_vec3_t a,
    flecs_vec3_t b)
{
    return (flecs_vec3_t){
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

static inline float flecsEngine_vec3_dot(
    flecs_vec3_t a,
    flecs_vec3_t b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline flecs_vec3_t flecsEngine_vec3_normalize(
    flecs_vec3_t v,
    flecs_vec3_t fallback)
{
    float len_sq = v.x * v.x + v.y * v.y + v.z * v.z;
    if (len_sq <= 1e-20f) {
        return fallback;
    }

    float inv_len = 1.0f / sqrtf(len_sq);
    return (flecs_vec3_t){v.x * inv_len, v.y * inv_len, v.z * inv_len};
}

static inline flecs_vec3_t flecsEngine_triangleNormal(
    flecs_vec3_t a,
    flecs_vec3_t b,
    flecs_vec3_t c,
    flecs_vec3_t default_fallback)
{
    flecs_vec3_t centroid = {
        (a.x + b.x + c.x) / 3.0f,
        (a.y + b.y + c.y) / 3.0f,
        (a.z + b.z + c.z) / 3.0f
    };
    flecs_vec3_t fallback = flecsEngine_vec3_normalize(
        centroid, default_fallback);

    flecs_vec3_t edge_ab = flecsEngine_vec3_sub(b, a);
    flecs_vec3_t edge_ac = flecsEngine_vec3_sub(c, a);
    flecs_vec3_t normal = flecsEngine_vec3_normalize(
        flecsEngine_vec3_cross(edge_ab, edge_ac), fallback);

    /* Ensure flat-shaded normals always point outwards. */
    if (flecsEngine_vec3_dot(normal, centroid) < 0.0f) {
        normal = (flecs_vec3_t){-normal.x, -normal.y, -normal.z};
    }

    return normal;
}

static inline int32_t flecsEngine_segmentsNormalize(
    int32_t segments,
    bool smooth,
    int32_t min_segments,
    int32_t max_smooth,
    int32_t max_flat)
{
    int32_t max_segments = smooth ? max_smooth : max_flat;
    if (segments < min_segments) {
        return min_segments;
    }
    if (segments > max_segments) {
        return max_segments;
    }
    return segments;
}

static inline uint32_t flecsEngine_floatBits(float f)
{
    union { float f; uint32_t u; } bits = { .f = f };
    return bits.u;
}

#define FLECS_GEOMETRY3_CACHE_SEGMENTS_MASK (0x7fffffffULL)
#define FLECS_GEOMETRY3_CACHE_SMOOTH_MASK   (1ULL << 63)

static inline uint64_t flecsEngine_cacheKey(
    int32_t segments,
    bool smooth,
    float extra)
{
    uint64_t key =
        (((uint64_t)segments & FLECS_GEOMETRY3_CACHE_SEGMENTS_MASK) << 32) |
        (uint64_t)flecsEngine_floatBits(extra);

    if (smooth) {
        key |= FLECS_GEOMETRY3_CACHE_SMOOTH_MASK;
    }

    return key;
}

static inline uint64_t flecsEngine_cacheKeySimple(
    int32_t segments,
    bool smooth)
{
    uint64_t key =
        ((uint64_t)segments & FLECS_GEOMETRY3_CACHE_SEGMENTS_MASK) << 32;

    if (smooth) {
        key |= FLECS_GEOMETRY3_CACHE_SMOOTH_MASK;
    }

    return key;
}

static inline ecs_entity_t flecsEngine_findCachedAsset(
    const ecs_map_t *cache,
    uint64_t key)
{
    ecs_map_val_t *entry = ecs_map_get(cache, (ecs_map_key_t)key);
    if (!entry) {
        return 0;
    }
    return (ecs_entity_t)entry[0];
}

#endif
