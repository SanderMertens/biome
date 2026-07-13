#ifndef FLECS_ENGINE_WINDOW_IMPL_H
#define FLECS_ENGINE_WINDOW_IMPL_H

extern const FlecsEngineSurfaceInterface flecsEngineWindowOps;

GLFWwindow *flecsEngine_createGlfwWindow(
    const char *title,
    int width,
    int height);

void flecsEngine_terminateGlfw(void);

#endif
