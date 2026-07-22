#define BIOME_PLANT_IMPL

#include "biome.h"

#define BiomePlantDecayChunk (1024)

typedef struct BiomePlantSpreadContext {
    ecs_map_t claimed;
    int64_t frame;
} BiomePlantSpreadContext;

static void biomePlantSpreadContextFree(void *ptr) {
    BiomePlantSpreadContext *ctx = ptr;
    ecs_map_fini(&ctx->claimed);
    ecs_os_free(ctx);
}

typedef struct BiomePlantDecayContext {
    int32_t frame;
} BiomePlantDecayContext;

static void biomePlantDecayContextFree(void *ptr) {
    ecs_os_free(ptr);
}

static bool biome_plant_tileHasPlant(
    const ecs_world_t *world,
    int32_t x,
    int32_t y)
{
    const TerrainItemRecord *record = biome_terrainItemIndex_get(
        world, x, y);
    if (!record) {
        return false;
    }

    int32_t count = ecs_vec_count(&record->entities);
    const ecs_entity_t *entities = ecs_vec_first_t(
        &record->entities, ecs_entity_t);
    for (int32_t i = 0; i < count; i ++) {
        if (ecs_is_alive(world, entities[i]) &&
            ecs_has(world, entities[i], BiomePlant))
        {
            return true;
        }
    }

    return false;
}

static void BiomePlantDecayFertility(ecs_iter_t *it) {
    ecs_world_t *world = it->world;
    const FlecsTerrain *terrains = ecs_field(it, FlecsTerrain, 0);
    const Terrain *cfgs = ecs_field(it, Terrain, 1);

    for (int32_t i = 0; i < it->count; i ++) {
        const FlecsTerrain *t = &terrains[i];
        const Terrain *cfg = &cfgs[i];
        int32_t cells = t->width * t->depth;
        if (cfg->fertility_decay <= 0 || cells <= 0) {
            continue;
        }

        TerrainSoil *soil = flecsEngine_terrain_getLayer(
            world, it->entities[i], TerrainSoilIndex, TerrainSoil);
        if (!soil) {
            continue;
        }

        int32_t frames = (cells + BiomePlantDecayChunk - 1) /
            BiomePlantDecayChunk;
        BiomePlantDecayContext *ctx = it->ctx;
        int32_t frame = ctx->frame % frames;
        int32_t begin = (int32_t)((int64_t)cells * frame / frames);
        int32_t end = (int32_t)((int64_t)cells * (frame + 1) / frames);
        ctx->frame = (frame + 1) % frames;

        float decay = cfg->fertility_decay * (float)frames;
        for (int32_t cell = begin; cell < end; cell ++) {
            if (soil[cell].fertility <= 0) {
                continue;
            }
            if (biome_plant_tileHasPlant(
                world, cell % t->width, cell / t->width))
            {
                continue;
            }
            soil[cell].fertility -= decay;
            if (soil[cell].fertility < 0) {
                soil[cell].fertility = 0;
            }
        }
    }
}

static bool biome_plant_cellAvailable(
    ecs_world_t *world,
    const FlecsTerrain *t,
    const Terrain *cfg,
    const TerrainSoil *soil,
    const TerrainGround *ground,
    const TerrainOccupancy *occupancy,
    const BiomePlant *plant,
    int32_t x,
    int32_t y)
{
    if (x < 0 || x >= t->width || y < 0 || y >= t->depth) {
        return false;
    }

    int32_t cell = y * t->width + x;
    if (soil[cell].fertility < plant->min_fertility ||
        ground[cell].moisture < plant->min_moisture)
    {
        return false;
    }
    if (occupancy && occupancy[cell].buildings) {
        return false;
    }
    if (cfg && flecsEngine_terrainCellHeight(t, x, y) <=
        cfg->water_level)
    {
        return false;
    }

    const TerrainItemRecord *record = biome_terrainItemIndex_get(
        world, x, y);
    if (record && ecs_vec_count(&record->entities)) {
        return false;
    }

    return true;
}

static void biome_plant_spread(
    ecs_world_t *world,
    ecs_entity_t entity,
    const BiomePlant *plant,
    const FlecsTerrainPosition *tp,
    const FlecsTerrain *t,
    const TerrainSoil *soil,
    const TerrainGround *ground,
    BiomePlantSpreadContext *ctx)
{
    ecs_entity_t prefab = ecs_get_target(world, entity, EcsIsA, 0);
    if (!prefab) {
        return;
    }

    const Terrain *cfg = ecs_get(world, tp->terrain, Terrain);
    const TerrainOccupancy *occupancy = flecsEngine_terrain_getLayer(
        world, tp->terrain, TerrainOccupancyIndex, TerrainOccupancy);

    for (int32_t dy = -1; dy <= 1; dy ++) {
        for (int32_t dx = -1; dx <= 1; dx ++) {
            if (!dx && !dy) {
                continue;
            }
            int32_t x = tp->x + dx;
            int32_t y = tp->y + dy;
            if (!biome_plant_cellAvailable(world, t, cfg, soil,
                ground, occupancy, plant, x, y))
            {
                continue;
            }

            ecs_map_key_t pos = (ecs_map_key_t)
                biome_terrainItemIndex_pos(x, y);
            if (ecs_map_get(&ctx->claimed, pos)) {
                continue;
            }
            ecs_map_ensure(&ctx->claimed, pos);

            ecs_entity_t instance = ecs_new_w_pair(
                world, EcsIsA, prefab);
            ecs_add_pair(world, instance, EcsChildOf, tp->terrain);
            ecs_set(world, instance, FlecsTerrainPosition, {
                .terrain = tp->terrain,
                .x = x,
                .y = y,
                .yaw = biomeHash2(x, y) * 6.2831853f
            });
        }
    }
}

