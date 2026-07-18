#define BIOME_LOGISTICS_IMPL

#include "biome.h"
#include "../tasks/logistics.h"

int32_t biome_logistics_mapValue(
    const BiomeResourceStorageMap *map,
    ecs_entity_t resource)
{
    if (!ecs_map_is_init(map)) {
        return 0;
    }
    const int32_t *value = (const int32_t*)ecs_map_get(
        map, (ecs_map_key_t)resource);
    return value ? *value : 0;
}

void biome_logistics_addMapValue(
    BiomeResourceStorageMap *map,
    ecs_entity_t resource,
    int32_t amount)
{
    if (!ecs_map_is_init(map)) {
        ecs_map_init(map, NULL);
    }
    int32_t *value = (int32_t*)ecs_map_ensure(
        map, (ecs_map_key_t)resource);
    *value += amount;
}

static bool biome_logistics_entityPowered(
    const ecs_world_t *world,
    ecs_entity_t storage)
{
    const BiomePowerConsumer *power = ecs_get(
        world, storage, BiomePowerConsumer);
    return !power || power->powered;
}

static ecs_entity_t biome_logistics_findStorage(
    ecs_world_t *world,
    ecs_entity_t resource,
    int32_t amount,
    ecs_entity_t exclude,
    bool reserve)
{
    ecs_query_t *query = ecs_query(world, {
        .terms = {
            { .id = ecs_id(BiomeResourceStorage) },
            { .id = ecs_id(BiomeResourceStorageDesc), .inout = EcsIn }
        }
    });
    ecs_entity_t result = 0;
    ecs_iter_t it = ecs_query_iter(world, query);

    while (!result && ecs_query_next(&it)) {
        BiomeResourceStorage *storages = ecs_field(
            &it, BiomeResourceStorage, 0);
        const BiomeResourceStorageDesc *descs = ecs_field(
            &it, BiomeResourceStorageDesc, 1);
        for (int32_t i = 0; i < it.count; i ++) {
            if (it.entities[i] == exclude ||
                descs[i].kind != BiomeResourceStorageKindStorage ||
                !biome_logistics_entityPowered(world, it.entities[i]))
            {
                continue;
            }
            int32_t stored = biome_logistics_mapValue(
                &storages[i].resources, resource);
            int32_t reserved = biome_logistics_mapValue(
                &storages[i].reserved, resource);
            if (descs[i].capacity - stored - reserved >= amount) {
                if (reserve) {
                    biome_logistics_addMapValue(
                        &storages[i].reserved, resource, amount);
                    ecs_modified_id(world, it.entities[i],
                        ecs_id(BiomeResourceStorage));
                }
                result = it.entities[i];
                break;
            }
        }
    }

    if (result) {
        ecs_iter_fini(&it);
    }

    ecs_query_fini(query);
    return result;
}

static ecs_entity_t biome_logistics_findSourceStorage(
    ecs_world_t *world,
    ecs_entity_t resource,
    int32_t amount,
    ecs_entity_t exclude,
    bool reserve)
{
    ecs_query_t *query = ecs_query(world, {
        .terms = {
            { .id = ecs_id(BiomeResourceStorage) },
            { .id = ecs_id(BiomeResourceStorageDesc), .inout = EcsIn }
        }
    });
    ecs_entity_t result = 0;
    ecs_iter_t it = ecs_query_iter(world, query);

    while (!result && ecs_query_next(&it)) {
        BiomeResourceStorage *storages = ecs_field(
            &it, BiomeResourceStorage, 0);
        const BiomeResourceStorageDesc *descs = ecs_field(
            &it, BiomeResourceStorageDesc, 1);
        for (int32_t i = 0; i < it.count; i ++) {
            if (it.entities[i] == exclude ||
                descs[i].kind != BiomeResourceStorageKindStorage ||
                !biome_logistics_entityPowered(world, it.entities[i]))
            {
                continue;
            }
            int32_t stored = biome_logistics_mapValue(
                &storages[i].resources, resource);
            int32_t reserved = biome_logistics_mapValue(
                &storages[i].reserved, resource);
            if (stored - reserved >= amount) {
                if (reserve) {
                    biome_logistics_addMapValue(
                        &storages[i].reserved, resource, amount);
                    ecs_modified_id(world, it.entities[i],
                        ecs_id(BiomeResourceStorage));
                }
                result = it.entities[i];
                break;
            }
        }
    }

    if (result) {
        ecs_iter_fini(&it);
    }

    ecs_query_fini(query);
    return result;
}

