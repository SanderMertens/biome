#define BIOME_RESOURCES_IMPL

#include "biome.h"

ECS_COMPONENT_DECLARE(BiomeResourceStorageMap);
ECS_COMPONENT_DECLARE(biome_resource_storageKind_t);

static void BiomeResourceSumTotals(ecs_iter_t *it) {
    ecs_map_t totals = {0};
    ecs_map_init(&totals, NULL);

    ecs_map_t capacityTotals = {0};
    ecs_map_init(&capacityTotals, NULL);

    ecs_value_t v = biome_playerAttr_get(it->real_world, "ResourceTotals");
    if (!v.ptr) {
        ecs_err("ResourceTotals is missing");
        ecs_map_fini(&totals);
        ecs_map_fini(&capacityTotals);
        return;
    }

    /* Make sure resulting maps have at least the same keys they had before */
    ecs_map_iter_t mit = ecs_map_iter(v.ptr);
    while (ecs_map_next(&mit)) {
        ecs_entity_t resource = ecs_map_key(&mit);
        ecs_map_ensure(&totals, resource);
        ecs_map_ensure(&capacityTotals, resource);
    }

    /* Sum totals from all storages */
    while (ecs_query_next(it)) {
        BiomeResourceStorage *storage = ecs_field(it, BiomeResourceStorage, 0);
        BiomeResourceStorageDesc *desc = ecs_field(it, BiomeResourceStorageDesc, 1);

        for (int i = 0; i < it->count; i ++) {
            BiomeResourceStorage *s = &storage[i];
            BiomeResourceStorageDesc *d = &desc[i];

            if (d->kind != BiomeResourceStorageKindStorage ||
                !ecs_map_is_init(&s->resources))
            {
                continue;
            }

            mit = ecs_map_iter(&s->resources);
            while (ecs_map_next(&mit)) {
                ecs_entity_t resource = ecs_map_key(&mit);

                {
                    int32_t amount = (int32_t)ecs_map_value(&mit);
                    ecs_map_val_t *total = ecs_map_ensure(&totals, resource);
                    *(int32_t*)total += amount;
                }
                {
                    ecs_map_val_t *total = ecs_map_ensure(&capacityTotals, resource);
                    *(int32_t*)total += d->capacity;
                }
            }
        }
    }

    /* First make space, then up date totals */
    biome_playerAttr_set(it->real_world, "ResourceCapacityTotals", 
        &(ecs_value_t) {
            .type = ecs_id(BiomeResourceStorageMap),
            .ptr = &capacityTotals
        });

    biome_playerAttr_set(it->real_world, "ResourceTotals", 
        &(ecs_value_t) {
            .type = ecs_id(BiomeResourceStorageMap),
            .ptr = &totals
        });

    ecs_map_fini(&totals);
    ecs_map_fini(&capacityTotals);
}

void biomeResourcesImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeResources);

    ecs_id(BiomeResourceStorageMap) = ecs_map_type(world, {
        .entity = ecs_entity(world, {
            .name = "StorageMap",
            .symbol = "BiomeResourceStorageMap"
        }),
        .key_type = ecs_id(ecs_entity_t),
        .type = ecs_id(ecs_i32_t)
    });

    ecs_id(biome_resource_storageKind_t) = ecs_enum(world, {
        .entity = ecs_entity(world, {
            .name = "StorageKind",
            .symbol = "biome_resource_storageKind_t"
        }),
        .constants = {
            {"Source", BiomeResourceStorageKindSource},
            {"Storage", BiomeResourceStorageKindStorage},
            {"Sink", BiomeResourceStorageKindSink}
        }
    });

    ecs_set_name_prefix(world, "Biome");
    ECS_META_COMPONENT(world, BiomeResource);
    ECS_META_COMPONENT(world, BiomeRecipe);

    ecs_set_name_prefix(world, "BiomeResource");
    ECS_META_COMPONENT(world, BiomeResourceStorageDesc);
    ECS_META_COMPONENT(world, BiomeResourceStorage);

    ecs_system(world, {
        .query.terms = {
            { ecs_id(BiomeResourceStorage) },
            { ecs_id(BiomeResourceStorageDesc) }
        },
        .phase = EcsPreUpdate,
        .run = BiomeResourceSumTotals
    });
}
