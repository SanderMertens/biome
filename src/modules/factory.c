#define BIOME_FACTORY_IMPL

#include "biome.h"

typedef struct BiomeFactoryProgress {
    float remaining;
    float total;
} BiomeFactoryProgress;

ECS_COMPONENT_DECLARE(BiomeFactoryProgress);
ECS_COMPONENT_DECLARE(biome_factory_outputMode_t);

static int32_t biome_factory_mapValue(
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

static int32_t *biome_factory_mapEnsure(
    BiomeResourceStorageMap *map,
    ecs_entity_t resource)
{
    if (!ecs_map_is_init(map)) {
        ecs_map_init(map, NULL);
    }
    return (int32_t*)ecs_map_ensure(map, (ecs_map_key_t)resource);
}

static bool biome_factory_consumeInputs(
    const BiomeRecipe *recipe,
    BiomeResourceStorage *storage)
{
    if (!ecs_map_is_init(&recipe->inputs)) {
        return true;
    }

    ecs_map_iter_t it = ecs_map_iter(&recipe->inputs);
    while (ecs_map_next(&it)) {
        int32_t amount = (int32_t)ecs_map_value(&it);
        if (amount > 0 && biome_factory_mapValue(
            &storage->resources, ecs_map_key(&it)) < amount)
        {
            return false;
        }
    }

    it = ecs_map_iter(&recipe->inputs);
    while (ecs_map_next(&it)) {
        int32_t amount = (int32_t)ecs_map_value(&it);
        if (amount > 0) {
            int32_t *stored = biome_factory_mapEnsure(
                &storage->resources, ecs_map_key(&it));
            *stored -= amount;
        }
    }

    return true;
}

static void biome_factory_requestInputs(
    ecs_world_t *world,
    ecs_entity_t factory,
    const BiomeRecipe *recipe,
    const BiomeResourceStorageDesc *desc,
    BiomeResourceStorage *storage)
{
    if (!ecs_map_is_init(&recipe->inputs)) {
        return;
    }

    bool modified = false;
    ecs_map_iter_t it = ecs_map_iter(&recipe->inputs);
    while (ecs_map_next(&it)) {
        ecs_entity_t resource = ecs_map_key(&it);
        int32_t stored = biome_factory_mapValue(
            &storage->resources, resource);
        int32_t reserved = biome_factory_mapValue(
            &storage->reserved, resource);
        int32_t room = desc->capacity - stored - reserved;

        const BiomeResource *resource_config = ecs_get(
            world, resource, BiomeResource);
        if (!resource_config ||
            resource_config->max_drone_amount <= 0 ||
            room <= 0)
        {
            continue;
        }

        int32_t amount = resource_config->max_drone_amount;
        if (amount > room) {
            amount = room;
        }

        int32_t *value = biome_factory_mapEnsure(
            &storage->reserved, resource);
        *value += amount;
        biome_logistics_postRequest(world, BiomeRequestDropOff,
            factory, resource, amount, 0);
        modified = true;
    }

    if (modified) {
        ecs_modified_id(world, factory, ecs_id(BiomeResourceStorage));
    }
}

static void biome_factory_vent(
    ecs_world_t *world,
    const BiomeFactory *factory,
    const BiomeRecipe *recipe)
{
    if (factory->output_mode != BiomeFactoryOutputVent ||
        !recipe->output)
    {
        return;
    }

    const BiomeResource *resource = ecs_get(
        world, recipe->output, BiomeResource);
    if (!resource) {
        return;
    }

    Weather *weather = ecs_singleton_get_mut(world, Weather);
    if (!weather) {
        return;
    }
    weather->greenhouse_gas += resource->greenhouse_gas;
    ecs_singleton_modified(world, Weather);
}

static void BiomeFactoryUpdate(ecs_iter_t *it) {
    BiomeFactoryProgress *progress = ecs_field(
        it, BiomeFactoryProgress, 0);
    BiomeResourceStorage *storages = ecs_field(
        it, BiomeResourceStorage, 1);

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t entity = it->entities[i];
        const BiomeFactory *factory = ecs_get(
            it->world, entity, BiomeFactory);
        const BiomeResourceStorageDesc *desc = ecs_get(
            it->world, entity, BiomeResourceStorageDesc);
        if (!factory || !desc || !factory->recipe ||
            factory->output_mode != BiomeFactoryOutputVent ||
            desc->kind != BiomeResourceStorageKindSink)
        {
            continue;
        }

        const BiomeRecipe *recipe = ecs_get(
            it->world, factory->recipe, BiomeRecipe);
        if (!recipe) {
            continue;
        }

        BiomeFactoryProgress *p = &progress[i];
        BiomeResourceStorage *storage = &storages[i];
        if (p->remaining <= 0) {
            if (!biome_factory_consumeInputs(recipe, storage)) {
                biome_factory_requestInputs(
                    it->world, entity, recipe, desc, storage);
                continue;
            }
            p->remaining = recipe->craft_time;
            if (p->remaining < 1) {
                p->remaining = 1;
            }
            p->total = p->remaining;
            ecs_modified_id(
                it->world, entity, ecs_id(BiomeResourceStorage));
            biome_factory_requestInputs(
                it->world, entity, recipe, desc, storage);
        }

        const BiomePowerConsumer *power = ecs_get(
            it->world, entity, BiomePowerConsumer);
        if (power && !power->powered) {
            continue;
        }

        p->remaining -= 1;
        if (p->remaining > 0) {
            continue;
        }

        biome_factory_vent(it->world, factory, recipe);
        if (biome_factory_consumeInputs(recipe, storage)) {
            p->remaining = recipe->craft_time;
            if (p->remaining < 1) {
                p->remaining = 1;
            }
            p->total = p->remaining;
            ecs_modified_id(
                it->world, entity, ecs_id(BiomeResourceStorage));
        }
        biome_factory_requestInputs(
            it->world, entity, recipe, desc, storage);
    }
}

