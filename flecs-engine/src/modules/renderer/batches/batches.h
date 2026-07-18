#ifndef FLECS_ENGINE_BATCHES_H
#define FLECS_ENGINE_BATCHES_H

#include "../render_batch.h"
#include "common/common.h"

typedef enum {
    FlecsPrimitiveBatchTint,
    FlecsPrimitiveBatchTransmission,
    FlecsPrimitiveBatchTransmissionData,
    FlecsPrimitiveBatchTransparent,
    FlecsPrimitiveBatchTransparentData
} flecsEngine_primitive_batch_kind_t;

ecs_entity_t flecsEngine_createBatchSet_primitives(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name,
    flecsEngine_primitive_batch_kind_t kind);

ecs_entity_t flecsEngine_createBatch_mesh_materialData(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_boxes(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_quads(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_triangles(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_right_triangles(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_triangle_prisms(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_right_triangle_prisms(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_mesh_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_boxes_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_quads_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_triangles_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_right_triangles_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_triangle_prisms_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_right_triangle_prisms_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_bevel_boxes(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_bevel_boxes_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_mesh_transparent(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_mesh_transparentData(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_mesh_transmission(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_mesh_transmissionData(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_textured_mesh(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_mesh_vertexColor(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_mesh_tint(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_immediate(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

ecs_entity_t flecsEngine_createBatch_immediateTransparent(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name);

void flecsEngine_draw_register(
    ecs_world_t *world);

void flecsEngine_batchSets_register(
    ecs_world_t *world);

void FlecsOnAddSkyBoxBatch(
    ecs_iter_t *it);

#endif
