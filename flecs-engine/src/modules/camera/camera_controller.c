#include <math.h>

#include "../../private.h"
#include "../../tracy_hooks.h"

static const float CameraMaxSpeed = 40.0;
static const float CameraSprintMultiplier = 5.0;
static const float CameraAngularMaxSpeed = 1.5;
static const float CameraMoveAccelSmoothing = 6.0;
static const float CameraMoveDecelSmoothing = 18.0;
static const float CameraAngularAccelSmoothing = 5.0;
static const float CameraAngularDecelSmoothing = 15.0;
static const float CameraMouseSensitivity = 0.003;
static const float CameraScrollZoomSpeed = 3.0;

extern ECS_COMPONENT_DECLARE(FlecsLookAt);
extern ECS_COMPONENT_DECLARE(FlecsPosition3);
extern ECS_COMPONENT_DECLARE(FlecsRotation3);
extern ECS_COMPONENT_DECLARE(FlecsVelocity3);
extern ECS_COMPONENT_DECLARE(FlecsAngularVelocity3);
extern ECS_COMPONENT_DECLARE(FlecsInput);
extern ECS_COMPONENT_DECLARE(FlecsCameraController);
extern ECS_COMPONENT_DECLARE(FlecsTerrain);

static
void CameraControllerClampTerrain(
    ecs_iter_t *it)
{
    const FlecsCameraController *cc = ecs_field(it, FlecsCameraController, 0);
    FlecsPosition3 *p = ecs_field(it, FlecsPosition3, 1);
    FlecsVelocity3 *v = ecs_field(it, FlecsVelocity3, 2);

    for (int32_t i = 0; i < it->count; i ++) {
        if (!cc[i].terrain || !ecs_is_alive(it->world, cc[i].terrain)) {
            continue;
        }

        const FlecsTerrain *t = ecs_get(
            it->world, cc[i].terrain, FlecsTerrain);
        if (!t) {
            continue;
        }

        float lx = p[i].x, ly = 0.0f, lz = p[i].z;
        const FlecsPosition3 *tp = ecs_get(
            it->world, cc[i].terrain, FlecsPosition3);
        if (tp) {
            lx -= tp->x;
            ly = tp->y;
            lz -= tp->z;
        }

        float min_y = ly + flecsEngine_terrainSampleHeight(t, lx, lz) +
            cc[i].clearance;
        if (p[i].y < min_y) {
            p[i].y = min_y;
            if (v[i].y < 0) {
                v[i].y = 0;
            }
        }
    }
}

static
void CameraControllerSyncRotation(
    ecs_iter_t *it)
{
    const FlecsPosition3 *p = ecs_field(it, FlecsPosition3, 0);
    const FlecsRotation3 *r = ecs_field(it, FlecsRotation3, 1);
    FlecsLookAt *lookat = ecs_field(it, FlecsLookAt, 2);

    for (int32_t i = 0; i < it->count; i ++) {
        lookat[i].x = p[i].x + sin(r[i].y) * cos(r[i].x);
        lookat[i].y = p[i].y + sin(r[i].x);
        lookat[i].z = p[i].z + cos(r[i].y) * cos(r[i].x);
    }
}

