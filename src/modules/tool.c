#define BIOME_TOOL_IMPL

#include "biome.h"

#define BiomeToolMaxGhosts (65536)

void biomeToolPlaceBuilding(
    ecs_world_t *world,
    ecs_entity_t building)
{
    BiomeTool *tool = ecs_singleton_ensure(world, BiomeTool);
    tool->building = building;
    tool->doze = false;
    ecs_singleton_modified(world, BiomeTool);
}

void biomeToolDoze(
    ecs_world_t *world)
{
    BiomeTool *tool = ecs_singleton_ensure(world, BiomeTool);
    tool->building = 0;
    tool->doze = true;
    tool->dragging = false;
    ecs_singleton_modified(world, BiomeTool);
}

static ecs_entity_t biome_tool_camera(ecs_world_t *world) {
    ecs_entity_t camera = 0;
    ecs_iter_t it = ecs_each(world, FlecsRenderView);
    while (ecs_each_next(&it)) {
        FlecsRenderView *v = ecs_field(&it, FlecsRenderView, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            if (!camera) {
                camera = v[i].camera;
            }
        }
    }
    return camera;
}

static ecs_entity_t biome_tool_terrain(ecs_world_t *world) {
    ecs_entity_t terrain = 0;
    ecs_iter_t it = ecs_each(world, FlecsTerrain);
    while (ecs_each_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            if (!terrain) {
                terrain = it.entities[i];
            }
        }
    }
    return terrain;
}

static bool biome_tool_pickTile(
    ecs_world_t *world,
    ecs_entity_t camera,
    ecs_entity_t terrain,
    float view_norm_x,
    float view_norm_y,
    int32_t *tile_x,
    int32_t *tile_y)
{
    const FlecsTerrain *t = ecs_get(world, terrain, FlecsTerrain);
    if (!t || t->width <= 0 || t->depth <= 0 || t->cell_size <= 0) {
        return false;
    }

    vec3 origin, dir;
    if (!flecsEngine_cameraScreenRay(
        world, camera, view_norm_x, view_norm_y, origin, dir))
    {
        return false;
    }

    float ox = 0, oy = 0, oz = 0;
    const FlecsPosition3 *tp = ecs_get(world, terrain, FlecsPosition3);
    if (tp) {
        ox = tp->x;
        oy = tp->y;
        oz = tp->z;
    }

    float step = t->cell_size * 0.25f;
    float max_dist = 2.0f * (float)(t->width + t->depth) * t->cell_size;

    for (float d = 0; d < max_dist; d += step) {
        float px = origin[0] + dir[0] * d;
        float py = origin[1] + dir[1] * d;
        float pz = origin[2] + dir[2] * d;

        int32_t cx = (int32_t)floorf((px - ox) / t->cell_size);
        int32_t cz = (int32_t)floorf((pz - oz) / t->cell_size);
        if (cx < 0 || cz < 0 || cx >= t->width || cz >= t->depth) {
            continue;
        }

        if (py - oy <= flecsEngine_terrainCellHeight(t, cx, cz)) {
            *tile_x = cx;
            *tile_y = cz;
            return true;
        }
    }

    return false;
}

static TerrainOccupancy* biome_tool_occupancy(
    ecs_world_t *world,
    ecs_entity_t terrain)
{
    const FlecsTerrain *t = ecs_get(world, terrain, FlecsTerrain);
    if (!t || ecs_vec_count(&t->layerTypes) <= TerrainOccupancyIndex) {
        return NULL;
    }

    return flecsEngine_terrain_getLayer(
        world, terrain, TerrainOccupancyIndex, TerrainOccupancy);
}

static uint64_t biome_tool_buildingMask(
    ecs_world_t *world,
    ecs_entity_t building)
{
    const BiomeBuildingBit *bit = ecs_get(
        world, building, BiomeBuildingBit);
    if (!bit || *bit < 0 || *bit >= 64) {
        return 0;
    }

    return 1llu << *bit;
}

static uint64_t biome_tool_buildingRequirementMask(
    ecs_world_t *world,
    ecs_entity_t building)
{
    const BiomeBuildingRequirementMask *mask = ecs_get(
        world, building, BiomeBuildingRequirementMask);
    return mask ? *mask : 0;
}

