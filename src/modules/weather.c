#define BIOME_WEATHER_IMPL

#include "biome.h"
#include "evaporation.h"
#include "radiative_balance.h"
#include "thermal_exchange.h"
#include "weather_aggregate.h"
#include "weather_ocean.h"

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

    float water_density = 997.0f;
    if (ecs_id(WaterConfig)) {
        const WaterConfig *water_config = ecs_singleton_get(
            world, WaterConfig);
        if (water_config && water_config->density > 0) {
            water_density = water_config->density;
        }
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
            float terrain_height = flecsEngine_terrainCellHeight(t, x, z);
            water[i].water_amount = biomeWeatherOceanWaterAmount(
                water[i].water_amount, terrain_height, c->ocean_level,
                water_density);
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

    ecs_world_t *world = it->world;
    ecs_entity_t e = it->entities[0];
    const FlecsTerrain *t = ecs_field(it, FlecsTerrain, 0);
    const Weather *weather = ecs_field(it, Weather, 1);
    const WeatherInfiltration *config = &weather->infiltration;
    if (!config->enabled || config->max_infiltration_rate <= 0 ||
        config->ground_moisture_capacity <= 0 || it->delta_time <= 0)
    {
        return;
    }

    int32_t ground_w;
    int32_t ground_d;
    int32_t water_w;
    int32_t water_d;
    flecsEngine_terrainLayerDimensions(
        t, TerrainGroundIndex, &ground_w, &ground_d);
    flecsEngine_terrainLayerDimensions(
        t, TerrainWaterIndex, &water_w, &water_d);
    if (ground_w <= 0 || ground_d <= 0 ||
        water_w != t->width || water_d != t->depth)
    {
        return;
    }

    WeatherGroundTile *ground = flecsEngine_terrain_getLayer(
        world, e, TerrainGroundIndex, WeatherGroundTile);
    WeatherWaterTile *water = flecsEngine_terrain_getLayer(
        world, e, TerrainWaterIndex, WeatherWaterTile);
    const TerrainSoil *soil = flecsEngine_terrain_getLayer(
        world, e, TerrainSoilIndex, TerrainSoil);
    if (!ground || !water || !soil) {
        return;
    }

    int32_t ground_scale = flecsEngine_terrainLayerScale(
        t, TerrainGroundIndex);
    float capacity = config->ground_moisture_capacity;
    float max_infiltration = config->max_infiltration_rate;
    float dt = it->delta_time;
    for (int32_t z = 0; z < water_d; z ++) {
        int32_t ground_z = z / ground_scale;
        if (ground_z >= ground_d) {
            ground_z = ground_d - 1;
        }
        for (int32_t x = 0; x < water_w; x ++) {
            int32_t i = z * water_w + x;
            float surface_water = water[i].water_amount;
            if (surface_water <= 0 || water[i].temperature <= 0) {
                continue;
            }

            int32_t ground_x = x / ground_scale;
            if (ground_x >= ground_w) {
                ground_x = ground_w - 1;
            }
            WeatherGroundTile *ground_tile = &ground[
                ground_z * ground_w + ground_x];
            float available_capacity = capacity -
                ground_tile->moisture_amount;
            if (available_capacity <= 0) {
                continue;
            }

            float sediment_factor = soil[i].sedimentFactor;
            if (sediment_factor <= 0) {
                continue;
            }
            float saturation = ground_tile->moisture_amount / capacity;
            float infiltration_rate = sediment_factor * max_infiltration *
                (1.0f - saturation);
            if (infiltration_rate <= 0) {
                continue;
            }

            float infiltrated = infiltration_rate * dt;
            if (infiltrated > surface_water) {
                infiltrated = surface_water;
            }
            if (infiltrated > available_capacity) {
                infiltrated = available_capacity;
            }
            water[i].water_amount -= infiltrated;
            ground_tile->moisture_amount += infiltrated;
        }
    }
}

