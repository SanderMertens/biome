#define BIOME_PRECIPITATION_IMPL

#include "biome.h"

#define PrecipEmitScale (100.0f)
#define PrecipMaxPerCell (1)
#define PrecipEmitFloor (0.0f)
#define PrecipEmitHeight (CloudBaseHeight)
#define PrecipEmitHeightVariance (3.0f)
#define PrecipRainFallSpeed (6.0f)
#define PrecipSnowFallSpeed (2.0f)
#define PrecipRainCapacity (65536)
#define PrecipSnowCapacity (65536)
#define PrecipMaxSlant (2.0f)
#define PrecipFade (0.1f)

static float precipRandf(uint32_t *state, float max) {
    uint32_t s = *state;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *state = s;
    return max * (float)(s & 0xffffff) / (float)0x1000000;
}

static int32_t precipGcd(int32_t a, int32_t b) {
    while (b) {
        int32_t next = a % b;
        a = b;
        b = next;
    }
    return a;
}

static int32_t precipStride(uint32_t *state, int32_t cells) {
    int32_t stride = 1 +
        (int32_t)precipRandf(state, (float)(cells - 1));
    while (precipGcd(stride, cells) != 1) {
        stride ++;
        if (stride >= cells) {
            stride = 1;
        }
    }
    return stride;
}

void PrecipitationEmit(ecs_iter_t *it) {
    ecs_assert(it->count == 1, ECS_INVALID_OPERATION, "can only have one terrain");

    ecs_world_t *world = it->world;
    ecs_entity_t e = it->entities[0];
    const FlecsTerrain *t = ecs_field(it, FlecsTerrain, 0);
    const Terrain *terrain = ecs_field(it, Terrain, 1);
    const Weather *weather = ecs_field(it, Weather, 2);
    PrecipitationEmitter *em = ecs_field(it, PrecipitationEmitter, 3);
    if (!weather->condensation.enabled || it->delta_time <= 0) {
        return;
    }

    int32_t width = t->width;
    int32_t depth = t->depth;
    int32_t air_width;
    flecsEngine_terrainLayerDimensions(
        t, TerrainAirIndex, &air_width, NULL);
    int32_t air_scale = flecsEngine_terrainLayerScale(
        t, TerrainAirIndex);
    const WeatherAirTile *air = flecsEngine_terrain_getLayer(
        world, e, TerrainAirIndex, WeatherAirTile);
    if (!air || width <= 0 || depth <= 0 ||
        air_width <= 0 || air_scale <= 0)
    {
        return;
    }

    FlecsParticles *rain = ecs_get_mut(
        world, em->rain_pool, FlecsParticles);
    FlecsParticles *snow = ecs_get_mut(
        world, em->snow_pool, FlecsParticles);
    const FlecsPosition3 *tp = ecs_get(world, e, FlecsPosition3);
    float ox = tp ? tp->x : 0;
    float oz = tp ? tp->z : 0;
    float cell_size = t->cell_size > 0 ? t->cell_size : 1.0f;
    float dt = it->delta_time;
    if (dt > 0.1f) {
        dt = 0.1f;
    }

    int32_t cells = width * depth;
    int32_t start = (int32_t)precipRandf(
        &em->rand_state, (float)cells);
    int32_t stride = precipStride(&em->rand_state, cells);
    for (int32_t j = 0; j < cells; j ++) {
        if (rain && snow &&
            rain->count >= rain->capacity &&
            snow->count >= snow->capacity)
        {
            break;
        }

        int32_t cell = (int32_t)(
            (start + (int64_t)j * stride) % cells);
        int32_t x = cell % width;
        int32_t z = cell / width;
        const WeatherAirTile *tile = &air[
            (z / air_scale) * air_width + x / air_scale];
        int32_t air_x = x / air_scale;
        int32_t air_z = z / air_scale;
        int32_t covered_width = width - air_x * air_scale;
        int32_t covered_depth = depth - air_z * air_scale;
        if (covered_width > air_scale) {
            covered_width = air_scale;
        }
        if (covered_depth > air_scale) {
            covered_depth = air_scale;
        }
        float rate = tile->precipitation_rate /
            (float)(covered_width * covered_depth);
        if (rate <= PrecipEmitFloor) {
            continue;
        }

        float expected = rate * PrecipEmitScale * dt;
        int32_t count = (int32_t)expected;
        if (precipRandf(&em->rand_state, 1.0f) <
            expected - (float)count)
        {
            count ++;
        }
        if (count > PrecipMaxPerCell) {
            count = PrecipMaxPerCell;
        }

        float ground = flecsEngine_terrainCellHeight(t, x, z);
        for (int32_t p = 0; p < count; p ++) {
            bool is_snow = tile->temperature <= 0;
            FlecsParticles *pool = is_snow ? snow : rain;
            if (!pool || pool->count >= pool->capacity) {
                continue;
            }

            float vx = tile->wind_velocity.x;
            float vz = tile->wind_velocity.z;
            float horizontal = sqrtf(vx * vx + vz * vz);
            if (horizontal > PrecipMaxSlant) {
                float scale = PrecipMaxSlant / horizontal;
                vx *= scale;
                vz *= scale;
            }
            float fall_speed = is_snow
                ? PrecipSnowFallSpeed
                : PrecipRainFallSpeed;
            float y = (tp ? tp->y : 0) + terrain->max_height +
                PrecipEmitHeight +
                precipRandf(
                    &em->rand_state, PrecipEmitHeightVariance);
            flecs_particle_t particle = {
                .pos = {
                    ((float)x + precipRandf(
                        &em->rand_state, 1.0f)) * cell_size + ox,
                    y,
                    ((float)z + precipRandf(
                        &em->rand_state, 1.0f)) * cell_size + oz
                },
                .vel = { vx, -fall_speed, vz },
                .size = is_snow ? 0.18f : 0.06f,
                .stretch = is_snow ? 1.0f : 10.0f,
                .max_age = (y - ground) / fall_speed + PrecipFade,
                .color = is_snow
                    ? (FlecsRgba){ 240, 245, 255, 220 }
                    : (FlecsRgba){ 150, 190, 255, 180 }
            };
            if (!is_snow) {
                particle.axis[0] = vx;
                particle.axis[1] = -fall_speed;
                particle.axis[2] = vz;
            }
            flecsEngine_particlesEmit(
                world,
                is_snow ? em->snow_pool : em->rain_pool,
                &particle);
        }
    }
}

