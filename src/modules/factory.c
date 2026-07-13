#define BIOME_FACTORY_IMPL

#include "biome.h"

int32_t biome_factory_canAfford(
    const ecs_world_t *world,
    ecs_entity_t item)
{
    const BiomeRecipe *recipe = ecs_get(world, item, BiomeRecipe);
    if (!recipe || !ecs_map_is_init(&recipe->inputs)) {
        return INT32_MAX;
    }

    ecs_world_t *w = ECS_CONST_CAST(ecs_world_t*, world);
    ecs_value_t totals = biome_playerAttr_get(w, "ResourceTotals");
    if (!totals.ptr) {
        return 0;
    }

    int32_t result = INT32_MAX;
    ecs_map_iter_t rit = ecs_map_iter(&recipe->inputs);
    while (ecs_map_next(&rit)) {
        int32_t required = (int32_t)ecs_map_value(&rit);
        if (required <= 0) {
            continue;
        }

        int32_t have = 0;
        ecs_map_val_t *v = ecs_map_get(totals.ptr, ecs_map_key(&rit));
        if (v) {
            have = *(int32_t*)v;
        }

        int32_t affordable = have / required;
        if (affordable < result) {
            result = affordable;
        }
    }

    return result;
}

bool biome_factory_purchase(
    const ecs_world_t *world,
    ecs_entity_t item,
    int32_t count)
{
    if (count < 0) {
        return false;
    }
    if (!count) {
        return true;
    }

    const BiomeRecipe *recipe = ecs_get(world, item, BiomeRecipe);
    if (!recipe || !ecs_map_is_init(&recipe->inputs)) {
        return true;
    }

    int32_t num_inputs = ecs_map_count(&recipe->inputs);
    if (!num_inputs) {
        return true;
    }

    ecs_world_t *w = ECS_CONST_CAST(ecs_world_t*, world);

    ecs_entity_t *resources = ecs_os_malloc_n(ecs_entity_t, num_inputs);
    int32_t *remaining = ecs_os_malloc_n(int32_t, num_inputs);
    int32_t *available = ecs_os_malloc_n(int32_t, num_inputs);

    int32_t i = 0;
    ecs_map_iter_t rit = ecs_map_iter(&recipe->inputs);
    while (ecs_map_next(&rit)) {
        resources[i] = ecs_map_key(&rit);
        remaining[i] = (int32_t)ecs_map_value(&rit) * count;
        available[i] = 0;
        i ++;
    }

    ecs_query_t *q = ecs_query(w, {
        .terms = {
            { ecs_id(BiomeResourceStorage) },
            { ecs_id(BiomeResourceStorageDesc), .inout = EcsIn }
        }
    });

    ecs_iter_t it = ecs_query_iter(w, q);
    while (ecs_query_next(&it)) {
        BiomeResourceStorage *storage = ecs_field(
            &it, BiomeResourceStorage, 0);
        for (int32_t e = 0; e < it.count; e ++) {
            if (!ecs_map_is_init(&storage[e])) {
                continue;
            }

            for (i = 0; i < num_inputs; i ++) {
                ecs_map_val_t *v = ecs_map_get(&storage[e], resources[i]);
                if (v) {
                    available[i] += *(int32_t*)v;
                }
            }
        }
    }

    bool result = true;
    for (i = 0; i < num_inputs; i ++) {
        if (available[i] < remaining[i]) {
            result = false;
            break;
        }
    }

    if (result) {
        it = ecs_query_iter(w, q);
        while (ecs_query_next(&it)) {
            BiomeResourceStorage *storage = ecs_field(
                &it, BiomeResourceStorage, 0);
            for (int32_t e = 0; e < it.count; e ++) {
                if (!ecs_map_is_init(&storage[e])) {
                    continue;
                }

                bool modified = false;
                for (i = 0; i < num_inputs; i ++) {
                    if (remaining[i] <= 0) {
                        continue;
                    }

                    ecs_map_val_t *v = ecs_map_get(
                        &storage[e], resources[i]);
                    if (!v) {
                        continue;
                    }

                    int32_t stored = *(int32_t*)v;
                    if (stored <= 0) {
                        continue;
                    }

                    int32_t take = stored < remaining[i] ?
                        stored : remaining[i];
                    *(int32_t*)v = stored - take;
                    remaining[i] -= take;
                    modified = true;
                }

                if (modified) {
                    ecs_modified_id(w, it.entities[e],
                        ecs_id(BiomeResourceStorage));
                }
            }
        }
    }

    ecs_query_fini(q);
    ecs_os_free(resources);
    ecs_os_free(remaining);
    ecs_os_free(available);

    return result;
}

void biome_factory_refund(
    const ecs_world_t *world,
    ecs_entity_t item,
    int32_t count)
{
    if (count <= 0) {
        return;
    }

    const BiomeRecipe *recipe = ecs_get(world, item, BiomeRecipe);
    if (!recipe || !ecs_map_is_init(&recipe->inputs)) {
        return;
    }

    int32_t num_inputs = ecs_map_count(&recipe->inputs);
    if (!num_inputs) {
        return;
    }

    ecs_world_t *w = ECS_CONST_CAST(ecs_world_t*, world);

    ecs_entity_t *resources = ecs_os_malloc_n(ecs_entity_t, num_inputs);
    int32_t *remaining = ecs_os_malloc_n(int32_t, num_inputs);

    int32_t i = 0;
    ecs_map_iter_t rit = ecs_map_iter(&recipe->inputs);
    while (ecs_map_next(&rit)) {
        resources[i] = ecs_map_key(&rit);
        remaining[i] = (int32_t)ecs_map_value(&rit) * count;
        i ++;
    }

    ecs_query_t *q = ecs_query(w, {
        .terms = {
            { ecs_id(BiomeResourceStorage) },
            { ecs_id(BiomeResourceStorageDesc), .inout = EcsIn }
        }
    });

    ecs_iter_t it = ecs_query_iter(w, q);
    while (ecs_query_next(&it)) {
        BiomeResourceStorage *storage = ecs_field(
            &it, BiomeResourceStorage, 0);
        BiomeResourceStorageDesc *desc = ecs_field(
            &it, BiomeResourceStorageDesc, 1);
        for (int32_t e = 0; e < it.count; e ++) {
            if (!ecs_map_is_init(&storage[e])) {
                continue;
            }

            int32_t capacity = desc[e].capacity;
            bool modified = false;
            for (i = 0; i < num_inputs; i ++) {
                if (remaining[i] <= 0) {
                    continue;
                }

                ecs_map_val_t *v = ecs_map_get(&storage[e], resources[i]);
                if (!v) {
                    continue;
                }

                int32_t stored = *(int32_t*)v;
                int32_t room = capacity - stored;
                if (room <= 0) {
                    continue;
                }

                int32_t give = room < remaining[i] ? room : remaining[i];
                *(int32_t*)v = stored + give;
                remaining[i] -= give;
                modified = true;
            }

            if (modified) {
                ecs_modified_id(w, it.entities[e],
                    ecs_id(BiomeResourceStorage));
            }
        }
    }

    ecs_query_fini(q);
    ecs_os_free(resources);
    ecs_os_free(remaining);
}

void biomeFactoryImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeFactory);

    ECS_IMPORT(world, biomeResources);
}