void ThermalExchange(ecs_iter_t *it) {
    ecs_assert(it->count == 1, ECS_INVALID_OPERATION, "can only have one terrain");

    ecs_world_t *world = it->world;
    ecs_entity_t e = it->entities[0];
    const FlecsTerrain *t = ecs_field(it, FlecsTerrain, 0);
    const Weather *weather = ecs_field(it, Weather, 1);
    WeatherBuffers *buffers = ecs_field(it, WeatherBuffers, 2);
    float rate = weather->thermal_exchange.rate;
    if (!weather->thermal_exchange.enabled || rate <= 0 ||
        it->delta_time <= 0)
    {
        return;
    }

    int32_t ground_w;
    int32_t ground_d;
    int32_t water_w;
    int32_t water_d;
    flecsEngine_terrainLayerDimensions(
        t, TerrainGroundIndex, &ground_w, &ground_d);
    flecsEngine_terrainLayerDimensions(
        t, TerrainWaterIndex, &water_w, &water_d);
    if (ground_w <= 0 || ground_d <= 0 ||
        water_w <= 0 || water_d <= 0)
    {
        return;
    }

    WeatherGroundTile *ground = flecsEngine_terrain_getLayer(
        world, e, TerrainGroundIndex, WeatherGroundTile);
    WeatherWaterTile *water = flecsEngine_terrain_getLayer(
        world, e, TerrainWaterIndex, WeatherWaterTile);
    WeatherAirTile *air = flecsEngine_terrain_getLayer(
        world, e, TerrainAirIndex, WeatherAirTile);
    if (!ground || !water || !air) {
        return;
    }

    int32_t ground_count = ground_w * ground_d;
    int32_t water_count = water_w * water_d;
    ecs_vec_set_count_t(
        NULL, &buffers->ground_buffer, WeatherGroundTile, ground_count);
    ecs_vec_set_count_t(
        NULL, &buffers->water_buffer, WeatherWaterTile, water_count);
    ecs_vec_set_count_t(
        NULL, &buffers->air_buffer, WeatherAirTile, ground_count);

    WeatherGroundTile *next_ground = ecs_vec_first_t(
        &buffers->ground_buffer, WeatherGroundTile);
    WeatherWaterTile *next_water = ecs_vec_first_t(
        &buffers->water_buffer, WeatherWaterTile);
    WeatherAirTile *next_air = ecs_vec_first_t(
        &buffers->air_buffer, WeatherAirTile);

    biomeGroundThermalExchange(
        ground, next_ground, ground_w, ground_d,
        biomeThermalExchangeFactor(0.05f * rate, it->delta_time));
    biomeWaterThermalExchange(
        water, next_water, water_w, water_d,
        biomeThermalExchangeFactor(0.1f * rate, it->delta_time));
    biomeAirThermalExchange(
        air, next_air, ground_w, ground_d,
        biomeThermalExchangeFactor(0.2f * rate, it->delta_time));

    ecs_os_memcpy_n(ground, next_ground, WeatherGroundTile, ground_count);
    ecs_os_memcpy_n(water, next_water, WeatherWaterTile, water_count);
    ecs_os_memcpy_n(air, next_air, WeatherAirTile, ground_count);
}

