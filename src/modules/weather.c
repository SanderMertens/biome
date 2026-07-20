#define BIOME_WEATHER_IMPL

#include "biome.h"

void WeatherInit(ecs_iter_t *it) {
    ecs_assert(it->count == 1, ECS_INVALID_OPERATION, "can only have one terrain");

    ecs_world_t *world = it->world;
    ecs_entity_t e = it->entities[0];

    const FlecsTerrain *t = ecs_field(it, FlecsTerrain, 0);
    const WeatherConfig *c = ecs_field(it, WeatherConfig, 1);
    WeatherBuffers *b = ecs_field(it, WeatherBuffers, 2);

    int32_t w, d;
    flecsEngine_terrainLayerDimensions(t, TerrainGroundIndex, &w, &d);
    if (w <= 0 || d <= 0) {
        return;
    }
    if (ecs_vec_count(&t->layerTypes) <= TerrainWaterIndex) {
        return;
    }

    const TerrainSoil *soil = flecsEngine_terrain_getLayer(world, e, TerrainSoilIndex, TerrainSoil);
    WeatherGroundTile *ground = flecsEngine_terrain_getLayer(world, e, TerrainGroundIndex, WeatherGroundTile);
    WeatherWaterTile *water = flecsEngine_terrain_getLayer(world, e, TerrainWaterIndex, WeatherWaterTile);
    WeatherAirTile *air = flecsEngine_terrain_getLayer(world, e, TerrainAirIndex, WeatherAirTile);

    if (!soil || !ground || !water || !air) {
        return;
    }

    int32_t air_w, air_d;
    int32_t water_w, water_d;
    flecsEngine_terrainLayerDimensions(t, TerrainAirIndex, &air_w, &air_d);
    flecsEngine_terrainLayerDimensions(
        t, TerrainWaterIndex, &water_w, &water_d);
    int32_t scale = flecsEngine_terrainLayerScale(t, TerrainGroundIndex);
    if (air_w != w || air_d != d ||
        flecsEngine_terrainLayerScale(t, TerrainAirIndex) != scale)
    {
        ecs_err("weather ground and air layers must have the same scale");
        return;
    }

    if (water_w != t->width || water_d != t->depth ||
        flecsEngine_terrainLayerScale(t, TerrainWaterIndex) != 1)
    {
        ecs_err("weather water layer must match terrain resolution");
        return;
    }

    for (int32_t z = 0; z < d; z ++) {
        for (int32_t x = 0; x < w; x ++) {
            int32_t i = z * w + x;
            float variation = (
                biomeFbm2((float)x * 0.13f + 17.0f,
                    (float)z * 0.13f + 31.0f, 3) - 0.5f) *
                2.0f * c->seed_variation;
            ground[i] = c->seed_ground;
            ground[i].temperature += variation;
            air[i] = c->seed_air;
            air[i].temperature += variation;
        }
    }

    for (int32_t z = 0; z < water_d; z ++) {
        for (int32_t x = 0; x < water_w; x ++) {
            int32_t i = z * water_w + x;
            float variation = (
                biomeFbm2((float)x * 0.0216667f + 17.0f,
                    (float)z * 0.0216667f + 31.0f, 3) - 0.5f) *
                2.0f * c->seed_variation;
            water[i] = c->seed_water;
            water[i].temperature += variation;
        }
    }

    ecs_vec_set_count_t(
        NULL, &b->ground_buffer, WeatherGroundTile, w * d);
    ecs_vec_set_count_t(
        NULL, &b->water_buffer, WeatherWaterTile, water_w * water_d);
    ecs_vec_set_count_t(
        NULL, &b->air_buffer, WeatherAirTile, w * d);
    ecs_os_memcpy_n(ecs_vec_first_t(&b->ground_buffer, WeatherGroundTile),
        ground, WeatherGroundTile, w * d);
    ecs_os_memcpy_n(ecs_vec_first_t(&b->water_buffer, WeatherWaterTile),
        water, WeatherWaterTile, water_w * water_d);
    ecs_os_memcpy_n(ecs_vec_first_t(&b->air_buffer, WeatherAirTile),
        air, WeatherAirTile, w * d);

    b->width = w;
    b->depth = d;
}

void WeatherUpdate(ecs_iter_t *it) {
    ecs_assert(it->count == 1, ECS_INVALID_OPERATION, "can only have one terrain");
}

static void WeatherBuffers_fini(
    WeatherBuffers *ptr)
{
    ecs_vec_fini_t(NULL, &ptr->ground_buffer, WeatherGroundTile);
    ecs_vec_fini_t(NULL, &ptr->water_buffer, WeatherWaterTile);
    ecs_vec_fini_t(NULL, &ptr->air_buffer, WeatherAirTile);
}

ECS_CTOR(WeatherBuffers, ptr, {
    ecs_os_zeromem(ptr);
    ecs_vec_init_t(NULL, &ptr->ground_buffer, WeatherGroundTile, 0);
    ecs_vec_init_t(NULL, &ptr->water_buffer, WeatherWaterTile, 0);
    ecs_vec_init_t(NULL, &ptr->air_buffer, WeatherAirTile, 0);
})

ECS_MOVE(WeatherBuffers, dst, src, {
    WeatherBuffers_fini(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_COPY(WeatherBuffers, dst, src, {
    WeatherBuffers_fini(dst);
    *dst = *src;
    dst->ground_buffer = ecs_vec_copy_t(
        NULL, &src->ground_buffer, WeatherGroundTile);
    dst->water_buffer = ecs_vec_copy_t(
        NULL, &src->water_buffer, WeatherWaterTile);
    dst->air_buffer = ecs_vec_copy_t(
        NULL, &src->air_buffer, WeatherAirTile);
})

ECS_DTOR(WeatherBuffers, ptr, {
    WeatherBuffers_fini(ptr);
})

void biomeWeatherImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeWeather);

    ECS_IMPORT(world, biomeTerrain);

    ecs_set_name_prefix(world, "Weather");

    ECS_META_COMPONENT(world, WeatherGroundTile);
    ECS_META_COMPONENT(world, WeatherWaterTile);
    ECS_META_COMPONENT(world, WeatherAirTile);
    ECS_META_COMPONENT(world, WeatherConfig);
    ECS_META_COMPONENT(world, WeatherBuffers);

    ecs_set_hooks(world, WeatherBuffers, {
        .ctor = ecs_ctor(WeatherBuffers),
        .move = ecs_move(WeatherBuffers),
        .copy = ecs_copy(WeatherBuffers),
        .dtor = ecs_dtor(WeatherBuffers)
    });

    ecs_add_id(world, ecs_id(WeatherConfig), EcsSingleton);
    ecs_add_pair(world, ecs_id(Terrain), EcsWith, ecs_id(WeatherBuffers));

    ECS_SYSTEM(world, WeatherInit, EcsOnStart,
        [inout] FlecsTerrain,
        [in]    WeatherConfig,
        [inout] WeatherBuffers);

    ECS_SYSTEM(world, WeatherUpdate, EcsPostUpdate,
        [inout] FlecsTerrain,
        [in]    WeatherConfig,
        [inout] WeatherBuffers);
}
