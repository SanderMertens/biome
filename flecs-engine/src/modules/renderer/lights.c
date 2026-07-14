#include <math.h>
#include <string.h>

#include "renderer.h"
#include "../../tracy_hooks.h"
#include "flecs_engine.h"

void flecsEngine_setupLights(
    const ecs_world_t *world,
    FlecsEngineImpl *engine)
{
    FLECS_TRACY_ZONE_BEGIN("SetupLights");
    int32_t count = 0;

    /* Point lights */
    if (engine->lighting.point_light_query) {
        ecs_iter_t it = ecs_query_iter(world, engine->lighting.point_light_query);
        while (ecs_query_next(&it)) {
            const FlecsPointLight *lights = ecs_field(&it, FlecsPointLight, 0);
            const FlecsWorldTransform3 *transforms = ecs_field(&it, FlecsWorldTransform3, 1);
            const FlecsRgba *colors = ecs_field(&it, FlecsRgba, 2);

            int32_t needed = count + it.count;
            flecsEngine_cluster_ensureLights(engine, needed);

            for (int32_t i = 0; i < it.count; i ++) {
                const FlecsPointLight *actual_light = flecs_transition_get(
                    world, it.entities[i], ecs_id(FlecsPointLight));
                const FlecsWorldTransform3 *actual_transform =
                    flecs_transition_get(world, it.entities[i],
                        ecs_id(FlecsWorldTransform3));
                if (!actual_light) actual_light = &lights[i];
                if (!actual_transform) actual_transform = &transforms[i];
                FlecsGpuLight *gpu_light = &engine->lighting.cpu_lights[count];
                gpu_light->position[0] = actual_transform->m[3][0];
                gpu_light->position[1] = actual_transform->m[3][1];
                gpu_light->position[2] = actual_transform->m[3][2];
                gpu_light->position[3] = actual_light->range;

                gpu_light->direction[0] = 0.0f;
                gpu_light->direction[1] = 0.0f;
                gpu_light->direction[2] = 0.0f;
                gpu_light->direction[3] = -2.0f; /* sentinel: not a spot light */

                float r = 1.0f, g = 1.0f, b = 1.0f;
                if (colors) {
                    FlecsRgba color_storage;
                    const FlecsRgba *color =
                        flecsEngine_material_resolveRgba(
                            world, it.entities[i], &colors[i],
                            &color_storage);
                    r = flecsEngine_colorChannelToFloat(color->r);
                    g = flecsEngine_colorChannelToFloat(color->g);
                    b = flecsEngine_colorChannelToFloat(color->b);
                }

                gpu_light->color[0] = r * actual_light->intensity;
                gpu_light->color[1] = g * actual_light->intensity;
                gpu_light->color[2] = b * actual_light->intensity;
                gpu_light->color[3] = 0.0f;

                count ++;
            }
        }
    }

    /* Spot lights */
    if (engine->lighting.spot_light_query) {
        ecs_iter_t it = ecs_query_iter(world, engine->lighting.spot_light_query);
        while (ecs_query_next(&it)) {
            const FlecsSpotLight *lights = ecs_field(&it, FlecsSpotLight, 0);
            const FlecsWorldTransform3 *transforms = ecs_field(&it, FlecsWorldTransform3, 1);
            const FlecsRgba *colors = ecs_field(&it, FlecsRgba, 2);

            int32_t needed = count + it.count;
            flecsEngine_cluster_ensureLights(engine, needed);

            for (int32_t i = 0; i < it.count; i ++) {
                const FlecsSpotLight *actual_light = flecs_transition_get(
                    world, it.entities[i], ecs_id(FlecsSpotLight));
                const FlecsWorldTransform3 *actual_transform =
                    flecs_transition_get(world, it.entities[i],
                        ecs_id(FlecsWorldTransform3));
                if (!actual_light) actual_light = &lights[i];
                if (!actual_transform) actual_transform = &transforms[i];
                FlecsGpuLight *gpu_light = &engine->lighting.cpu_lights[count];
                gpu_light->position[0] = actual_transform->m[3][0];
                gpu_light->position[1] = actual_transform->m[3][1];
                gpu_light->position[2] = actual_transform->m[3][2];
                gpu_light->position[3] = actual_light->range;

                /* Extract forward direction (-Z axis) from world transform */
                float dx = -actual_transform->m[2][0];
                float dy = -actual_transform->m[2][1];
                float dz = -actual_transform->m[2][2];
                float len = sqrtf(dx * dx + dy * dy + dz * dz);
                if (len > 1e-6f) {
                    dx /= len;
                    dy /= len;
                    dz /= len;
                } else {
                    dx = 0.0f;
                    dy = -1.0f;
                    dz = 0.0f;
                }

                gpu_light->direction[0] = dx;
                gpu_light->direction[1] = dy;
                gpu_light->direction[2] = dz;
                gpu_light->direction[3] = cosf(actual_light->outer_angle * (3.141592653589793f / 180.0f));

                float r = 1.0f, g = 1.0f, b = 1.0f;
                if (colors) {
                    FlecsRgba color_storage;
                    const FlecsRgba *color =
                        flecsEngine_material_resolveRgba(
                            world, it.entities[i], &colors[i],
                            &color_storage);
                    r = flecsEngine_colorChannelToFloat(color->r);
                    g = flecsEngine_colorChannelToFloat(color->g);
                    b = flecsEngine_colorChannelToFloat(color->b);
                }

                gpu_light->color[0] = r * actual_light->intensity;
                gpu_light->color[1] = g * actual_light->intensity;
                gpu_light->color[2] = b * actual_light->intensity;
                gpu_light->color[3] = cosf(actual_light->inner_angle * (3.141592653589793f / 180.0f));

                count ++;
            }
        }
    }

    engine->lighting.light_count = count;
    FLECS_TRACY_ZONE_END;
}