void Evaporation(ecs_iter_t *it) {
    ecs_assert(it->count == 1, ECS_INVALID_OPERATION, "can only have one terrain");

    ecs_world_t *world = it->world;
    ecs_entity_t e = it->entities[0];
    const FlecsTerrain *t = ecs_field(it, FlecsTerrain, 0);
    const Weather *weather = ecs_field(it, Weather, 1);
    const WeatherEvaporation *config = &weather->evaporation;
    float dt = it->delta_time;
    if (!config->enabled || dt <= 0 ||
        (config->surface_water_rate <= 0 &&
         config->ground_moisture_rate <= 0))
    {
        return;
    }

    int32_t ground_w;
    int32_t ground_d;
    int32_t water_w;
    int32_t water_d;
    flecsEngine_terrainLayerDimensions(
        t, TerrainGroundIndex, &ground_w, &ground_d);
    flecsEngine_terrainLayerDimensions(
        t, TerrainWaterIndex, &water_w, &water_d);
    int32_t scale = flecsEngine_terrainLayerScale(t, TerrainGroundIndex);
    if (ground_w <= 0 || ground_d <= 0 || water_w <= 0 ||
        water_d <= 0 || scale <= 0)
    {
        return;
    }

    WeatherGroundTile *ground = flecsEngine_terrain_getLayer(
        world, e, TerrainGroundIndex, WeatherGroundTile);
    WeatherWaterTile *water = flecsEngine_terrain_getLayer(
        world, e, TerrainWaterIndex, WeatherWaterTile);
    WeatherAirTile *air = flecsEngine_terrain_getLayer(
        world, e, TerrainAirIndex, WeatherAirTile);
    if (!ground || !water || !air) {
        return;
    }

    float cell_size = t->cell_size > 0 ? t->cell_size : 1.0f;
    float cell_area = cell_size * cell_size;
    float capacity = weather->infiltration.ground_moisture_capacity;
    float cooling = config->evaporative_cooling;
    float gravity = weather->gravity;
    if (gravity <= 0) {
        return;
    }

    for (int32_t gz = 0; gz < ground_d; gz ++) {
        for (int32_t gx = 0; gx < ground_w; gx ++) {
            int32_t gi = gz * ground_w + gx;
            WeatherGroundTile *ground_tile = &ground[gi];
            WeatherAirTile *air_tile = &air[gi];

            int32_t min_x = gx * scale;
            int32_t min_z = gz * scale;
            int32_t max_x = min_x + scale;
            int32_t max_z = min_z + scale;
            if (max_x > water_w) {
                max_x = water_w;
            }
            if (max_z > water_d) {
                max_z = water_d;
            }
            float air_area = cell_area *
                (float)((max_x - min_x) * (max_z - min_z));
            float atmospheric_pressure = biomeAtmosphericPressure(
                air_tile, air_area, gravity);
            float air_energy_share = biomeEvaporationAirEnergyShare(
                atmospheric_pressure);
            float surface_energy_share = 1.0f - air_energy_share;
            float vapor_capacity = biomeSaturationVaporCapacity(
                air_tile->temperature, air_area, gravity);
            float vapor_available = vapor_capacity -
                air_tile->vapor_amount;
            float humidity_factor = biomeEvaporationHumidityFactor(
                air_tile->vapor_amount, air_tile->temperature, air_area,
                gravity);
            if (humidity_factor <= 0 || vapor_available <= 0) {
                continue;
            }

            float surface_evaporated = 0;
            int32_t exposed_cells = 0;
            for (int32_t z = min_z; z < max_z; z ++) {
                for (int32_t x = min_x; x < max_x; x ++) {
                    WeatherWaterTile *water_tile = &water[z * water_w + x];
                    if (water_tile->water_amount <= 0) {
                        exposed_cells ++;
                        continue;
                    }
                    float amount = config->surface_water_rate * cell_area *
                        dt * humidity_factor *
                        biomeEvaporationTemperatureFactor(
                            water_tile->temperature);
                    if (amount > water_tile->water_amount) {
                        amount = water_tile->water_amount;
                    }
                    float surface_cooling = cooling *
                        surface_energy_share;
                    float energy_limit = biomeEvaporationEnergyLimit(
                        water_tile->temperature,
                        water_tile->water_amount *
                            BiomeEvaporationWaterHeatCapacity,
                        surface_cooling);
                    if (amount > energy_limit) {
                        amount = energy_limit;
                    }
                    if (amount > vapor_available - surface_evaporated) {
                        amount = vapor_available - surface_evaporated;
                    }
                    if (amount <= 0) {
                        continue;
                    }
                    float original_amount = water_tile->water_amount;
                    water_tile->water_amount -= amount;
                    water_tile->temperature -= biomeEvaporativeCooling(
                        amount,
                        original_amount *
                            BiomeEvaporationWaterHeatCapacity,
                        surface_cooling);
                    surface_evaporated += amount;
                }
            }

            float ground_evaporated = 0;
            if (ground_tile->moisture_amount > 0 && exposed_cells > 0 &&
                config->ground_moisture_rate > 0)
            {
                float moisture_factor = capacity > 0
                    ? ground_tile->moisture_amount / capacity
                    : 1.0f;
                if (moisture_factor > 1.0f) {
                    moisture_factor = 1.0f;
                }
                float exposed_area = cell_area * (float)exposed_cells;
                ground_evaporated = config->ground_moisture_rate *
                    exposed_area * dt * humidity_factor * moisture_factor *
                    biomeEvaporationTemperatureFactor(
                        ground_tile->temperature);
                if (ground_evaporated > ground_tile->moisture_amount) {
                    ground_evaporated = ground_tile->moisture_amount;
                }
                float surface_cooling = cooling *
                    surface_energy_share;
                float ground_heat_capacity = exposed_area *
                    BiomeEvaporationGroundHeatCapacity;
                float energy_limit = biomeEvaporationEnergyLimit(
                    ground_tile->temperature,
                    ground_heat_capacity,
                    surface_cooling);
                if (ground_evaporated > energy_limit) {
                    ground_evaporated = energy_limit;
                }
                float remaining_vapor = vapor_available -
                    surface_evaporated;
                if (ground_evaporated > remaining_vapor) {
                    ground_evaporated = remaining_vapor;
                }
                ground_tile->moisture_amount -= ground_evaporated;
                ground_tile->temperature -= biomeEvaporativeCooling(
                    ground_evaporated,
                    ground_heat_capacity,
                    surface_cooling);
            }

            float evaporated = surface_evaporated + ground_evaporated;
            air_tile->vapor_amount += evaporated;
            float atmospheric_mass = biomeAtmosphericMass(air_tile);
            air_tile->temperature -= biomeEvaporativeCooling(
                evaporated,
                atmospheric_mass * BiomeEvaporationAirHeatCapacity,
                cooling * air_energy_share);
        }
    }
}

