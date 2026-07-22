#include "../../src/modules/plant.c"
#include "../../src/modules/terrain_item_index.c"
#include <biome_test.h>

ECS_COMPONENT_DECLARE(TerrainPower);

static int32_t *Plant_mapEnsure(
    BiomeResourceStorageMap *map,
    ecs_entity_t resource)
{
    if (!ecs_map_is_init(map)) {
        ecs_map_init(map, NULL);
    }
    return (int32_t*)ecs_map_ensure(map, (ecs_map_key_t)resource);
}

static ecs_world_t* Plant_world(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);
    ECS_COMPONENT_DEFINE(world, TerrainPower);
    ECS_COMPONENT_DEFINE(world, BiomePowerConsumer);
    ECS_IMPORT(world, biomePlant);
    return world;
}

static ecs_entity_t Plant_terrain(ecs_world_t *world) {
    ecs_entity_t terrain = ecs_new(world);
    ecs_set(world, terrain, Terrain, {
        .width = 3,
        .height = 3,
        .scale = 1,
        .octaves = 1,
        .warp_octaves = 1,
        .ridge_octaves = 1,
        .water_level = -10
    });
    return terrain;
}

void Plant_capture_and_vent(void) {
    ecs_world_t *world = Plant_world();
    ecs_entity_t terrain = Plant_terrain(world);

    ecs_singleton_set(world, Weather, {
        .greenhouse_gas = 10
    });

    ecs_entity_t ghg = ecs_new(world);
    ecs_set(world, ghg, BiomeResource, { .greenhouse_gas = 0.5f });
    ecs_entity_t o2 = ecs_new(world);
    ecs_set(world, o2, BiomeResource, { .o2 = 0.25f });
    ecs_entity_t nutrients = ecs_new(world);
    ecs_set(world, nutrients, BiomeResource, { .fertility = 0.1f });
    ecs_entity_t water = ecs_new(world);
    ecs_set(world, water, BiomeResource, { .moisture = 0.1f });

    ecs_entity_t recipe = ecs_new(world);
    ecs_add(world, recipe, BiomeRecipe);
    BiomeRecipe *r = ecs_get_mut(world, recipe, BiomeRecipe);
    r->craft_time = 1;
    *Plant_mapEnsure(&r->inputs, ghg) = 1;
    *Plant_mapEnsure(&r->outputs, o2) = 1;
    *Plant_mapEnsure(&r->outputs, nutrients) = 1;
    *Plant_mapEnsure(&r->outputs, water) = 1;
    ecs_modified(world, recipe, BiomeRecipe);

    ecs_entity_t plant = ecs_new(world);
    ecs_set(world, plant, BiomeResourceStorageDesc, {
        BiomeResourceStorageKindSink, 10
    });
    ecs_set(world, plant, BiomeFactory, {
        .recipe = recipe,
        .output_mode = BiomeFactoryOutputVent,
        .input_mode = BiomeFactoryInputCapture
    });
    ecs_set(world, plant, FlecsTerrainPosition, {
        .terrain = terrain, .x = 1, .y = 1
    });

    ecs_progress(world, 0);

    const Weather *weather = ecs_singleton_get(world, Weather);
    test_assert(weather != NULL);
    test_flt(weather->greenhouse_gas, 9.0f);
    test_flt(weather->o2, 0.25f);

    const TerrainSoil *soil = flecsEngine_terrain_getLayer(
        world, terrain, TerrainSoilIndex, TerrainSoil);
    const TerrainGround *ground = flecsEngine_terrain_getLayer(
        world, terrain, TerrainGroundIndex, TerrainGround);
    test_assert(soil != NULL);
    test_assert(ground != NULL);
    test_flt(soil[1 * 3 + 1].fertility, 0.1f);
    test_flt(soil[0 * 3 + 0].fertility, 0.025f);
    test_flt(ground[1 * 3 + 1].moisture, 0.1f);
    test_flt(ground[0 * 3 + 0].moisture, 0.025f);

    ecs_fini(world);
}

