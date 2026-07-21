#define BIOME_RESOURCES_IMPL
#define BIOME_LOGISTICS_IMPL
#include "../../src/modules/resources.c"
#include "../../src/modules/logistics.c"
#include "../../src/tasks/logistics.c"
#include <biome_test.h>

ECS_COMPONENT_DECLARE(BiomeFactory);
ECS_COMPONENT_DECLARE(BiomePowerConsumer);
ECS_COMPONENT_DECLARE(BiomeResourceStorageDesc);
ECS_COMPONENT_DECLARE(BiomeResourceStorage);
ECS_COMPONENT_DECLARE(BiomeResource);
ECS_COMPONENT_DECLARE(BiomeResourceMiner);
ECS_COMPONENT_DECLARE(BiomePlayerStorage);

static ecs_map_t Logistics_player_totals;
static ecs_map_t Logistics_player_capacity;
static ecs_map_t Logistics_player_reserved;
static ecs_entity_t Logistics_player_storage;

void biomeBehaviorImport(ecs_world_t *world) {
    (void)world;
}

void biomeBuildingsImport(ecs_world_t *world) {
    (void)world;
}

static ecs_map_t *Logistics_playerMap(
    const char *attribute)
{
    return !ecs_os_strcmp(attribute, "ResourceTotals")
        ? &Logistics_player_totals
        : !ecs_os_strcmp(attribute, "ResourceCapacityTotals")
            ? &Logistics_player_capacity
            : &Logistics_player_reserved;
}

ecs_value_t biome_playerAttr_get(
    ecs_world_t *world,
    const char *attribute)
{
    (void)world;
    return (ecs_value_t) {
        .type = ecs_id(BiomeResourceStorageMap),
        .ptr = Logistics_playerMap(attribute)
    };
}

