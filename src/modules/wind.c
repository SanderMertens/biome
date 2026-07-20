#define BIOME_WIND_IMPL

#include "biome.h"

#define WindSpeedThreshold (0.05f)
#define WindEmitChance (0.5f)
#define WindEmitBiasSpeed (4.0f)
#define WindColdTemperature (-30.0f)
#define WindHotTemperature (30.0f)
#define WindMinAge (1.2f)
#define WindAgeVariance (1.0f)
#define WindHeightOffset (1.0f)
#define WindHeightVariance (0.8f)
#define WindMinHeight (0.3f)
#define WindBaseLength (0.4f)
#define WindLengthPerSpeed (0.15f)
#define WindFollowRate (2.0f)
#define WindMaxDeltaTime (0.1f)
#define WindEmissiveStrength (2.0f)
#define WindGrowTime (0.35f)
#define WindShrinkTime (0.5f)
#define WindWidth (0.12f)

static float windRandf(uint32_t *state, float max) {
    uint32_t s = *state;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *state = s;
    return max * (float)(s & 0xffffff) / (float)0x1000000;
}

static FlecsRgba windColor(float temperature) {
    float f = (temperature - WindColdTemperature) /
        (WindHotTemperature - WindColdTemperature);
    if (f < 0) f = 0;
    if (f > 1.0f) f = 1.0f;

    float r, g, b;
    if (f < 0.5f) {
        float m = f / 0.5f;
        r = 70.0f + (215.0f - 70.0f) * m;
        g = 130.0f + (215.0f - 130.0f) * m;
        b = 255.0f + (215.0f - 255.0f) * m;
    } else {
        float m = (f - 0.5f) / 0.5f;
        r = 215.0f + (210.0f - 215.0f) * m;
        g = 215.0f + (55.0f - 215.0f) * m;
        b = 215.0f + (45.0f - 215.0f) * m;
    }

    return (FlecsRgba){ (uint8_t)r, (uint8_t)g, (uint8_t)b, 255 };
}

static float windEnvelope(const flecs_particle_t *pt) {
    float grow = pt->age / WindGrowTime;
    float shrink = (pt->max_age - pt->age) / WindShrinkTime;
    float envelope = grow < shrink ? grow : shrink;
    if (envelope < 0.01f) envelope = 0.01f;
    if (envelope > 1.0f) envelope = 1.0f;
    return envelope;
}

static void windShape(flecs_particle_t *pt) {
    float speed = sqrtf(pt->vel[0] * pt->vel[0] +
        pt->vel[1] * pt->vel[1] + pt->vel[2] * pt->vel[2]);
    if (speed > 1e-4f) {
        pt->axis[0] = pt->vel[0] / speed;
        pt->axis[1] = pt->vel[1] / speed;
        pt->axis[2] = pt->vel[2] / speed;
    }
    pt->size = WindWidth * windEnvelope(pt);
    pt->stretch = (WindBaseLength + WindLengthPerSpeed * speed) / WindWidth;
}

void WindEmit(ecs_iter_t *it) {
    ecs_assert(it->count == 1, ECS_INVALID_OPERATION, "can only have one terrain");

    ecs_world_t *world = it->world;
    ecs_entity_t e = it->entities[0];
    const FlecsTerrain *t = ecs_field(it, FlecsTerrain, 0);
    WindState *ws = ecs_field(it, WindState, 1);
    const Weather *weather = ecs_field(it, Weather, 2);
    if (!weather->wind.enabled) {
        return;
    }

    int32_t w = t->width, d = t->depth;
    int32_t cells = w * d;
    if (cells <= 0 || ecs_vec_count(&t->layerTypes) <= TerrainAirIndex) {
        return;
    }

    const WeatherAirTile *air = flecsEngine_terrain_getLayer(
        world, e, TerrainAirIndex, WeatherAirTile);
    if (!air) {
        return;
    }
    int32_t air_w;
    flecsEngine_terrainLayerDimensions(
        t, TerrainAirIndex, &air_w, NULL);
    int32_t air_scale = flecsEngine_terrainLayerScale(t, TerrainAirIndex);

    const FlecsPosition3 *tp = ecs_get(world, e, FlecsPosition3);
    float ox = tp ? tp->x : 0;
    float oz = tp ? tp->z : 0;

    const FlecsParticles *pool = ecs_get(world, ws->pool, FlecsParticles);

    float cs = t->cell_size > 0 ? t->cell_size : 1.0f;
    float dt = it->delta_time;
    if (dt > WindMaxDeltaTime) dt = WindMaxDeltaTime;

    int32_t start = (int32_t)windRandf(&ws->rand_state, (float)cells);
    for (int32_t j = 0; j < cells; j ++) {
        if (pool && pool->count >= WindParticleCap) {
            break;
        }

        int32_t i = (start + j) % cells;
        int32_t x = i % w, z = i / w;
        int32_t ai = (z / air_scale) * air_w + x / air_scale;
        const WeatherAirTile *a = &air[ai];
        float mag = sqrtf(a->wind_velocity.x * a->wind_velocity.x +
            a->wind_velocity.z * a->wind_velocity.z);
        if (mag < WindSpeedThreshold) {
            continue;
        }
        float emit_rate = mag * (mag / WindEmitBiasSpeed);
        if (windRandf(&ws->rand_state, 1.0f) >= emit_rate * WindEmitChance * dt) {
            continue;
        }

        flecs_particle_t pt = {
            .pos = {
                ((float)x + windRandf(&ws->rand_state, 1.0f)) * cs + ox,
                flecsEngine_terrainCellHeight(t, x, z) + WindHeightOffset +
                    windRandf(&ws->rand_state, WindHeightVariance),
                ((float)z + windRandf(&ws->rand_state, 1.0f)) * cs + oz
            },
            .vel = {
                a->wind_velocity.x,
                a->wind_velocity.y,
                a->wind_velocity.z
            },
            .max_age = WindMinAge + windRandf(&ws->rand_state, WindAgeVariance),
            .color = windColor(a->temperature)
        };
        windShape(&pt);

        flecsEngine_particlesEmit(world, ws->pool, &pt);
    }
}

