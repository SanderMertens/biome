#ifndef FLECS_ENGINE_RENDERER_SHADERS_H
#define FLECS_ENGINE_RENDERER_SHADERS_H

#include "../../../types.h"

bool flecsEngine_shader_usesIbl(
    const FlecsShader *shader);

bool flecsEngine_shader_usesShadow(
    const FlecsShader *shader);

bool flecsEngine_shader_usesCluster(
    const FlecsShader *shader);

ecs_entity_t flecsEngine_shader_pbr(
    ecs_world_t *world);

bool flecsEngine_shader_usesMaterialBuffer(
    const FlecsShader *shader);

bool flecsEngine_shader_usesInstanceBuffer(
    const FlecsShader *shader);

ecs_entity_t flecsEngine_shader_skybox(
    ecs_world_t *world);

bool flecsEngine_shader_usesTextures(
    const FlecsShader *shader);

ecs_entity_t flecsEngine_shader_pbrTransmission(
    ecs_world_t *world);

ecs_entity_t flecsEngine_shader_pbrVertexColor(
    ecs_world_t *world);

#endif