void biome_playerAttr_set(
    ecs_world_t *world,
    const char *attribute,
    const ecs_value_t *value)
{
    (void)world;
    ecs_map_t *map = Logistics_playerMap(attribute);
    ecs_map_clear(map);
    ecs_map_copy(map, value->ptr);
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

static ecs_entity_t Logistics_playerStorage(
    ecs_world_t *world,
    ecs_entity_t resource,
    int32_t capacity)
{
    ecs_entity_t result = ecs_new(world);
    ecs_set(world, result, BiomePlayerStorage, {capacity, {0}});
    *(int32_t*)ecs_map_ensure(&Logistics_player_capacity, resource) =
        capacity;
    Logistics_player_storage = result;
    return result;
}

static void Logistics_storageFini(
    ecs_world_t *world,
    ecs_entity_t entity)
{
    BiomeResourceStorage *storage = ecs_get_mut(
        world, entity, BiomeResourceStorage);
    if (!storage) {
        return;
    }
    if (ecs_map_is_init(&storage->resources)) {
        ecs_map_fini(&storage->resources);
    }
    if (ecs_map_is_init(&storage->reserved)) {
        ecs_map_fini(&storage->reserved);
    }
    if (storage->outstanding_requests.array) {
        ecs_vec_fini_t(
            NULL, &storage->outstanding_requests, ecs_entity_t);
    }
}

static ecs_entity_t Logistics_firstRequest(
    ecs_world_t *world,
    int32_t expected_count)
{
    ecs_vec_t requests;
    ecs_vec_init_t(NULL, &requests, ecs_entity_t, 0);
    biome_logistics_collectRequests(
        world, Logistics_player_storage, &requests);
    test_int(ecs_vec_count(&requests), expected_count);
    ecs_entity_t result = expected_count
        ? ecs_vec_first_t(&requests, ecs_entity_t)[0]
        : 0;
    ecs_vec_fini_t(NULL, &requests, ecs_entity_t);
    return result;
}

void Logistics_setupWorld(ecs_world_t *world) {
    if (ecs_map_is_init(&Logistics_player_totals)) {
        ecs_map_fini(&Logistics_player_totals);
        ecs_map_fini(&Logistics_player_capacity);
        ecs_map_fini(&Logistics_player_reserved);
    }
    ecs_map_init(&Logistics_player_totals, NULL);
    ecs_map_init(&Logistics_player_capacity, NULL);
    ecs_map_init(&Logistics_player_reserved, NULL);
    Logistics_player_storage = 0;

    ECS_COMPONENT_DEFINE(world, BiomeFactory);
    ECS_COMPONENT_DEFINE(world, BiomePowerConsumer);
    ECS_COMPONENT_DEFINE(world, BiomeLogisticsCarrier);
    ECS_COMPONENT_DEFINE(world, BiomeResourceStorageDesc);
    ECS_COMPONENT_DEFINE(world, BiomeResourceStorage);
    ECS_COMPONENT_DEFINE(world, BiomeResource);
    ECS_COMPONENT_DEFINE(world, BiomeResourceMiner);
    ECS_COMPONENT_DEFINE(world, BiomePlayerStorage);
    ECS_META_COMPONENT(world, biome_logisticsRequestKind_t);
    ECS_META_COMPONENT(world, BiomeLogisticsRequest);

    ecs_entity_t jobs = ecs_entity(world, { .name = "jobs" });
    ecs_add_id(world, jobs, EcsOrderedChildren);
}

static ecs_world_t *Logistics_world(void) {
    ecs_world_t *world = ecs_init();
    Logistics_setupWorld(world);
    return world;
}

static void Logistics_setResource(
    ecs_world_t *world,
    ecs_entity_t storage,
    ecs_entity_t resource,
    int32_t amount)
{
    BiomeResourceStorage *value = ecs_get_mut(
        world, storage, BiomeResourceStorage);
    *Logistics_mapEnsure(&value->resources, resource) = amount;
}

static ecs_entity_t Logistics_request(
    ecs_world_t *world,
    ecs_entity_t source,
    int32_t index)
{
    const BiomeResourceStorage *storage = ecs_get(
        world, source, BiomeResourceStorage);
    test_assert(storage != NULL);
    test_assert(index < ecs_vec_count(&storage->outstanding_requests));
    return ecs_vec_first_t(
        &storage->outstanding_requests, ecs_entity_t)[index];
}

void Logistics_first_come_first_serve(void) {
    ecs_world_t *world = ecs_init();

    Logistics_setupWorld(world);
    ecs_entity_t jobs = ecs_lookup(world, "jobs");

    ecs_entity_t resource = ecs_new(world);
    ecs_entity_t source_a = Logistics_storage(
        world, BiomeResourceStorageKindSource, 100);
    ecs_entity_t source_b = Logistics_storage(
        world, BiomeResourceStorageKindSource, 100);
    ecs_entity_t storage = Logistics_playerStorage(
        world, resource, 10);
    *Logistics_mapEnsure(
        &ecs_get_mut(world, source_a, BiomeResourceStorage)->resources,
        resource) = 20;
    *Logistics_mapEnsure(
        &ecs_get_mut(world, source_b, BiomeResourceStorage)->resources,
        resource) = 5;

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
        world, second, storage, true, &job));
    test_assert(ecs_is_alive(world, first));
    ecs_delete(world, second);

    *(int32_t*)ecs_map_ensure(
        &Logistics_player_capacity, resource) = 100;
    *(int32_t*)ecs_map_ensure(
        &Logistics_player_reserved, resource) = 0;
    test_uint(Logistics_firstRequest(world, 1), first);
    test_assert(biome_logistics_tryRequest(
        world, first, storage, true, &job));
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
    *Logistics_mapEnsure(
        &ecs_get_mut(world, source_c, BiomeResourceStorage)->resources,
        resource) = 5;
    biome_logistics_postRequest(
        world, BiomeRequestDropOff, factory, factory_resource, 5, 100);
    biome_logistics_postRequest(
        world, BiomeRequestPickup, source_c, resource, 5, -100);

    children = ecs_children(world, jobs);
    test_assert(ecs_children_next(&children));
    test_int(children.count, 2);
    first = children.entities[0];
    second = children.entities[1];

    test_uint(Logistics_firstRequest(world, 1), second);
    test_assert(biome_logistics_tryRequest(
        world, second, storage, true, &job));
    test_assert(ecs_is_alive(world, first));
    ecs_delete(world, second);

    *(int32_t*)ecs_map_ensure(
        &Logistics_player_totals, factory_resource) = 5;
    *(int32_t*)ecs_map_ensure(
        &Logistics_player_reserved, resource) = 0;
    test_uint(Logistics_firstRequest(world, 1), first);
    test_assert(biome_logistics_tryRequest(
        world, first, storage, true, &job));

    Logistics_storageFini(world, source_a);
    Logistics_storageFini(world, source_b);
    Logistics_storageFini(world, source_c);
    Logistics_storageFini(world, storage);
    Logistics_storageFini(world, factory);
    ecs_fini(world);
}

