#define BIOME_LOGISTICS_IMPL

#include "biome.h"

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

    ECS_IMPORT(world, biomeResources);
    ECS_IMPORT(world, biomeBuildings);

    ecs_set_name_prefix(world, "BiomeLogistics");

    ECS_META_COMPONENT(world, biome_logisticsRequestKind_t);
    ECS_META_COMPONENT(world, BiomeLogisticsRequest);
    ECS_META_COMPONENT(world, BiomeLogisticsCarrier);

    ecs_entity_t prev_scope = ecs_set_scope(world, 0);
    ecs_entity(world, { .name = "jobs" });
    ecs_set_scope(world, prev_scope);
}