static int32_t biome_tool_buildingMaxCount(
    ecs_world_t *world,
    ecs_entity_t building)
{
    const BiomeBuilding *b = ecs_get(world, building, BiomeBuilding);
    if (!b) {
        return 0;
    }

    return (int32_t)b->max_count;
}

static int32_t biome_tool_buildingCount(
    ecs_world_t *world,
    ecs_entity_t building)
{
    int32_t result = 0;
    ecs_iter_t it = ecs_each_id(world, ecs_pair(EcsIsA, building));
    while (ecs_each_next(&it)) {
        if (it.table &&
            ecs_table_has_id(ecs_get_world(world), it.table, EcsPrefab))
        {
            continue;
        }

        result += it.count;
    }

    return result;
}

static void biome_tool_footprint(
    ecs_world_t *world,
    ecs_entity_t building,
    int32_t *w,
    int32_t *h,
    int32_t *stride)
{
    *w = 1;
    *h = 1;
    *stride = 0;

    const BiomeBuilding *b = ecs_get(world, building, BiomeBuilding);
    if (b) {
        if (b->footprint.x > 1) {
            *w = (int32_t)b->footprint.x;
        }
        if (b->footprint.y > 1) {
            *h = (int32_t)b->footprint.y;
        }
        if (b->drag_stride > 0) {
            *stride = (int32_t)b->drag_stride;
        }
    }
}

static bool biome_tool_slotFree(
    const FlecsTerrain *t,
    const TerrainOccupancy *occ,
    int32_t x,
    int32_t y,
    int32_t fw,
    int32_t fh,
    uint64_t requirement_mask)
{
    if (x < 0 || y < 0 || (x + fw) > t->width || (y + fh) > t->depth) {
        return false;
    }

    if (!occ) {
        return requirement_mask == 0;
    }

    for (int32_t cy = y; cy < y + fh; cy ++) {
        for (int32_t cx = x; cx < x + fw; cx ++) {
            uint64_t buildings = occ[cy * t->width + cx].buildings;
            if (requirement_mask && !(buildings & requirement_mask)) {
                return false;
            }
            if (buildings & ~requirement_mask) {
                return false;
            }
        }
    }

    return true;
}

static int32_t biome_tool_freeSlots(
    const FlecsTerrain *t,
    const TerrainOccupancy *occ,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1,
    int32_t fw,
    int32_t fh,
    int32_t step_x,
    int32_t step_y,
    uint64_t requirement_mask)
{
    int32_t result = 0;
    for (int32_t sy = y0; sy <= y1; sy += step_y) {
        for (int32_t sx = x0; sx <= x1; sx += step_x) {
            if (biome_tool_slotFree(
                t, occ, sx, sy, fw, fh, requirement_mask))
            {
                result ++;
            }
        }
    }

    return result;
}

static bool biome_tool_shiftDown(const FlecsInput *input) {
    return input->keys[FLECS_KEY_LEFT_SHIFT].state ||
        input->keys[FLECS_KEY_RIGHT_SHIFT].state;
}

static void biome_tool_region(
    const BiomeTool *tool,
    bool rect,
    int32_t cur_x,
    int32_t cur_y,
    int32_t step_x,
    int32_t step_y,
    int32_t *x0,
    int32_t *y0,
    int32_t *x1,
    int32_t *y1,
    int32_t *count)
{
    if (!tool->dragging) {
        *x0 = *x1 = cur_x;
        *y0 = *y1 = cur_y;
        *count = 1;
        return;
    }

    int32_t ax = tool->anchor_x, ay = tool->anchor_y;
    int32_t dx = cur_x > ax ? cur_x - ax : ax - cur_x;
    int32_t dy = cur_y > ay ? cur_y - ay : ay - cur_y;

    *x0 = ax < cur_x ? ax : cur_x;
    *x1 = ax > cur_x ? ax : cur_x;
    *y0 = ay < cur_y ? ay : cur_y;
    *y1 = ay > cur_y ? ay : cur_y;

    if (!rect) {
        if (dx >= dy) {
            *y0 = *y1 = ay;
        } else {
            *x0 = *x1 = ax;
        }
    }

    if (*x0 < ax) {
        *x0 = ax - ((ax - *x0 + step_x - 1) / step_x) * step_x;
    }
    if (*y0 < ay) {
        *y0 = ay - ((ay - *y0 + step_y - 1) / step_y) * step_y;
    }

    *count = ((*x1 - *x0) / step_x + 1) * ((*y1 - *y0) / step_y + 1);
}