static void Logistics_deleteRequests(
    ecs_world_t *world,
    ecs_entity_t source)
{
    BiomeResourceStorage *storage = ecs_get_mut(
        world, source, BiomeResourceStorage);
    while (ecs_vec_count(&storage->outstanding_requests)) {
        ecs_entity_t request = ecs_vec_first_t(
            &storage->outstanding_requests, ecs_entity_t)[0];
        biome_logistics_removeOutstandingRequest(
            world, source, request);
        ecs_delete(world, request);
        storage = ecs_get_mut(world, source, BiomeResourceStorage);
    }
}

void Logistics_combine_requests(void) {
    ecs_world_t *world = ecs_init();

    Logistics_setupWorld(world);

    ecs_entity_t resource = ecs_new(world);
    ecs_set(world, resource, BiomeResource, {
        .max_drone_amount = 100
    });

    ecs_entity_t pickup_source = Logistics_storage(
        world, BiomeResourceStorageKindSource, 300);
    ecs_entity_t storage = Logistics_playerStorage(
        world, resource, 70);
    *Logistics_mapEnsure(
        &ecs_get_mut(
            world, pickup_source, BiomeResourceStorage)->resources,
        resource) = 200;

    for (int32_t i = 0; i < 3; i ++) {
        biome_logistics_postRequest(
            world, BiomeRequestPickup, pickup_source,
            resource, 30, 0);
    }

    BiomeResourceStorage *pickup_storage = ecs_get_mut(
        world, pickup_source, BiomeResourceStorage);
    test_int(
        ecs_vec_count(&pickup_storage->outstanding_requests), 3);
    ecs_entity_t pickup_request = ecs_vec_first_t(
        &pickup_storage->outstanding_requests, ecs_entity_t)[0];

    ecs_vec_t accepted;
    ecs_vec_init_t(NULL, &accepted, ecs_entity_t, 0);
    BiomeLogisticsJob job;
    test_assert(biome_logistics_acceptRequest(
        world, pickup_request, storage, &accepted, &job));
    test_int(ecs_vec_count(&accepted), 2);
    test_int(job.amount, 60);
    test_uint(job.src, pickup_source);
    test_uint(job.dst, storage);
    Logistics_deleteRequests(world, pickup_source);

    *(int32_t*)ecs_map_ensure(
        &Logistics_player_totals, resource) = 70;
    *(int32_t*)ecs_map_ensure(
        &Logistics_player_reserved, resource) = 0;

    ecs_entity_t dropoff_target = Logistics_storage(
        world, BiomeResourceStorageKindSink, 300);
    for (int32_t i = 0; i < 3; i ++) {
        biome_logistics_postRequest(
            world, BiomeRequestDropOff, dropoff_target,
            resource, 30, 0);
    }

    BiomeResourceStorage *dropoff_storage = ecs_get_mut(
        world, dropoff_target, BiomeResourceStorage);
    test_int(
        ecs_vec_count(&dropoff_storage->outstanding_requests), 3);
    ecs_entity_t dropoff_request = ecs_vec_first_t(
        &dropoff_storage->outstanding_requests, ecs_entity_t)[0];

    test_assert(biome_logistics_acceptRequest(
        world, dropoff_request, storage, &accepted, &job));
    test_int(ecs_vec_count(&accepted), 2);
    test_int(job.amount, 60);
    test_uint(job.src, storage);
    test_uint(job.dst, dropoff_target);
    Logistics_deleteRequests(world, dropoff_target);

    *(int32_t*)ecs_map_ensure(
        &Logistics_player_totals, resource) = 200;
    *(int32_t*)ecs_map_ensure(
        &Logistics_player_reserved, resource) = 0;
    biome_logistics_postRequest(
        world, BiomeRequestDropOff, dropoff_target,
        resource, 120, 0);
    dropoff_storage = ecs_get_mut(
        world, dropoff_target, BiomeResourceStorage);
    test_int(
        ecs_vec_count(&dropoff_storage->outstanding_requests), 2);
    ecs_entity_t *split_requests = ecs_vec_first_t(
        &dropoff_storage->outstanding_requests, ecs_entity_t);
    BiomeLogisticsRequest split;
    test_assert(biome_logistics_getRequest(
        world, split_requests[0], &split));
    test_int(split.amount, 100);
    test_assert(biome_logistics_getRequest(
        world, split_requests[1], &split));
    test_int(split.amount, 20);
    Logistics_deleteRequests(world, dropoff_target);

    ecs_vec_fini_t(NULL, &accepted, ecs_entity_t);
    Logistics_storageFini(world, pickup_source);
    Logistics_storageFini(world, storage);
    Logistics_storageFini(world, dropoff_target);
    ecs_fini(world);
}

