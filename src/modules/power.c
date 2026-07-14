#define BIOME_POWER_IMPL

#include "biome.h"

#include <stdlib.h>

ECS_COMPONENT_DECLARE(BiomePowerGrid);

#define BiomePowerUnreached (UINT32_MAX)

static const int32_t biome_power_dx[] = {0, 1, 0, -1};
static const int32_t biome_power_dy[] = {-1, 0, 1, 0};

typedef struct biome_power_masks_t {
    uint64_t body;
    uint64_t source;
} biome_power_masks_t;

static void BiomePower_onSet(ecs_iter_t *it) {
    BiomePower *power = ecs_field(it, BiomePower, 0);

    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];

        if (power[i].producer) {
            ecs_set(it->world, e, BiomePowerProducer, {100}); /* Hardcoded for now, will be replaced later */
        }
        if (power[i].demand) {
            ecs_set(it->world, e, BiomePowerConsumer, {0});
        }
    }
}

static void UpdatePowerTint(ecs_iter_t *it) {
    BiomePowerConsumer *c = ecs_field(it, BiomePowerConsumer, 0);

    for (int i = 0; i < it->count; i ++) {
        bool owns_tint = ecs_owns(
            it->world, it->entities[i], FlecsTint);
        const FlecsTint *tint = owns_tint
            ? ecs_get(it->world, it->entities[i], FlecsTint)
            : NULL;
        if (c[i].powered) {
            if (tint && (tint->r != 0 || tint->g != 0 || tint->b != 0 ||
                tint->a != 0))
            {
                ecs_set(it->world, it->entities[i], FlecsTint,
                    {0, 0, 0, 0});
            }
        } else if (!tint || tint->r != 0 || tint->g != 0 || tint->b != 0 ||
            tint->a != 230)
        {
            ecs_set(it->world, it->entities[i], FlecsTint,
                {0, 0, 0, 230});
        }
    }
}

static void biome_power_clearNetworks(BiomePowerGrid *grid) {
    int32_t i, count = ecs_vec_count(&grid->networks);
    biome_power_network_t *networks = ecs_vec_first_t(
        &grid->networks, biome_power_network_t);
    for (i = 0; i < count; i ++) {
        ecs_vec_fini_t(NULL, &networks[i].consumers, biome_power_consumer_t);
    }
    ecs_vec_clear(&grid->networks);
}

static void BiomePowerGrid_ctor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    BiomePowerGrid *grid = ptr;
    for (int32_t i = 0; i < count; i ++) {
        ecs_os_zeromem(&grid[i]);
        ecs_vec_init_t(NULL, &grid[i].networks, biome_power_network_t, 0);
    }
}

static void BiomePowerGrid_dtor(
    void *ptr,
    int32_t count,
    const ecs_type_info_t *ti)
{
    (void)ti;
    BiomePowerGrid *grid = ptr;
    for (int32_t i = 0; i < count; i ++) {
        biome_power_clearNetworks(&grid[i]);
        ecs_vec_fini_t(NULL, &grid[i].networks, biome_power_network_t);
    }
}

void biome_power_markDirty(ecs_world_t *world) {
    BiomePowerGrid *grid = ecs_get_mut(
        world, ecs_id(BiomePowerGrid), BiomePowerGrid);
    if (grid) {
        grid->dirty = true;
    }
}