static void biome_tool_ghostInstance(
    const FlecsTerrain *t,
    const FlecsPosition3 *terrain_pos,
    int32_t x,
    int32_t y,
    int32_t fw,
    int32_t fh,
    flecs_draw_instance_t *out)
{
    float px = ((float)x + (float)fw * 0.5f) * t->cell_size;
    float pz = ((float)y + (float)fh * 0.5f) * t->cell_size;
    float py = flecsEngine_terrainCellHeight(t, x, y);
    for (int32_t cy = y; cy < y + fh; cy ++) {
        for (int32_t cx = x; cx < x + fw; cx ++) {
            float ch = flecsEngine_terrainCellHeight(t, cx, cy);
            if (ch > py) {
                py = ch;
            }
        }
    }

    if (terrain_pos) {
        px += terrain_pos->x;
        py += terrain_pos->y;
        pz += terrain_pos->z;
    }

    *out = (flecs_draw_instance_t){
        .position = { .x = px, .y = py, .z = pz }
    };
}

static void biome_tool_burst(
    ecs_world_t *world,
    ecs_entity_t effect,
    const FlecsTerrain *t,
    const FlecsPosition3 *terrain_pos,
    int32_t x,
    int32_t y,
    int32_t fw,
    int32_t fh)
{
    const FlecsParticleBurst *burst = ecs_get(
        world, effect, FlecsParticleBurst);
    if (!burst || !burst->pool) {
        return;
    }

    float pos[3];
    pos[0] = ((float)x + (float)fw * 0.5f) * t->cell_size;
    pos[2] = ((float)y + (float)fh * 0.5f) * t->cell_size;
    pos[1] = flecsEngine_terrainCellHeight(t, x, y);
    for (int32_t cy = y; cy < y + fh; cy ++) {
        for (int32_t cx = x; cx < x + fw; cx ++) {
            float ch = flecsEngine_terrainCellHeight(t, cx, cy);
            if (ch > pos[1]) {
                pos[1] = ch;
            }
        }
    }

    if (terrain_pos) {
        pos[0] += terrain_pos->x;
        pos[1] += terrain_pos->y;
        pos[2] += terrain_pos->z;
    }

    FlecsParticleBurst b = *burst;
    b.count = burst->count * fw * fh;
    b.spread.x = burst->spread.x + (float)(fw - 1) * t->cell_size;
    b.spread.z = burst->spread.z + (float)(fh - 1) * t->cell_size;

    flecsEngine_particlesBurst(world, burst->pool, pos, &b);
}

static void biome_tool_doze(
    ecs_world_t *world,
    ecs_entity_t terrain,
    ecs_entity_t doze_effect,
    int32_t x0,
    int32_t y0,
    int32_t x1,
    int32_t y1)
{
    const FlecsTerrain *t = ecs_get(world, terrain, FlecsTerrain);
    TerrainOccupancy *occ = biome_tool_occupancy(world, terrain);
    if (!t) {
        return;
    }

    const FlecsPosition3 *terrain_pos = ecs_get(
        world, terrain, FlecsPosition3);

    bool power_dirty = false;

    ecs_iter_t it = ecs_each(world, FlecsTerrainPosition);
    while (ecs_each_next(&it)) {
        FlecsTerrainPosition *tp = ecs_field(&it, FlecsTerrainPosition, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            if (tp[i].terrain != terrain) {
                continue;
            }

            int32_t fw = tp[i].span_x ? tp[i].span_x : 1;
            int32_t fh = tp[i].span_y ? tp[i].span_y : 1;
            if (tp[i].x > x1 || (tp[i].x + fw) <= x0 ||
                tp[i].y > y1 || (tp[i].y + fh) <= y0)
            {
                continue;
            }

            ecs_entity_t e = it.entities[i];
            const BiomeBuilding *b = ecs_get(world, e, BiomeBuilding);
            if (!b || !b->can_doze) {
                continue;
            }

            uint64_t mask = biome_tool_buildingMask(world, e);
            if (occ && mask) {
                for (int32_t cy = tp[i].y; cy < tp[i].y + fh; cy ++) {
                    if (cy < 0 || cy >= t->depth) {
                        continue;
                    }
                    for (int32_t cx = tp[i].x; cx < tp[i].x + fw; cx ++) {
                        if (cx < 0 || cx >= t->width) {
                            continue;
                        }
                        occ[cy * t->width + cx].buildings &= ~mask;
                    }
                }
            }

            if (ecs_has(world, e, BiomePower)) {
                power_dirty = true;
            }

            biome_factory_refund(world, e, 1);

            if (doze_effect) {
                biome_tool_burst(world, doze_effect, t, terrain_pos,
                    tp[i].x, tp[i].y, fw, fh);
            }

            biome_terrainItemIndex_remove(world, e);
            ecs_delete(world, e);
        }
    }

    if (power_dirty) {
        biome_power_markDirty(world);
    }
}

