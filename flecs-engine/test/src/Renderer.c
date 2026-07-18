#include <flecs_engine_test.h>

#include "../../src/modules/renderer/batches/common/common.h"

static ecs_entity_t renderer_batch_root(
    ecs_world_t *world)
{
    ecs_entity_t tag = ecs_lookup(
        world, "flecs.engine.renderer.GeometryBatch");
    test_assert(tag != 0);
    ecs_entity_t root = ecs_entity(world, { .name = "BatchRoot" });
    ecs_add_id(world, root, tag);
    return root;
}

static ecs_entity_t renderer_object(
    ecs_world_t *world,
    bool dynamic,
    bool mesh,
    bool owned_material,
    bool tint,
    bool transmissive,
    bool transparent)
{
    ecs_entity_t base = ecs_entity(world, {
        .name = NULL,
        .add = ecs_ids(EcsPrefab)
    });

    if (mesh) {
        ecs_set(world, base, FlecsMesh3Impl, {
            .index_buffer = (WGPUBuffer)(uintptr_t)1,
            .index_count = 3
        });
    }
    if (!owned_material) {
        ecs_set(world, base, FlecsMaterialId, {1});
        if (transmissive) {
            ecs_set(world, base, FlecsTransmission, {
                .transmission_factor = 1,
                .ior = 1.5f
            });
        }
        if (transparent) {
            ecs_add(world, base, FlecsAlphaBlend);
        }
    }

    ecs_entity_t result = ecs_new_w_pair(world, EcsIsA, base);
    if (!mesh) {
        ecs_set(world, result, FlecsBox, {1, 1, 1});
    }
    if (owned_material) {
        ecs_set(world, result, FlecsRgba, {255, 255, 255, 255});
        if (transmissive) {
            ecs_set(world, result, FlecsTransmission, {
                .transmission_factor = 1,
                .ior = 1.5f
            });
        }
        if (transparent) {
            ecs_add(world, result, FlecsAlphaBlend);
        }
    }
    if (tint) {
        ecs_set(world, result, FlecsActualTint, {255, 128, 64, 255});
    }
    if (dynamic) {
        ecs_add(world, result, FlecsDynamicTransform);
    }
    ecs_set(world, result, FlecsWorldTransform3, {0});
    ecs_set(world, result, FlecsAABB, {0});
    return result;
}

static void renderer_prepare_buffer_set(
    flecsEngine_batch_buffers_t *buffers,
    int32_t capacity)
{
    if (buffers->capacity) {
        return;
    }
    buffers->cpu_transforms =
        ecs_os_calloc_n(FlecsGpuTransform, capacity);
    buffers->cpu_material_ids =
        ecs_os_calloc_n(FlecsMaterialId, capacity);
    buffers->cpu_materials =
        ecs_os_calloc_n(FlecsGpuMaterial, capacity);
    buffers->cpu_aabb =
        ecs_os_calloc_n(flecsEngine_gpuAabb_t, capacity);
    buffers->cpu_slot_to_group =
        ecs_os_calloc_n(uint32_t, capacity);
    buffers->cpu_group_info =
        ecs_os_calloc_n(flecsEngine_gpuCullGroupInfo_t, capacity);
    buffers->cpu_indirect_args =
        ecs_os_calloc_n(flecsEngine_gpuDrawArgs_t, capacity);
    buffers->capacity = capacity;
    buffers->group_capacity = capacity;
}

static void renderer_prepare_batch(
    const FlecsRenderBatch *batch)
{
    flecsEngine_batch_t *buffers =
        flecsEngine_batch_getCullBuf(batch);
    test_assert(buffers != NULL);
    renderer_prepare_buffer_set(&buffers->buffers, 128);
    renderer_prepare_buffer_set(&buffers->static_buffers, 128);
    if (batch->ctx != buffers) {
        flecsEngine_primitive_batch_group_t *primitive = batch->ctx;
        primitive->group.mesh.index_buffer =
            (WGPUBuffer)(uintptr_t)1;
        primitive->group.mesh.index_count = 3;
    }
}

static const char* renderer_expected_batch(
    bool mesh,
    bool owned_material,
    bool tint,
    bool transmissive,
    bool transparent)
{
    if (transmissive) {
        if (mesh) {
            return owned_material
                ? "BatchRoot.TransmissiveDataMeshes"
                : "BatchRoot.TransmissiveMeshes";
        }
        return owned_material
            ? "BatchRoot.TransmissiveDataGeometry.Boxes"
            : "BatchRoot.TransmissiveGeometry.Boxes";
    }
    if (transparent) {
        if (mesh) {
            return owned_material
                ? "BatchRoot.TransparentDataMeshes"
                : "BatchRoot.TransparentMeshes";
        }
        return owned_material
            ? "BatchRoot.TransparentDataGeometry.Boxes"
            : "BatchRoot.TransparentGeometry.Boxes";
    }
    if (tint && !owned_material) {
        return mesh
            ? "BatchRoot.TintMeshes"
            : "BatchRoot.TintGeometry.Boxes";
    }
    if (owned_material) {
        return mesh
            ? "BatchRoot.ColoredGeometry.Meshes"
            : "BatchRoot.ColoredGeometry.Boxes";
    }
    return mesh
        ? "BatchRoot.MaterialGeometry.Meshes"
        : "BatchRoot.MaterialGeometry.Boxes";
}

