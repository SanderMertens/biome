#include "../../src/modules/building_rule.c"
#include <biome_test.h>

ECS_COMPONENT_DECLARE(BiomeBuildingBit);
ECS_COMPONENT_DECLARE(BiomeBuildingOccupancyChanged);
ECS_COMPONENT_DECLARE(TerrainOccupancy);

void BuildingRule_self_referencing_drag(void) {
    ecs_world_t *world = ecs_init();

    ECS_COMPONENT_DEFINE(world, BiomeBuildingRule3x1);
    ECS_COMPONENT_DEFINE(world, BiomeBuildingRule3x1Impl);

    ecs_entity_t rule = ecs_new(world);
    ecs_set(world, rule, BiomeBuildingRule3x1, {
        .rotate = true
    });
    ecs_set(world, rule, BiomeBuildingRule3x1Impl, {
        .building_masks = { 1, 0, 0, 0 },
        .populated_slots = 1 << BiomeBuildingRuleLeft,
        .valid = true
    });

    FlecsTerrain terrain = {
        .width = 5,
        .depth = 1
    };
    TerrainOccupancy occupancy[5] = {0};
    BiomeBuildingRulePlacement placements[3] = {
        { .x = 1, .y = 0, .width = 1, .height = 1, .active = true },
        { .x = 2, .y = 0, .width = 1, .height = 1, .active = true },
        { .x = 3, .y = 0, .width = 1, .height = 1, .active = true }
    };

    test_bool(biomeBuildingRuleMatches(
        world, rule, &terrain, occupancy, 1, 0), false);
    test_int(biomeBuildingRuleFilterPlacements(
        world, rule, &terrain, occupancy, 1, placements, 3), 3);
    test_bool(placements[0].active, true);
    test_bool(placements[1].active, true);
    test_bool(placements[2].active, true);

    ecs_fini(world);
}
