#ifndef FLECS_ENGINE_DRAW_H
#define FLECS_ENGINE_DRAW_H

/* Immediate mode rendering. Submitted objects are rendered for a single frame
 * without creating entities. Objects are resolved from a (prefab) entity that
 * describes the renderable content (meshes, primitive shapes, materials,
 * children), which makes the API usable for object ghosts, gizmos and debug
 * shapes. */

typedef struct flecs_draw_instance_t {
    flecs_vec3_t position;
    flecs_vec3_t rotation;
    flecs_vec3_t scale;     /* {0, 0, 0} is treated as {1, 1, 1} */
} flecs_draw_instance_t;

void flecsEngine_draw(
    ecs_world_t *world,
    ecs_entity_t prefab,
    const flecs_draw_instance_t *instances,
    int32_t count);

#endif
