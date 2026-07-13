#ifndef FLECS_ENGINE_TERRAIN_H
#define FLECS_ENGINE_TERRAIN_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_TERRAIN_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(FlecsTerrain, {
    int32_t width;      /* cells along x */
    int32_t depth;      /* cells along z */
    float cell_size;
    ecs_vec_t layerTypes;
    ecs_vec_t layer_scale; /* vec<int8_t>, per-layer resolution divisor */

ECS_PRIVATE
    ecs_vec_t heights;  /* vec<f32>, (width+1)*(depth+1) corner heights */
    ecs_vec_t colors;   /* vec<rgba>, width*depth cell colors (optional) */
    ecs_vec_t layers;
});

extern ECS_COMPONENT_DECLARE(FlecsTerrain);

ECS_STRUCT(FlecsTerrainPosition, {
    ecs_entity_t terrain;
    int32_t x;       /* tile along the terrain x axis */
    int32_t y;       /* tile along the terrain z axis */
    int32_t span_x;  /* tiles covered along x (0 = 1) */
    int32_t span_y;  /* tiles covered along z (0 = 1) */
    float yaw;
});

extern ECS_COMPONENT_DECLARE(FlecsTerrainPosition);

void flecsEngine_terrainColorsModified(
    ecs_world_t *world,
    ecs_entity_t terrain);

/* Flatten a rectangular tile region by setting all of its corner samples. */
void flecsEngine_terrain_setHeight(
    ecs_world_t *world,
    ecs_entity_t terrainEntity,
    int32_t x,
    int32_t z,
    int32_t width,
    int32_t depth,
    float targetHeight);

float flecsEngine_terrainCellHeight(
    const FlecsTerrain *terrain,
    int32_t x,
    int32_t y);

float flecsEngine_terrainSampleHeight(
    const FlecsTerrain *terrain,
    float x,
    float z);

void* flecsEngine_terrainGetLayer(
    ecs_world_t *world,
    ecs_entity_t terrain,
    int16_t index,
    ecs_entity_t type);

int32_t flecsEngine_terrainLayerScale(
    const FlecsTerrain *terrain,
    int16_t index);

void flecsEngine_terrainLayerDimensions(
    const FlecsTerrain *terrain,
    int16_t index,
    int32_t *width_out,
    int32_t *depth_out);

void flecsEngine_terrain_computeSlope(
    ecs_world_t *world,
    ecs_entity_t terrain,
    flecs_vec2_t *slope_array);

#define flecsEngine_terrain_getLayer(world, terrain, index, Type)\
    (ECS_CAST(Type*, flecsEngine_terrainGetLayer(world, terrain, index, ecs_id(Type))))

void FlecsEngineTerrainImport(
    ecs_world_t *world);

#endif