void Logistics_track_outstanding_requests(void) {
    ecs_world_t *world = Logistics_world();

    ecs_entity_t resource = ecs_new(world);
    ecs_set(world, resource, BiomeResource, {
        .max_drone_amount = 100
    });

    ecs_entity_t storage_source = Logistics_storage(
        world, BiomeResourceStorageKindSource, 100);
    ecs_entity_t factory = Logistics_storage(
        world, BiomeResourceStorageKindSink, 100);
    ecs_entity_t drill = Logistics_storage(
        world, BiomeResourceStorageKindSource, 100);
    ecs_entity_t central = Logistics_playerStorage(
        world, resource, 300);
    ecs_set(world, factory, BiomeFactory, {0});
    ecs_set(world, drill, BiomeResourceMiner, {0});

    Logistics_setResource(
        world, storage_source, resource, 20);
    Logistics_setResource(world, drill, resource, 20);
    *(int32_t*)ecs_map_ensure(
        &Logistics_player_totals, resource) = 20;

    biome_logistics_postRequest(
        world, BiomeRequestPickup, storage_source, resource, 10, 0);
    biome_logistics_postRequest(
        world, BiomeRequestDropOff, factory, resource, 10, 0);
    biome_logistics_postRequest(
        world, BiomeRequestPickup, drill, resource, 10, 0);

    test_int(ecs_vec_count(
        &ecs_get(world, storage_source,
            BiomeResourceStorage)->outstanding_requests), 1);
    test_int(ecs_vec_count(
        &ecs_get(world, factory,
            BiomeResourceStorage)->outstanding_requests), 1);
    test_int(ecs_vec_count(
        &ecs_get(world, drill,
            BiomeResourceStorage)->outstanding_requests), 1);

    ecs_entity_t factory_request = Logistics_request(
        world, factory, 0);
    ecs_vec_t accepted;
    ecs_vec_init_t(NULL, &accepted, ecs_entity_t, 0);
    BiomeLogisticsJob job;
    test_assert(biome_logistics_acceptRequest(
        world, factory_request, central, &accepted, &job));
    biome_logistics_finishRequests(world, &accepted);

    test_int(ecs_vec_count(
        &ecs_get(world, factory,
            BiomeResourceStorage)->outstanding_requests), 0);
    test_int(ecs_vec_count(
        &ecs_get(world, storage_source,
            BiomeResourceStorage)->outstanding_requests), 1);
    test_int(ecs_vec_count(
        &ecs_get(world, drill,
            BiomeResourceStorage)->outstanding_requests), 1);
    test_assert(!ecs_is_alive(world, factory_request));

    Logistics_deleteRequests(world, storage_source);
    Logistics_deleteRequests(world, drill);
    ecs_vec_fini_t(NULL, &accepted, ecs_entity_t);
    Logistics_storageFini(world, storage_source);
    Logistics_storageFini(world, factory);
    Logistics_storageFini(world, drill);
    Logistics_storageFini(world, central);
    ecs_fini(world);
}

