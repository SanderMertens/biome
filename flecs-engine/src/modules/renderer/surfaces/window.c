#include "../renderer.h"

static bool flecs_engine_glfw_initialized = false;

GLFWwindow *flecsEngine_createGlfwWindow(
    const char *title,
    int width,
    int height)
{
    if (!flecs_engine_glfw_initialized) {
        if (!glfwInit()) {
            ecs_err("Failed to initialize GLFW\n");
            return NULL;
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        flecs_engine_glfw_initialized = true;
    }

    GLFWwindow *window = glfwCreateWindow(width, height, title, NULL, NULL);
    if (!window) {
        ecs_err("Failed to create window '%s'\n", title);
        return NULL;
    }

    return window;
}

void flecsEngine_terminateGlfw(void)
{
    if (flecs_engine_glfw_initialized) {
        glfwTerminate();
        flecs_engine_glfw_initialized = false;
    }
}

static int flecsEngine_window_initInstance(
    FlecsEngineImpl *engine,
    const FlecsSurface *config,
    FlecsSurfaceImpl *impl)
{
    (void)config;

    impl->wgpu_surface = flecsEngine_createSurface(engine->instance, impl->window);
    if (!impl->wgpu_surface) {
        ecs_err("Failed to create wgpu surface\n");
        return -1;
    }

    ecs_dbg("[engine] created instance=%p surface=%p",
        (void*)engine->instance, (void*)impl->wgpu_surface);
    return 0;
}

static int flecsEngine_window_configureTarget(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl)
{
    if (flecsEngine_configureSurface(world, engine, impl)) {
        return -1;
    }

    engine->target_format = impl->surface_config.format;

    ecs_dbg("[engine] surface configured %ux%u format=%d",
        impl->surface_config.width,
        impl->surface_config.height,
        (int)impl->surface_config.format);

    return 0;
}

static int flecsEngine_window_prepareFrame(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl)
{
#ifndef __EMSCRIPTEN__
    if (glfwWindowShouldClose(impl->window)) {
        ecs_quit(world);
        return 1;
    }
#endif

    int width = 0;
    int height = 0;
    flecsEngine_getFramebufferSize(impl->window, &width, &height);

    const FlecsSurface *surface = ecs_get(world, engine->surface, FlecsSurface);
    if (width != surface->width || height != surface->height) {
        impl->surface_config.width = (uint32_t)width;
        impl->surface_config.height = (uint32_t)height;

        flecsEngine_surface_set(
            world,
            engine->surface,
            width,
            height,
            surface->resolution_scale);

        flecsEngine_reconfigureSurface(engine, impl);
    }

    /* Window size in points (mouse coordinate space); differs from the
     * framebuffer size on high-DPI displays. */
    int point_w = 0;
    int point_h = 0;
    glfwGetWindowSize(impl->window, &point_w, &point_h);
    if (point_w > 0 && point_h > 0) {
        FlecsSurface *s = ecs_get_mut(world, engine->surface, FlecsSurface);
        s->point_width = point_w;
        s->point_height = point_h;
    }

    return 0;
}

static int flecsEngine_window_acquireFrame(
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl,
    FlecsEngineSurface *target)
{
    return flecsEngine_acquireFrame(engine, impl, target);
}

static int flecsEngine_window_submitFrame(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl,
    const FlecsEngineSurface *target)
{
    (void)world;
    (void)engine;
    (void)target;
    flecsEngine_presentSurface(impl->wgpu_surface);
    return 0;
}

#ifdef __EMSCRIPTEN__
static void flecsEngine_window_cleanup(
    FlecsEngineImpl *engine,
    FlecsSurfaceImpl *impl,
    bool terminate_runtime)
{
    (void)engine;
    (void)impl;
    (void)terminate_runtime;
    flecsEngine_releaseSwapChain();
}
#endif

const FlecsEngineSurfaceInterface flecsEngineWindowOps = {
    .init_instance = flecsEngine_window_initInstance,
    .configure_target = flecsEngine_window_configureTarget,
    .prepare_frame = flecsEngine_window_prepareFrame,
    .acquire_frame = flecsEngine_window_acquireFrame,
    .submit_frame = flecsEngine_window_submitFrame,
#ifdef __EMSCRIPTEN__
    .cleanup = flecsEngine_window_cleanup
#endif
};
