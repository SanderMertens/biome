#include <flecs_engine_test.h>

static ecs_entity_t Terrain_create(
    ecs_world_t *world,
    int32_t width,
    int32_t depth)
{
    ecs_entity_t terrain = ecs_new(world);
    FlecsTerrain *t = ecs_ensure(world, terrain, FlecsTerrain);
    t->width = width;
    t->depth = depth;
    t->cell_size = 1.0f;

    int32_t corner_count = (width + 1) * (depth + 1);
    ecs_vec_set_count_t(NULL, &t->heights, float, corner_count);
    float *heights = ecs_vec_first_t(&t->heights, float);
    for (int32_t i = 0; i < corner_count; i ++) {
        heights[i] = (float)i;
    }

    ecs_modified(world, terrain, FlecsTerrain);
    return terrain;
}

void Terrain_set_height_flattens_footprint(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);

    ecs_entity_t terrain = Terrain_create(world, 3, 3);
    flecsEngine_terrain_setHeight(world, terrain, 1, 0, 2, 2, 42.0f);

    const FlecsTerrain *t = ecs_get(world, terrain, FlecsTerrain);
    test_assert(t != NULL);
    for (int32_t z = 0; z < 2; z ++) {
        for (int32_t x = 1; x < 3; x ++) {
            test_flt(flecsEngine_terrainCellHeight(t, x, z), 42.0f);
        }
    }

    const float *heights = ecs_vec_first_t(&t->heights, float);
    int32_t stride = t->width + 1;
    for (int32_t z = 0; z <= 2; z ++) {
        for (int32_t x = 1; x <= 3; x ++) {
            test_flt(heights[z * stride + x], 42.0f);
        }
    }
    test_flt(heights[0], 0.0f);
    test_flt(heights[3 * stride], 12.0f);

    ecs_fini(world);
}

void Terrain_set_height_refreshes_positions(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);

    ecs_entity_t terrain = Terrain_create(world, 3, 3);
    ecs_entity_t item = ecs_new(world);
    ecs_set(world, item, FlecsTerrainPosition, {
        .terrain = terrain,
        .x = 1,
        .y = 0,
        .span_x = 2,
        .span_y = 2
    });

    flecsEngine_terrain_setHeight(world, terrain, 1, 0, 2, 2, 42.0f);

    const FlecsPosition3 *position = ecs_get(world, item, FlecsPosition3);
    test_assert(position != NULL);
    test_flt(position->x, 2.0f);
    test_flt(position->y, 42.0f);
    test_flt(position->z, 1.0f);

    ecs_fini(world);
}
