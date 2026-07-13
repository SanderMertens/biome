#define BIOME_PRECIPITATION_IMPL

#include "biome.h"

#define PrecipEmitScale (100.0f)
#define PrecipMaxPerCell (1)
#define PrecipEmitFloor (0.0f)
#define PrecipEmitHeight (CloudBaseHeight)
#define PrecipEmitHeightVariance (3.0f)
#define PrecipRainFallSpeed (6.0f)
#define PrecipSnowFallSpeed (2.0f)
#define PrecipRainCapacity (4096)
#define PrecipSnowCapacity (4096)
#define PrecipMaxSlant (0.0f)
#define PrecipFade (0.1f)

static float precipRandf(uint32_t *state, float max) {
    uint32_t s = *state;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *state = s;
    return max * (float)(s & 0xffffff) / (float)0x1000000;
}

void PrecipitationEmit(ecs_iter_t *it) {
    ecs_assert(it->count == 1, ECS_INVALID_OPERATION, "can only have one terrain");

    ecs_world_t *world = it->world;
    ecs_entity_t e = it->entities[0];
    const FlecsTerrain *t = ecs_field(it, FlecsTerrain, 0);
    PrecipitationEmitter *em = ecs_field(it, PrecipitationEmitter, 2);

    int32_t w = t->width, d = t->depth;
    if (w <= 0 || d <= 0) {
        return;
    }
    if (ecs_vec_count(&t->layerTypes) <= TerrainAirIndex) {
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

    float cs = t->cell_size > 0 ? t->cell_size : 1.0f;

    const float *heights = NULL;
    if (ecs_vec_count(&t->heights) == (w + 1) * (d + 1)) {
        heights = ecs_vec_first_t(&t->heights, float);
    }
    FlecsParticleGround ground_col = {
        .width = w,
        .depth = d,
        .cell_size = cs,
        .origin = { ox, oz },
        .corner_heights = heights
    };
    *ecs_ensure(world, em->rain_pool, FlecsParticleGround) = ground_col;
    *ecs_ensure(world, em->snow_pool, FlecsParticleGround) = ground_col;

    for (int32_t z = 0; z < d; z ++) {
        for (int32_t x = 0; x < w; x ++) {
            int32_t i = (z / air_scale) * air_w + x / air_scale;
            float p = air[i].precipitation;
            if (p <= PrecipEmitFloor) {
                continue;
            }

            float expect = p * PrecipEmitScale;
            int32_t count = (int32_t)expect;
            if (precipRandf(&em->rand_state, 1.0f) < expect - (float)count) {
                count ++;
            }
            if (count > PrecipMaxPerCell) {
                count = PrecipMaxPerCell;
            }
            if (!count) {
                continue;
            }

            bool snow = air[i].temperature <= 0;
            float fall = snow ? PrecipSnowFallSpeed : PrecipRainFallSpeed;
            float h = flecsEngine_terrainCellHeight(t, x, z);

            float wx = air[i].wind_velocity.x;
            float wz = air[i].wind_velocity.z;
            float wind_mag = sqrtf(wx * wx + wz * wz);
            float max_slant = PrecipMaxSlant * fall;
            if (wind_mag > max_slant) {
                wx *= max_slant / wind_mag;
                wz *= max_slant / wind_mag;
            }

            for (int32_t k = 0; k < count; k ++) {
                float y = h + PrecipEmitHeight -
                    precipRandf(&em->rand_state, PrecipEmitHeightVariance);

                flecsEngine_particlesEmit(world,
                    snow ? em->snow_pool : em->rain_pool,
                    &(flecs_particle_t){
                        .pos = {
                            ((float)x + precipRandf(&em->rand_state, 1.0f)) *
                                cs + ox,
                            y,
                            ((float)z + precipRandf(&em->rand_state, 1.0f)) *
                                cs + oz },
                        .vel = { wx, -fall, wz },
                        .size = snow ? 0.09f : 0.05f,
                        .stretch = snow ? 1.0f : 9.0f,
                        .max_age = y / fall + 1.0f,
                        .color = snow ?
                            (FlecsRgba){ 235, 240, 248, 235 } :
                            (FlecsRgba){ 120, 170, 235, 200 }
                    });
            }
        }
    }
}

void biomePrecipitationImport(ecs_world_t *world) {
    ECS_MODULE(world, biomePrecipitation);

    ecs_set_name_prefix(world, "Precipitation");

    ECS_META_COMPONENT(world, PrecipitationEmitter);

    ecs_add_id(world, ecs_id(PrecipitationEmitter), EcsSingleton);

    ecs_entity_t rain_pool = ecs_entity(world, { .name = "fx_rain" });
    ecs_set(world, rain_pool, FlecsParticles, {
        .capacity = PrecipRainCapacity,
        .alpha_envelope = {
            .mode = FlecsParticleEnvelopeLinear,
            .fade_out = PrecipFade
        }
    });

    ecs_entity_t snow_pool = ecs_entity(world, { .name = "fx_snow" });
    ecs_set(world, snow_pool, FlecsParticles, {
        .capacity = PrecipSnowCapacity,
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
        [in]    WeatherConfig,
        [inout] PrecipitationEmitter,
        [none]  WeatherBuffers);
}