void Logistics_combine_pickup_requests(void) {
    ecs_world_t *world = Logistics_world();

    ecs_entity_t resource = ecs_new(world);
    ecs_set(world, resource, BiomeResource, {
        .max_drone_amount = 100
    });
    ecs_entity_t factory = Logistics_storage(
        world, BiomeResourceStorageKindSource, 300);
    ecs_entity_t storage = Logistics_playerStorage(
        world, resource, 300);
    ecs_set(world, factory, BiomeFactory, {0});
    Logistics_setResource(world, factory, resource, 200);

    for (int32_t i = 0; i < 4; i ++) {
        biome_logistics_postRequest(
            world, BiomeRequestPickup, factory, resource, 30, 0);
    }

    ecs_vec_t accepted;
    ecs_vec_init_t(NULL, &accepted, ecs_entity_t, 0);
    BiomeLogisticsJob job;
    ecs_entity_t combined_request = Logistics_request(
        world, factory, 1);
    test_assert(biome_logistics_acceptRequest(
        world, Logistics_request(world, factory, 0),
        storage, &accepted, &job));
    test_int(ecs_vec_count(&accepted), 3);
    test_int(job.amount, 90);
    test_uint(job.src, factory);
    test_uint(job.dst, storage);

    biome_logistics_finishRequests(world, &accepted);
    test_assert(!ecs_is_alive(world, combined_request));
    test_assert(!biome_logistics_acceptRequest(
        world, combined_request, storage, &accepted, &job));
    test_int(ecs_vec_count(
        &ecs_get(world, factory,
            BiomeResourceStorage)->outstanding_requests), 1);

    Logistics_deleteRequests(world, factory);
    ecs_vec_fini_t(NULL, &accepted, ecs_entity_t);
    Logistics_storageFini(world, factory);
    Logistics_storageFini(world, storage);
    ecs_fini(world);
}

void Logistics_combine_dropoff_requests(void) {
    ecs_world_t *world = Logistics_world();

    ecs_entity_t resource = ecs_new(world);
    ecs_set(world, resource, BiomeResource, {
        .max_drone_amount = 100
    });
    ecs_entity_t factory = Logistics_storage(
        world, BiomeResourceStorageKindSink, 300);
    ecs_entity_t storage = Logistics_playerStorage(
        world, resource, 300);
    ecs_set(world, factory, BiomeFactory, {0});
    *(int32_t*)ecs_map_ensure(
        &Logistics_player_totals, resource) = 200;

    for (int32_t i = 0; i < 4; i ++) {
        biome_logistics_postRequest(
            world, BiomeRequestDropOff, factory, resource, 30, 0);
    }

    ecs_vec_t accepted;
    ecs_vec_init_t(NULL, &accepted, ecs_entity_t, 0);
    BiomeLogisticsJob job;
    test_assert(biome_logistics_acceptRequest(
        world, Logistics_request(world, factory, 0),
        storage, &accepted, &job));
    test_int(ecs_vec_count(&accepted), 3);
    test_int(job.amount, 90);
    test_uint(job.src, storage);
    test_uint(job.dst, factory);

    biome_logistics_finishRequests(world, &accepted);
    test_int(ecs_vec_count(
        &ecs_get(world, factory,
            BiomeResourceStorage)->outstanding_requests), 1);

    Logistics_deleteRequests(world, factory);
    ecs_vec_fini_t(NULL, &accepted, ecs_entity_t);
    Logistics_storageFini(world, factory);
    Logistics_storageFini(world, storage);
    ecs_fini(world);
}

void Logistics_combine_matching_resources_only(void) {
    ecs_world_t *world = Logistics_world();

    ecs_entity_t resource_a = ecs_new(world);
    ecs_entity_t resource_b = ecs_new(world);
    ecs_set(world, resource_a, BiomeResource, {
        .max_drone_amount = 100
    });
    ecs_set(world, resource_b, BiomeResource, {
        .max_drone_amount = 100
    });

    ecs_entity_t factory = Logistics_storage(
        world, BiomeResourceStorageKindSource, 400);
    ecs_entity_t storage = Logistics_playerStorage(
        world, resource_a, 400);
    *(int32_t*)ecs_map_ensure(
        &Logistics_player_capacity, resource_b) = 400;
    ecs_set(world, factory, BiomeFactory, {0});
    Logistics_setResource(world, factory, resource_a, 100);
    Logistics_setResource(world, factory, resource_b, 100);
    *(int32_t*)ecs_map_ensure(
        &Logistics_player_totals, resource_a) = 100;

    biome_logistics_postRequest(
        world, BiomeRequestPickup, factory, resource_a, 20, 0);
    biome_logistics_postRequest(
        world, BiomeRequestPickup, factory, resource_b, 20, 0);
    biome_logistics_postRequest(
        world, BiomeRequestPickup, factory, resource_a, 30, 0);
    biome_logistics_postRequest(
        world, BiomeRequestDropOff, factory, resource_a, 30, 0);

    ecs_vec_t accepted;
    ecs_vec_init_t(NULL, &accepted, ecs_entity_t, 0);
    BiomeLogisticsJob job;
    test_assert(biome_logistics_acceptRequest(
        world, Logistics_request(world, factory, 0),
        storage, &accepted, &job));
    test_int(ecs_vec_count(&accepted), 2);
    test_int(job.amount, 50);
    test_uint(job.resource, resource_a);

    biome_logistics_finishRequests(world, &accepted);
    const BiomeResourceStorage *factory_storage = ecs_get(
        world, factory, BiomeResourceStorage);
    test_int(
        ecs_vec_count(&factory_storage->outstanding_requests), 2);

    BiomeLogisticsRequest request;
    test_assert(biome_logistics_getRequest(
        world, Logistics_request(world, factory, 0), &request));
    test_uint(request.resource, resource_b);
    test_assert(biome_logistics_getRequest(
        world, Logistics_request(world, factory, 1), &request));
    test_int(request.kind, BiomeRequestDropOff);

    Logistics_deleteRequests(world, factory);
    ecs_vec_fini_t(NULL, &accepted, ecs_entity_t);
    Logistics_storageFini(world, factory);
    Logistics_storageFini(world, storage);
    ecs_fini(world);
}

