#define FLECS_ENGINE_ENGINE_IMPL
#include "engine.h"

#include "../renderer/renderer.h"
#include "../transition/transition.h"
#include "../geometry3/geometry3.h"
#include "../transform3/transform3.h"
#include "../movement/movement.h"
#include "../input/input.h"
#include "../camera/camera.h"
#include "../light/light.h"
#include "../texture/texture.h"
#include "../material/material.h"
#include "../gltf/gltf.h"
#include "../time_of_day/time_of_day.h"
#include "../terrain/terrain.h"

ECS_COMPONENT_DECLARE(flecs_vec2_t);
ECS_COMPONENT_DECLARE(flecs_vec3_t);
ECS_COMPONENT_DECLARE(flecs_vec4_t);
ECS_COMPONENT_DECLARE(flecs_mat4_t);
ECS_COMPONENT_DECLARE(flecs_rgba_t);

ECS_COMPONENT_DECLARE(FlecsEngineImpl);

static void flecsEngine_uploadPendingMeshes(
    ecs_world_t *world);

static void flecsEngine_cleanup(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    bool terminate_runtime)
{
    // Queries do not have to be freed because they are automatically cleaned up
    // when the world is deleted.

    impl->fallback_hdri = 0;

    /* Delete views first so that per-view shadow/cluster/snapshot/bind
     * groups are released before engine-side shared resources. */
    ecs_delete_with(world, ecs_id(FlecsRenderView));

    flecsEngine_renderer_cleanup(impl);
    flecsEngine_shadow_cleanupShared(impl);
    flecsEngine_material_releaseBuffer(impl);
    flecsEngine_transmission_releaseShared(impl);

    if (impl->surface && ecs_is_alive(world, impl->surface)) {
        FlecsSurfaceImpl *surf = ecs_get_mut(world, impl->surface, FlecsSurfaceImpl);
        if (surf) {
            flecsEngine_surfaceInterface_cleanup(impl, surf, terminate_runtime);
        }
        ecs_delete(world, impl->surface);
    }
    impl->surface = 0;

    ecs_delete_with(world, ecs_id(FlecsRenderBatch));

    FLECS_WGPU_RELEASE(impl->queue, wgpuQueueRelease);
    FLECS_WGPU_RELEASE(impl->device, wgpuDeviceRelease);
    FLECS_WGPU_RELEASE(impl->adapter, wgpuAdapterRelease);
    FLECS_WGPU_RELEASE(impl->instance, wgpuInstanceRelease);

    if (terminate_runtime) {
        flecsEngine_terminateGlfw();
    }

    flecsEngine_defaultAttrCache_free(impl->default_attr_cache);
}

int flecsEngine_init(
    ecs_world_t *world,
    ecs_entity_t surface_entity,
    const FlecsSurface *config,
    FlecsSurfaceImpl *impl)
{
    if (!config || !impl || !impl->interface) {
        ecs_err("Invalid engine output backend\n");
        return -1;
    }

    int32_t width = config->width;
    int32_t height = config->height;
    if (width <= 0) {
        width = 1280;
    }
    if (height <= 0) {
        height = 800;
    }

    flecsEngine_surface_set(
        world,
        surface_entity,
        width,
        height,
        config->resolution_scale);

    bool was_deferred = ecs_is_deferred(world);
    if (was_deferred) {
        ecs_defer_suspend(world);
    }

    FlecsEngineImpl *engine = ecs_singleton_ensure(world, FlecsEngineImpl);
    engine->surface = surface_entity;
    engine->default_attr_cache = flecsEngine_defaultAttrCache_create();

    WGPUInstanceDescriptor instance_desc = {0};
    engine->instance = wgpuCreateInstance(&instance_desc);
    if (!engine->instance) {
        ecs_err("Failed to create wgpu instance\n");
        goto error;
    }

    if (flecsEngine_surfaceInterface_initInstance(engine, config, impl)) {
        goto error;
    }

    engine->adapter = flecsEngine_requestAdapter(
        engine->instance, impl->wgpu_surface);
    if (!engine->adapter) {
        goto error;
    }

    engine->device = flecsEngine_requestDevice(engine->adapter, engine->instance);
    if (!engine->device) {
        goto error;
    }

    flecsEngine_setDeviceErrorCallback(engine->device);

    engine->queue = wgpuDeviceGetQueue(engine->device);

    if (flecsEngine_surfaceInterface_configureTarget(world, engine, impl)) {
        goto error;
    }

    if (flecsEngine_initRenderer(world, engine)) {
        goto error;
    }

    flecsEngine_uploadPendingMeshes(world);

    if (was_deferred) {
        ecs_defer_resume(world);
    }
    return 0;

error:
    if (impl->interface && impl->interface->cleanup) {
        impl->interface->cleanup(engine, impl, false);
    }
    flecsEngine_surfaceImpl_release(impl);
    flecsEngine_cleanup(world, engine, false);
    if (was_deferred) {
        ecs_defer_resume(world);
    }
    return -1;
}