void BiomeToolPreview(ecs_iter_t *it) {
    ecs_world_t *world = it->world;
    BiomeTool *tool = ecs_field(it, BiomeTool, 0);
    FlecsTerrain *t = ecs_field(it, FlecsTerrain, 1);

    if (!tool->building && !tool->doze) {
        return;
    }

    const FlecsInput *input = ecs_singleton_get(world, FlecsInput);
    ecs_entity_t camera = biome_tool_camera(world);

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t terrain = it->entities[i];

        int32_t x = 0, y = 0;
        bool hover = input && camera &&
            !flecsEngine_uiMouseCaptured(world) &&
            biome_tool_pickTile(world, camera, terrain,
                input->mouse.view_norm.x, input->mouse.view_norm.y, &x, &y);

        if (!hover) {
            continue;
        }

        int32_t fw, fh, stride;
        if (tool->doze) {
            fw = 1;
            fh = 1;
            stride = 0;
        } else {
            biome_tool_footprint(world, tool->building, &fw, &fh, &stride);
        }
        int32_t step_x = fw + stride, step_y = fh + stride;

        int32_t x0, y0, x1, y1, count;
        biome_tool_region(tool, biome_tool_shiftDown(input), x, y,
            step_x, step_y, &x0, &y0, &x1, &y1, &count);

        if (tool->doze) {
            if (ecs_vec_count(&t[i].colors) == t[i].width * t[i].depth) {
                flecs_rgba_t *colors = ecs_vec_first_t(
                    &t[i].colors, flecs_rgba_t);
                int32_t cy0 = y0 < 0 ? 0 : y0;
                int32_t cx0 = x0 < 0 ? 0 : x0;
                int32_t cy1 = y1 >= t[i].depth ? t[i].depth - 1 : y1;
                int32_t cx1 = x1 >= t[i].width ? t[i].width - 1 : x1;
                for (int32_t cy = cy0; cy <= cy1; cy ++) {
                    for (int32_t cx = cx0; cx <= cx1; cx ++) {
                        colors[cy * t[i].width + cx] =
                            (flecs_rgba_t){ 230, 120, 40, 230 };
                    }
                }
                flecsEngine_terrainColorsModified(world, terrain);
            }
            continue;
        }

        const TerrainOccupancy *occ = biome_tool_occupancy(world, terrain);
        uint64_t requirement_mask = biome_tool_buildingRequirementMask(
            world, tool->building);
        int32_t maxCount = biome_tool_buildingMaxCount(world, tool->building);
        int32_t curCount = biome_tool_buildingCount(world, tool->building);
        int32_t freeSlots = biome_tool_freeSlots(
            &t[i], occ, x0, y0, x1, y1, fw, fh, step_x, step_y,
            requirement_mask);
        int32_t affordable = biome_factory_canAfford(world, tool->building);
        bool tooMany = (maxCount > 0 &&
            (freeSlots + curCount) > maxCount) ||
            freeSlots > affordable;

        if (ecs_vec_count(&t[i].colors) == t[i].width * t[i].depth) {
            flecs_rgba_t *colors = ecs_vec_first_t(&t[i].colors, flecs_rgba_t);
            for (int32_t sy = y0; sy <= y1; sy += step_y) {
                for (int32_t sx = x0; sx <= x1; sx += step_x) {
                    bool free = biome_tool_slotFree(
                        &t[i], occ, sx, sy, fw, fh, requirement_mask);
                    flecs_rgba_t color = (tooMany || !free)
                        ? (flecs_rgba_t){ 220, 60, 50, 230 }
                        : (flecs_rgba_t){ 90, 210, 90, 230 };
                    int32_t cy0 = sy < 0 ? 0 : sy;
                    int32_t cx0 = sx < 0 ? 0 : sx;
                    int32_t cy1 = sy + fh > t[i].depth ? t[i].depth : sy + fh;
                    int32_t cx1 = sx + fw > t[i].width ? t[i].width : sx + fw;
                    for (int32_t cy = cy0; cy < cy1; cy ++) {
                        for (int32_t cx = cx0; cx < cx1; cx ++) {
                            colors[cy * t[i].width + cx] = color;
                        }
                    }
                }
            }
            flecsEngine_terrainColorsModified(world, terrain);
        }

        if (tooMany) {
            count = 0;
        }

        if (count > BiomeToolMaxGhosts) {
            count = BiomeToolMaxGhosts;
        }

        ecs_entity_t ghost = 0;
        if (count) {
            ghost = biomeGhostGet(world, tool->building);
        }

        if (ghost) {
            const FlecsPosition3 *tp = ecs_get(world, terrain, FlecsPosition3);
            flecs_draw_instance_t *instances = ecs_os_malloc_n(
                flecs_draw_instance_t, count);
            int32_t g = 0;
            for (int32_t sy = y0; sy <= y1 && g < count; sy += step_y) {
                for (int32_t sx = x0; sx <= x1 && g < count; sx += step_x) {
                    if (!biome_tool_slotFree(
                        &t[i], occ, sx, sy, fw, fh, requirement_mask))
                    {
                        continue;
                    }
                    biome_tool_ghostInstance(&t[i], tp, sx, sy, fw, fh,
                        &instances[g ++]);
                }
            }
            flecsEngine_draw(world, ghost, instances, g);
            ecs_os_free(instances);
        }
    }
}

