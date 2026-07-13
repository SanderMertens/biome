#ifndef FLECS_ENGINE_CAMERA_H
#define FLECS_ENGINE_CAMERA_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_CAMERA_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_STRUCT(FlecsCameraController, {
    ecs_entity_t terrain;
    float clearance;
});

extern ECS_COMPONENT_DECLARE(FlecsCameraController);

ECS_STRUCT(FlecsCamera, {
    float fov;
    float near_;
    float far_;
    float aspect_ratio;
    bool orthographic;
});

/* View basis vectors of a camera entity (Position3 + Rotation3), matching
 * the camera controller's conventions. Any output may be NULL. */
bool flecsEngine_cameraViewBasis(
    const ecs_world_t *world,
    ecs_entity_t camera,
    vec3 out_forward,
    vec3 out_right,
    vec3 out_up);

/* World-space picking ray through a screen point. view_norm_x/y are in
 * [-1, 1] with y down, as provided by FlecsInput.mouse.view_norm. */
bool flecsEngine_cameraScreenRay(
    const ecs_world_t *world,
    ecs_entity_t camera,
    float view_norm_x,
    float view_norm_y,
    vec3 out_origin,
    vec3 out_dir);

/* Intersect a ray with the horizontal plane at plane_y. */
bool flecsEngine_rayPlaneY(
    const vec3 origin,
    const vec3 dir,
    float plane_y,
    vec3 out_point);

#endif