static void flecsEngine_destroy(
    ecs_iter_t *it)
{
    FlecsEngineImpl *impl = ecs_field(it, FlecsEngineImpl, 0);
    flecsEngine_cleanup(it->world, impl, true);
}

static void flecsEngine_uploadPendingMeshes(
    ecs_world_t *world)
{
    ecs_query_t *query = ecs_query(world, {
        .terms = {{ .id = ecs_id(FlecsMesh3) }},
        .flags = EcsQueryMatchPrefab
    });
    ecs_iter_t it = ecs_query_iter(world, query);
    while (ecs_query_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            const FlecsMesh3Impl *mesh = ecs_get(
                world, it.entities[i], FlecsMesh3Impl);
            if (mesh && mesh->vertex_count && !mesh->vertex_buffer) {
                ecs_modified(world, it.entities[i], FlecsMesh3);
            }
        }
    }
    ecs_query_fini(query);
}

void FlecsEngineImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngine);

    ecs_id(flecs_vec2_t) = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "vec2" }),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) },
        }
    });

    ecs_id(flecs_vec3_t) = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "vec3" }),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) },
            { .name = "z", .type = ecs_id(ecs_f32_t) },
        }
    });

    ecs_id(flecs_vec4_t) = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "vec4" }),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) },
            { .name = "z", .type = ecs_id(ecs_f32_t) },
            { .name = "w", .type = ecs_id(ecs_f32_t) },
        }
    });

    ecs_id(flecs_rgba_t) = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "rgba" }),
        .members = {
            { .name = "r", .type = ecs_id(ecs_u8_t) },
            { .name = "g", .type = ecs_id(ecs_u8_t) },
            { .name = "b", .type = ecs_id(ecs_u8_t) },
            { .name = "a", .type = ecs_id(ecs_u8_t) },
        }
    });

    ecs_set_alias(world, ecs_id(flecs_vec2_t), "flecs_vec2_t");
    ecs_set_alias(world, ecs_id(flecs_vec3_t), "flecs_vec3_t");
    ecs_set_alias(world, ecs_id(flecs_vec4_t), "flecs_vec4_t");
    ecs_set_alias(world, ecs_id(flecs_rgba_t), "flecs_rgba_t");

    ecs_id(flecs_mat4_t) = ecs_component(world, {
        .entity = ecs_entity(world, { .name = "mat4" }),
        .type.size = ECS_SIZEOF(flecs_mat4_t),
        .type.alignment = ECS_ALIGNOF(flecs_mat4_t)
    });

    ecs_struct(world, {
        .entity = ecs_id(flecs_mat4_t),
        .members = {
            { .name = "v", .type = ecs_id(ecs_f32_t), .count = 16}
        }
    });

    ecs_set_alias(world, ecs_id(flecs_mat4_t), "flecs_mat4_t");

    ecs_set_name_prefix(world, "Flecs");

    ECS_COMPONENT_DEFINE(world, FlecsEngineImpl);

    ecs_set_hooks(world, FlecsEngineImpl, {
        .ctor = flecs_default_ctor,
        .on_remove = flecsEngine_destroy
    });

    flecsEngine_surface_register(world);

    ECS_IMPORT(world, FlecsEngineTransition);
    ECS_IMPORT(world, FlecsEngineLight);
    ECS_IMPORT(world, FlecsEngineStars);
    ECS_IMPORT(world, FlecsEngineTexture);
    ECS_IMPORT(world, FlecsEngineMaterial);
    ECS_IMPORT(world, FlecsEngineAtmosphere);
    ECS_IMPORT(world, FlecsEngineRenderer);
    ECS_IMPORT(world, FlecsEngineGeometry3);
    ECS_IMPORT(world, FlecsEngineTerrain);
    ECS_IMPORT(world, FlecsEngineTransform3);
    ECS_IMPORT(world, FlecsEngineMovement);
    ECS_IMPORT(world, FlecsEngineInput);
    ECS_IMPORT(world, FlecsEngineCamera);
    ECS_IMPORT(world, FlecsEngineGltf);
    ECS_IMPORT(world, FlecsEngineTime_of_day);
    ECS_IMPORT(world, FlecsEngineUi2d);
    ECS_IMPORT(world, FlecsEngineUi);
    ECS_IMPORT(world, FlecsEngineParticles);
}
