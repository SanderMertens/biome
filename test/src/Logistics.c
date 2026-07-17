#include "../../src/modules/logistics.c"
#include <biome_test.h>

ECS_COMPONENT_DECLARE(BiomeFactory);
ECS_COMPONENT_DECLARE(BiomePowerConsumer);
ECS_COMPONENT_DECLARE(BiomeResourceStorageDesc);
ECS_COMPONENT_DECLARE(BiomeResourceStorage);

void biomeBehaviorImport(ecs_world_t *world) {
    (void)world;
}

void biomeBuildingsImport(ecs_world_t *world) {
    (void)world;
}

void biomeResourcesImport(ecs_world_t *world) {
    (void)world;
}

static int32_t *Logistics_mapEnsure(
    BiomeResourceStorageMap *map,
    ecs_entity_t resource)
{
    if (!ecs_map_is_init(map)) {
        ecs_map_init(map, NULL);
    }
    return (int32_t*)ecs_map_ensure(map, (ecs_map_key_t)resource);
}

static ecs_entity_t Logistics_storage(
    ecs_world_t *world,
    biome_resource_storageKind_t kind,
    int32_t capacity)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, BiomeResourceStorageDesc, {kind, capacity});
    ecs_set(world, result, BiomeResourceStorage, {0});
    return result;
}

static void Logistics_storageFini(
    ecs_world_t *world,
    ecs_entity_t entity)
{
    BiomeResourceStorage *storage = ecs_get_mut(
        world, entity, BiomeResourceStorage);
    if (ecs_map_is_init(&storage->resources)) {
        ecs_map_fini(&storage->resources);
    }
    if (ecs_map_is_init(&storage->reserved)) {
        ecs_map_fini(&storage->reserved);
    }
}

static ecs_entity_t Logistics_firstRequest(
    ecs_world_t *world,
    int32_t expected_count)
{
    ecs_vec_t requests;
    ecs_vec_init_t(NULL, &requests, ecs_entity_t, 0);
    biome_logistics_collectRequests(world, &requests);
    test_int(ecs_vec_count(&requests), expected_count);
    ecs_entity_t result = expected_count
        ? ecs_vec_first_t(&requests, ecs_entity_t)[0]
        : 0;
    ecs_vec_fini_t(NULL, &requests, ecs_entity_t);
    return result;
}

void Logistics_first_come_first_serve(void) {
    ecs_world_t *world = ecs_init();

    ECS_COMPONENT_DEFINE(world, BiomeFactory);
    ECS_COMPONENT_DEFINE(world, BiomePowerConsumer);
    ECS_COMPONENT_DEFINE(world, BiomeResourceStorageDesc);
    ECS_COMPONENT_DEFINE(world, BiomeResourceStorage);
    ECS_META_COMPONENT(world, biome_logisticsRequestKind_t);
    ECS_META_COMPONENT(world, BiomeLogisticsRequest);

    ecs_entity_t jobs = ecs_entity(world, { .name = "jobs" });
    ecs_add_id(world, jobs, EcsOrderedChildren);

    ecs_entity_t resource = ecs_new(world);
    ecs_entity_t source_a = Logistics_storage(
        world, BiomeResourceStorageKindSource, 100);
    ecs_entity_t source_b = Logistics_storage(
        world, BiomeResourceStorageKindSource, 100);
    ecs_entity_t storage = Logistics_storage(
        world, BiomeResourceStorageKindStorage, 10);

    biome_logistics_postRequest(
        world, BiomeRequestPickup, source_a, resource, 20, 100);
    biome_logistics_postRequest(
        world, BiomeRequestPickup, source_b, resource, 5, -100);

    ecs_iter_t children = ecs_children(world, jobs);
    test_assert(ecs_children_next(&children));
    test_int(children.count, 2);
    ecs_entity_t first = children.entities[0];
    ecs_entity_t second = children.entities[1];

    BiomeLogisticsJob job;
    test_uint(Logistics_firstRequest(world, 1), second);
    test_assert(biome_logistics_tryRequest(
        world, second, true, &job));
    test_assert(ecs_is_alive(world, first));
    ecs_delete(world, second);

    ecs_set(world, storage, BiomeResourceStorageDesc, {
        BiomeResourceStorageKindStorage, 100
    });
    BiomeResourceStorage *storage_data = ecs_get_mut(
        world, storage, BiomeResourceStorage);
    *Logistics_mapEnsure(&storage_data->reserved, resource) = 0;
    test_uint(Logistics_firstRequest(world, 1), first);
    test_assert(biome_logistics_tryRequest(
        world, first, true, &job));
    ecs_delete(world, first);

    ecs_entity_t factory_resource = ecs_new(world);
    ecs_entity_t factory = Logistics_storage(
        world, BiomeResourceStorageKindSink, 100);
    ecs_set(world, factory, BiomeFactory, {
        0, BiomeFactoryOutputVent
    });
    BiomeResourceStorage *factory_storage = ecs_get_mut(
        world, factory, BiomeResourceStorage);
    *Logistics_mapEnsure(
        &factory_storage->reserved, factory_resource) = 5;

    ecs_entity_t source_c = Logistics_storage(
        world, BiomeResourceStorageKindSource, 100);
    biome_logistics_postRequest(
        world, BiomeRequestPickup, factory, factory_resource, 5, 100);
    biome_logistics_postRequest(
        world, BiomeRequestPickup, source_c, resource, 5, -100);

    children = ecs_children(world, jobs);
    test_assert(ecs_children_next(&children));
    test_int(children.count, 2);
    first = children.entities[0];
    second = children.entities[1];

    test_uint(Logistics_firstRequest(world, 1), second);
    test_assert(biome_logistics_tryRequest(
        world, second, true, &job));
    test_assert(ecs_is_alive(world, first));
    ecs_delete(world, second);

    *Logistics_mapEnsure(&storage_data->resources, factory_resource) = 5;
    *Logistics_mapEnsure(&storage_data->reserved, resource) = 0;
    test_uint(Logistics_firstRequest(world, 1), first);
    test_assert(biome_logistics_tryRequest(
        world, first, true, &job));

    Logistics_storageFini(world, source_a);
    Logistics_storageFini(world, source_b);
    Logistics_storageFini(world, source_c);
    Logistics_storageFini(world, storage);
    Logistics_storageFini(world, factory);
    ecs_fini(world);
}
