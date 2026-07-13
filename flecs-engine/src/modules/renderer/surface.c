#define FLECS_ENGINE_SURFACE_IMPL
#include "renderer.h"
#include "../engine/engine.h"

ECS_COMPONENT_DECLARE(FlecsAnisotropy);
ECS_COMPONENT_DECLARE(FlecsMsaa);
ECS_COMPONENT_DECLARE(FlecsTextureQuality);
ECS_COMPONENT_DECLARE(FlecsSurface);
ECS_COMPONENT_DECLARE(FlecsSurfaceImpl);

void flecsEngine_surfaceImpl_release(
    FlecsSurfaceImpl *ptr)
{
    FLECS_WGPU_RELEASE(ptr->offscreen_view, wgpuTextureViewRelease);
    FLECS_WGPU_RELEASE(ptr->offscreen_texture, wgpuTextureRelease);
    if (ptr->path) {
        ecs_os_free(ptr->path);
        ptr->path = NULL;
    }
    FLECS_WGPU_RELEASE(ptr->wgpu_surface, wgpuSurfaceRelease);
    if (ptr->window) {
        glfwDestroyWindow(ptr->window);
        ptr->window = NULL;
    }
}

ECS_DTOR(FlecsSurfaceImpl, ptr, {
    flecsEngine_surfaceImpl_release(ptr);
})

ECS_MOVE(FlecsSurfaceImpl, dst, src, {
    flecsEngine_surfaceImpl_release(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

int flecsEngine_surfaceInterface_initInstance(
    FlecsEngineImpl *engine,
    const FlecsSurface *config,
    FlecsSurfaceImpl *impl)
{
    if (!impl || !impl->interface || !impl->interface->init_instance) {
        return 0;
    }
    return impl->interface->init_instance(engine, config, impl);
}

int flecsEngine_surfaceInterface_configureTarget(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl)
{
    if (!impl || !impl->interface || !impl->interface->configure_target) {
        return 0;
    }
    return impl->interface->configure_target(world, engine, impl);
}

int flecsEngine_surfaceInterface_prepareFrame(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl)
{
    if (!impl || !impl->interface || !impl->interface->prepare_frame) {
        return -1;
    }
    return impl->interface->prepare_frame(world, engine, impl);
}

int flecsEngine_surfaceInterface_acquireFrame(
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl,
    FlecsEngineSurface *target)
{
    if (!impl || !impl->interface || !impl->interface->acquire_frame) {
        return -1;
    }
    return impl->interface->acquire_frame(engine, impl, target);
}

int flecsEngine_surfaceInterface_encodeFrame(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl,
    WGPUCommandEncoder encoder,
    FlecsEngineSurface *target)
{
    if (!impl || !impl->interface || !impl->interface->encode_frame) {
        return 0;
    }
    return impl->interface->encode_frame(world, engine, impl, encoder, target);
}

int flecsEngine_surfaceInterface_submitFrame(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl,
    const FlecsEngineSurface *target)
{
    if (!impl || !impl->interface || !impl->interface->submit_frame) {
        return -1;
    }
    return impl->interface->submit_frame(world, engine, impl, target);
}

void flecsEngine_surfaceInterface_onFrameFailed(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl)
{
    if (!impl || !impl->interface || !impl->interface->on_frame_failed) {
        return;
    }
    impl->interface->on_frame_failed(world, engine, impl);
}

void flecsEngine_surfaceInterface_cleanup(
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl,
    bool terminate_runtime)
{
    if (!impl || !impl->interface || !impl->interface->cleanup) {
        return;
    }
    impl->interface->cleanup(engine, impl, terminate_runtime);
}

void flecsEngine_surface_set(
    ecs_world_t *world,
    ecs_entity_t surface_entity,
    int32_t width,
    int32_t height,
    int32_t resolution_scale)
{
    if (resolution_scale < 1) resolution_scale = 1;

    int32_t actual_width = width / resolution_scale;
    int32_t actual_height = height / resolution_scale;
    if (actual_width < 1) actual_width = 1;
    if (actual_height < 1) actual_height = 1;

    FlecsSurface *surface = ecs_get_mut(world, surface_entity, FlecsSurface);
    surface->width = width;
    surface->height = height;
    surface->resolution_scale = resolution_scale;
    surface->actual_width = actual_width;
    surface->actual_height = actual_height;
}

int32_t flecsEngine_surface_sampleCount(
    const FlecsSurface *surface)
{
    if (!surface || surface->msaa == FlecsMsaaDefault ||
        surface->msaa == FlecsMsaaOff)
    {
        return 1;
    }
    return (int32_t)surface->msaa;
}

static void FlecsOnSurfaceSet(
    ecs_iter_t *it)
{
    if (ecs_singleton_get(it->world, FlecsEngineImpl)) {
        return;
    }

    FlecsSurface *configs = ecs_field(it, FlecsSurface, 0);
    for (int32_t i = 0; i < it->count; i ++) {
        FlecsSurface *config = &configs[i];
        ecs_entity_t entity = it->entities[i];

        FlecsSurfaceImpl impl = {0};

        if (config->write_to_file) {
            impl.interface = &flecsEngineFrameOutOps;
        } else {
            int w = config->width > 0 ? config->width : 1280;
            int h = config->height > 0 ? config->height : 800;
            const char *title = config->title ? config->title : "Flecs Engine";

#ifdef __EMSCRIPTEN__
            /* On the web the WebGPU surface is created directly from the
             * "#canvas" element, so no GLFW window is needed. Creating one
             * (glfwInit) does not complete in the browser, so skip it. */
            (void)title; (void)w; (void)h;
            impl.window = NULL;
#else
            impl.window = flecsEngine_createGlfwWindow(title, w, h);
            if (!impl.window) {
                ecs_quit(it->world);
                return;
            }
#endif
            impl.vsync = config->vsync;
            impl.interface = &flecsEngineWindowOps;
        }

        if (flecsEngine_init(it->world, entity, config, &impl)) {
            ecs_quit(it->world);
            return;
        }

        ecs_set_ptr(it->world, entity, FlecsSurfaceImpl, &impl);
        break;
    }
}

void flecsEngine_surface_register(
    ecs_world_t *world)
{
    ECS_META_COMPONENT(world, FlecsAnisotropy);
    ECS_META_COMPONENT(world, FlecsMsaa);
    ECS_META_COMPONENT(world, FlecsTextureQuality);
    ECS_META_COMPONENT(world, FlecsSurface);

    ECS_COMPONENT_DEFINE(world, FlecsSurfaceImpl);
    ecs_set_hooks(world, FlecsSurfaceImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsSurfaceImpl),
        .dtor = ecs_dtor(FlecsSurfaceImpl)
    });

    ecs_add_pair(world, ecs_id(FlecsSurface), EcsWith, ecs_id(FlecsSurfaceImpl));

    ecs_set_hooks(world, FlecsSurface, {
        .ctor = flecs_default_ctor,
        .on_set = FlecsOnSurfaceSet
    });
}
