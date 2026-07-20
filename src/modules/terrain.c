#define BIOME_TERRAIN_IMPL

#include "biome.h"

ECS_COMPONENT_DECLARE(TerrainScatterAssets);

float biomeHash2(int32_t x, int32_t z) {
    uint32_t h = (uint32_t)x * 0x27d4eb2du;
    h ^= h >> 15;
    h += (uint32_t)z * 0x165667b1u;
    h ^= h >> 13;
    h *= 0x9e3779b1u;
    h ^= h >> 16;
    return (float)(h % 100000) / 100000.0f;
}

float biomeNoise2(float x, float z) {
    int32_t x0 = (int32_t)floorf(x), z0 = (int32_t)floorf(z);
    float fx = x - (float)x0, fz = z - (float)z0;
    fx = fx * fx * (3.0f - 2.0f * fx);
    fz = fz * fz * (3.0f - 2.0f * fz);
    float a = biomeHash2(x0, z0);
    float b = biomeHash2(x0 + 1, z0);
    float c = biomeHash2(x0, z0 + 1);
    float d = biomeHash2(x0 + 1, z0 + 1);
    float ab = a + (b - a) * fx;
    float cd = c + (d - c) * fx;
    return ab + (cd - ab) * fz;
}

float biomeFbm2(float x, float z, int32_t octaves) {
    float sum = 0, amp = 1.0f, freq = 1.0f, norm = 0;
    for (int32_t i = 0; i < octaves; i ++) {
        sum += biomeNoise2(x * freq, z * freq) * amp;
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return sum / norm;
}

float biomeTerrainHeightF(const Terrain *t, float fx, float fz) {
    float wx = fx / t->scale, wz = fz / t->scale;

    float qx = biomeFbm2(wx + 13.7f, wz + 71.3f, t->warp_octaves);
    float qz = biomeFbm2(wx + 47.2f, wz + 5.9f, t->warp_octaves);
    float h = biomeFbm2(
        wx + t->warp * (qx - 0.5f), wz + t->warp * (qz - 0.5f), t->octaves);

    float r = 1.0f - fabsf(2.0f * biomeFbm2(
        wx * t->ridge_freq + 29.0f, wz * t->ridge_freq + 83.0f,
        t->ridge_octaves) - 1.0f);
    h = h * (1.0f - t->ridge_mix) + r * r * t->ridge_mix;

    h = (h - t->height_offset) * t->height_gain + t->height_base;
    if (h < 0) h = 0;
    if (h > 1.0f) h = 1.0f;
    h = h * h * (3.0f - 2.0f * h);

    return h * t->max_height;
}

float biomeCornerHeight(const Terrain *t, int32_t x, int32_t z) {
    return biomeTerrainHeightF(t, (float)x - 0.5f, (float)z - 0.5f);
}

void FlecsTerrainOnSet(ecs_iter_t *it) {
    ecs_assert(it->count == 1, ECS_INVALID_OPERATION, "can only have one terrain");

    ecs_world_t *world = it->world;
    ecs_entity_t e = it->entities[0];
    FlecsTerrain *cfg = ecs_field(it, FlecsTerrain, 0);

    int32_t w = cfg->width, d = cfg->depth;
    if (w <= 0 || d <= 0) {
        return;
    }
    if (ecs_vec_count(&cfg->heights) != (w + 1) * (d + 1)) {
        return;
    }

    flecs_vec2_t *slope = flecsEngine_terrain_getLayer(world, e, TerrainSlopeIndex, flecs_vec2_t);
    if (!slope) {
        return;
    }

    flecsEngine_terrain_computeSlope(world, e, slope);

    TerrainSoil *soil = flecsEngine_terrain_getLayer(
        world, e, TerrainSoilIndex, TerrainSoil);
    const Terrain *terrain = ecs_get(world, e, Terrain);
    if (!soil || !terrain) {
        return;
    }

    float max_h = terrain->max_height > 0 ? terrain->max_height : 1.0f;
    for (int32_t z = 0; z < d; z ++) {
        for (int32_t x = 0; x < w; x ++) {
            int32_t i = z * w + x;
            float h = flecsEngine_terrainCellHeight(cfg, x, z);

            float n = biomeFbm2(
                (float)x * 0.06f + 11.3f, (float)z * 0.06f + 42.7f, 3);

            float altitude = h / max_h;
            if (altitude < 0) altitude = 0;
            if (altitude > 1.0f) altitude = 1.0f;

            float sand = (n - altitude) * 4.0f + 0.5f;
            if (sand < 0) sand = 0;
            if (sand > 1.0f) sand = 1.0f;

            float v = biomeFbm2(
                (float)x * 0.045f + 91.7f, (float)z * 0.045f + 23.1f, 3);
            float vein = 1.0f - fabsf(2.0f * v - 1.0f);
            vein = (vein - 0.9f) * 12.0f;
            if (vein < 0) vein = 0;
            if (vein > 1.0f) vein = 1.0f;

            soil[i].sedimentFactor = sand * (1.0f - vein);
            soil[i].fertility = 0;
        }
    }
}

void TerrainOnSet(ecs_iter_t *it) {
    ecs_assert(it->count == 1, ECS_INVALID_OPERATION, "can only have one terrain");

    ecs_world_t *world = it->world;
    ecs_entity_t e = it->entities[0];
    Terrain *cfg = ecs_field(it, Terrain, 0);

    float ex = (float)(cfg->width / 2) * TerrainCellSize;
    float ez = (float)(cfg->height / 2) * TerrainCellSize;
    ecs_set(world, e, FlecsPosition3, { -ex, 0, -ez });

    FlecsTerrain *t = ecs_ensure(world, e, FlecsTerrain);
    t->width = cfg->width;
    t->depth = cfg->height;
    t->cell_size = TerrainCellSize;

    int32_t cw = cfg->width + 1, ch = cfg->height + 1;
    ecs_vec_set_count_t(NULL, &t->heights, float, cw * ch);
    float *h = ecs_vec_first_t(&t->heights, float);
    for (int32_t z = 0; z < ch; z ++) {
        for (int32_t x = 0; x < cw; x ++) {
            h[z * cw + x] = biomeCornerHeight(cfg, x, z);
        }
    }

    int32_t cells = cfg->width * cfg->height;
    ecs_vec_set_count_t(NULL, &t->colors, flecs_rgba_t, cells);
    flecs_rgba_t *colors = ecs_vec_first_t(&t->colors, flecs_rgba_t);
    for (int32_t i = 0; i < cells; i ++) {
        colors[i] = (flecs_rgba_t){ 120, 96, 74, 230 };
    }

    ecs_vec_set_count_t(NULL, &t->layerTypes, ecs_entity_t, 6);
    ecs_entity_t *layerTypes = ecs_vec_first_t(&t->layerTypes, ecs_entity_t);
    layerTypes[TerrainSlopeIndex] = ecs_id(flecs_vec2_t);
    layerTypes[TerrainSoilIndex] = ecs_id(TerrainSoil);
    layerTypes[TerrainGroundIndex] = ecs_id(WeatherGroundTile);
    layerTypes[TerrainAirIndex] = ecs_id(WeatherAirTile);
    layerTypes[TerrainOccupancyIndex] = ecs_id(TerrainOccupancy);
    layerTypes[TerrainPowerIndex] = ecs_id(TerrainPower);

    ecs_vec_set_count_t(NULL, &t->layer_scale, int8_t, 6);
    int8_t *layer_scale = ecs_vec_first_t(&t->layer_scale, int8_t);
    for (int32_t i = 0; i < 6; i ++) {
        layer_scale[i] = 1;
    }

    layer_scale[TerrainGroundIndex] = TerrainWeatherScale;
    layer_scale[TerrainAirIndex] = TerrainWeatherScale;
    layer_scale[TerrainAirIndex] = TerrainWeatherScale;

    ecs_modified(world, e, FlecsTerrain);
}

static float biomeClampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void biomeMixColor(float rgb[3], const float target[3], float f) {
    rgb[0] += (target[0] - rgb[0]) * f;
    rgb[1] += (target[1] - rgb[1]) * f;
    rgb[2] += (target[2] - rgb[2]) * f;
}

static flecs_rgba_t biomeGroundColor(
    const WeatherGroundTile *g,
    const TerrainSoil *s)
{
    static const float rock[3] = {125, 125, 125};
    static const float sand[3] = {160, 120, 80};
    static const float grass[3] = {70, 110, 45};
    static const float liquid[3] = {30, 70, 140};
    static const float ice[3] = {235, 240, 245};

    float rgb[3] = {rock[0], rock[1], rock[2]};
    biomeMixColor(rgb, sand, biomeClampf(s->sedimentFactor, 0, 1.0f));
    biomeMixColor(rgb, grass, 0.6f * biomeClampf(s->fertility, 0, 1.0f));

    float m = biomeClampf(g->moisture_amount, 0, 1.0f);
    float darken = 1.0f - m * (0.35f + 0.3f * m);
    rgb[0] *= darken;
    rgb[1] *= darken;
    rgb[2] *= darken;

    float roughness = 230.0f;

    roughness = 255;

    return (flecs_rgba_t){
        (uint8_t)biomeClampf(rgb[0], 0, 255.0f),
        (uint8_t)biomeClampf(rgb[1], 0, 255.0f),
        (uint8_t)biomeClampf(rgb[2], 0, 255.0f),
        (uint8_t)biomeClampf(roughness, 0, 255.0f)
    };
}

static float biomeLerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static WeatherGroundTile biomeSampleGroundLinear(
    const WeatherGroundTile *ground,
    int32_t width,
    int32_t depth,
    int32_t scale,
    int32_t x,
    int32_t z)
{
    /* Layer values represent samples at the centers of their cells. Convert
     * the terrain cell center to that coordinate space before interpolating. */
    float fx = ((float)x + 0.5f) / (float)scale - 0.5f;
    float fz = ((float)z + 0.5f) / (float)scale - 0.5f;
    fx = biomeClampf(fx, 0, (float)(width - 1));
    fz = biomeClampf(fz, 0, (float)(depth - 1));

    int32_t x0 = (int32_t)floorf(fx);
    int32_t z0 = (int32_t)floorf(fz);
    int32_t x1 = x0 + 1 < width ? x0 + 1 : x0;
    int32_t z1 = z0 + 1 < depth ? z0 + 1 : z0;
    float tx = fx - (float)x0;
    float tz = fz - (float)z0;

    const WeatherGroundTile *g00 = &ground[z0 * width + x0];
    const WeatherGroundTile *g10 = &ground[z0 * width + x1];
    const WeatherGroundTile *g01 = &ground[z1 * width + x0];
    const WeatherGroundTile *g11 = &ground[z1 * width + x1];

#define BIOME_SAMPLE_FIELD(field) biomeLerp( \
    biomeLerp(g00->field, g10->field, tx), \
    biomeLerp(g01->field, g11->field, tx), tz)

    WeatherGroundTile result = {
        .temperature = BIOME_SAMPLE_FIELD(temperature),
        .moisture_amount = BIOME_SAMPLE_FIELD(moisture_amount)
    };

#undef BIOME_SAMPLE_FIELD

    return result;
}

