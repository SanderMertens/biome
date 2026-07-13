#ifndef FLECS_ENGINE_MATERIAL_H
#define FLECS_ENGINE_MATERIAL_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_MATERIAL_IMPL
#define ECS_META_IMPL EXTERN
#endif

typedef flecs_rgba_t FlecsRgba;

extern ECS_COMPONENT_DECLARE(FlecsRgba);

typedef flecs_rgba_t FlecsTint;

extern ECS_COMPONENT_DECLARE(FlecsTint);

typedef flecs_rgba_t FlecsActualTint;

extern ECS_COMPONENT_DECLARE(FlecsActualTint);

ECS_STRUCT(FlecsPbrMaterial, {
    float metallic;
    float roughness;
});

extern ECS_COMPONENT_DECLARE(FlecsPbrMaterial);

ECS_STRUCT(FlecsEmissive, {
    float strength;
    flecs_rgba_t color;
});

typedef FlecsEmissive EcsEmissive;

extern ECS_COMPONENT_DECLARE(FlecsEmissive);

ECS_STRUCT(FlecsMaterialId, {
    uint32_t value;
});

extern ECS_COMPONENT_DECLARE(FlecsMaterialId);

ECS_STRUCT(FlecsTexture, {
    const char *path;
});

extern ECS_COMPONENT_DECLARE(FlecsTexture);

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t mip_count;
    const char *format;
} flecs_texture_info_t;

ECS_STRUCT(FlecsTextureInfo, {
    flecs_texture_info_t source;
    flecs_texture_info_t actual;
});

extern ECS_COMPONENT_DECLARE(FlecsTextureInfo);

ECS_STRUCT(FlecsPbrTextures, {
    ecs_entity_t albedo;
    ecs_entity_t emissive;
    ecs_entity_t roughness;
    ecs_entity_t normal;
});

extern ECS_COMPONENT_DECLARE(FlecsPbrTextures);

extern ECS_TAG_DECLARE(FlecsAlphaBlend);

/* Routes an entity to the vertex-color mesh batch: its (owned or inherited)
 * FlecsMesh3 must have per-vertex colors. rgb multiplies albedo, alpha
 * multiplies material roughness. */
extern ECS_TAG_DECLARE(FlecsVertexColors);

ECS_STRUCT(FlecsTransmission, {
    float transmission_factor;
    float ior;
    float thickness_factor;
    float attenuation_distance;
    flecs_rgba_t attenuation_color;
});

extern ECS_COMPONENT_DECLARE(FlecsTransmission);

ECS_STRUCT(FlecsTextureTransform, {
    float scale_x;
    float scale_y;
    float offset_x;
    float offset_y;
});

extern ECS_COMPONENT_DECLARE(FlecsTextureTransform);

#endif
