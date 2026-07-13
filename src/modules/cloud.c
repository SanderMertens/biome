#define BIOME_CLOUD_IMPL

#include "biome.h"

#define CloudEmitScale (0.08f)
#define CloudVisibleFloor (0.04f)
#define CloudMaxPerCell (4)
#define CloudCapacity (65536)
#define CloudFadeIn (0.01f)
#define CloudFadeOut (0.3f)
#define CloudThickness (5.0f)
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

void CloudEmit(ecs_iter_t *it) {
    ecs_assert(it->count == 1, ECS_INVALID_OPERATION, "can only have one terrain");

    ecs_world_t *world = it->world;
    ecs_entity_t e = it->entities[0];
    const FlecsTerrain *t = ecs_field(it, FlecsTerrain, 0);
    CloudEmitter *em = ecs_field(it, CloudEmitter, 1);

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

    for (int32_t z = 0; z < d; z ++) {
        for (int32_t x = 0; x < w; x ++) {
            int32_t i = (z / air_scale) * air_w + x / air_scale;
            float cw = air[i].cloud_water + air[i].precipitation;
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

            float h = flecsEngine_terrainCellHeight(t, x, z);

            for (int32_t k = 0; k < count; k ++) {
                float size = CloudMinSize +
                    cloudRandf(&em->rand_state, CloudSizeVariance);
                float y = h + CloudBaseHeight + size * 0.5f +
                    cloudRandf(&em->rand_state, CloudThickness);

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
}

void biomeCloudImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeCloud);

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
        [inout] CloudEmitter,
        [none]  WeatherBuffers);
}