void Plant_capture_requires_gas(void) {
    ecs_world_t *world = Plant_world();
    ecs_entity_t terrain = Plant_terrain(world);

    ecs_singleton_set(world, Weather, {
        .greenhouse_gas = 0.4f
    });

    ecs_entity_t ghg = ecs_new(world);
    ecs_set(world, ghg, BiomeResource, { .greenhouse_gas = 0.5f });
    ecs_entity_t o2 = ecs_new(world);
    ecs_set(world, o2, BiomeResource, { .o2 = 0.25f });

    ecs_entity_t recipe = ecs_new(world);
    ecs_add(world, recipe, BiomeRecipe);
    BiomeRecipe *r = ecs_get_mut(world, recipe, BiomeRecipe);
    r->craft_time = 1;
    *Plant_mapEnsure(&r->inputs, ghg) = 1;
    *Plant_mapEnsure(&r->outputs, o2) = 1;
    ecs_modified(world, recipe, BiomeRecipe);

    ecs_entity_t plant = ecs_new(world);
    ecs_set(world, plant, BiomeResourceStorageDesc, {
        BiomeResourceStorageKindSink, 10
    });
    ecs_set(world, plant, BiomeFactory, {
        .recipe = recipe,
        .output_mode = BiomeFactoryOutputVent,
        .input_mode = BiomeFactoryInputCapture
    });
    ecs_set(world, plant, FlecsTerrainPosition, {
        .terrain = terrain, .x = 1, .y = 1
    });

    ecs_progress(world, 0);

    const Weather *weather = ecs_singleton_get(world, Weather);
    test_assert(weather != NULL);
    test_flt(weather->greenhouse_gas, 0.4f);
    test_flt(weather->o2, 0);
    test_assert(!biome_factory_isActive(world, plant));

    ecs_fini(world);
}

void Plant_dies_without_needs(void) {
    ecs_world_t *world = Plant_world();
    ecs_entity_t terrain = Plant_terrain(world);

    ecs_entity_t plant = ecs_new(world);
    ecs_set(world, plant, BiomePlant, {
        .min_temperature = -50,
        .max_temperature = 50,
        .min_moisture = 0.5f,
        .min_fertility = 0,
        .resilience = 2
    });
    ecs_set(world, plant, FlecsTerrainPosition, {
        .terrain = terrain, .x = 1, .y = 1
    });

    ecs_progress(world, 0);
    ecs_progress(world, 0);
    test_assert(ecs_is_alive(world, plant));

    ecs_progress(world, 0);
    test_assert(!ecs_is_alive(world, plant));

    const TerrainItemRecord *record = biome_terrainItemIndex_get(
        world, 1, 1);
    test_assert(!record || !ecs_vec_count(&record->entities));

    ecs_fini(world);
}

void Plant_spreads_to_neighbors(void) {
    ecs_world_t *world = Plant_world();
    ecs_entity_t terrain = Plant_terrain(world);

    ecs_entity_t prefab = ecs_entity(world, {
        .name = "TestPlant",
        .add = ecs_ids(EcsPrefab)
    });
    ecs_set(world, prefab, BiomePlant, {
        .min_temperature = -50,
        .max_temperature = 50,
        .min_moisture = 0,
        .min_fertility = 0,
        .resilience = 100
    });

    ecs_entity_t instance = ecs_new_w_pair(world, EcsIsA, prefab);
    ecs_set(world, instance, FlecsTerrainPosition, {
        .terrain = terrain, .x = 1, .y = 1
    });

    ecs_progress(world, 0);

    int32_t count = 0;
    for (int32_t y = 0; y < 3; y ++) {
        for (int32_t x = 0; x < 3; x ++) {
            const TerrainItemRecord *record = biome_terrainItemIndex_get(
                world, x, y);
            if (record) {
                count += ecs_vec_count(&record->entities);
            }
        }
    }
    test_int(count, 9);

    ecs_progress(world, 0);

    count = 0;
    for (int32_t y = 0; y < 3; y ++) {
        for (int32_t x = 0; x < 3; x ++) {
            const TerrainItemRecord *record = biome_terrainItemIndex_get(
                world, x, y);
            if (record) {
                count += ecs_vec_count(&record->entities);
            }
        }
    }
    test_int(count, 9);

    ecs_fini(world);
}
