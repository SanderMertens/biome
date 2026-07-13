#include "renderer.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsShader);
ECS_COMPONENT_DECLARE(FlecsShaderImpl);

static void flecsShaderImplRelease(
    FlecsShaderImpl *ptr)
{
    FLECS_WGPU_RELEASE(ptr->shader_module, wgpuShaderModuleRelease);
}

ECS_DTOR(FlecsShaderImpl, ptr, {
    flecsShaderImplRelease(ptr);
})

ECS_MOVE(FlecsShaderImpl, dst, src, {
    flecsShaderImplRelease(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

static bool flecsEngine_shader_compile(
    ecs_world_t *world,
    ecs_entity_t shader_entity,
    const FlecsShader *shader,
    FlecsShaderImpl *shader_impl)
{
    const FlecsEngineImpl *engine = ecs_singleton_ensure(world, FlecsEngineImpl);
    if (!engine || !engine->device) {
        char *name = ecs_get_path(world, shader_entity);
        ecs_err("cannot compile shader '%s': engine is not initialized", name);
        ecs_os_free(name);
        return false;
    }

    if (!shader || !shader->source) {
        char *name = ecs_get_path(world, shader_entity);
        ecs_err("shader asset '%s' has no source", name);
        ecs_os_free(name);
        flecsShaderImplRelease(shader_impl);
        return false;
    }

    flecsShaderImplRelease(shader_impl);

    shader_impl->shader_module = flecsEngine_createShaderModule(
        engine->device, shader->source);

    if (!shader_impl->shader_module) {
        char *name = ecs_get_path(world, shader_entity);
        ecs_err("failed to compile shader asset '%s'", name);
        ecs_os_free(name);
        return false;
    }

    /* Detect shader features from source once at compile time */
    shader_impl->uses_ibl = flecsEngine_shader_usesIbl(shader);
    shader_impl->uses_shadow = flecsEngine_shader_usesShadow(shader);
    shader_impl->uses_cluster = flecsEngine_shader_usesCluster(shader);
    shader_impl->uses_textures = flecsEngine_shader_usesTextures(shader);
    shader_impl->uses_material_buffer =
        flecsEngine_shader_usesMaterialBuffer(shader);
    shader_impl->uses_instance_buffer =
        flecsEngine_shader_usesInstanceBuffer(shader);

    return true;
}

const FlecsShaderImpl* flecsEngine_shader_ensureImpl(
    ecs_world_t *world,
    ecs_entity_t shader_entity)
{
    const FlecsShader *shader = ecs_get(world, shader_entity, FlecsShader);
    if (!shader) {
        char *name = ecs_get_path(world, shader_entity);
        ecs_err("entity '%s' does not have FlecsShader", name);
        ecs_os_free(name);
        return NULL;
    }

    FlecsShaderImpl *shader_impl = ecs_ensure(world, shader_entity, FlecsShaderImpl);
    if (!shader_impl->shader_module) {
        if (!flecsEngine_shader_compile(world, shader_entity, shader, shader_impl)) {
            return NULL;
        }
    }

    return shader_impl;
}

static bool flecsEngine_shader_strDiffer(
    const char *a,
    const char *b)
{
    if (a == b) return false;
    if (!a || !b) return true;
    return ecs_os_strcmp(a, b) != 0;
}

ecs_entity_t flecsEngine_shader_ensure(
    ecs_world_t *world,
    const char *name,
    const FlecsShader *shader)
{
    ecs_entity_t renderer_module = ecs_lookup(world, "flecs.engine.renderer");
    ecs_entity_t shader_entity = ecs_entity_init(world, &(ecs_entity_desc_t){
        .name = name,
        .parent = renderer_module
    });

    const FlecsShader *existing = ecs_get(world, shader_entity, FlecsShader);
    if (!existing ||
        flecsEngine_shader_strDiffer(existing->source, shader->source) ||
        flecsEngine_shader_strDiffer(existing->vertex_entry, shader->vertex_entry) ||
        flecsEngine_shader_strDiffer(existing->fragment_entry, shader->fragment_entry))
    {
        ecs_set_ptr(world, shader_entity, FlecsShader, shader);
    }

    return shader_entity;
}

WGPUShaderModule flecsEngine_shader_ensureModule(
    ecs_world_t *world,
    const char *name,
    const char *source)
{
    bool was_deferred = ecs_is_deferred(world);
    if (was_deferred) {
        ecs_defer_suspend(world);
    }

    ecs_entity_t shader_entity = flecsEngine_shader_ensure(world, name,
        &(FlecsShader){ .source = source });

    WGPUShaderModule module = NULL;
    const FlecsShaderImpl *shader_impl = flecsEngine_shader_ensureImpl(
        world, shader_entity);
    if (shader_impl) {
        module = shader_impl->shader_module;
    }

    if (was_deferred) {
        ecs_defer_resume(world);
    }

    return module;
}

static void FlecsShader_on_set(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t shader_entity = it->entities[i];
        if (!ecs_has(world, shader_entity, FlecsShaderImpl)) {
            continue;
        }
        FlecsShaderImpl *shader_impl = ecs_ensure(
            world, shader_entity, FlecsShaderImpl);
        flecsShaderImplRelease(shader_impl);
        ecs_os_zeromem(shader_impl);
    }
}

void flecsEngine_shader_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsShader);
    ECS_COMPONENT_DEFINE(world, FlecsShaderImpl);

    ecs_set_hooks(world, FlecsShader, {
        .ctor = flecs_default_ctor,
        .on_set = FlecsShader_on_set
    });

    ecs_set_hooks(world, FlecsShaderImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsShaderImpl),
        .dtor = ecs_dtor(FlecsShaderImpl)
    });
}