void ApplyTerrainColors(ecs_iter_t *it) {
    ecs_assert(it->count == 1, ECS_INVALID_OPERATION, "can only have one terrain");

    ecs_world_t *world = it->world;
    ecs_entity_t e = it->entities[0];
    FlecsTerrain *t = ecs_field(it, FlecsTerrain, 0);
    const Terrain *cfg = ecs_get(world, e, Terrain);

    int32_t cells = t->width * t->depth;
    if (cells <= 0 || ecs_vec_count(&t->colors) != cells) {
        return;
    }
    if (ecs_vec_count(&t->layerTypes) <= TerrainGroundIndex) {
        return;
    }

    const WeatherGroundTile *ground = flecsEngine_terrain_getLayer(
        world, e, TerrainGroundIndex, WeatherGroundTile);
    const TerrainSoil *soil = flecsEngine_terrain_getLayer(
        world, e, TerrainSoilIndex, TerrainSoil);
    if (!ground || !soil) {
        return;
    }

    flecs_rgba_t *colors = ecs_vec_first_t(&t->colors, flecs_rgba_t);
    int32_t ground_w, ground_d;
    flecsEngine_terrainLayerDimensions(
        t, TerrainGroundIndex, &ground_w, &ground_d);
    int32_t ground_scale = flecsEngine_terrainLayerScale(
        t, TerrainGroundIndex);
    if (ground_w <= 0 || ground_d <= 0 || ground_scale <= 0) {
        return;
    }

    TerrainSampleKind sample_kind = cfg
        ? cfg->sample_kind
        : TerrainSampleKindNearest;
    for (int32_t z = 0; z < t->depth; z ++) {
        for (int32_t x = 0; x < t->width; x ++) {
            int32_t i = z * t->width + x;
            if (sample_kind == TerrainSampleKindLinear) {
                WeatherGroundTile sample = biomeSampleGroundLinear(
                    ground, ground_w, ground_d, ground_scale, x, z);
                colors[i] = biomeGroundColor(&sample, &soil[i]);
            } else {
                int32_t ground_x = x / ground_scale;
                int32_t ground_z = z / ground_scale;
                if (ground_x >= ground_w) ground_x = ground_w - 1;
                if (ground_z >= ground_d) ground_z = ground_d - 1;
                colors[i] = biomeGroundColor(
                    &ground[ground_z * ground_w + ground_x], &soil[i]);
            }
        }
    }

    flecsEngine_terrainColorsModified(world, e);
}

