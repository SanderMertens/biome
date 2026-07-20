#include "../../src/modules/terrain.c"
#include <biome_test.h>

ECS_COMPONENT_DECLARE(TerrainPower);

void Terrain_regenerate_moisture(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);
    ECS_IMPORT(world, biomeTerrain);
    ECS_COMPONENT_DEFINE(world, TerrainPower);

    ecs_entity_t entity = ecs_new(world);
    ecs_set(world, entity, Terrain, {
        .width = 3,
        .height = 1,
        .scale = 1,
        .octaves = 1,
        .warp_octaves = 1,
        .ridge_octaves = 1,
        .water_level = 0.1f,
        .water_moisture_range_tiles = 2
    });

    const FlecsTerrain *generated = ecs_get(world, entity, FlecsTerrain);
    test_assert(generated != NULL);
    const float *heights = ecs_vec_first_t(&generated->heights, float);
    test_flt(heights[0], 0.0f);
    test_flt(ecs_get(world, entity, Terrain)->water_level, 0.1f);

    TerrainGround *ground = flecsEngine_terrain_getLayer(
        world, entity, TerrainGroundIndex, TerrainGround);
    test_assert(ground != NULL);
    test_flt(ground[0].moisture, 1.0f);
    test_flt(ground[1].moisture, 1.0f);
    test_flt(ground[2].moisture, 1.0f);

    Terrain *config = ecs_get_mut(world, entity, Terrain);
    config->water_level = -0.1f;
    ecs_modified(world, entity, Terrain);

    ground = flecsEngine_terrain_getLayer(
        world, entity, TerrainGroundIndex, TerrainGround);
    test_assert(ground != NULL);
    test_flt(ground[0].moisture, 0.0f);
    test_flt(ground[1].moisture, 0.0f);
    test_flt(ground[2].moisture, 0.0f);

    ecs_fini(world);
}
