#define BIOME_LOGISTICS_IMPL

#include "biome.h"

typedef struct BiomeLogisticsWaiter {
    ecs_script_future_t *future;
} BiomeLogisticsWaiter;

typedef struct BiomeLogisticsMotion {
    ecs_script_future_t *future;
    FlecsPosition3 start;
    FlecsPosition3 target;
    int32_t elapsed;
    int32_t duration;
} BiomeLogisticsMotion;

ECS_COMPONENT_DECLARE(BiomeLogisticsWaiter);
ECS_COMPONENT_DECLARE(BiomeLogisticsMotion);

static void biome_logistics_resolve(
    ecs_script_future_t *future)
{
    int32_t result = 0;
    ecs_script_future_resolve(future,
        &(ecs_value_t){ecs_id(ecs_i32_t), &result});
}

static int32_t biome_logistics_mapValue(
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

static void biome_logistics_addMapValue(
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
            if (descs[i].kind != BiomeResourceStorageKindStorage ||
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

static ecs_entity_t biome_logistics_findRequest(
    ecs_world_t *world,
    BiomeLogisticsJob *job)
{
    ecs_query_t *query = ecs_query(world, {
        .terms = {{
            .id = ecs_pair(
                ecs_id(BiomeLogisticsRequest), EcsWildcard),
            .inout = EcsIn
        }}
    });
    ecs_entity_t result = 0;
    int32_t priority = INT32_MIN;
    ecs_iter_t it = ecs_query_iter(world, query);

    while (ecs_query_next(&it)) {
        const BiomeLogisticsRequest *requests = ecs_field(
            &it, BiomeLogisticsRequest, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            const BiomeLogisticsRequest *request = &requests[i];
            if (request->kind != BiomeRequestPickup ||
                request->amount <= 0 ||
                !ecs_is_alive(world, request->source) ||
                request->priority < priority)
            {
                continue;
            }
            ecs_entity_t dst = biome_logistics_findStorage(
                world, request->resource, request->amount, false);
            if (!dst) {
                continue;
            }
            result = it.entities[i];
            priority = request->priority;
            *job = (BiomeLogisticsJob){
                request->resource,
                request->amount,
                request->source,
                dst
            };
        }
    }

    ecs_query_fini(query);
    if (result) {
        job->dst = biome_logistics_findStorage(
            world, job->resource, job->amount, true);
        if (!job->dst) {
            result = 0;
        }
    }
    return result;
}

static void BiomeLogisticsDispatch(ecs_iter_t *it) {
    BiomeLogisticsWaiter *waiters = ecs_field(
        it, BiomeLogisticsWaiter, 0);

    for (int32_t i = 0; i < it->count; i ++) {
        const BiomeLogisticsCarrier *carrier = ecs_get(
            it->world, it->entities[i], BiomeLogisticsCarrier);
        if (carrier && carrier->home &&
            !biome_logistics_entityPowered(it->world, carrier->home))
        {
            continue;
        }
        BiomeLogisticsJob job = {0};
        ecs_entity_t request = biome_logistics_findRequest(
            it->world, &job);
        if (!request) {
            continue;
        }
        ecs_value_t value = {
            ecs_id(BiomeLogisticsJob), &job
        };
        ecs_script_future_resolve(waiters[i].future, &value);
        ecs_remove(it->world, it->entities[i], BiomeLogisticsWaiter);

        ecs_defer_suspend(it->world);
        ecs_delete(it->world, request);
        ecs_defer_resume(it->world);
    }
}

static void biome_logistics_acceptJob(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    (void)argc;
    (void)argv;
    ecs_set(ctx->world, ctx->entity, BiomeLogisticsWaiter, {future});
}

static void biome_logistics_cancelWaiter(
    const ecs_function_ctx_t *ctx,
    ecs_script_future_t *future)
{
    const BiomeLogisticsWaiter *waiter = ecs_get(
        ctx->world, ctx->entity, BiomeLogisticsWaiter);
    if (waiter && waiter->future == future) {
        ecs_remove(ctx->world, ctx->entity, BiomeLogisticsWaiter);
    }
}

static void biome_logistics_startMotion(
    const ecs_function_ctx_t *ctx,
    ecs_script_future_t *future,
    const FlecsPosition3 *target,
    float speed)
{
    if (speed <= 0) {
        ecs_script_future_reject(
            future, "motion speed must be greater than zero");
        return;
    }
    const FlecsPosition3 *position = ecs_get(
        ctx->world, ctx->entity, FlecsPosition3);
    if (!position) {
        biome_logistics_resolve(future);
        return;
    }
    float dx = target->x - position->x;
    float dy = target->y - position->y;
    float dz = target->z - position->z;
    float distance = sqrtf(dx * dx + dy * dy + dz * dz);
    int32_t duration = (int32_t)ceilf(distance / speed);
    if (!duration) {
        ecs_set_ptr(ctx->world, ctx->entity, FlecsPosition3, target);
        biome_logistics_resolve(future);
        return;
    }
    ecs_set(ctx->world, ctx->entity, BiomeLogisticsMotion, {
        future, *position, *target, 0, duration
    });
}

static void biome_logistics_takeOff(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    if (argc != 1) {
        ecs_script_future_reject(future, "takeOff expects a speed");
        return;
    }
    const FlecsPosition3 *position = ecs_get(
        ctx->world, ctx->entity, FlecsPosition3);
    if (!position) {
        biome_logistics_resolve(future);
        return;
    }
    FlecsPosition3 target = *position;
    target.y += 5.0f;
    biome_logistics_startMotion(
        ctx, future, &target, *(float*)argv[0].ptr);
}

static void biome_logistics_land(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    if (argc != 2) {
        ecs_script_future_reject(
            future, "land expects a destination and speed");
        return;
    }
    ecs_entity_t dst = *(ecs_entity_t*)argv[0].ptr;
    const FlecsPosition3 *position = ecs_get(
        ctx->world, ctx->entity, FlecsPosition3);
    const FlecsPosition3 *dst_position = ecs_get(
        ctx->world, dst, FlecsPosition3);
    FlecsAABB dst_aabb;
    FlecsAABB drone_aabb;
    if (!position || !dst_position ||
        !flecsEngine_objectWorldAABB(ctx->world, dst, &dst_aabb) ||
        !flecsEngine_objectWorldAABB(
            ctx->world, ctx->entity, &drone_aabb))
    {
        ecs_script_future_reject(
            future, "land destination or drone has no renderable bounds");
        return;
    }
    float drone_bottom_offset = position->y - drone_aabb.min[1];
    FlecsPosition3 target = {
        dst_position->x,
        dst_aabb.max[1] + drone_bottom_offset,
        dst_position->z
    };
    biome_logistics_startMotion(
        ctx, future, &target, *(float*)argv[1].ptr);
}

static void biome_logistics_moveTo(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    if (argc != 2) {
        ecs_script_future_reject(
            future, "moveTo expects a destination and speed");
        return;
    }
    ecs_entity_t dst = *(ecs_entity_t*)argv[0].ptr;
    const FlecsPosition3 *position = ecs_get(
        ctx->world, ctx->entity, FlecsPosition3);
    const FlecsPosition3 *dst_position = ecs_get(
        ctx->world, dst, FlecsPosition3);
    if (!position || !dst_position) {
        ecs_script_future_reject(future, "moveTo destination has no position");
        return;
    }
    FlecsPosition3 target = {
        dst_position->x, dst_position->y + 6.0f, dst_position->z
    };
    biome_logistics_startMotion(
        ctx, future, &target, *(float*)argv[1].ptr);
}

static void biome_logistics_cancelMotion(
    const ecs_function_ctx_t *ctx,
    ecs_script_future_t *future)
{
    const BiomeLogisticsMotion *motion = ecs_get(
        ctx->world, ctx->entity, BiomeLogisticsMotion);
    if (motion && motion->future == future) {
        ecs_remove(ctx->world, ctx->entity, BiomeLogisticsMotion);
    }
}

static void BiomeLogisticsMove(ecs_iter_t *it) {
    BiomeLogisticsMotion *motions = ecs_field(
        it, BiomeLogisticsMotion, 0);

    for (int32_t i = 0; i < it->count; i ++) {
        BiomeLogisticsMotion *motion = &motions[i];
        motion->elapsed ++;
        float t = (float)motion->elapsed / (float)motion->duration;
        if (t > 1.0f) {
            t = 1.0f;
        }
        FlecsPosition3 position = {
            motion->start.x + (motion->target.x - motion->start.x) * t,
            motion->start.y + (motion->target.y - motion->start.y) * t,
            motion->start.z + (motion->target.z - motion->start.z) * t
        };
        ecs_set_ptr(it->world, it->entities[i], FlecsPosition3, &position);
        if (motion->elapsed >= motion->duration) {
            ecs_script_future_t *future = motion->future;
            ecs_remove(it->world, it->entities[i], BiomeLogisticsMotion);
            biome_logistics_resolve(future);
        }
    }
}

static bool biome_logistics_transfer(
    ecs_world_t *world,
    ecs_entity_t resource,
    int32_t amount,
    ecs_entity_t entity,
    bool pickup)
{
    if (amount <= 0) {
        return false;
    }
    BiomeResourceStorage *storage = ecs_get_mut(
        world, entity, BiomeResourceStorage);
    if (!storage) {
        return false;
    }
    int32_t stored = biome_logistics_mapValue(
        &storage->resources, resource);
    int32_t reserved = biome_logistics_mapValue(
        &storage->reserved, resource);
    if (pickup && stored < amount) {
        return false;
    }
    biome_logistics_addMapValue(
        &storage->resources, resource, pickup ? -amount : amount);
    if (reserved >= amount) {
        biome_logistics_addMapValue(
            &storage->reserved, resource, -amount);
    }
    ecs_modified_id(world, entity, ecs_id(BiomeResourceStorage));
    return true;
}

static void biome_logistics_pickUp(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    if (argc != 4 || !biome_logistics_transfer(ctx->world,
        *(ecs_entity_t*)argv[0].ptr, *(int32_t*)argv[1].ptr,
        *(ecs_entity_t*)argv[2].ptr, true))
    {
        ecs_script_future_reject(future, "pickup failed");
        return;
    }
    biome_logistics_resolve(future);
}

static void biome_logistics_dropOff(
    const ecs_function_ctx_t *ctx,
    int32_t argc,
    const ecs_value_t *argv,
    ecs_script_future_t *future)
{
    if (argc != 4 || !biome_logistics_transfer(ctx->world,
        *(ecs_entity_t*)argv[0].ptr, *(int32_t*)argv[1].ptr,
        *(ecs_entity_t*)argv[3].ptr, false))
    {
        ecs_script_future_reject(future, "dropoff failed");
        return;
    }
    biome_logistics_resolve(future);
}

void biome_logistics_postRequest(
    ecs_world_t *world,
    biome_logisticsRequestKind_t kind,
    ecs_entity_t source,
    ecs_entity_t resource,
    int32_t amount,
    int32_t priority)
{
    ecs_entity_t jobs = ecs_lookup(world, "jobs");
    ecs_assert(jobs != 0, ECS_INTERNAL_ERROR, NULL);

    const EcsConstants *request_kinds = ecs_get(world,
        ecs_id(biome_logisticsRequestKind_t), EcsConstants);
    ecs_assert(request_kinds != NULL, ECS_INTERNAL_ERROR, NULL);

    const ecs_enum_constant_t *request_kind = ecs_map_get_deref(
        request_kinds->constants, ecs_enum_constant_t, kind);
    ecs_assert(request_kind != NULL, ECS_INVALID_PARAMETER, NULL);

    ecs_entity_t job = ecs_new_w_pair(world, EcsChildOf, jobs);
    ecs_set_pair(world, job, BiomeLogisticsRequest, request_kind->constant, {
        kind, source, resource, amount, priority
    });
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
    ECS_COMPONENT_DEFINE(world, BiomeLogisticsWaiter);
    ECS_COMPONENT_DEFINE(world, BiomeLogisticsMotion);

    ecs_entity_t module = ecs_id(biomeLogistics);
    ecs_async_function(world, {
        .name = "acceptJob",
        .parent = module,
        .return_type = ecs_id(BiomeLogisticsJob),
        .callback = biome_logistics_acceptJob,
        .cancel = biome_logistics_cancelWaiter
    });
    ecs_async_function(world, {
        .name = "takeOff",
        .parent = module,
        .return_type = ecs_id(ecs_i32_t),
        .params = {{"speed", ecs_id(ecs_f32_t)}},
        .callback = biome_logistics_takeOff,
        .cancel = biome_logistics_cancelMotion
    });
    ecs_async_function(world, {
        .name = "moveTo",
        .parent = module,
        .return_type = ecs_id(ecs_i32_t),
        .params = {
            {"dst", ecs_id(ecs_entity_t)},
            {"speed", ecs_id(ecs_f32_t)}
        },
        .callback = biome_logistics_moveTo,
        .cancel = biome_logistics_cancelMotion
    });
    ecs_async_function(world, {
        .name = "land",
        .parent = module,
        .return_type = ecs_id(ecs_i32_t),
        .params = {
            {"dst", ecs_id(ecs_entity_t)},
            {"speed", ecs_id(ecs_f32_t)}
        },
        .callback = biome_logistics_land,
        .cancel = biome_logistics_cancelMotion
    });
    ecs_async_function(world, {
        .name = "pickUp",
        .parent = module,
        .return_type = ecs_id(ecs_i32_t),
        .params = {
            {"resource", ecs_id(ecs_entity_t)},
            {"amount", ecs_id(ecs_i32_t)},
            {"src", ecs_id(ecs_entity_t)},
            {"drone", ecs_id(ecs_entity_t)}
        },
        .callback = biome_logistics_pickUp
    });
    ecs_async_function(world, {
        .name = "dropOff",
        .parent = module,
        .return_type = ecs_id(ecs_i32_t),
        .params = {
            {"resource", ecs_id(ecs_entity_t)},
            {"amount", ecs_id(ecs_i32_t)},
            {"drone", ecs_id(ecs_entity_t)},
            {"dst", ecs_id(ecs_entity_t)}
        },
        .callback = biome_logistics_dropOff
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Dispatch" }),
        .query.terms = {{ .id = ecs_id(BiomeLogisticsWaiter) }},
        .phase = EcsOnUpdate,
        .callback = BiomeLogisticsDispatch,
        .immediate = true
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Move" }),
        .query.terms = {{ .id = ecs_id(BiomeLogisticsMotion) }},
        .phase = EcsOnUpdate,
        .callback = BiomeLogisticsMove
    });

    ecs_entity_t prev_scope = ecs_set_scope(world, 0);
    ecs_entity(world, { .name = "jobs" });
    ecs_set_scope(world, prev_scope);
}