void Logistics_dispatch_finishes_iterator(void) {
    ecs_world_t *world = Logistics_world();
    ECS_COMPONENT_DEFINE(world, BiomeLogisticsWaiter);

    ecs_entity_t waiter_a = ecs_new(world);
    ecs_entity_t waiter_b = ecs_new(world);
    ecs_set(world, waiter_a, BiomeLogisticsWaiter, {0});
    ecs_set(world, waiter_b, BiomeLogisticsWaiter, {0});
    ecs_set(world, waiter_a, BiomeLogisticsCarrier, {0});
    ecs_set(world, waiter_b, BiomeLogisticsCarrier, {0});

    ecs_query_t *query = ecs_query(world, {
        .terms = {
            { .id = ecs_id(BiomeLogisticsWaiter) },
            { .id = ecs_id(BiomeLogisticsCarrier) }
        }
    });
    ecs_iter_t it = ecs_query_iter(world, query);
    BiomeLogisticsDispatch(&it);

    test_assert(ecs_has(
        world, waiter_a, BiomeLogisticsWaiter));
    test_assert(ecs_has(
        world, waiter_b, BiomeLogisticsWaiter));

    ecs_query_fini(query);
    ecs_fini(world);
}

void Logistics_different_power_network(void) {
    ecs_world_t *world = Logistics_world();

    ecs_entity_t resource = ecs_new(world);
    ecs_entity_t other_home = ecs_new(world);
    ecs_entity_t other_network = ecs_new(world);
    ecs_entity_t source_network = ecs_new(world);
    ecs_entity_t source = Logistics_storage(
        world, BiomeResourceStorageKindSource, 100);
    ecs_entity_t storage = Logistics_playerStorage(
        world, resource, 100);

    ecs_set(world, other_home, BiomePowerConsumer,
        {true, other_network});
    ecs_set(world, source, BiomePowerConsumer, {true, source_network});
    Logistics_setResource(world, source, resource, 20);
    biome_logistics_postRequest(
        world, BiomeRequestPickup, source, resource, 20, 0);

    BiomeLogisticsCarrier carrier = {other_home, storage};
    BiomeLogisticsPlayerAttrs attrs =
        biome_logistics_playerAttrs(world);
    ecs_vec_t accepted;
    ecs_vec_init_t(NULL, &accepted, ecs_entity_t, 0);
    BiomeLogisticsJob job;
    ecs_entity_t request = Logistics_request(world, source, 0);

    test_assert(biome_logistics_acceptRequestWithAttrs(
        world, &attrs, request, carrier.storage, &accepted, &job));
    test_int(ecs_vec_count(&accepted), 1);
    test_uint(job.src, source);
    test_uint(job.dst, storage);

    ecs_vec_fini_t(NULL, &accepted, ecs_entity_t);
    Logistics_deleteRequests(world, source);
    Logistics_storageFini(world, source);
    Logistics_storageFini(world, storage);
    ecs_fini(world);
}