static ecs_entity_t biome_power_terrain(ecs_world_t *world) {
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

static biome_power_masks_t biome_power_masks(
    ecs_world_t *world,
    BiomePowerGrid *grid)
{
    biome_power_masks_t masks = {0};

    ecs_iter_t it = ecs_query_iter(world, grid->prefabs);
    while (ecs_query_next(&it)) {
        BiomeBuildingBit *building_bit = ecs_field(
            &it, BiomeBuildingBit, 0);
        BiomePower *power = ecs_field(&it, BiomePower, 1);
        for (int32_t i = 0; i < it.count; i ++) {
            if (building_bit[i] < 0 || building_bit[i] >= 64) {
                continue;
            }

            uint64_t bit = 1llu << building_bit[i];
            if (power[i].conductor) {
                masks.body |= bit;
            }
            if (power[i].producer) {
                masks.body |= bit;
                masks.source |= bit;
            }
        }
    }

    return masks;
}

static void biome_power_push(ecs_vec_t *v, int32_t value) {
    *ecs_vec_append_t(NULL, v, int32_t) = value;
}

static void biome_power_stamp(
    TerrainPower *pow,
    int32_t idx,
    uint32_t gen,
    uint32_t network)
{
    pow[idx].generation = gen;
    pow[idx].network = network;
    pow[idx].distance = BiomePowerUnreached;
}

static uint32_t biome_power_networkNew(BiomePowerGrid *grid) {
    uint32_t id = (uint32_t)ecs_vec_count(&grid->networks);
    biome_power_network_t *net = ecs_vec_append_t(
        NULL, &grid->networks, biome_power_network_t);
    net->production = 0;
    net->producer_count = 0;
    ecs_vec_init_t(NULL, &net->consumers, biome_power_consumer_t, 0);
    return id;
}

static void biome_power_flood(
    const TerrainOccupancy *occ,
    TerrainPower *pow,
    int32_t width,
    int32_t depth,
    uint32_t gen,
    uint32_t network,
    biome_power_masks_t masks,
    int32_t x0,
    int32_t y0,
    int32_t fw,
    int32_t fh,
    ecs_vec_t *queue,
    ecs_vec_t *seeds)
{
    ecs_vec_clear(queue);
    ecs_vec_clear(seeds);

    for (int32_t y = y0; y < y0 + fh; y ++) {
        for (int32_t x = x0; x < x0 + fw; x ++) {
            int32_t idx = y * width + x;
            if (pow[idx].generation != gen) {
                biome_power_stamp(pow, idx, gen, network);
                biome_power_push(queue, idx);
            }
        }
    }

    int32_t head = 0;
    while (head < ecs_vec_count(queue)) {
        int32_t idx = ecs_vec_get_t(queue, int32_t, head)[0];
        head ++;

        if (occ[idx].buildings & masks.source) {
            biome_power_push(seeds, idx);
        }

        int32_t x = idx % width, y = idx / width;
        for (int32_t dir = 0; dir < 4; dir ++) {
            int32_t nx = x + biome_power_dx[dir];
            int32_t ny = y + biome_power_dy[dir];
            if (nx < 0 || ny < 0 || nx >= width || ny >= depth) {
                continue;
            }

            int32_t nidx = ny * width + nx;
            if (pow[nidx].generation == gen) {
                continue;
            }
            if (!(occ[nidx].buildings & masks.body)) {
                continue;
            }

            biome_power_stamp(pow, nidx, gen, network);
            biome_power_push(queue, nidx);
        }
    }

    ecs_vec_clear(queue);
    for (int32_t s = 0; s < ecs_vec_count(seeds); s ++) {
        int32_t idx = ecs_vec_get_t(seeds, int32_t, s)[0];
        pow[idx].distance = 0;
        biome_power_push(queue, idx);
    }

    head = 0;
    while (head < ecs_vec_count(queue)) {
        int32_t idx = ecs_vec_get_t(queue, int32_t, head)[0];
        head ++;

        uint32_t dist = pow[idx].distance;
        int32_t x = idx % width, y = idx / width;
        for (int32_t dir = 0; dir < 4; dir ++) {
            int32_t nx = x + biome_power_dx[dir];
            int32_t ny = y + biome_power_dy[dir];
            if (nx < 0 || ny < 0 || nx >= width || ny >= depth) {
                continue;
            }

            int32_t nidx = ny * width + nx;
            if (pow[nidx].generation != gen) {
                continue;
            }
            if (pow[nidx].network != network) {
                continue;
            }
            if (pow[nidx].distance != BiomePowerUnreached) {
                continue;
            }

            pow[nidx].distance = dist + 1;
            biome_power_push(queue, nidx);
        }
    }
}

static void biome_power_setPowered(
    ecs_world_t *world,
    ecs_entity_t e,
    bool powered)
{
    BiomePowerConsumer *c = ecs_get_mut(world, e, BiomePowerConsumer);
    if (c) {
        c->powered = powered;
    } else {
        ecs_set(world, e, BiomePowerConsumer, { powered });
    }
}

static int biome_power_consumerCmp(const void *a, const void *b) {
    const biome_power_consumer_t *ca = a;
    const biome_power_consumer_t *cb = b;
    if (ca->distance != cb->distance) {
        return ca->distance < cb->distance ? -1 : 1;
    }
    if (ca->entity != cb->entity) {
        return ca->entity < cb->entity ? -1 : 1;
    }
    return 0;
}

static bool biome_power_rebuild(
    ecs_world_t *world,
    BiomePowerGrid *grid,
    ecs_entity_t terrain,
    const FlecsTerrain *t)
{
    TerrainPower *pow = flecsEngine_terrain_getLayer(
        world, terrain, TerrainPowerIndex, TerrainPower);
    const TerrainOccupancy *occ = flecsEngine_terrain_getLayer(
        world, terrain, TerrainOccupancyIndex, TerrainOccupancy);
    if (!pow || !occ) {
        return false;
    }

    grid->generation ++;
    uint32_t gen = grid->generation;
    int32_t width = t->width, depth = t->depth;

    biome_power_clearNetworks(grid);
    biome_power_masks_t masks = biome_power_masks(world, grid);

    ecs_vec_t queue, seeds;
    ecs_vec_init_t(NULL, &queue, int32_t, 0);
    ecs_vec_init_t(NULL, &seeds, int32_t, 0);

    ecs_iter_t it = ecs_query_iter(world, grid->buildings);
    while (ecs_query_next(&it)) {
        FlecsTerrainPosition *tp = ecs_field(&it, FlecsTerrainPosition, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            const BiomePower *power = ecs_get(world, e, BiomePower);
            if (!power || !power->producer) {
                continue;
            }
            if (tp[i].terrain != terrain) {
                continue;
            }

            int32_t fw = tp[i].span_x ? tp[i].span_x : 1;
            int32_t fh = tp[i].span_y ? tp[i].span_y : 1;
            if (tp[i].x < 0 || tp[i].y < 0 ||
                (tp[i].x + fw) > width || (tp[i].y + fh) > depth)
            {
                continue;
            }

            int32_t idx = tp[i].y * width + tp[i].x;
            if (pow[idx].generation != gen) {
                uint32_t network = biome_power_networkNew(grid);
                biome_power_flood(occ, pow, width, depth, gen, network,
                    masks, tp[i].x, tp[i].y, fw, fh, &queue, &seeds);
            }

            biome_power_network_t *networks = ecs_vec_first_t(
                &grid->networks, biome_power_network_t);
            biome_power_network_t *net = &networks[pow[idx].network];
            const BiomePowerProducer *producer = ecs_get(
                world, e, BiomePowerProducer);
            if (producer) {
                net->production += producer->production;
            }
            net->producer_count ++;
        }
    }

    it = ecs_query_iter(world, grid->buildings);
    while (ecs_query_next(&it)) {
        FlecsTerrainPosition *tp = ecs_field(&it, FlecsTerrainPosition, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            const BiomePower *power = ecs_get(world, e, BiomePower);
            if (!power || power->demand <= 0) {
                continue;
            }
            if (tp[i].terrain != terrain) {
                continue;
            }

            int32_t fw = tp[i].span_x ? tp[i].span_x : 1;
            int32_t fh = tp[i].span_y ? tp[i].span_y : 1;
            if (tp[i].x < 0 || tp[i].y < 0 ||
                (tp[i].x + fw) > width || (tp[i].y + fh) > depth)
            {
                continue;
            }

            uint32_t best = BiomePowerUnreached;
            uint32_t best_network = 0;

            for (int32_t y = tp[i].y; y < tp[i].y + fh; y ++) {
                for (int32_t x = tp[i].x; x < tp[i].x + fw; x ++) {
                    int32_t idx = y * width + x;
                    if (pow[idx].generation == gen &&
                        pow[idx].distance < best)
                    {
                        best = pow[idx].distance;
                        best_network = pow[idx].network;
                    }

                    for (int32_t dir = 0; dir < 4; dir ++) {
                        int32_t nx = x + biome_power_dx[dir];
                        int32_t ny = y + biome_power_dy[dir];
                        if (nx < 0 || ny < 0 || nx >= width || ny >= depth) {
                            continue;
                        }

                        int32_t nidx = ny * width + nx;
                        if (pow[nidx].generation != gen) {
                            continue;
                        }
                        if (pow[nidx].distance == BiomePowerUnreached) {
                            continue;
                        }
                        if ((pow[nidx].distance + 1) < best) {
                            best = pow[nidx].distance + 1;
                            best_network = pow[nidx].network;
                        }
                    }
                }
            }

            if (best != BiomePowerUnreached) {
                biome_power_network_t *networks = ecs_vec_first_t(
                    &grid->networks, biome_power_network_t);
                biome_power_consumer_t *c = ecs_vec_append_t(NULL,
                    &networks[best_network].consumers, biome_power_consumer_t);
                c->entity = e;
                c->distance = best;
                c->demand = power->demand;
            } else {
                biome_power_setPowered(world, e, false);
            }
        }
    }

    int32_t n, network_count = ecs_vec_count(&grid->networks);
    biome_power_network_t *networks = ecs_vec_first_t(
        &grid->networks, biome_power_network_t);
    for (n = 0; n < network_count; n ++) {
        int32_t consumer_count = ecs_vec_count(&networks[n].consumers);
        if (consumer_count > 1) {
            qsort(ecs_vec_first_t(&networks[n].consumers,
                biome_power_consumer_t), (size_t)consumer_count,
                sizeof(biome_power_consumer_t), biome_power_consumerCmp);
        }
    }

    ecs_vec_fini_t(NULL, &queue, int32_t);
    ecs_vec_fini_t(NULL, &seeds, int32_t);

    return true;
}

static void biome_power_distribute(
    ecs_world_t *world,
    BiomePowerGrid *grid)
{
    float total_production = 0;

    int32_t n, network_count = ecs_vec_count(&grid->networks);
    biome_power_network_t *networks = ecs_vec_first_t(
        &grid->networks, biome_power_network_t);

    for (n = 0; n < network_count; n ++) {
        biome_power_network_t *net = &networks[n];
        float budget = net->production;

        int32_t c, consumer_count = ecs_vec_count(&net->consumers);
        biome_power_consumer_t *consumers = ecs_vec_first_t(
            &net->consumers, biome_power_consumer_t);

        for (c = 0; c < consumer_count; c ++) {
            if (!ecs_is_alive(world, consumers[c].entity)) {
                continue;
            }

            bool powered = budget >= consumers[c].demand;
            if (powered) {
                budget -= consumers[c].demand;
            }

            biome_power_setPowered(world, consumers[c].entity, powered);
        }

        total_production += net->production;
    }

    float total_demand = 0, satisfied = 0;
    ecs_iter_t it = ecs_query_iter(world, grid->buildings);
    while (ecs_query_next(&it)) {
        BiomePower *power = ecs_field(&it, BiomePower, 1);
        for (int32_t i = 0; i < it.count; i ++) {
            if (power[i].demand <= 0) {
                continue;
            }

            total_demand += power[i].demand;
            const BiomePowerConsumer *consumer = ecs_get(
                world, it.entities[i], BiomePowerConsumer);
            if (consumer && consumer->powered) {
                satisfied += power[i].demand;
            }
        }
    }

    grid->total_production = total_production;
    grid->total_demand = total_demand;
    grid->satisfied_demand = satisfied;
}

void BiomePowerUpdate(ecs_iter_t *it) {
    ecs_world_t *world = it->world;
    BiomePowerGrid *grid = ecs_field(it, BiomePowerGrid, 0);

    ecs_entity_t terrain = biome_power_terrain(world);
    if (!terrain) {
        return;
    }

    const FlecsTerrain *t = ecs_get(world, terrain, FlecsTerrain);
    if (!t || t->width <= 0 || t->depth <= 0) {
        return;
    }
    if (ecs_vec_count(&t->layerTypes) <= TerrainPowerIndex) {
        return;
    }

    if (grid->dirty) {
        if (biome_power_rebuild(world, grid, terrain, t)) {
            grid->dirty = false;
        }
    }

    biome_power_distribute(world, grid);
}

static void BiomePowerPlacement(ecs_iter_t *it) {
    biome_power_markDirty(it->world);
}

void biomePowerImport(ecs_world_t *world) {
    ECS_MODULE(world, biomePower);

    ECS_IMPORT(world, biomeBuildings);

    ecs_set_name_prefix(world, "Biome");

    ECS_META_COMPONENT(world, BiomePower);
    ECS_META_COMPONENT(world, BiomePowerProducer);
    ECS_META_COMPONENT(world, BiomePowerConsumer);
    ECS_META_COMPONENT(world, TerrainPower);

    ECS_COMPONENT_DEFINE(world, BiomePowerGrid);

    ecs_set_hooks(world, BiomePowerGrid, {
        .ctor = BiomePowerGrid_ctor,
        .dtor = BiomePowerGrid_dtor
    });

    ecs_struct(world, {
        .entity = ecs_id(BiomePowerGrid),
        .members = {
            { .name = "dirty", .type = ecs_id(ecs_bool_t),
                .offset = offsetof(BiomePowerGrid, dirty) },
            { .name = "generation", .type = ecs_id(ecs_u32_t),
                .offset = offsetof(BiomePowerGrid, generation) },
            { .name = "total_production", .type = ecs_id(ecs_f32_t),
                .offset = offsetof(BiomePowerGrid, total_production) },
            { .name = "total_demand", .type = ecs_id(ecs_f32_t),
                .offset = offsetof(BiomePowerGrid, total_demand) },
            { .name = "satisfied_demand", .type = ecs_id(ecs_f32_t),
                .offset = offsetof(BiomePowerGrid, satisfied_demand) }
        }
    });

    ecs_set_hooks(world, BiomePower, {
        .on_set = BiomePower_onSet
    });

    ecs_add_id(world, ecs_id(BiomePowerGrid), EcsSingleton);

    BiomePowerGrid *grid = ecs_singleton_ensure(world, BiomePowerGrid);
    grid->dirty = true;
    grid->buildings = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "buildingsQuery" }),
        .terms = {
            { .id = ecs_id(FlecsTerrainPosition) },
            { .id = ecs_id(BiomePower) }
        }
    });
    grid->prefabs = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "prefabsQuery" }),
        .terms = {
            { .id = ecs_id(BiomeBuildingBit) },
            { .id = ecs_id(BiomePower) },
            { .id = EcsPrefab }
        }
    });

    ecs_observer(world, {
        .query.terms = {
            { .id = ecs_id(FlecsTerrainPosition) },
            { .id = ecs_id(BiomePower) }
        },
        .events = { EcsOnSet, EcsOnRemove },
        .callback = BiomePowerPlacement
    });

    ECS_SYSTEM(world, BiomePowerUpdate, EcsPreUpdate,
        [inout] BiomePowerGrid);

    ECS_SYSTEM(world, UpdatePowerTint, EcsPostUpdate,
        [in] BiomePowerConsumer);
}
