#ifndef FLECS_RESOURCES_H
#define FLECS_RESOURCES_H

#undef ECS_META_IMPL
#ifndef BIOME_RESOURCES_IMPL
#define ECS_META_IMPL EXTERN
#endif

extern ECS_COMPONENT_DECLARE(biome_resource_storageKind_t);
extern ECS_COMPONENT_DECLARE(BiomeResourceStorageMap);

/* Storage kind */
typedef enum {
    BiomeResourceStorageKindSource,
    BiomeResourceStorageKindStorage,
    BiomeResourceStorageKindSink,
} biome_resource_storageKind_t;

/* Resource parameters */
ECS_STRUCT(BiomeResource, {
    int32_t mine_time;          /* Time it takes for an extractor to mine a resource in frames. */
    int32_t mine_amount;        /* Number of resources mined after mine_time (or however many are left). */
    int32_t min_pickup_amount;  /* Minimum number of resources for a pickup (drones won't consider storages with less than this) */
    float green_house_gass;
    float toxic_gass;
});

/* Resource storage specification. */
ECS_STRUCT(BiomeResourceStorageDesc, {
    biome_resource_storageKind_t kind;
    int32_t capacity;
});

/* Live storage state */
typedef ecs_map_t BiomeResourceStorageMap;

ECS_STRUCT(BiomeResourceStorage, {
    BiomeResourceStorageMap resources;  /* Resources available in storage */
    BiomeResourceStorageMap reserved;   /* Resources reserved for pickup */
    ecs_vec_t outstsanding_requests;    /* Vector with outstanding unaccepted requests. */
});

/* Component that describes how to create a resource */
ECS_STRUCT(BiomeRecipe, {
    BiomeResourceStorageMap inputs;
    ecs_entity_t output;
    float craft_time;
});

void biomeResourcesImport(ecs_world_t *world);

#endif
