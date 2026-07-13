#ifndef BIOME_BUILDING_RULE_H
#define BIOME_BUILDING_RULE_H

#undef ECS_META_IMPL
#ifndef BIOME_BUILDING_RULE_IMPL
#define ECS_META_IMPL EXTERN
#endif

/* Instantiates out in the center of a matching 2x1 building pattern. The
 * left/right fields define a horizontal pattern and top/bottom define a
 * vertical pattern. If top/bottom are unset and rotate is true, the vertical
 * pattern is derived from left/right for backwards compatibility. Empty slots
 * in a configured pattern are wildcards. A populated slot matches when at
 * least one of its prefabs occupies the corresponding terrain tile.
 * 
 * Buildings placed by the pattern are not part of the occupancy grid. They are
 * only intended to be visual (for example, a wire between electricity poles).
 *
 * The implementation assumes a single terrain, that all rules are created 
 * before buildings are created, and that rules are persistent (so it is not
 * necessary to cleanup instances when a rule is deleted).
 */
ECS_STRUCT(BiomeBuildingRule2x1, {
    ecs_vec(ecs_entity_t) left;
    ecs_vec(ecs_entity_t) right;
    ecs_vec(ecs_entity_t) top;
    ecs_vec(ecs_entity_t) bottom;
    ecs_entity_t out;
    bool rotate; /* If true, configured patterns are also tested in the reverse direction */
    bool follow_slope; /* Apply rotation that follows the terrain slope */
});

/* Instantiated by implementation after setting BiomeBuildingRule2x1 */
ECS_STRUCT(BiomeBuildingRule2x1Impl, {
    /* Mask that is built from the rule. If an element is 0, it is treated as a
     * wildcard. If it is non-zero, at least one of the bits must match the occupancy
     * grid of the terrain. */
    uint64_t building_masks[4]; /* left, right, top, bottom */

    /* Vector with observers that track prefabs that are part of the mask. An
     * observer is created for each slot, with context that stores which slot the
     * observer is created for. This way the triggered observer can trivially
     * match the pattern against surrounding tiles. 
     *
     * Observers are created for OnAdd/OnRemove events, so instances are 
     * automatically created and removed after a pattern starts/stops matching.
     */
    ecs_vec(ecs_entity_t) observers;

    /* Map with instances instantiated by rule. The map is keyed by a packed 
     * position, similar to the terrain item index. However, because the instance
     * does not occupy a tile, the values are duplicated, because we can't
     * neatly represent halves in a packed key:
     *
     *   x = left_tile_x * 2 + 1
     *   y = left_tile_y * 2 + 1
     * 
     *   instances[pack(x, y)] = ...
     *
     * This means that when an OnRemove observer triggers, it has to check each
     * of its four edges for a possible instance.
     */
    ecs_map_t instances; /* map<uint64_t, vec<ecs_entity_t>> */
});

/* Observer context for a rule */
typedef struct BiomeBuildingRule2x1ObserverCtx {
    ecs_entity_t rule;
    int32_t slot;
} BiomeBuildingRule2x1ObserverCtx;

void biomeBuilding_ruleImport(ecs_world_t *world);

#endif