static ecs_entity_t renderer_matched_batch(
    ecs_world_t *world,
    ecs_entity_t object)
{
    ecs_entity_t result = 0;
    ecs_iter_t it = ecs_each(world, FlecsRenderBatch);
    while (ecs_each_next(&it)) {
        FlecsRenderBatch *batches = ecs_field(&it, FlecsRenderBatch, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            if (!batches[i].query) {
                continue;
            }
            ecs_iter_t query_it;
            if (ecs_query_has(batches[i].query, object, &query_it)) {
                test_assert(result == 0);
                result = it.entities[i];
                ecs_iter_fini(&query_it);
            }
        }
    }
    return result;
}

void Renderer_batch_permutations(void) {
    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);
    ecs_singleton_set(world, FlecsEngineImpl, {0});
    FlecsEngineImpl *engine = ecs_singleton_get_mut(
        world, FlecsEngineImpl);
    engine->default_attr_cache = flecsEngine_defaultAttrCache_create();
    ecs_log_set_level(-4);
    renderer_batch_root(world);
    ecs_log_set_level(-1);

    ecs_entity_t objects[64];
    ecs_entity_t matched_batches[64];
    int32_t object_count = 0;

    for (int32_t dynamic = 0; dynamic < 2; dynamic ++) {
        for (int32_t mesh = 0; mesh < 2; mesh ++) {
            for (int32_t owned = 0; owned < 2; owned ++) {
                for (int32_t tint = 0; tint < 2; tint ++) {
                    for (int32_t transmission = 0;
                        transmission < 2; transmission ++)
                    {
                        for (int32_t transparent = 0;
                            transparent < 2; transparent ++)
                        {
                            ecs_entity_t object = renderer_object(
                                world, dynamic, mesh, owned, tint,
                                transmission, transparent);
                            objects[object_count] = object;
                            ecs_entity_t batch = renderer_matched_batch(
                                world, object);
                            matched_batches[object_count] = batch;
                            object_count ++;
                            test_assert(batch != 0);

                            char *path = ecs_get_path(world, batch);
                            test_str(path, renderer_expected_batch(
                                mesh, owned, tint,
                                transmission, transparent));
                            ecs_os_free(path);

                            const FlecsRenderBatch *rb = ecs_get(
                                world, batch, FlecsRenderBatch);
                            flecsEngine_batch_t *buffers =
                                flecsEngine_batch_getCullBuf(rb);
                            test_assert(buffers != NULL);
                            test_assert(
                                buffers->flags & FLECS_BATCH_TRACK_STATIC);
                        }
                    }
                }
            }
        }
    }

    for (int32_t i = 0; i < object_count; i ++) {
        const FlecsRenderBatch *batch = ecs_get(
            world, matched_batches[i], FlecsRenderBatch);
        renderer_prepare_batch(batch);
    }

    for (int32_t i = 0; i < object_count; i ++) {
        bool seen = false;
        for (int32_t b = 0; b < i; b ++) {
            if (matched_batches[b] == matched_batches[i]) {
                seen = true;
                break;
            }
        }
        if (seen) {
            continue;
        }
        const FlecsRenderBatch *batch = ecs_get(
            world, matched_batches[i], FlecsRenderBatch);
        batch->extract_callback(world, engine, NULL, batch);
    }

    for (int32_t i = 0; i < object_count; i ++) {
        bool dynamic = i >= 32;
        const FlecsRenderBatch *batch = ecs_get(
            world, matched_batches[i], FlecsRenderBatch);
        flecsEngine_batch_t *buffers =
            flecsEngine_batch_getCullBuf(batch);
        const FlecsBufferSlot *slot = ecs_get(
            world, objects[i], FlecsBufferSlot);
        if (dynamic) {
            test_assert(slot == NULL);
            test_assert(buffers->buffers.count > 0);
        } else {
            test_assert(slot != NULL);
            test_assert(slot->group != NULL);
            test_assert(slot->group->batch == buffers);
            test_assert(buffers->static_buffers.count > 0);
        }
    }

    for (int32_t i = 0; i < object_count; i ++) {
        ecs_entity_t base = ecs_get_target(
            world, objects[i], EcsIsA, 0);
        FlecsMesh3Impl *mesh = ecs_get_mut(
            world, base, FlecsMesh3Impl);
        if (mesh) {
            mesh->index_buffer = NULL;
            mesh->index_count = 0;
        }
    }

    ecs_fini(world);
}
