#include "../../src/modules/factory.c"
#include <biome_test.h>

ECS_COMPONENT_DECLARE(BiomeRecipe);

void Logistics_setupWorld(ecs_world_t *world);

void biomeWeatherImport(ecs_world_t *world) {
    (void)world;
}

void biomePowerImport(ecs_world_t *world) {
    (void)world;
}

static int32_t *Factory_mapEnsure(
    BiomeResourceStorageMap *map,
    ecs_entity_t resource)
{
    if (!ecs_map_is_init(map)) {
        ecs_map_init(map, NULL);
    }
    return (int32_t*)ecs_map_ensure(map, (ecs_map_key_t)resource);
}

static const BiomeLogisticsRequest *Factory_request(
    const ecs_world_t *world,
    ecs_entity_t request)
{
    ecs_entity_t kind = ecs_get_target(
        world, request, ecs_id(BiomeLogisticsRequest), 0);
    return ecs_get_id(world, request, ecs_pair(
        ecs_id(BiomeLogisticsRequest), kind));
}

void Factory_request_drone_amount(void) {
    ecs_world_t *world = ecs_init();

    Logistics_setupWorld(world);
    ECS_COMPONENT_DEFINE(world, BiomeRecipe);

    ecs_entity_t resource = ecs_new(world);
    ecs_set(world, resource, BiomeResource, {
        .max_drone_amount = 200
    });

    BiomeRecipe recipe = {0};
    *Factory_mapEnsure(&recipe.inputs, resource) = 20;

    ecs_entity_t factory = ecs_new(world);
    ecs_set(world, factory, BiomeResourceStorageDesc, {
        BiomeResourceStorageKindSink, 500
    });
    ecs_set(world, factory, BiomeResourceStorage, {0});
    BiomeResourceStorage *storage = ecs_get_mut(
        world, factory, BiomeResourceStorage);
    *Factory_mapEnsure(&storage->resources, resource) = 100;
    *Factory_mapEnsure(&storage->reserved, resource) = 50;

    const BiomeResourceStorageDesc *desc = ecs_get(
        world, factory, BiomeResourceStorageDesc);
    biome_factory_requestInputs(
        world, factory, &recipe, desc, storage);

    test_int(*Factory_mapEnsure(
        &storage->reserved, resource), 250);
    test_int(ecs_vec_count(&storage->outstanding_requests), 1);
    ecs_entity_t request_entity = ecs_vec_first_t(
        &storage->outstanding_requests, ecs_entity_t)[0];
    const BiomeLogisticsRequest *request = Factory_request(
        world, request_entity);
    test_assert(request != NULL);
    test_int(request->amount, 200);

    ecs_delete(world, request_entity);
    ecs_vec_clear(&storage->outstanding_requests);
    *Factory_mapEnsure(&storage->resources, resource) = 430;
    *Factory_mapEnsure(&storage->reserved, resource) = 50;
    biome_factory_requestInputs(
        world, factory, &recipe, desc, storage);

    test_int(*Factory_mapEnsure(
        &storage->reserved, resource), 70);
    test_int(ecs_vec_count(&storage->outstanding_requests), 1);
    request_entity = ecs_vec_first_t(
        &storage->outstanding_requests, ecs_entity_t)[0];
    request = Factory_request(world, request_entity);
    test_assert(request != NULL);
    test_int(request->amount, 20);

    ecs_delete(world, request_entity);
    ecs_map_fini(&recipe.inputs);
    ecs_map_fini(&storage->resources);
    ecs_map_fini(&storage->reserved);
    ecs_vec_fini_t(
        NULL, &storage->outstanding_requests, ecs_entity_t);
    ecs_fini(world);
}

void Factory_request_drone_amount_edge_cases(void) {
    ecs_world_t *world = ecs_init();

    Logistics_setupWorld(world);
    ECS_COMPONENT_DEFINE(world, BiomeRecipe);

    ecs_entity_t factory = ecs_new(world);
    ecs_set(world, factory, BiomeResourceStorageDesc, {
        BiomeResourceStorageKindSink, 100
    });
    ecs_set(world, factory, BiomeResourceStorage, {0});
    BiomeResourceStorage *storage = ecs_get_mut(
        world, factory, BiomeResourceStorage);
    const BiomeResourceStorageDesc *desc = ecs_get(
        world, factory, BiomeResourceStorageDesc);

    BiomeRecipe recipe = {0};
    biome_factory_requestInputs(
        world, factory, &recipe, desc, storage);
    test_int(ecs_vec_count(&storage->outstanding_requests), 0);

    ecs_map_init(&recipe.inputs, NULL);
    biome_factory_requestInputs(
        world, factory, &recipe, desc, storage);
    test_int(ecs_vec_count(&storage->outstanding_requests), 0);

    ecs_entity_t resource = ecs_new(world);
    *Factory_mapEnsure(&recipe.inputs, resource) = 10;
    biome_factory_requestInputs(
        world, factory, &recipe, desc, storage);
    test_int(ecs_vec_count(&storage->outstanding_requests), 0);

    ecs_set(world, resource, BiomeResource, {
        .max_drone_amount = 0
    });
    biome_factory_requestInputs(
        world, factory, &recipe, desc, storage);
    test_int(ecs_vec_count(&storage->outstanding_requests), 0);

    ecs_set(world, resource, BiomeResource, {
        .max_drone_amount = 50
    });
    *Factory_mapEnsure(&storage->resources, resource) = 80;
    *Factory_mapEnsure(&storage->reserved, resource) = 20;
    biome_factory_requestInputs(
        world, factory, &recipe, desc, storage);
    test_int(ecs_vec_count(&storage->outstanding_requests), 0);
    test_int(biome_factory_mapValue(
        &storage->reserved, resource), 20);

    ecs_map_fini(&recipe.inputs);
    ecs_map_fini(&storage->resources);
    ecs_map_fini(&storage->reserved);
    ecs_fini(world);
}