static bool biome_logistics_getRequest(
    const ecs_world_t *world,
    ecs_entity_t request_entity,
    BiomeLogisticsRequest *result)
{
    if (!ecs_is_alive(world, request_entity)) {
        return false;
    }

    ecs_entity_t request_kind = ecs_get_target(
        world, request_entity, ecs_id(BiomeLogisticsRequest), 0);
    const BiomeLogisticsRequest *request = request_kind
        ? ecs_get_id(world, request_entity, ecs_pair(
            ecs_id(BiomeLogisticsRequest), request_kind))
        : NULL;
    if (!request ||
        (request->kind != BiomeRequestPickup &&
            request->kind != BiomeRequestDropOff) ||
        request->amount <= 0 ||
        !ecs_is_alive(world, request->source))
    {
        return false;
    }

    *result = *request;
    return true;
}

static bool biome_logistics_requestEndpointFits(
    const ecs_world_t *world,
    const BiomeLogisticsRequest *request,
    int32_t amount)
{
    const BiomeResourceStorage *storage = ecs_get(
        world, request->source, BiomeResourceStorage);
    const BiomeResourceStorageDesc *desc = ecs_get(
        world, request->source, BiomeResourceStorageDesc);
    if (!storage || !desc) {
        return false;
    }

    int32_t stored = biome_logistics_mapValue(
        &storage->resources, request->resource);
    if (request->kind == BiomeRequestPickup) {
        return stored >= amount;
    }
    if (request->kind == BiomeRequestDropOff) {
        return desc->capacity - stored >= amount;
    }
    return false;
}

static bool biome_logistics_resolveRequest(
    ecs_world_t *world,
    const BiomeLogisticsRequest *request,
    int32_t amount,
    bool reserve,
    BiomeLogisticsJob *job)
{
    if (amount <= 0 ||
        !biome_logistics_requestEndpointFits(world, request, amount))
    {
        return false;
    }

    ecs_entity_t src;
    ecs_entity_t dst;
    if (request->kind == BiomeRequestPickup) {
        src = request->source;
        dst = biome_logistics_findStorage(
            world, request->resource, amount, request->source, reserve);
    } else if (request->kind == BiomeRequestDropOff) {
        dst = request->source;
        src = biome_logistics_findSourceStorage(
            world, request->resource, amount, request->source, reserve);
    } else {
        return false;
    }
    if (!src || !dst) {
        return false;
    }

    *job = (BiomeLogisticsJob){
        request->resource,
        amount,
        src,
        dst
    };
    return true;
}

static bool biome_logistics_tryRequest(
    ecs_world_t *world,
    ecs_entity_t request_entity,
    bool reserve,
    BiomeLogisticsJob *job)
{
    BiomeLogisticsRequest request;
    return biome_logistics_getRequest(
        world, request_entity, &request) &&
        biome_logistics_resolveRequest(
            world, &request, request.amount, reserve, job);
}

static bool biome_logistics_acceptRequest(
    ecs_world_t *world,
    ecs_entity_t request_entity,
    ecs_vec_t *accepted,
    BiomeLogisticsJob *job)
{
    ecs_vec_clear(accepted);

    BiomeLogisticsRequest request;
    if (!biome_logistics_getRequest(
        world, request_entity, &request))
    {
        return false;
    }

    int32_t max_amount = request.amount;
    const BiomeResource *resource = ecs_get(
        world, request.resource, BiomeResource);
    if (resource && resource->max_drone_amount > max_amount) {
        max_amount = resource->max_drone_amount;
    }

    int32_t amount = request.amount;
    *ecs_vec_append_t(NULL, accepted, ecs_entity_t) = request_entity;

    const BiomeResourceStorage *storage = ecs_get(
        world, request.source, BiomeResourceStorage);
    if (storage) {
        int32_t count = ecs_vec_count(&storage->outstanding_requests);
        const ecs_entity_t *requests = ecs_vec_first_t(
            &storage->outstanding_requests, ecs_entity_t);
        bool found = false;

        for (int32_t i = 0; i < count; i ++) {
            ecs_entity_t candidate_entity = requests[i];
            if (candidate_entity == request_entity) {
                found = true;
                continue;
            }
            if (!found) {
                continue;
            }

            BiomeLogisticsRequest candidate;
            if (!biome_logistics_getRequest(
                    world, candidate_entity, &candidate) ||
                candidate.source != request.source ||
                candidate.kind != request.kind ||
                candidate.resource != request.resource ||
                candidate.amount > max_amount - amount)
            {
                continue;
            }

            BiomeLogisticsJob combined_job;
            if (!biome_logistics_resolveRequest(
                world, &request, amount + candidate.amount,
                false, &combined_job))
            {
                continue;
            }

            amount += candidate.amount;
            *ecs_vec_append_t(NULL, accepted, ecs_entity_t) =
                candidate_entity;
            if (amount == max_amount) {
                break;
            }
        }
    }

    if (!biome_logistics_resolveRequest(
        world, &request, amount, true, job))
    {
        ecs_vec_clear(accepted);
        return false;
    }

    return true;
}

