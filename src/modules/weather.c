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
    if (ecs_vec_count(&t->layerTypes) <= TerrainAirIndex) {
        return;
    }

    const TerrainSoil *soil = flecsEngine_terrain_getLayer(world, e, TerrainSoilIndex, TerrainSoil);
    WeatherGroundTile *ground = flecsEngine_terrain_getLayer(world, e, TerrainGroundIndex, WeatherGroundTile);
    WeatherAirTile *air = flecsEngine_terrain_getLayer(world, e, TerrainAirIndex, WeatherAirTile);

    if (!soil || !ground || !air) {
        return;
    }

    int32_t air_w, air_d;
    flecsEngine_terrainLayerDimensions(t, TerrainAirIndex, &air_w, &air_d);
    int32_t scale = flecsEngine_terrainLayerScale(t, TerrainGroundIndex);
    if (air_w != w || air_d != d ||
        flecsEngine_terrainLayerScale(t, TerrainAirIndex) != scale)
    {
        ecs_err("weather ground and air layers must have the same scale");
        return;
    }

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
    ecs_vec_fini_t(NULL, &ptr->air_buffer, WeatherAirTile);
}

ECS_CTOR(WeatherBuffers, ptr, {
    ecs_os_zeromem(ptr);
    ecs_vec_init_t(NULL, &ptr->ground_buffer, WeatherGroundTile, 0);
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
