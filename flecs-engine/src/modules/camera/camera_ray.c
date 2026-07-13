#include <math.h>

#include "../../private.h"

/* Reusable camera math for games: view basis vectors, screen point to
 * world ray conversion, and a ground plane intersection helper. Uses the
 * same conventions as the camera controller (Position3 + Rotation3). */

bool flecsEngine_cameraViewBasis(
    const ecs_world_t *world,
    ecs_entity_t camera,
    vec3 out_forward,
    vec3 out_right,
    vec3 out_up)
{
    const FlecsPosition3 *pos = ecs_get(world, camera, FlecsPosition3);
    const FlecsRotation3 *rot = ecs_get(world, camera, FlecsRotation3);
    if (!pos || !rot) {
        return false;
    }

    vec3 f = {
        sinf(rot->y) * cosf(rot->x),
        sinf(rot->x),
        cosf(rot->y) * cosf(rot->x)
    };
    vec3 up = { 0, 1, 0 };
    vec3 s, u;
    glm_vec3_cross(f, up, s);
    glm_vec3_normalize(s);
    glm_vec3_cross(s, f, u);

    if (out_forward) glm_vec3_copy(f, out_forward);
    if (out_right) glm_vec3_copy(s, out_right);
    if (out_up) glm_vec3_copy(u, out_up);
    return true;
}

bool flecsEngine_cameraScreenRay(
    const ecs_world_t *world,
    ecs_entity_t camera,
    float view_norm_x,
    float view_norm_y,
    vec3 out_origin,
    vec3 out_dir)
{
    const FlecsPosition3 *pos = ecs_get(world, camera, FlecsPosition3);
    const FlecsCamera *cam = ecs_get(world, camera, FlecsCamera);
    if (!pos || !cam) {
        return false;
    }

    vec3 f, s, u;
    if (!flecsEngine_cameraViewBasis(world, camera, f, s, u)) {
        return false;
    }

    /* view_norm is in [-1, 1] with y pointing down (window coords), as
     * provided by FlecsInput.mouse.view_norm. */
    float ndc_x = view_norm_x;
    float ndc_y = -view_norm_y;

    float aspect = cam->aspect_ratio > 0 ? cam->aspect_ratio : 1.6f;
    float tan_half = tanf(cam->fov * 0.5f);

    vec3 dir;
    for (int i = 0; i < 3; i ++) {
        dir[i] = f[i]
            + s[i] * ndc_x * tan_half * aspect
            + u[i] * ndc_y * tan_half;
    }
    glm_vec3_normalize(dir);

    if (out_origin) {
        out_origin[0] = pos->x;
        out_origin[1] = pos->y;
        out_origin[2] = pos->z;
    }
    if (out_dir) glm_vec3_copy(dir, out_dir);
    return true;
}

bool flecsEngine_rayPlaneY(
    const vec3 origin,
    const vec3 dir,
    float plane_y,
    vec3 out_point)
{
    if (fabsf(dir[1]) < 1e-6f) {
        return false;
    }
    float t = (plane_y - origin[1]) / dir[1];
    if (t < 0) {
        return false;
    }
    if (out_point) {
        out_point[0] = origin[0] + dir[0] * t;
        out_point[1] = plane_y;
        out_point[2] = origin[2] + dir[2] * t;
    }
    return true;
}