static void BiomeFactoryUpdateEmitter(ecs_iter_t *it) {
    const BiomeFactoryProgress *progress = ecs_field(
        it, BiomeFactoryProgress, 0);
    const BiomePowerConsumer *power = ecs_field(
        it, BiomePowerConsumer, 1);
    FlecsParticleEmitter *emitters = ecs_field(
        it, FlecsParticleEmitter, 2);

    for (int32_t i = 0; i < it->count; i ++) {
        emitters[i].enabled =
            power[i].powered && progress[i].remaining > 0;
    }
}

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
    ecs_world_t *w = ECS_CONST_CAST(ecs_world_t*, world);
    if (biome_factory_canAfford(world, item) < count) {
        return false;
    }

    ecs_map_iter_t it = ecs_map_iter(&recipe->inputs);
    while (ecs_map_next(&it)) {
        int32_t required = (int32_t)ecs_map_value(&it);
        if (required > 0) {
            biome_resource_playerAdd(
                w, "ResourceTotals", ecs_map_key(&it),
                -required * count);
        }
    }
    return true;
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

    ecs_world_t *w = ECS_CONST_CAST(ecs_world_t*, world);
    ecs_map_iter_t it = ecs_map_iter(&recipe->inputs);
    while (ecs_map_next(&it)) {
        int32_t amount = (int32_t)ecs_map_value(&it);
        if (amount <= 0) {
            continue;
        }
        ecs_entity_t resource = ecs_map_key(&it);
        int32_t stored = biome_resource_playerAmount(
            w, "ResourceTotals", resource);
        int32_t capacity = biome_resource_playerAmount(
            w, "ResourceCapacityTotals", resource);
        int64_t refund = (int64_t)amount * count;
        int32_t room = capacity - stored;
        if (room > 0) {
            biome_resource_playerAdd(
                w, "ResourceTotals", resource,
                refund < room ? (int32_t)refund : room);
        }
    }
}

void biomeFactoryImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeFactory);

    ECS_IMPORT(world, biomeResources);
    ECS_IMPORT(world, biomeLogistics);
    ECS_IMPORT(world, biomeWeather);
    ECS_IMPORT(world, biomePower);

    ecs_set_name_prefix(world, "Biome");

    ecs_id(biome_factory_outputMode_t) = ecs_enum(world, {
        .entity = ecs_entity(world, {
            .name = "OutputMode",
            .symbol = "biome_factory_outputMode_t"
        }),
        .constants = {
            {"Vent", BiomeFactoryOutputVent},
            {"Store", BiomeFactoryOutputStore}
        }
    });
    ECS_META_COMPONENT(world, BiomeFactory);
    ECS_COMPONENT_DEFINE(world, BiomeFactoryProgress);

    ecs_struct(world, {
        .entity = ecs_id(BiomeFactoryProgress),
        .members = {
            { .name = "remaining", .type = ecs_id(ecs_f32_t),
                .offset = offsetof(BiomeFactoryProgress, remaining) },
            { .name = "total", .type = ecs_id(ecs_f32_t),
                .offset = offsetof(BiomeFactoryProgress, total) }
        }
    });

    ecs_add_pair(world, ecs_id(BiomeFactory),
        EcsWith, ecs_id(BiomeResourceStorage));
    ecs_add_pair(world, ecs_id(BiomeFactory),
        EcsWith, ecs_id(BiomeFactoryProgress));
    ecs_add_pair(world, ecs_id(BiomeFactoryProgress),
        EcsOnInstantiate, EcsOverride);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "Update" }),
        .query.terms = {
            { .id = ecs_id(BiomeFactoryProgress) },
            { .id = ecs_id(BiomeResourceStorage) }
        },
        .phase = EcsOnUpdate,
        .callback = BiomeFactoryUpdate
    });

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "UpdateEmitter" }),
        .query.terms = {
            { .id = ecs_id(BiomeFactoryProgress), .inout = EcsIn },
            { .id = ecs_id(BiomePowerConsumer), .inout = EcsIn },
            { .id = ecs_id(FlecsParticleEmitter) }
        },
        .phase = EcsOnUpdate,
        .callback = BiomeFactoryUpdateEmitter
    });
}