void RadiativeBalance(ecs_iter_t *it) {
    ecs_assert(it->count == 1, ECS_INVALID_OPERATION, "can only have one terrain");

    ecs_world_t *world = it->world;
    ecs_entity_t e = it->entities[0];
    const FlecsTerrain *t = ecs_field(it, FlecsTerrain, 0);
    const Weather *weather = ecs_field(it, Weather, 1);
    const WeatherRadiativeBalance *config = &weather->radiative_balance;
    if (!config->enabled || it->delta_time <= 0) {
        return;
    }

    int32_t ground_width;
    int32_t ground_depth;
    int32_t water_width;
    int32_t water_depth;
    flecsEngine_terrainLayerDimensions(
        t, TerrainGroundIndex, &ground_width, &ground_depth);
    flecsEngine_terrainLayerDimensions(
        t, TerrainWaterIndex, &water_width, &water_depth);
    if (ground_width <= 0 || ground_depth <= 0) {
        return;
    }

    WeatherGroundTile *ground = flecsEngine_terrain_getLayer(
        world, e, TerrainGroundIndex, WeatherGroundTile);
    WeatherWaterTile *water = flecsEngine_terrain_getLayer(
        world, e, TerrainWaterIndex, WeatherWaterTile);
    const WeatherAirTile *air = flecsEngine_terrain_getLayer(
        world, e, TerrainAirIndex, WeatherAirTile);
    if (!ground || !air) {
        return;
    }

    biomeRadiativeBalanceSurface(
        ground,
        water,
        air,
        ground_width,
        ground_depth,
        water_width,
        water_depth,
        flecsEngine_terrainLayerScale(t, TerrainGroundIndex),
        weather->stellar_intensity,
        config,
        it->delta_time);
}

