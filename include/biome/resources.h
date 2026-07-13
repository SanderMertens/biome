#ifndef FLECS_RESOURCES_H
#define FLECS_RESOURCES_H

#undef ECS_META_IMPL
#ifndef BIOME_RESOURCES_IMPL
#define ECS_META_IMPL EXTERN
#endif

extern ECS_COMPONENT_DECLARE(BiomeResourceSource);
extern ECS_COMPONENT_DECLARE(BiomeResourceStorage);
extern ECS_COMPONENT_DECLARE(BiomeResourceSink);

/* Resource parameters */
ECS_STRUCT(BiomeResource, {
    int32_t mine_time;          /* Time it takes for an extractor to mine a resource in frames. */
    int32_t mine_amount;        /* Number of resources mined after mine_time (or however many are left). */
});

/* Resource source specification. */
ECS_STRUCT(BiomeResourceSourceDesc, {
    int32_t capacity;
});

/* Resource storage specification. */
ECS_STRUCT(BiomeResourceStorageDesc, {
    int32_t capacity;
});

/* Resource sink specification. */
ECS_STRUCT(BiomeResourceSinkDesc, {
    int32_t capacity;
});

/* Source for resources */
typedef ecs_map_t BiomeResourceSource;

/* Storage for resources */
typedef ecs_map_t BiomeResourceStorage;

/* Sink for resources */
typedef ecs_map_t BiomeResourceSink;

/* Resource deposit. */
ECS_STRUCT(BiomeResourceDeposit, {
    ecs_entity_t resource;
    int32_t amount;
});

/* Item that mines resource from a deposit */
ECS_STRUCT(BiomeResourceMiner, {
    ecs_entity_t deposit;       /* Entity with ResourceDeposit. */
});

/* Component that describes how to create a resource */
ECS_STRUCT(BiomeRecipe, {
    BiomeResourceStorage inputs;
    float craft_time;
});

void biomeResourcesImport(ecs_world_t *world);
void biomeMinerImport(ecs_world_t *world);

#endif
