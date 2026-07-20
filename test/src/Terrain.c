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

void Terrain_spread_colors_across_frames(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);
    ECS_IMPORT(world, biomeTerrain);
    ECS_COMPONENT_DEFINE(world, Weather);
    ecs_add_id(world, ecs_id(Weather), EcsSingleton);
    ECS_COMPONENT_DEFINE(world, TerrainPower);

    ecs_entity_t entity = ecs_new(world);
    ecs_set(world, entity, Terrain, {
        .width = 60,
        .height = 1,
        .scale = 1,
        .octaves = 1,
        .warp_octaves = 1,
        .ridge_octaves = 1
    });

    FlecsTerrain *terrain = ecs_get_mut(world, entity, FlecsTerrain);
    test_assert(terrain != NULL);
    flecs_rgba_t *colors = ecs_vec_first_t(&terrain->colors, flecs_rgba_t);
    for (int32_t i = 0; i < 60; i ++) {
        colors[i] = (flecs_rgba_t){1, 2, 3, 4};
    }

    ecs_progress(world, 0);

    int32_t changed = 0;
    for (int32_t i = 0; i < 60; i ++) {
        flecs_rgba_t color = colors[i];
        if (color.r != 1 || color.g != 2 || color.b != 3 || color.a != 4) {
            changed ++;
        }
    }
    test_int(changed, 1);

    Terrain *config = ecs_get_mut(world, entity, Terrain);
    config->color_update_frames = 3;

    ecs_progress(world, 0);

    changed = 0;
    for (int32_t i = 0; i < 60; i ++) {
        flecs_rgba_t color = colors[i];
        if (color.r != 1 || color.g != 2 || color.b != 3 || color.a != 4) {
            changed ++;
        }
    }
    test_int(changed, 21);

    ecs_fini(world);
}