void WeatherComputeAggregate(ecs_iter_t *it) {
    ecs_assert(it->count == 1, ECS_INVALID_OPERATION, "can only have one terrain");

    ecs_world_t *world = it->world;
    ecs_entity_t e = it->entities[0];
    const FlecsTerrain *t = ecs_field(it, FlecsTerrain, 0);
    WeatherAggregate *aggregate = ecs_field(it, WeatherAggregate, 1);
    const Weather *weather = ecs_field(it, Weather, 2);

    int32_t ground_width;
    int32_t ground_depth;
    int32_t water_width;
    int32_t water_depth;
    int32_t air_width;
    int32_t air_depth;
    flecsEngine_terrainLayerDimensions(
        t, TerrainGroundIndex, &ground_width, &ground_depth);
    flecsEngine_terrainLayerDimensions(
        t, TerrainWaterIndex, &water_width, &water_depth);
    flecsEngine_terrainLayerDimensions(
        t, TerrainAirIndex, &air_width, &air_depth);

    const WeatherGroundTile *ground = flecsEngine_terrain_getLayer(
        world, e, TerrainGroundIndex, WeatherGroundTile);
    const WeatherWaterTile *water = flecsEngine_terrain_getLayer(
        world, e, TerrainWaterIndex, WeatherWaterTile);
    const WeatherAirTile *air = flecsEngine_terrain_getLayer(
        world, e, TerrainAirIndex, WeatherAirTile);

    biomeWeatherComputeAggregate(
        ground, ground_width * ground_depth,
        water, water_width * water_depth,
        air, air_width * air_depth,
        aggregate);
    float cell_size = t->cell_size > 0 ? t->cell_size : 1.0f;
    biomeWeatherComputePressureAggregate(
        air,
        air_width,
        air_depth,
        t->width,
        t->depth,
        flecsEngine_terrainLayerScale(t, TerrainAirIndex),
        cell_size * cell_size,
        weather->gravity,
        &aggregate->air.pressure);
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
    ECS_META_COMPONENT(world, WeatherInfiltration);
    ECS_META_COMPONENT(world, WeatherThermalExchange);
    ECS_META_COMPONENT(world, WeatherEvaporation);
    ECS_META_COMPONENT(world, WeatherRadiativeBalance);
    ECS_META_COMPONENT(world, Weather);
    ECS_META_COMPONENT(world, WeatherGroundAggregate);
    ECS_META_COMPONENT(world, WeatherWaterAggregate);
    ECS_META_COMPONENT(world, WeatherFloatAggregate);
    ECS_META_COMPONENT(world, WeatherAirAggregate);
    ECS_META_COMPONENT(world, WeatherAggregate);
    ECS_META_COMPONENT(world, WeatherBuffers);

    ecs_set_hooks(world, WeatherBuffers, {
        .ctor = ecs_ctor(WeatherBuffers),
        .move = ecs_move(WeatherBuffers),
        .copy = ecs_copy(WeatherBuffers),
        .dtor = ecs_dtor(WeatherBuffers)
    });

    ecs_add_id(world, ecs_id(WeatherConfig), EcsSingleton);
    ecs_add_id(world, ecs_id(Weather), EcsSingleton);
    ecs_add_id(world, ecs_id(WeatherAggregate), EcsSingleton);
    ecs_singleton_set(world, WeatherAggregate, {0});
    ecs_add_pair(world, ecs_id(Terrain), EcsWith, ecs_id(WeatherBuffers));

    ECS_SYSTEM(world, WeatherInit, EcsOnStart,
        [inout] FlecsTerrain,
        [in]    WeatherConfig,
        [inout] WeatherBuffers);

    ECS_SYSTEM(world, WeatherUpdate, EcsPostUpdate,
        [inout] FlecsTerrain,
        [in]    Weather,
        [inout] WeatherBuffers);

    ECS_SYSTEM(world, ThermalExchange, EcsPostUpdate,
        [inout] FlecsTerrain,
        [in]    Weather,
        [inout] WeatherBuffers);

    ECS_SYSTEM(world, Evaporation, EcsPostUpdate,
        [inout] FlecsTerrain,
        [in]    Weather,
        [none]  WeatherBuffers);

    ECS_SYSTEM(world, RadiativeBalance, EcsPostUpdate,
        [inout] FlecsTerrain,
        [in]    Weather);

    ECS_SYSTEM(world, WeatherComputeAggregate, EcsPreStore,
        [in]  FlecsTerrain,
        [out] WeatherAggregate,
        [in]  Weather);
}