static uint32_t biomeScatterRandom(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static float biomeScatterVariance(uint32_t *state, float variance) {
    if (variance <= 0) {
        return 0;
    }

    float unit = (float)biomeScatterRandom(state) / 4294967296.0f;
    return (unit * 2.0f - 1.0f) * variance;
}

typedef struct biome_scatter_candidate_t {
    int32_t cell;
    float score;
} biome_scatter_candidate_t;

static int biomeScatterCompareCandidate(const void *ptr_a, const void *ptr_b) {
    const biome_scatter_candidate_t *a = ptr_a;
    const biome_scatter_candidate_t *b = ptr_b;
    if (a->score < b->score) return -1;
    if (a->score > b->score) return 1;
    return (a->cell > b->cell) - (a->cell < b->cell);
}

static void biomeScatterClusterCells(
    int32_t *available,
    int32_t available_count,
    int32_t width,
    int32_t depth,
    float cluster,
    float cluster_scale,
    uint32_t *random)
{
    if (cluster <= 0 || available_count <= 1) {
        return;
    }
    if (cluster > 1) {
        cluster = 1;
    }
    if (!(cluster_scale > 0)) {
        cluster_scale = (float)(width > depth ? width : depth) / 8.0f;
        if (cluster_scale < 1) {
            cluster_scale = 1;
        }
    }

    float noise_x = (float)biomeScatterRandom(random) /
        4194304.0f;
    float noise_y = (float)biomeScatterRandom(random) /
        4194304.0f;
    float random_weight = 1.0f - cluster;

    biome_scatter_candidate_t *candidates = ecs_os_malloc_n(
        biome_scatter_candidate_t, available_count);
    for (int32_t i = 0; i < available_count; i ++) {
        int32_t cell = available[i];
        float x = ((float)(cell % width) + 0.5f) / cluster_scale;
        float y = ((float)(cell / width) + 0.5f) / cluster_scale;
        float noise_score = 1.0f - biomeFbm2(
            x + noise_x, y + noise_y, 3);
        float random_score = (float)biomeScatterRandom(random) /
            4294967296.0f;
        candidates[i] = (biome_scatter_candidate_t){
            cell,
            random_score * random_weight + noise_score * cluster
        };
    }

    qsort(candidates, available_count, sizeof(biome_scatter_candidate_t),
        biomeScatterCompareCandidate);
    for (int32_t i = 0; i < available_count; i ++) {
        available[i] = candidates[i].cell;
    }
    ecs_os_free(candidates);
}

static void TerrainScatterOnSet(ecs_iter_t *it) {
    ecs_world_t *world = it->world;
    TerrainScatter *scatter = ecs_field(it, TerrainScatter, 0);

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t terrain = it->entities[i];
        if (!ecs_has(world, terrain, FlecsTerrain)) {
            terrain = ecs_id(Terrain);
        }
        const FlecsTerrain *t = ecs_get(world, terrain, FlecsTerrain);
        if (!t || t->width <= 0 || t->depth <= 0 ||
            scatter[i].count <= 0)
        {
            continue;
        }

        int32_t prefab_count = ecs_vec_count(&scatter[i].prefab);
        ecs_entity_t *prefabs = ecs_vec_first_t(
            &scatter[i].prefab, ecs_entity_t);
        ecs_entity_t *valid_prefabs = ecs_os_malloc_n(
            ecs_entity_t, prefab_count);
        int32_t valid_prefab_count = 0;
        for (int32_t p = 0; p < prefab_count; p ++) {
            if (prefabs[p] && ecs_is_alive(world, prefabs[p])) {
                valid_prefabs[valid_prefab_count ++] = prefabs[p];
            }
        }
        if (!valid_prefab_count) {
            ecs_os_free(valid_prefabs);
            continue;
        }

        int64_t cell_count_64 = (int64_t)t->width * t->depth;
        if (cell_count_64 > INT32_MAX ||
            ecs_vec_count(&t->layerTypes) <= TerrainOccupancyIndex)
        {
            ecs_os_free(valid_prefabs);
            continue;
        }

        const TerrainOccupancy *occupancy = flecsEngine_terrain_getLayer(
            world, terrain, TerrainOccupancyIndex, TerrainOccupancy);
        if (!occupancy) {
            ecs_os_free(valid_prefabs);
            continue;
        }

        int32_t cell_count = (int32_t)cell_count_64;
        int32_t *available = ecs_os_malloc_n(int32_t, cell_count);
        int32_t available_count = 0;
        for (int32_t cell = 0; cell < cell_count; cell ++) {
            if (!occupancy[cell].buildings) {
                available[available_count ++] = cell;
            }
        }

        int32_t spawn_count = scatter[i].count;
        if (spawn_count > available_count) {
            spawn_count = available_count;
        }

        /* Use a local PRNG so scattering is reproducible and does not perturb
         * random number generation in other systems. */
        uint32_t selection_random = 0;
        selection_random ^= (uint32_t)t->width * 0x85ebca6bu;
        selection_random ^= (uint32_t)t->depth * 0xc2b2ae35u;
        selection_random ^= (uint32_t)valid_prefabs[0];
        for (int32_t p = 1; p < valid_prefab_count; p ++) {
            selection_random ^= (uint32_t)valid_prefabs[p] *
                (0x85ebca6bu + (uint32_t)p * 0xc2b2ae35u);
        }
        if (!selection_random) {
            selection_random = 1;
        }
        uint32_t variance_random = selection_random ^ 0xa511e9b3u;
        uint32_t prefab_random = selection_random ^ 0x63d83595u;
        float cluster = scatter[i].cluster;
        if (!(cluster > 0)) {
            cluster = 0;
        }
        biomeScatterClusterCells(available, available_count,
            t->width, t->depth, cluster, scatter[i].cluster_scale,
            &selection_random);

        bool was_deferred = ecs_is_deferred(world);
        if (was_deferred) {
            ecs_defer_suspend(world);
        }

        for (int32_t n = 0; n < spawn_count; n ++) {
            int32_t cell;
            if (cluster > 0) {
                cell = available[n];
            } else {
                uint32_t remaining = (uint32_t)(available_count - n);
                int32_t selected = n + (int32_t)(
                    biomeScatterRandom(&selection_random) % remaining);
                cell = available[selected];
                available[selected] = available[n];
            }

            int32_t x = cell % t->width;
            int32_t y = cell / t->width;
            float offset_x = biomeScatterVariance(
                &variance_random, scatter[i].position_variance.x);
            float offset_z = biomeScatterVariance(
                &variance_random, scatter[i].position_variance.y);
            float rotation_x = biomeScatterVariance(
                &variance_random, scatter[i].rotation_variance.x);
            float rotation_y = biomeScatterVariance(
                &variance_random, scatter[i].rotation_variance.y);
            float scale_x = biomeScatterVariance(
                &variance_random, scatter[i].scale_variance.x);
            float scale_y = biomeScatterVariance(
                &variance_random, scatter[i].scale_variance.y);
            float scale_z = biomeScatterVariance(
                &variance_random, scatter[i].scale_variance.z);

            ecs_entity_t prefab = valid_prefabs[
                biomeScatterRandom(&prefab_random) %
                    (uint32_t)valid_prefab_count];
            ecs_entity_t instance = ecs_new_w_pair(
                world, EcsIsA, prefab);
            ecs_add_pair(world, instance, EcsChildOf, terrain);
            ecs_set(world, instance, FlecsTerrainPosition, {
                .terrain = terrain,
                .x = x,
                .y = y,
                .yaw = rotation_y
            });

            if (offset_x || offset_z) {
                float px = ((float)x + 0.5f) * t->cell_size + offset_x;
                float pz = ((float)y + 0.5f) * t->cell_size + offset_z;
                ecs_set(world, instance, FlecsPosition3, {
                    px, flecsEngine_terrainSampleHeight(t, px, pz), pz
                });
            }
            if (rotation_x) {
                ecs_set(world, instance, FlecsRotation3, {
                    rotation_x, rotation_y, 0
                });
            }
            if (scale_x || scale_y || scale_z) {
                FlecsScale3 base_scale = {1, 1, 1};
                const FlecsScale3 *prefab_scale = ecs_get(
                    world, prefab, FlecsScale3);
                if (prefab_scale) {
                    base_scale = *prefab_scale;
                }
                ecs_set(world, instance, FlecsScale3, {
                    base_scale.x + scale_x,
                    base_scale.y + scale_y,
                    base_scale.z + scale_z
                });
            }
        }

        if (was_deferred) {
            ecs_defer_resume(world);
        }

        ecs_os_free(available);
        ecs_os_free(valid_prefabs);
    }
}

void biomeTerrainImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeTerrain);

    ecs_set_name_prefix(world, "Terrain");
    ECS_META_COMPONENT(world, TerrainSampleKind);

    ecs_set_name_prefix(world, NULL);
    ECS_META_COMPONENT(world, Terrain);

    ecs_set_name_prefix(world, "Terrain");

    ECS_META_COMPONENT(world, TerrainSoil);
    ECS_META_COMPONENT(world, TerrainOccupancy);
    ecs_id(TerrainScatterAssets) = ecs_vector(world, {
        .entity = ecs_entity(world, {
            .name = "ScatterAssets",
            .symbol = "TerrainScatterAssets"
        }),
        .type = ecs_id(ecs_entity_t)
    });
    ECS_META_COMPONENT(world, TerrainScatter);

    ecs_set_hooks(world, Terrain, {
        .ctor = flecs_default_ctor,
        .on_set = TerrainOnSet
    });

    ecs_observer(world, {
        .query.terms = {{ ecs_id(FlecsTerrain) }},
        .events = { EcsOnSet },
        .callback = FlecsTerrainOnSet
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(TerrainScatter) }},
        .events = { EcsOnSet },
        .callback = TerrainScatterOnSet
    });

    ECS_SYSTEM(world, ApplyTerrainColors, EcsPreStore,
        [inout] FlecsTerrain);
}