static void biome_logistics_collectRequests(
    ecs_world_t *world,
    ecs_vec_t *requests)
{
    ecs_entity_t jobs = ecs_lookup(world, "jobs");
    ecs_assert(jobs != 0, ECS_INTERNAL_ERROR, NULL);

    ecs_iter_t it = ecs_children(world, jobs);
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            BiomeLogisticsJob job;
            if (biome_logistics_tryRequest(
                world, it.entities[i], false, &job))
            {
                *ecs_vec_append_t(NULL, requests, ecs_entity_t) =
                    it.entities[i];
            }
        }
    }
}

static void biome_logistics_collectPendingRequests(
    ecs_world_t *world,
    ecs_vec_t *requests)
{
    ecs_entity_t jobs = ecs_lookup(world, "jobs");
    ecs_assert(jobs != 0, ECS_INTERNAL_ERROR, NULL);

    ecs_iter_t it = ecs_children(world, jobs);
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            *ecs_vec_append_t(NULL, requests, ecs_entity_t) =
                it.entities[i];
        }
    }
}

static void biome_logistics_removeOutstandingRequest(
    ecs_world_t *world,
    ecs_entity_t source,
    ecs_entity_t request)
{
    BiomeResourceStorage *storage = ecs_get_mut(
        world, source, BiomeResourceStorage);
    if (!storage) {
        return;
    }

    int32_t count = ecs_vec_count(&storage->outstanding_requests);
    ecs_entity_t *requests = ecs_vec_first_t(
        &storage->outstanding_requests, ecs_entity_t);
    for (int32_t i = 0; i < count; i ++) {
        if (requests[i] == request) {
            ecs_vec_remove_ordered_t(
                &storage->outstanding_requests, ecs_entity_t, i);
            ecs_modified_id(
                world, source, ecs_id(BiomeResourceStorage));
            return;
        }
    }
}

static void biome_logistics_finishRequests(
    ecs_world_t *world,
    ecs_vec_t *accepted)
{
    int32_t accepted_count = ecs_vec_count(accepted);
    ecs_entity_t *accepted_requests = ecs_vec_first_t(
        accepted, ecs_entity_t);
    for (int32_t r = 0; r < accepted_count; r ++) {
        BiomeLogisticsRequest accepted_request;
        if (biome_logistics_getRequest(
            world, accepted_requests[r], &accepted_request))
        {
            biome_logistics_removeOutstandingRequest(
                world, accepted_request.source, accepted_requests[r]);
        }
        ecs_delete(world, accepted_requests[r]);
    }
}