void biomePrecipitationImport(ecs_world_t *world) {
    ECS_MODULE(world, biomePrecipitation);

    ECS_IMPORT(world, biomeWeather);

    ecs_set_name_prefix(world, "Precipitation");

    ECS_META_COMPONENT(world, PrecipitationEmitter);

    ecs_add_id(world, ecs_id(PrecipitationEmitter), EcsSingleton);

    ecs_entity_t rain_pool = ecs_entity(world, { .name = "fx_rain" });
    ecs_set(world, rain_pool, FlecsParticles, {
        .capacity = PrecipRainCapacity,
        .sort_mode = FlecsParticleSortNone,
        .alpha_envelope = {
            .mode = FlecsParticleEnvelopeLinear,
            .fade_out = PrecipFade
        }
    });

    ecs_entity_t snow_pool = ecs_entity(world, { .name = "fx_snow" });
    ecs_set(world, snow_pool, FlecsParticles, {
        .capacity = PrecipSnowCapacity,
        .sort_mode = FlecsParticleSortNone,
        .alpha_envelope = {
            .mode = FlecsParticleEnvelopeLinear,
            .fade_out = PrecipFade
        }
    });

    ecs_singleton_set(world, PrecipitationEmitter, {
        .rain_pool = rain_pool,
        .snow_pool = snow_pool,
        .rand_state = 0x9e3779b9u
    });

    ECS_SYSTEM(world, PrecipitationEmit, EcsPreStore,
        [in]    FlecsTerrain,
        [in]    Terrain,
        [in]    Weather,
        [inout] PrecipitationEmitter,
        [in]    WeatherBuffers);
}