void WindParticleUpdate(ecs_iter_t *it) {
    ecs_assert(it->count == 1, ECS_INVALID_OPERATION, "can only have one terrain");

    ecs_world_t *world = it->world;
    ecs_entity_t e = it->entities[0];
    const FlecsTerrain *t = ecs_field(it, FlecsTerrain, 0);
    WindState *ws = ecs_field(it, WindState, 1);
    const Weather *weather = ecs_field(it, Weather, 2);
    if (!weather->wind.enabled) {
        return;
    }

    FlecsParticles *p = ecs_get_mut(world, ws->pool, FlecsParticles);
    if (!p || !p->count) {
        return;
    }

    const WeatherAirTile *air = flecsEngine_terrain_getLayer(
        world, e, TerrainAirIndex, WeatherAirTile);
    if (!air) {
        return;
    }

    const FlecsPosition3 *tp = ecs_get(world, e, FlecsPosition3);
    float ox = tp ? tp->x : 0;
    float oz = tp ? tp->z : 0;

    int32_t w = t->width, d = t->depth;
    int32_t air_w;
    flecsEngine_terrainLayerDimensions(
        t, TerrainAirIndex, &air_w, NULL);
    int32_t air_scale = flecsEngine_terrainLayerScale(t, TerrainAirIndex);
    float cs = t->cell_size > 0 ? t->cell_size : 1.0f;
    float dt = it->delta_time;
    if (dt > WindMaxDeltaTime) dt = WindMaxDeltaTime;

    for (int32_t i = 0; i < p->count; i ++) {
        flecs_particle_t *pt = &p->particles[i];

        int32_t x = (int32_t)floorf((pt->pos[0] - ox) / cs);
        int32_t z = (int32_t)floorf((pt->pos[2] - oz) / cs);
        if (x < 0 || x >= w || z < 0 || z >= d) {
            p->particles[i] = p->particles[-- p->count];
            p->stat_expired ++;
            i --;
            continue;
        }

        const WeatherAirTile *a = &air[
            (z / air_scale) * air_w + x / air_scale];
        float follow = WindFollowRate * dt;
        if (follow > 1.0f) follow = 1.0f;
        pt->vel[0] += (a->wind_velocity.x - pt->vel[0]) * follow;
        pt->vel[1] += (a->wind_velocity.y - pt->vel[1]) * follow;
        pt->vel[2] += (a->wind_velocity.z - pt->vel[2]) * follow;

        float min_y = flecsEngine_terrainCellHeight(t, x, z) + WindMinHeight;
        if (pt->pos[1] < min_y) {
            pt->pos[1] = min_y;
        }

        pt->color = windColor(a->temperature);
        windShape(pt);
    }
}

void biomeWindImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeWind);

    ECS_IMPORT(world, biomeWeather);

    ecs_set_name_prefix(world, "Wind");

    ECS_META_COMPONENT(world, WindState);

    ecs_entity_t pool = ecs_entity(world, { .name = "fx_wind" });
    ecs_set(world, pool, FlecsParticles, {
        .capacity = WindParticleCap,
        .emissive = WindEmissiveStrength
    });

    ecs_add_id(world, ecs_id(WindState), EcsSingleton);
    ecs_singleton_set(world, WindState, {
        .pool = pool,
        .rand_state = 0x2545f491u
    });

    ECS_SYSTEM(world, WindEmit, EcsPreStore,
        [in]    FlecsTerrain,
        [inout] WindState,
        [in]    Weather);

    ECS_SYSTEM(world, WindParticleUpdate, EcsPreStore,
        [in]    FlecsTerrain,
        [inout] WindState,
        [in]    Weather);
}