static void BiomeLogisticsDispatch(ecs_iter_t *it) {
    ecs_vec_t requests;
    ecs_vec_init_t(NULL, &requests, ecs_entity_t, 0);

    ecs_vec_t accepted;
    ecs_vec_init_t(NULL, &accepted, ecs_entity_t, 0);

    int32_t request_index = 0;
    int32_t request_count = 0;
    ecs_entity_t *request_entities = NULL;
    bool requests_collected = false;

    while (ecs_query_next(it)) {
        if (!requests_collected) {
            biome_logistics_collectPendingRequests(it->world, &requests);
            request_count = ecs_vec_count(&requests);
            request_entities = ecs_vec_first_t(
                &requests, ecs_entity_t);
            requests_collected = true;
        }

        if (request_index >= request_count) {
            continue;
        }

        BiomeLogisticsWaiter *waiters = ecs_field(
            it, BiomeLogisticsWaiter, 0);

        for (int32_t i = 0;
            i < it->count && request_index < request_count;
            i ++)
        {
            const BiomeLogisticsCarrier *carrier = ecs_get(
                it->world, it->entities[i], BiomeLogisticsCarrier);
            if (carrier && carrier->home &&
                !biome_logistics_entityPowered(it->world, carrier->home))
            {
                continue;
            }

            BiomeLogisticsJob job;
            ecs_entity_t request = 0;
            while (request_index < request_count) {
                ecs_entity_t candidate =
                    request_entities[request_index ++];
                if (biome_logistics_acceptRequest(
                    it->world, candidate, &accepted, &job))
                {
                    request = candidate;
                    break;
                }
            }
            if (!request) {
                break;
            }

            ecs_value_t value = {
                ecs_id(BiomeLogisticsJob), &job
            };
            ecs_script_future_resolve(waiters[i].future, &value);
            ecs_remove(it->world, it->entities[i], BiomeLogisticsWaiter);

            ecs_defer_suspend(it->world);
            biome_logistics_finishRequests(it->world, &accepted);
            ecs_defer_resume(it->world);
        }
    }

    ecs_vec_fini_t(NULL, &accepted, ecs_entity_t);
    ecs_vec_fini_t(NULL, &requests, ecs_entity_t);
}

void biome_logistics_postRequest(
    ecs_world_t *world,
    biome_logisticsRequestKind_t kind,
    ecs_entity_t source,
    ecs_entity_t resource,
    int32_t amount,
    int32_t priority)
{
    if (amount <= 0) {
        return;
    }

    ecs_entity_t jobs = ecs_lookup(world, "jobs");
    ecs_assert(jobs != 0, ECS_INTERNAL_ERROR, NULL);

    const EcsConstants *request_kinds = ecs_get(world,
        ecs_id(biome_logisticsRequestKind_t), EcsConstants);
    ecs_assert(request_kinds != NULL, ECS_INTERNAL_ERROR, NULL);

    const ecs_enum_constant_t *request_kind = ecs_map_get_deref(
        request_kinds->constants, ecs_enum_constant_t, kind);
    ecs_assert(request_kind != NULL, ECS_INVALID_PARAMETER, NULL);

    int32_t max_amount = amount;
    const BiomeResource *resource_config = ecs_get(
        world, resource, BiomeResource);
    if (resource_config && resource_config->max_drone_amount > 0) {
        max_amount = resource_config->max_drone_amount;
    }

    while (amount > 0) {
        int32_t request_amount = amount < max_amount ? amount : max_amount;
        ecs_entity_t job = ecs_new_w_pair(world, EcsChildOf, jobs);
        ecs_set_pair(
            world, job, BiomeLogisticsRequest, request_kind->constant, {
                kind, source, resource, request_amount, priority
            });

        BiomeResourceStorage *storage = ecs_get_mut(
            world, source, BiomeResourceStorage);
        if (storage) {
            ecs_vec_init_if_t(
                &storage->outstanding_requests, ecs_entity_t);
            *ecs_vec_append_t(
                NULL, &storage->outstanding_requests, ecs_entity_t) = job;
            ecs_modified_id(
                world, source, ecs_id(BiomeResourceStorage));
        }

        amount -= request_amount;
    }
}

void biomeLogisticsImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeLogistics);

    ECS_IMPORT(world, biomeBehavior);
    ECS_IMPORT(world, biomeResources);
    ECS_IMPORT(world, biomeBuildings);

    ecs_set_name_prefix(world, "BiomeLogistics");

    ECS_META_COMPONENT(world, biome_logisticsRequestKind_t);
    ECS_META_COMPONENT(world, BiomeLogisticsRequest);
    ECS_META_COMPONENT(world, BiomeLogisticsCarrier);
    ECS_META_COMPONENT(world, BiomeLogisticsJob);

    ecs_entity_t module = ecs_id(biomeLogistics);
    biomeLogisticsTasksImport(world, module);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Dispatch" }),
        .query.terms = {{ .id = ecs_id(BiomeLogisticsWaiter) }},
        .phase = EcsOnUpdate,
        .run = BiomeLogisticsDispatch,
        .immediate = true
    });

    ecs_entity_t prev_scope = ecs_set_scope(world, 0);
    ecs_entity_t jobs = ecs_entity(world, { .name = "jobs" });
    ecs_add_id(world, jobs, EcsOrderedChildren);
    ecs_set_scope(world, prev_scope);
}
