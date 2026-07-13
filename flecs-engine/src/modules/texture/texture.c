#include "texture.h"

ECS_COMPONENT_DECLARE(FlecsTexture);
ECS_COMPONENT_DECLARE(FlecsTextureInfo);

static void flecs_texture_info_fini(flecs_texture_info_t *ti)
{
    ecs_os_free((char*)ti->format);
    ti->format = NULL;
}

static void flecs_texture_info_copy(
    flecs_texture_info_t *dst,
    const flecs_texture_info_t *src)
{
    dst->width = src->width;
    dst->height = src->height;
    dst->mip_count = src->mip_count;
    dst->format = src->format ? ecs_os_strdup(src->format) : NULL;
}

ECS_CTOR(FlecsTextureInfo, ptr, {
    ecs_os_zeromem(ptr);
})

ECS_DTOR(FlecsTextureInfo, ptr, {
    flecs_texture_info_fini(&ptr->source);
    flecs_texture_info_fini(&ptr->actual);
})

ECS_MOVE(FlecsTextureInfo, dst, src, {
    flecs_texture_info_fini(&dst->source);
    flecs_texture_info_fini(&dst->actual);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_COPY(FlecsTextureInfo, dst, src, {
    flecs_texture_info_fini(&dst->source);
    flecs_texture_info_fini(&dst->actual);
    flecs_texture_info_copy(&dst->source, &src->source);
    flecs_texture_info_copy(&dst->actual, &src->actual);
})

static void FlecsTexture_fini(
    FlecsTexture *ptr)
{
    ecs_os_free((char*)ptr->path);
}

ECS_CTOR(FlecsTexture, ptr, {
    ecs_os_zeromem(ptr);
})

ECS_DTOR(FlecsTexture, ptr, {
    FlecsTexture_fini(ptr);
})

ECS_MOVE(FlecsTexture, dst, src, {
    FlecsTexture_fini(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_COPY(FlecsTexture, dst, src, {
    FlecsTexture_fini(dst);
    dst->path = src->path ? ecs_os_strdup(src->path) : NULL;
})

void FlecsEngineTextureImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineTexture);

    ECS_COMPONENT_DEFINE(world, FlecsTexture);

    ecs_set_hooks(world, FlecsTexture, {
        .ctor = ecs_ctor(FlecsTexture),
        .dtor = ecs_dtor(FlecsTexture),
        .move = ecs_move(FlecsTexture),
        .copy = ecs_copy(FlecsTexture)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsTexture),
        .members = {
            { .name = "path", .type = ecs_id(ecs_string_t) }
        }
    });

    ECS_COMPONENT_DEFINE(world, FlecsTextureInfo);

    ecs_set_hooks(world, FlecsTextureInfo, {
        .ctor = ecs_ctor(FlecsTextureInfo),
        .dtor = ecs_dtor(FlecsTextureInfo),
        .move = ecs_move(FlecsTextureInfo),
        .copy = ecs_copy(FlecsTextureInfo)
    });

    ecs_entity_t ti_type = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "flecs_texture_info_t" }),
        .members = {
            { .name = "width", .type = ecs_id(ecs_u32_t) },
            { .name = "height", .type = ecs_id(ecs_u32_t) },
            { .name = "mip_count", .type = ecs_id(ecs_u32_t) },
            { .name = "format", .type = ecs_id(ecs_string_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsTextureInfo),
        .members = {
            { .name = "source", .type = ti_type },
            { .name = "actual", .type = ti_type }
        }
    });
}
