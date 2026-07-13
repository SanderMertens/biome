#include "renderer.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsRenderBatchSet);

ECS_CTOR(FlecsRenderBatchSet, ptr, {
    ecs_vec_init_t(NULL, &ptr->batches, ecs_entity_t, 0);
})

ECS_MOVE(FlecsRenderBatchSet, dst, src, {
    ecs_vec_fini_t(NULL, &dst->batches, ecs_entity_t);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_COPY(FlecsRenderBatchSet, dst, src, {
    ecs_vec_fini_t(NULL, &dst->batches, ecs_entity_t);
    dst->batches = ecs_vec_copy_t(NULL, &src->batches, ecs_entity_t);
})

ECS_DTOR(FlecsRenderBatchSet, ptr, {
    ecs_vec_fini_t(NULL, &ptr->batches, ecs_entity_t);
})

bool flecsEngine_renderBatchSet_hasTransmission(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderBatchSet *batch_set)
{
    int32_t count = ecs_vec_count(&batch_set->batches);
    ecs_entity_t *batches = ecs_vec_first(&batch_set->batches);

    for (int32_t i = 0; i < count; i ++) {
        ecs_entity_t batch_entity = batches[i];
        if (!batch_entity) {
            continue;
        }

        const FlecsRenderBatchSet *nested = ecs_get(
            world, batch_entity, FlecsRenderBatchSet);
        if (nested) {
            if (flecsEngine_renderBatchSet_hasTransmission(world, engine, nested)) {
                return true;
            }
            continue;
        }

        const FlecsRenderBatch *batch = ecs_get(
            world, batch_entity, FlecsRenderBatch);
        if (batch && batch->needs_transmission &&
            ecs_query_is_true(batch->query))
        {
            return true;
        }
    }

    return false;
}

void flecsEngine_renderBatchSet_extract(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatchSet *batch_set)
{
    int32_t count = ecs_vec_count(&batch_set->batches);
    ecs_entity_t *batches = ecs_vec_first(&batch_set->batches);

    for (int32_t i = 0; i < count; i ++) {
        ecs_entity_t batch_entity = batches[i];
        if (!batch_entity) {
            continue;
        }

        const FlecsRenderBatchSet *nested = ecs_get(
            world, batch_entity, FlecsRenderBatchSet);
        if (nested) {
            flecsEngine_renderBatchSet_extract(world, engine, view_impl, nested);
            continue;
        }

        flecsEngine_renderBatch_extract(world, engine, view_impl, batch_entity);
    }
}

void flecsEngine_renderBatchSet_upload(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatchSet *batch_set)
{
    int32_t count = ecs_vec_count(&batch_set->batches);
    ecs_entity_t *batches = ecs_vec_first(&batch_set->batches);

    for (int32_t i = 0; i < count; i ++) {
        ecs_entity_t batch_entity = batches[i];
        if (!batch_entity) {
            continue;
        }

        const FlecsRenderBatchSet *nested = ecs_get(
            world, batch_entity, FlecsRenderBatchSet);
        if (nested) {
            flecsEngine_renderBatchSet_upload(world, engine, view_impl, nested);
            continue;
        }

        flecsEngine_renderBatch_upload(world, engine, view_impl, batch_entity);
    }
}

void flecsEngine_renderBatchSet_render(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatchSet *batch_set,
    WGPURenderPassEncoder pass,
    const FlecsRenderView *view,
    int phase)
{
    int32_t count = ecs_vec_count(&batch_set->batches);
    ecs_entity_t *batches = ecs_vec_first(&batch_set->batches);

    for (int32_t i = 0; i < count; i ++) {
        ecs_entity_t batch_entity = batches[i];
        if (!batch_entity) {
            continue;
        }

        const FlecsRenderBatchSet *nested = ecs_get(
            world, batch_entity, FlecsRenderBatchSet);
        if (nested) {
            flecsEngine_renderBatchSet_render(
                world, engine, view_impl, nested, pass, view, phase);
            continue;
        }

        /* Phase filtering: 0 = all, 1 = opaque only, 2 = post-snapshot only */
        if (phase != 0) {
            const FlecsRenderBatch *batch = ecs_get(
                world, batch_entity, FlecsRenderBatch);
            if (batch) {
                bool after = batch->render_after_snapshot;
                if (phase == 1 && after) continue;
                if (phase == 2 && !after) continue;
            }
        }

        flecsEngine_renderBatch_render(
            world, engine, view_impl, pass, view, batch_entity);
    }
}

void flecsEngine_renderBatchSet_renderShadow(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    const FlecsRenderBatchSet *batch_set,
    WGPURenderPassEncoder pass)
{
    int32_t count = ecs_vec_count(&batch_set->batches);
    ecs_entity_t *batches = ecs_vec_first(&batch_set->batches);

    for (int32_t i = 0; i < count; i ++) {
        ecs_entity_t batch_entity = batches[i];
        if (!batch_entity) {
            continue;
        }

        const FlecsRenderBatchSet *nested = ecs_get(
            world, batch_entity, FlecsRenderBatchSet);
        if (nested) {
            flecsEngine_renderBatchSet_renderShadow(
                world, engine, view_impl, nested, pass);
            continue;
        }

        flecsEngine_renderBatch_renderShadow(
            world, engine, view_impl, pass, batch_entity);
    }
}

void flecsEngine_renderBatchSet_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsRenderBatchSet);

    ecs_set_hooks(world, FlecsRenderBatchSet, {
        .ctor = ecs_ctor(FlecsRenderBatchSet),
        .move = ecs_move(FlecsRenderBatchSet),
        .copy = ecs_copy(FlecsRenderBatchSet),
        .dtor = ecs_dtor(FlecsRenderBatchSet)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsRenderBatchSet),
        .members = {
            { .name = "batches", .type = flecsEngine_vecEntity(world) }
        }
    });
}
