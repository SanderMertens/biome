#ifndef FLECS_ENGINE_SHADER_H
#define FLECS_ENGINE_SHADER_H

void flecsEngine_shader_register(
    ecs_world_t *world);

ecs_entity_t flecsEngine_shader_ensure(
    ecs_world_t *world,
    const char *name,
    const FlecsShader *shader);

const FlecsShaderImpl* flecsEngine_shader_ensureImpl(
    ecs_world_t *world,
    ecs_entity_t shader_entity);

WGPUShaderModule flecsEngine_shader_ensureModule(
    ecs_world_t *world,
    const char *name,
    const char *source);

#endif