static
void CameraControllerApplyInput(
    ecs_iter_t *it)
{
    const FlecsInput *input = ecs_field(it, FlecsInput, 0);
    FlecsRotation3 *r = ecs_field(it, FlecsRotation3, 1);
    FlecsVelocity3 *v = ecs_field(it, FlecsVelocity3, 2);
    FlecsAngularVelocity3 *av = ecs_field(it, FlecsAngularVelocity3, 3);

    float dt = it->delta_time;
    if (dt <= 0) {
        return;
    }

    float move_accel_alpha = 1.0f - expf(-CameraMoveAccelSmoothing * dt);
    float move_decel_alpha = 1.0f - expf(-CameraMoveDecelSmoothing * dt);
    float angular_accel_alpha = 1.0f - expf(-CameraAngularAccelSmoothing * dt);
    float angular_decel_alpha = 1.0f - expf(-CameraAngularDecelSmoothing * dt);

    for (int32_t i = 0; i < it->count; i ++) {
        float yaw = r[i].y;

        vec3 forward = { sinf(yaw), 0.0f, cosf(yaw) };
        vec3 right = { cosf(yaw), 0.0f, -sinf(yaw) };

        vec3 target_v = {0, 0, 0};
        if (input->keys[FLECS_KEY_W].state) glm_vec3_add(target_v, forward, target_v);
        if (input->keys[FLECS_KEY_S].state) glm_vec3_sub(target_v, forward, target_v);
        if (input->keys[FLECS_KEY_A].state) glm_vec3_add(target_v, right, target_v);
        if (input->keys[FLECS_KEY_D].state) glm_vec3_sub(target_v, right, target_v);
        if (input->keys[FLECS_KEY_E].state) target_v[1] += 1.0f;
        if (input->keys[FLECS_KEY_Q].state) target_v[1] -= 1.0f;

        bool sprint = input->keys[FLECS_KEY_LEFT_SHIFT].state
            || input->keys[FLECS_KEY_RIGHT_SHIFT].state;
        float max_speed = sprint
            ? CameraMaxSpeed * CameraSprintMultiplier
            : CameraMaxSpeed;

        float tlen = glm_vec3_norm(target_v);
        if (tlen > 0) {
            glm_vec3_scale(target_v, max_speed / tlen, target_v);
        }

        float m_alpha = (tlen > 0) ? move_accel_alpha : move_decel_alpha;
        v[i].x += (target_v[0] - v[i].x) * m_alpha;
        v[i].y += (target_v[1] - v[i].y) * m_alpha;
        v[i].z += (target_v[2] - v[i].z) * m_alpha;

        float target_av_x = 0.0f, target_av_y = 0.0f;
        if (input->keys[FLECS_KEY_RIGHT].state) target_av_y -= 1.0f;
        if (input->keys[FLECS_KEY_LEFT].state)  target_av_y += 1.0f;
        if (input->keys[FLECS_KEY_UP].state)    target_av_x += 1.0f;
        if (input->keys[FLECS_KEY_DOWN].state)  target_av_x -= 1.0f;

        float yaw_scale = sprint ? 0.33f : 1.0f;

        float alen = sqrtf(target_av_x * target_av_x + target_av_y * target_av_y);
        if (alen > 0) {
            target_av_x *= CameraAngularMaxSpeed / alen;
            target_av_y *= CameraAngularMaxSpeed * yaw_scale / alen;
        }

        float a_alpha = (alen > 0) ? angular_accel_alpha : angular_decel_alpha;
        av[i].x += (target_av_x - av[i].x) * a_alpha;
        av[i].y += (target_av_y - av[i].y) * a_alpha;

        if (input->mouse.right.state) {
            av[i].y = -input->mouse.rel.x * CameraMouseSensitivity * yaw_scale / dt;
            av[i].x = -input->mouse.rel.y * CameraMouseSensitivity / dt;
        }

        float scroll_zoom = input->mouse.scroll.y;
        if (scroll_zoom != 0.0f) {
            vec3 look = {
                sinf(r[i].y) * cosf(r[i].x),
                sinf(r[i].x),
                cosf(r[i].y) * cosf(r[i].x)
            };
            float impulse = scroll_zoom * CameraScrollZoomSpeed;
            v[i].x += look[0] * impulse;
            v[i].y += look[1] * impulse;
            v[i].z += look[2] * impulse;
        }

        if (r[i].x > GLM_PI / 2.0 - 0.05) {
            r[i].x = GLM_PI / 2.0 - 0.05;
            if (av[i].x > 0) av[i].x = 0;
        }
        if (r[i].x < -(GLM_PI / 2.0 - 0.05)) {
            r[i].x = -(GLM_PI / 2.0 - 0.05);
            if (av[i].x < 0) av[i].x = 0;
        }
    }
}

void FlecsEngineCameraControllerImport(
    ecs_world_t *world)
{
    ECS_SYSTEM(world, CameraControllerClampTerrain, EcsPostUpdate,
        [in]     CameraController,
        [inout]  flecs.engine.transform3.Position3,
        [inout]  flecs.engine.movement.Velocity3);

    ECS_SYSTEM(world, CameraControllerSyncRotation, EcsPostUpdate,
        [in]     flecs.engine.transform3.Position3,
        [in]     flecs.engine.transform3.Rotation3,
        [out]    flecs.engine.transform3.LookAt,
        [none]   CameraController);

    ECS_SYSTEM(world, CameraControllerApplyInput, EcsPostUpdate,
        [in]     flecs.engine.input.Input,
        [inout]  flecs.engine.transform3.Rotation3,
        [inout]  flecs.engine.movement.Velocity3,
        [inout]  flecs.engine.movement.AngularVelocity3,
        [none]   CameraController);

    ecs_add_pair(world, ecs_id(FlecsCameraController), EcsWith, ecs_id(FlecsPosition3));
    ecs_add_pair(world, ecs_id(FlecsCameraController), EcsWith, ecs_id(FlecsRotation3));
    ecs_add_pair(world, ecs_id(FlecsCameraController), EcsWith, ecs_id(FlecsVelocity3));
    ecs_add_pair(world, ecs_id(FlecsCameraController), EcsWith, ecs_id(FlecsAngularVelocity3));
}