void Logistics_player_storage_transfer(void) {
    ecs_world_t *world = Logistics_world();
    ecs_entity_t resource = ecs_new(world);
    ecs_entity_t storage = Logistics_playerStorage(
        world, resource, 100);

    test_assert(biome_logistics_transfer(
        world, resource, 30, storage, false));
    test_int(biome_resource_playerAmount(
        world, "ResourceTotals", resource), 30);

    *(int32_t*)ecs_map_ensure(
        &Logistics_player_reserved, resource) = 20;
    test_assert(biome_logistics_transfer(
        world, resource, 20, storage, true));
    test_int(biome_resource_playerAmount(
        world, "ResourceTotals", resource), 10);
    test_int(biome_resource_playerAmount(
        world, "ResourceReservedTotals", resource), 0);

    ecs_fini(world);
}

void Logistics_player_storage_capacity(void) {
    ecs_world_t *world = Logistics_world();
    ECS_COMPONENT_DEFINE(world, FlecsTerrainPosition);

    ecs_observer(world, {
        .query.terms = {
            { ecs_id(FlecsTerrainPosition) },
            { ecs_id(BiomePlayerStorage) }
        },
        .events = { EcsOnSet, EcsOnRemove },
        .callback = BiomePlayerStoragePlaced
    });

    ecs_entity_t resource = ecs_new(world);
    *(int32_t*)ecs_map_ensure(
        &Logistics_player_capacity, resource) = 0;

    ecs_entity_t first_prefab = ecs_new(world);
    ecs_add_id(world, first_prefab, EcsPrefab);
    ecs_set(world, first_prefab, BiomePlayerStorage, {100, {0}});
    ecs_entity_t first = ecs_new_w_pair(world, EcsIsA, first_prefab);
    ecs_set(world, first, FlecsTerrainPosition, {0});
    test_int(biome_resource_playerAmount(
        world, "ResourceCapacityTotals", resource), 100);

    ecs_entity_t second_prefab = ecs_new(world);
    ecs_add_id(world, second_prefab, EcsPrefab);
    ecs_set(world, second_prefab, BiomePlayerStorage, {50, {0}});
    ecs_entity_t second = ecs_new_w_pair(world, EcsIsA, second_prefab);
    ecs_set(world, second, FlecsTerrainPosition, {0});
    test_int(biome_resource_playerAmount(
        world, "ResourceCapacityTotals", resource), 150);

    ecs_delete(world, second);
    test_int(biome_resource_playerAmount(
        world, "ResourceCapacityTotals", resource), 100);

    ecs_fini(world);
}

void Logistics_closest_storage(void) {
    ecs_world_t *world = Logistics_world();

    ECS_COMPONENT_DEFINE(world, FlecsTerrain);
    ECS_COMPONENT_DEFINE(world, FlecsTerrainPosition);
    ECS_COMPONENT_DEFINE(world, BiomeLogisticsCarrier);
    ecs_set_hooks(world, BiomeLogisticsCarrier, {
        .on_set = BiomeLogisticsCarrierOnSet
    });

    ecs_entity_t terrain = ecs_new(world);
    ecs_set(world, terrain, FlecsTerrain, {
        .width = 5,
        .depth = 5
    });

    ecs_entity_t first_prefab = ecs_new(world);
    ecs_add_id(world, first_prefab, EcsPrefab);
    ecs_set(world, first_prefab, BiomePlayerStorage, {100, {0}});
    ecs_entity_t first = ecs_new_w_pair(world, EcsIsA, first_prefab);
    ecs_set(world, first, FlecsTerrainPosition, {
        .terrain = terrain,
        .x = 2,
        .y = 1
    });

    ecs_entity_t second_prefab = ecs_new(world);
    ecs_add_id(world, second_prefab, EcsPrefab);
    ecs_set(world, second_prefab, BiomePlayerStorage, {100, {0}});
    ecs_entity_t second = ecs_new_w_pair(world, EcsIsA, second_prefab);
    ecs_set(world, second, FlecsTerrainPosition, {
        .terrain = terrain,
        .x = 3,
        .y = 2
    });

    ecs_entity_t home = ecs_new(world);
    ecs_set(world, home, FlecsTerrainPosition, {
        .terrain = terrain,
        .x = 2,
        .y = 2
    });

    ecs_entity_t drone = ecs_new(world);
    ecs_set(world, drone, BiomeLogisticsCarrier, {
        .home = home
    });

    const BiomeLogisticsCarrier *carrier = ecs_get(
        world, drone, BiomeLogisticsCarrier);
    test_uint(carrier->storage, first);

    ecs_fini(world);
}
