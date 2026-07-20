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
    // TODO
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
