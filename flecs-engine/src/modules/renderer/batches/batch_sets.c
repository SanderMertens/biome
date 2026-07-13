#include "../renderer.h"
#include "batches.h"
#include "flecs_engine.h"

ECS_TAG_DECLARE(FlecsSkyBoxBatch);
ECS_TAG_DECLARE(FlecsGeometryBatch);

static ecs_entity_t flecsEngine_createBatchSet_geometry_materialData(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch_set_entity = ecs_entity(world, { .parent = parent, .name = name });
    FlecsRenderBatchSet batch_set = *ecs_ensure(
        world, batch_set_entity, FlecsRenderBatchSet);

    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_boxes(world, batch_set_entity, "Boxes");
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_quads(world, batch_set_entity, "Quads");
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_triangles(world, batch_set_entity, "Triangles");
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_right_triangles(world, batch_set_entity, "RightTriangles");
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_triangle_prisms(world, batch_set_entity, "TrianglePrisms");
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_right_triangle_prisms(
            world, batch_set_entity, "RightTrianglePrisms");
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_bevel_boxes(world, batch_set_entity, "BevelBoxes");
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_mesh_materialData(world, batch_set_entity, "Meshes");

    ecs_set_ptr(world, batch_set_entity, FlecsRenderBatchSet, &batch_set);
    return batch_set_entity;
}

static ecs_entity_t flecsEngine_createBatchSet_geometry_materialIndex(
    ecs_world_t *world,
    ecs_entity_t parent,
    const char *name)
{
    ecs_entity_t batch_set_entity = ecs_entity(world, { .parent = parent, .name = name });
    FlecsRenderBatchSet batch_set = *ecs_ensure(
        world, batch_set_entity, FlecsRenderBatchSet);

    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_boxes_materialIndex(world, batch_set_entity, "Boxes");
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_quads_materialIndex(world, batch_set_entity, "Quads");
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_triangles_materialIndex(
            world, batch_set_entity, "Triangles");
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_right_triangles_materialIndex(
            world, batch_set_entity, "RightTriangles");
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_triangle_prisms_materialIndex(
            world, batch_set_entity, "TrianglePrisms");
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_right_triangle_prisms_materialIndex(
            world, batch_set_entity, "RightTrianglePrisms");
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_bevel_boxes_materialIndex(
            world, batch_set_entity, "BevelBoxes");
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
        flecsEngine_createBatch_mesh_materialIndex(world, batch_set_entity, "Meshes");

    ecs_set_ptr(world, batch_set_entity, FlecsRenderBatchSet, &batch_set);
    return batch_set_entity;
}

static void FlecsOnAddGeometryBatch(
    ecs_iter_t *it)
{
    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t batch_set_entity = it->entities[i];
        FlecsRenderBatchSet batch_set = {0};

        bool was_deferred = ecs_is_deferred(it->world);
        if (was_deferred) {
            ecs_defer_suspend(it->world);
        }

        ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
            flecsEngine_createBatchSet_geometry_materialData(
                it->world, batch_set_entity, "ColoredGeometry");
        ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
            flecsEngine_createBatchSet_geometry_materialIndex(
                it->world, batch_set_entity, "MaterialGeometry");
        ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
            flecsEngine_createBatch_textured_mesh(
                it->world, batch_set_entity, "TexturedMeshes");
        ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
            flecsEngine_createBatch_mesh_vertexColor(
                it->world, batch_set_entity, "VertexColorMeshes");
        ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
            flecsEngine_createBatch_mesh_tint(
                it->world, batch_set_entity, "TintMeshes");
        ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
            flecsEngine_createBatch_immediate(
                it->world, batch_set_entity, "ImmediateMeshes");

        /* Transmissive objects render after opaques, before transparents.
         * They sample the opaque scene snapshot for refraction. */
        ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
            flecsEngine_createBatch_mesh_transmission(
                it->world, batch_set_entity, "TransmissiveMeshes");
        ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
            flecsEngine_createBatch_mesh_transmissionData(
                it->world, batch_set_entity, "TransmissiveDataMeshes");

        /* Transparent batches must render last (after all opaques) */
        ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
            flecsEngine_createBatch_mesh_transparent(
                it->world, batch_set_entity, "TransparentMeshes");
        ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
            flecsEngine_createBatch_mesh_transparentData(
                it->world, batch_set_entity, "TransparentDataMeshes");
        ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] =
            flecsEngine_createBatch_immediateTransparent(
                it->world, batch_set_entity, "ImmediateTransparentMeshes");

        if (was_deferred) {
            ecs_defer_resume(it->world);
        }

        ecs_set_ptr(it->world, batch_set_entity, FlecsRenderBatchSet,
            &batch_set);
    }
}

void flecsEngine_batchSets_register(
    ecs_world_t *world)
{
    ecs_set_name_prefix(world, "Flecs");
    ECS_TAG_DEFINE(world, FlecsSkyBoxBatch);
    ECS_TAG_DEFINE(world, FlecsGeometryBatch);

    ecs_observer(world, {
        .query.terms = {{ FlecsSkyBoxBatch }},
        .events = { EcsOnAdd },
        .callback = FlecsOnAddSkyBoxBatch
    });

    ecs_observer(world, {
        .query.terms = {{ FlecsGeometryBatch }},
        .events = { EcsOnAdd },
        .callback = FlecsOnAddGeometryBatch
    });
}
