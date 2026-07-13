#include "../../private.h"
#include "shaders/shaders.h"

#ifndef FLECS_ENGINE_RENDERER_IMPL
#define FLECS_ENGINE_RENDERER_IMPL

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_RENDERER_IMPL_IMPL
#define ECS_META_IMPL EXTERN
#endif

#include "render_effects.h"
#include "render_materials.h"
#include "render_textures.h"
#include "render_view.h"
#include "render_batch.h"
#include "shadow.h"
#include "ibl.h"
#include "cluster.h"
#include "shader.h"
#include "surface.h"

int flecsEngine_initRenderer(
    ecs_world_t *world,
    FlecsEngineImpl *impl);

void flecsEngine_renderer_cleanup(
    FlecsEngineImpl *impl);

/* Debug HTTP server (port 8000) — see debug_server.c */
void flecsEngine_debugServer_init(ecs_world_t *world);
void flecsEngine_debugServer_fini(void);
void flecsEngine_debugServer_dequeue(float delta_time);

// Import renderer module
void FlecsEngineRendererImport(
    ecs_world_t *world);

#undef ECS_META_IMPL

#endif