static void BiomePlantUpdate(ecs_iter_t *it) {
    ecs_world_t *world = it->world;
    const BiomePlant *plants = ecs_field(it, BiomePlant, 0);
    BiomePlantState *states = ecs_field(it, BiomePlantState, 1);
    const FlecsTerrainPosition *positions = ecs_field(
        it, FlecsTerrainPosition, 2);

    const Weather *weather = ecs_singleton_get(world, Weather);
    float temperature = weather ? weather->temperature : 0;

    BiomePlantSpreadContext *ctx = it->ctx;
    const ecs_world_info_t *info = ecs_get_world_info(world);
    if (ctx->frame != info->frame_count_total) {
        ctx->frame = info->frame_count_total;
        ecs_map_clear(&ctx->claimed);
    }

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t entity = it->entities[i];
        const BiomePlant *plant = &plants[i];
        const FlecsTerrainPosition *tp = &positions[i];
        if (!tp->terrain || !ecs_is_alive(world, tp->terrain)) {
            continue;
        }

        const FlecsTerrain *t = ecs_get(
            world, tp->terrain, FlecsTerrain);
        if (!t || ecs_vec_count(&t->layerTypes) <= TerrainGroundIndex) {
            continue;
        }
        if (tp->x < 0 || tp->x >= t->width ||
            tp->y < 0 || tp->y >= t->depth)
        {
            continue;
        }

        const TerrainSoil *soil = flecsEngine_terrain_getLayer(
            world, tp->terrain, TerrainSoilIndex, TerrainSoil);
        const TerrainGround *ground = flecsEngine_terrain_getLayer(
            world, tp->terrain, TerrainGroundIndex, TerrainGround);
        if (!soil || !ground) {
            continue;
        }

        int32_t cell = tp->y * t->width + tp->x;
        bool needs_met =
            temperature >= plant->min_temperature &&
            temperature <= plant->max_temperature &&
            ground[cell].moisture >= plant->min_moisture &&
            soil[cell].fertility >= plant->min_fertility;
        if (needs_met && ecs_has(world, entity, BiomeFactory) &&
            !biome_factory_isActive(world, entity))
        {
            needs_met = false;
        }

        if (!needs_met) {
            states[i].stress ++;
            if (states[i].stress > plant->resilience) {
                ecs_delete(world, entity);
            }
            continue;
        }

        states[i].stress = 0;
        biome_plant_spread(
            world, entity, plant, tp, t, soil, ground, ctx);
    }
}

static void BiomePlantPlace(ecs_iter_t *it) {
    if (it->event_id != ecs_id(FlecsTerrainPosition)) {
        return;
    }

    for (int32_t i = 0; i < it->count; i ++) {
        biome_terrainItemIndex_place(it->world, it->entities[i]);
    }
}

static void BiomePlantRemove(ecs_iter_t *it) {
    if (it->event_id != ecs_id(FlecsTerrainPosition)) {
        return;
    }

    for (int32_t i = 0; i < it->count; i ++) {
        biome_terrainItemIndex_remove(it->world, it->entities[i]);
    }
}

void biomePlantImport(ecs_world_t *world) {
    ECS_MODULE(world, biomePlant);

    ECS_IMPORT(world, biomeTerrain);
    ECS_IMPORT(world, biomeWeather);
    ECS_IMPORT(world, biomeResources);
    ECS_IMPORT(world, biomeFactory);
    ECS_IMPORT(world, biomeTerrainItemIndex);

    ecs_set_name_prefix(world, "Biome");

    ECS_META_COMPONENT(world, BiomePlant);
    ECS_META_COMPONENT(world, BiomePlantState);

    ecs_add_pair(world, ecs_id(BiomePlant),
        EcsWith, ecs_id(BiomePlantState));
    ecs_add_pair(world, ecs_id(BiomePlantState),
        EcsOnInstantiate, EcsOverride);

    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_id(FlecsTerrainPosition) },
            { .id = ecs_id(BiomePlant) }
        },
        .events = { EcsOnSet },
        .callback = BiomePlantPlace
    });

    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_id(FlecsTerrainPosition) },
            { .id = ecs_id(BiomePlant) }
        },
        .events = { EcsOnRemove },
        .callback = BiomePlantRemove
    });

    BiomePlantSpreadContext *ctx = ecs_os_calloc_t(
        BiomePlantSpreadContext);
    ecs_map_init(&ctx->claimed, NULL);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Update" }),
        .query.terms = {
            { .id = ecs_id(BiomePlant), .inout = EcsIn },
            { .id = ecs_id(BiomePlantState) },
            { .id = ecs_id(FlecsTerrainPosition), .inout = EcsIn }
        },
        .phase = EcsOnUpdate,
        .callback = BiomePlantUpdate,
        .ctx = ctx,
        .ctx_free = biomePlantSpreadContextFree
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "DecayFertility" }),
        .query.terms = {
            { .id = ecs_id(FlecsTerrain), .inout = EcsIn },
            { .id = ecs_id(Terrain), .inout = EcsIn }
        },
        .phase = EcsOnUpdate,
        .callback = BiomePlantDecayFertility,
        .ctx = ecs_os_calloc_t(BiomePlantDecayContext),
        .ctx_free = biomePlantDecayContextFree
    });
}
