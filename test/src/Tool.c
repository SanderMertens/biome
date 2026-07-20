#include "../../src/modules/ghost.c"
#include <biome_test.h>
#include <stdio.h>

typedef struct GhostRenderCtx {
    ecs_entity_t asset;
    ecs_entity_t ghost;
    int32_t frames;
} GhostRenderCtx;

static void GhostRender(ecs_iter_t *it) {
    GhostRenderCtx *ctx = it->ctx;
    FlecsMesh3 *mesh = ecs_get_mut(it->world, ctx->asset, FlecsMesh3);
    flecs_vec3_t *vertices = ecs_vec_first_t(
        &mesh->vertices, flecs_vec3_t);
    vertices[0].x = ctx->frames & 1 ? -0.5f : -0.45f;
    flecsEngine_mesh3_updateVertices(it->world, ctx->asset);

    flecs_draw_instance_t instance = {
        .position = {0, 0, 0}
    };
    flecsEngine_draw(it->world, ctx->ghost, &instance, 1);
    ctx->frames ++;
}

static ecs_entity_t ghost_render_asset(ecs_world_t *world) {
    ecs_entity_t asset = ecs_entity(world, {
        .name = "ghost_render_asset",
        .add = ecs_ids(EcsPrefab)
    });
    FlecsMesh3 *mesh = ecs_ensure(world, asset, FlecsMesh3);
    ecs_vec_set_count_t(NULL, &mesh->vertices, flecs_vec3_t, 3);
    ecs_vec_set_count_t(NULL, &mesh->normals, flecs_vec3_t, 3);
    ecs_vec_set_count_t(NULL, &mesh->indices, uint32_t, 3);
    flecs_vec3_t *vertices = ecs_vec_first_t(
        &mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *normals = ecs_vec_first_t(
        &mesh->normals, flecs_vec3_t);
    uint32_t *indices = ecs_vec_first_t(
        &mesh->indices, uint32_t);
    vertices[0] = (flecs_vec3_t){-0.5f, -0.5f, 0};
    vertices[1] = (flecs_vec3_t){0.5f, -0.5f, 0};
    vertices[2] = (flecs_vec3_t){0, 0.5f, 0};
    for (int32_t i = 0; i < 3; i ++) {
        normals[i] = (flecs_vec3_t){0, 0, 1};
        indices[i] = (uint32_t)i;
    }
    ecs_modified(world, asset, FlecsMesh3);
    ecs_set(world, asset, FlecsRgba, {255, 255, 255, 255});
    ecs_set(world, asset, FlecsEmissive, {
        .strength = 1,
        .color = {255, 255, 255, 255}
    });
    return asset;
}

void Tool_render_ghost(void) {
    const char *output = "/tmp/biome_render_ghost.ppm";
    remove(output);

    ecs_world_t *world = ecs_init();
    ECS_IMPORT(world, FlecsEngine);
    ECS_IMPORT(world, biomeGhost);

    ecs_entity_t surface = ecs_entity(world, { .name = "surface" });
    ecs_set(world, surface, FlecsSurface, {
        .width = 64,
        .height = 64,
        .resolution_scale = 1,
        .msaa = FlecsMsaaOff,
        .write_to_file = output,
        .write_nth = 3
    });

    ecs_entity_t camera = ecs_entity(world, { .name = "camera" });
    ecs_set(world, camera, FlecsPosition3, {0, 0, 3});
    ecs_set(world, camera, FlecsCamera, {
        .fov = 0.785398f,
        .near_ = 0.1f,
        .far_ = 100,
        .aspect_ratio = 1
    });

    ecs_entity_t geometry = ecs_entity(world, { .name = "geometry" });
    ecs_entity_t geometry_batch = ecs_lookup(
        world, "flecs.engine.renderer.GeometryBatch");
    test_assert(geometry_batch != 0);
    ecs_add_id(world, geometry, geometry_batch);

    ecs_entity_t view = ecs_entity(world, { .name = "view" });
    ecs_set(world, view, FlecsRenderView, {
        .camera = camera,
        .ambient_intensity = 1
    });
    FlecsRenderBatchSet batch_set = {0};
    ecs_vec_append_t(NULL, &batch_set.batches, ecs_entity_t)[0] = geometry;
    ecs_set_ptr(world, view, FlecsRenderBatchSet, &batch_set);
    ecs_vec_fini_t(NULL, &batch_set.batches, ecs_entity_t);

    GhostRenderCtx ctx = {
        .asset = ghost_render_asset(world)
    };
    ctx.ghost = biomeGhostGet(world, ctx.asset);
    test_assert(ctx.ghost != 0);

    ecs_system(world, {
        .entity = ecs_entity(world, { .name = "GhostRender" }),
        .query.terms = {{
            .id = ecs_id(FlecsSurface),
            .src.id = surface,
            .inout = EcsIn
        }},
        .callback = GhostRender,
        .ctx = &ctx,
        .phase = EcsPreStore
    });

    for (int32_t i = 0; i < 4; i ++) {
        if (!ecs_progress(world, 0.016)) {
            break;
        }
    }

    test_assert(ctx.frames >= 3);
    ecs_fini(world);

    FILE *file = fopen(output, "rb");
    test_assert(file != NULL);
    int32_t width = 0;
    int32_t height = 0;
    int32_t max_value = 0;
    test_int(fscanf(file, "P6 %d %d %d ", &width, &height, &max_value), 3);
    test_int(width, 64);
    test_int(height, 64);
    test_int(max_value, 255);
    bool visible = false;
    for (int32_t i = 0; i < width * height * 3; i ++) {
        if (fgetc(file) > 0) {
            visible = true;
            break;
        }
    }
    fclose(file);
    test_bool(visible, true);
    test_int(remove(output), 0);
}
