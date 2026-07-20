#define BIOME_CLOUD_IMPL

#include "biome.h"

#define CloudEmitScale (0.08f)
#define CloudVisibleFloor (0.04f)
#define CloudMaxPerCell (4)
#define CloudCapacity (65536)
#define CloudFadeIn (0.01f)
#define CloudFadeOut (0.3f)
#define CloudMinSize (5.0f)
#define CloudSizeVariance (3.0f)
#define CloudMinAge (15.0f)
#define CloudAgeVariance (25.0f)
#define CloudMaxDeltaTime (0.1f)
#define CloudAlpha (2)

static float cloudRandf(uint32_t *state, float max) {
    uint32_t s = *state;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *state = s;
    return max * (float)(s & 0xffffff) / (float)0x1000000;
}

static int32_t cloudGcd(int32_t a, int32_t b) {
    while (b) {
        int32_t next = a % b;
        a = b;
        b = next;
    }
    return a;
}

static int32_t cloudStride(uint32_t *state, int32_t cells) {
    int32_t stride = 1 +
        (int32_t)cloudRandf(state, (float)(cells - 1));
    while (cloudGcd(stride, cells) != 1) {
        stride ++;
        if (stride >= cells) {
            stride = 1;
        }
    }
    return stride;
}

void CloudEmit(ecs_iter_t *it) {
    ecs_assert(it->count == 1, ECS_INVALID_OPERATION, "can only have one terrain");

    ecs_world_t *world = it->world;
    ecs_entity_t e = it->entities[0];
    const FlecsTerrain *t = ecs_field(it, FlecsTerrain, 0);
    const Terrain *terrain = ecs_field(it, Terrain, 1);
    CloudEmitter *em = ecs_field(it, CloudEmitter, 2);

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
    float dt = it->delta_time;
    if (dt > CloudMaxDeltaTime) {
        dt = CloudMaxDeltaTime;
    }

    const FlecsParticles *pool = ecs_get(
        world, em->cloud_pool, FlecsParticles);
    int32_t cells = w * d;
    int32_t start = (int32_t)cloudRandf(
        &em->rand_state, (float)cells);
    int32_t stride = cloudStride(&em->rand_state, cells);
    for (int32_t j = 0; j < cells; j ++) {
        if (pool && pool->count >= pool->capacity) {
            break;
        }

        int32_t cell = (int32_t)(
            (start + (int64_t)j * stride) % cells);
        int32_t x = cell % w;
        int32_t z = cell / w;
        int32_t i = (z / air_scale) * air_w + x / air_scale;
        int32_t air_x = x / air_scale;
        int32_t air_z = z / air_scale;
        int32_t covered_width = w - air_x * air_scale;
        int32_t covered_depth = d - air_z * air_scale;
        if (covered_width > air_scale) {
            covered_width = air_scale;
        }
        if (covered_depth > air_scale) {
            covered_depth = air_scale;
        }
        float cw = air[i].water /
            (float)(covered_width * covered_depth);
        if (cw <= CloudVisibleFloor) {
            continue;
        }

        float expect = cw * CloudEmitScale * dt;
        int32_t count = (int32_t)expect;
        if (cloudRandf(&em->rand_state, 1.0f) < expect - (float)count) {
            count ++;
        }
        if (count > CloudMaxPerCell) {
            count = CloudMaxPerCell;
        }
        if (!count) {
            continue;
        }

        for (int32_t k = 0; k < count; k ++) {
            if (pool && pool->count >= pool->capacity) {
                break;
            }
            float size = CloudMinSize +
                cloudRandf(&em->rand_state, CloudSizeVariance);
            float y = (tp ? tp->y : 0) + terrain->max_height +
                CloudBaseHeight + size * 0.5f;

            flecsEngine_particlesEmit(world, em->cloud_pool,
                &(flecs_particle_t){
                    .pos = {
                        ((float)x + cloudRandf(&em->rand_state, 1.0f)) *
                            cs + ox,
                        y,
                        ((float)z + cloudRandf(&em->rand_state, 1.0f)) *
                            cs + oz },
                    .size = size,
                    .stretch = 1.0f,
                    .max_age = CloudMinAge +
                        cloudRandf(&em->rand_state, CloudAgeVariance),
                    .color = { 255, 255, 255, CloudAlpha }
                });
        }
    }
}

void biomeCloudImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeCloud);

    ECS_IMPORT(world, biomeWeather);

    ecs_set_name_prefix(world, "Cloud");

    ECS_META_COMPONENT(world, CloudEmitter);

    ecs_add_id(world, ecs_id(CloudEmitter), EcsSingleton);

    ecs_entity_t cloud_pool = ecs_entity(world, { .name = "fx_cloud" });
    ecs_set(world, cloud_pool, FlecsParticles, {
        .capacity = CloudCapacity,
        .size_envelope = {
            .mode = FlecsParticleEnvelopeEaseInOut,
            .fade_in = CloudFadeIn,
            .fade_out = CloudFadeOut
        }
    });

    ecs_singleton_set(world, CloudEmitter, {
        .cloud_pool = cloud_pool,
        .rand_state = 0x51ed270bu
    });

    ECS_SYSTEM(world, CloudEmit, EcsPreStore,
        [in]    FlecsTerrain,
        [in]    Terrain,
        [inout] CloudEmitter,
        [in]    Weather);
}
