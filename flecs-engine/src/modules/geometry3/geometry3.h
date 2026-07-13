#include "../../private.h"

#ifndef FLECS_ENGINE_GEOMETRY_3D_IMPL
#define FLECS_ENGINE_GEOMETRY_3D_IMPL

typedef struct {
    ecs_map_t sphere_cache;
    ecs_map_t hemisphere_cache;
    ecs_map_t icosphere_cache;
    ecs_map_t cone_cache;
    ecs_map_t ngon_cache;
    ecs_map_t cylinder_cache;
    ecs_map_t bevel_cache;
    ecs_map_t bevel_corner_cache;
    ecs_entity_t unit_box_asset;
    ecs_entity_t unit_quad_asset;
    ecs_entity_t unit_triangle_asset;
    ecs_entity_t unit_right_triangle_asset;
    ecs_entity_t unit_triangle_prism_asset;
    ecs_entity_t unit_right_triangle_prism_asset;
} FlecsGeometry3Cache;

extern ECS_COMPONENT_DECLARE(FlecsGeometry3Cache);

ecs_entity_t flecsEngine_geometry3_createAsset(
    ecs_world_t *world,
    FlecsGeometry3Cache *ctx,
    const char *name);

const FlecsMesh3Impl* flecsEngine_box_getAsset(
    ecs_world_t *world);

const FlecsMesh3Impl* flecsEngine_quad_getAsset(
    ecs_world_t *world);

const FlecsMesh3Impl* flecsEngine_triangle_getAsset(
    ecs_world_t *world);

const FlecsMesh3Impl* flecsEngine_rightTriangle_getAsset(
    ecs_world_t *world);

const FlecsMesh3Impl* flecsEngine_trianglePrism_getAsset(
    ecs_world_t *world);

const FlecsMesh3Impl* flecsEngine_rightTrianglePrism_getAsset(
    ecs_world_t *world);

void FlecsSphere_on_replace(
    ecs_iter_t *it);

void FlecsHemiSphere_on_replace(
    ecs_iter_t *it);

void FlecsSphere_on_set(
    ecs_iter_t *it);

void FlecsIcoSphere_on_replace(
    ecs_iter_t *it);

void FlecsNGon_on_replace(
    ecs_iter_t *it);

void FlecsCone_on_replace(
    ecs_iter_t *it);

void FlecsCylinder_on_replace(
    ecs_iter_t *it);

void FlecsBevel_on_replace(
    ecs_iter_t *it);

void FlecsBevelCorner_on_replace(
    ecs_iter_t *it);

const FlecsMesh3Impl* flecsEngine_bevel_getAssetImpl(
    ecs_world_t *world,
    int32_t segments,
    bool smooth);

const FlecsMesh3Impl* flecsEngine_bevelCorner_getAssetImpl(
    ecs_world_t *world,
    int32_t segments,
    bool smooth);

void FlecsEngineGeometry3Import(
    ecs_world_t *world);

#endif