void BiomeToolBind(ecs_iter_t *it) {
    BiomeTool *tool = ecs_field(it, BiomeTool, 0);
    tool->place_effect = ecs_lookup(it->world, "effects.build_burst");
    if (!tool->place_effect) {
        ecs_warn("tool: effect 'effects.build_burst' not found");
    }
    tool->doze_effect = ecs_lookup(it->world, "effects.doze_burst");
    if (!tool->doze_effect) {
        ecs_warn("tool: effect 'effects.doze_burst' not found");
    }
}

void BiomeToolUpdate(ecs_iter_t *it) {
    ecs_world_t *world = it->world;
    BiomeTool *tool = ecs_field(it, BiomeTool, 0);

    const FlecsInput *input = ecs_singleton_get(world, FlecsInput);
    if (!input) {
        return;
    }

    if (!tool->doze &&
        (!tool->building || !ecs_is_alive(world, tool->building)))
    {
        tool->dragging = false;
        return;
    }

    if (input->keys[FLECS_KEY_ESCAPE].pressed) {
        tool->building = 0;
        tool->doze = false;
        tool->dragging = false;
        return;
    }

    ecs_entity_t camera = biome_tool_camera(world);
    ecs_entity_t terrain = biome_tool_terrain(world);
    if (!camera || !terrain) {
        return;
    }

    int32_t x = 0, y = 0;
    bool hover = !flecsEngine_uiMouseCaptured(world) &&
        biome_tool_pickTile(world, camera, terrain,
            input->mouse.view_norm.x, input->mouse.view_norm.y, &x, &y);

    if (!tool->dragging) {
        if (hover && input->mouse.left.pressed) {
            tool->dragging = true;
            tool->anchor_x = x;
            tool->anchor_y = y;
        }
        return;
    }

    if (input->mouse.left.state) {
        return;
    }

    if (hover) {
        int32_t fw, fh, stride;
        if (tool->doze) {
            fw = 1;
            fh = 1;
            stride = 0;
        } else {
            biome_tool_footprint(world, tool->building, &fw, &fh, &stride);
        }
        int32_t step_x = fw + stride, step_y = fh + stride;

        int32_t x0, y0, x1, y1, count;
        biome_tool_region(tool, biome_tool_shiftDown(input), x, y,
            step_x, step_y, &x0, &y0, &x1, &y1, &count);

        if (tool->doze) {
            biome_tool_doze(world, terrain, tool->doze_effect,
                x0, y0, x1, y1);
            tool->dragging = false;
            return;
        }

        const FlecsTerrain *t = ecs_get(world, terrain, FlecsTerrain);
        TerrainOccupancy *occ = biome_tool_occupancy(world, terrain);
        uint64_t mask = biome_tool_buildingMask(world, tool->building);
        uint64_t requirement_mask = biome_tool_buildingRequirementMask(
            world, tool->building);
        int32_t maxCount = biome_tool_buildingMaxCount(world, tool->building);
        int32_t curCount = biome_tool_buildingCount(world, tool->building);
        int32_t freeSlots = biome_tool_freeSlots(
            t, occ, x0, y0, x1, y1, fw, fh, step_x, step_y,
            requirement_mask);

        if (maxCount > 0 && (freeSlots + curCount) > maxCount) {
            tool->dragging = false;
            return;
        }

        if (!biome_factory_purchase(world, tool->building, freeSlots)) {
            tool->dragging = false;
            return;
        }

        const FlecsParticleBurst *place_burst = NULL;
        FlecsParticleBurst burst;
        if (tool->place_effect) {
            place_burst = ecs_get(
                world, tool->place_effect, FlecsParticleBurst);
        }
        if (place_burst) {
            burst = *place_burst;
            burst.count = place_burst->count * fw * fh;
            burst.spread.x = place_burst->spread.x +
                (float)(fw - 1) * t->cell_size;
            burst.spread.z = place_burst->spread.z +
                (float)(fh - 1) * t->cell_size;
        }

        ecs_entity_t parent = ecs_lookup(world, "scene.buildings");
        if (!parent) {
            parent = terrain;
        }

        int32_t placed = 0;
        for (int32_t sy = y0; sy <= y1; sy += step_y) {
            for (int32_t sx = x0; sx <= x1; sx += step_x) {
                if (!biome_tool_slotFree(
                    t, occ, sx, sy, fw, fh, requirement_mask))
                {
                    continue;
                }

                ecs_entity_t building = ecs_new_w_pair(
                    world, EcsIsA, tool->building);
                ecs_add_pair(world, building, EcsChildOf, parent);
                ecs_set(world, building, FlecsTerrainPosition, {
                    .terrain = terrain, .x = sx, .y = sy,
                    .span_x = fw, .span_y = fh });
                if (place_burst) {
                    ecs_set_ptr(world, building, FlecsParticleBurst, &burst);
                }

                if (occ) {
                    for (int32_t cy = sy; cy < sy + fh; cy ++) {
                        for (int32_t cx = sx; cx < sx + fw; cx ++) {
                            occ[cy * t->width + cx].buildings |= mask;
                        }
                    }
                }

                biome_terrainItemIndex_place(world, building);

                placed ++;
            }
        }

        if (placed) {
            biome_playerAttr_addFlag(world, "BuildingsPlaced", mask);
        }

        if (maxCount > 0 && (curCount + placed) >= maxCount) {
            tool->building = 0;
        }
    }

    tool->dragging = false;
}

void biomeToolImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeTool);

    ECS_IMPORT(world, biomeTerrainItemIndex);

    ecs_set_name_prefix(world, "Biome");

    ECS_META_COMPONENT(world, BiomeTool);

    ecs_add_id(world, ecs_id(BiomeTool), EcsSingleton);
    ecs_singleton_set(world, BiomeTool, {0});

    ecs_entity_t old_scope = ecs_set_scope(world, 0);
    ecs_entity(world, { .name = "scene.buildings" });
    ecs_set_scope(world, old_scope);

    ECS_SYSTEM(world, BiomeToolBind, EcsOnStart, [inout] BiomeTool);

    ECS_SYSTEM(world, BiomeToolUpdate, EcsOnUpdate, [inout] BiomeTool);

    ECS_SYSTEM(world, BiomeToolPreview, EcsPreStore,
        [inout] BiomeTool,
        [inout] FlecsTerrain);
}
