#ifndef FLECS_ENGINE
#define FLECS_ENGINE

#include <flecs.h>
#include <cglm/cglm.h>

typedef struct {
    float x, y;
} flecs_vec2_t;

extern ECS_COMPONENT_DECLARE(flecs_vec2_t);

typedef struct {
    float x, y, z;
} flecs_vec3_t;

extern ECS_COMPONENT_DECLARE(flecs_vec3_t);

typedef struct {
    float x, y, z, w;
} flecs_vec4_t;

extern ECS_COMPONENT_DECLARE(flecs_vec4_t);

typedef mat4 flecs_mat4_t;

extern ECS_COMPONENT_DECLARE(flecs_mat4_t);

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} flecs_rgba_t;

extern ECS_COMPONENT_DECLARE(flecs_rgba_t);

#include "modules/surface.h"
#include "modules/engine.h"
#include "modules/renderer.h"
#include "modules/render_batches.h"
#include "modules/render_effects.h"
#include "modules/atmosphere.h"
#include "modules/clouds.h"
#include "modules/transform3.h"
#include "modules/movement.h"
#include "modules/input.h"
#include "modules/camera.h"
#include "modules/light.h"
#include "modules/stars.h"
#include "modules/time_of_day.h"
#include "modules/geometry_mesh.h"
#include "modules/geometry_primitives3.h"
#include "modules/terrain.h"
#include "modules/material.h"
#include "modules/gltf.h"
#include "modules/ui2d.h"
#include "modules/ui.h"
#include "modules/particles.h"
#include "modules/draw.h"

void FlecsEngineImport(
    ecs_world_t *world);

void flecsEngine_initTracy(void);

#endif
