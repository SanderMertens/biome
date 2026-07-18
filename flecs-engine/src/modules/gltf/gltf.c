#include "gltf.h"

#include <cgltf.h>
#include <stb_image.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

ECS_COMPONENT_DECLARE(FlecsGltf);

static char* flecsEngine_gltf_resolvePath(
    const char *gltf_path,
    const char *uri)
{
    if (!uri || !gltf_path) {
        return NULL;
    }

    /* Find directory of the gltf file */
    const char *last_slash = strrchr(gltf_path, '/');
    if (!last_slash) {
        last_slash = strrchr(gltf_path, '\\');
    }

    if (last_slash) {
        size_t dir_len = (size_t)(last_slash - gltf_path + 1);
        size_t uri_len = strlen(uri);
        char *result = ecs_os_malloc((ecs_size_t)(dir_len + uri_len + 1));
        memcpy(result, gltf_path, dir_len);
        memcpy(result + dir_len, uri, uri_len);
        result[dir_len + uri_len] = '\0';
        return result;
    }

    return ecs_os_strdup(uri);
}

static int32_t flecsEngine_gltf_readAccessor_f32(
    const cgltf_accessor *accessor,
    float *out,
    int32_t component_count)
{
    if (!accessor) {
        return 0;
    }

    int32_t count = (int32_t)accessor->count;
    for (int32_t i = 0; i < count; i++) {
        cgltf_accessor_read_float(accessor, (cgltf_size)i,
            &out[i * component_count], (cgltf_size)component_count);
    }

    return count;
}

static int32_t flecsEngine_gltf_readIndices(
    const cgltf_accessor *accessor,
    uint32_t *out)
{
    if (!accessor) {
        return 0;
    }

    int32_t count = (int32_t)accessor->count;
    for (int32_t i = 0; i < count; i++) {
        out[i] = (uint32_t)cgltf_accessor_read_index(accessor, (cgltf_size)i);
    }

    return count;
}

static const cgltf_accessor* flecsEngine_gltf_findAttribute(
    const cgltf_primitive *prim,
    cgltf_attribute_type type)
{
    for (cgltf_size i = 0; i < prim->attributes_count; i++) {
        if (prim->attributes[i].type == type) {
            return prim->attributes[i].data;
        }
    }
    return NULL;
}

static bool flecsEngine_gltf_readMesh(
    FlecsMesh3 *mesh3,
    const cgltf_primitive *prim)
{
    const cgltf_accessor *pos_acc = flecsEngine_gltf_findAttribute(
        prim, cgltf_attribute_type_position);
    const cgltf_accessor *nrm_acc = flecsEngine_gltf_findAttribute(
        prim, cgltf_attribute_type_normal);
    const cgltf_accessor *uv_acc = flecsEngine_gltf_findAttribute(
        prim, cgltf_attribute_type_texcoord);
    const cgltf_accessor *tan_acc = flecsEngine_gltf_findAttribute(
        prim, cgltf_attribute_type_tangent);
    const cgltf_accessor *idx_acc = prim->indices;

    if (!pos_acc || !idx_acc) {
        return false;
    }

    int32_t vert_count = (int32_t)pos_acc->count;
    int32_t idx_count = (int32_t)idx_acc->count;

    ecs_vec_init_t(NULL, &mesh3->vertices, flecs_vec3_t, vert_count);
    ecs_vec_init_t(NULL, &mesh3->normals, flecs_vec3_t, vert_count);
    ecs_vec_init_t(NULL, &mesh3->uvs, flecs_vec2_t, vert_count);
    ecs_vec_init_t(NULL, &mesh3->tangents, flecs_vec4_t, vert_count);
    ecs_vec_init_t(NULL, &mesh3->indices, uint32_t, idx_count);
    ecs_vec_set_count_t(NULL, &mesh3->vertices, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh3->normals, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh3->uvs, flecs_vec2_t, vert_count);
    ecs_vec_set_count_t(NULL, &mesh3->indices, uint32_t, idx_count);

    flecsEngine_gltf_readAccessor_f32(
        pos_acc,
        (float*)ecs_vec_first_t(&mesh3->vertices, flecs_vec3_t), 3);

    if (nrm_acc) {
        flecsEngine_gltf_readAccessor_f32(
            nrm_acc,
            (float*)ecs_vec_first_t(&mesh3->normals, flecs_vec3_t), 3);
    } else {
        flecs_vec3_t *normals = ecs_vec_first_t(&mesh3->normals, flecs_vec3_t);
        for (int32_t i = 0; i < vert_count; i++) {
            normals[i] = (flecs_vec3_t){0, 1, 0};
        }
    }

    if (uv_acc) {
        flecsEngine_gltf_readAccessor_f32(
            uv_acc,
            (float*)ecs_vec_first_t(&mesh3->uvs, flecs_vec2_t), 2);
    } else {
        ecs_vec_set_count_t(NULL, &mesh3->uvs, flecs_vec2_t, 0);
    }

    if (tan_acc && (int32_t)tan_acc->count == vert_count) {
        ecs_vec_set_count_t(NULL, &mesh3->tangents, flecs_vec4_t, vert_count);
        flecsEngine_gltf_readAccessor_f32(
            tan_acc,
            (float*)ecs_vec_first_t(&mesh3->tangents, flecs_vec4_t), 4);

        const flecs_vec4_t *tans =
            ecs_vec_first_t(&mesh3->tangents, flecs_vec4_t);
        const flecs_vec3_t *nrms =
            ecs_vec_first_t(&mesh3->normals, flecs_vec3_t);
        bool tangents_ok = true;
        for (int32_t v = 0; v < vert_count; v ++) {
            float tx = tans[v].x, ty = tans[v].y, tz = tans[v].z;
            float t_len2 = tx*tx + ty*ty + tz*tz;
            if (t_len2 < 1e-12f) {
                tangents_ok = false;
                break;
            }
            float nx = nrms[v].x, ny = nrms[v].y, nz = nrms[v].z;
            float dotnt = nx*tx + ny*ty + nz*tz;
            /* cos²(angle) > 0.99 means angle < ~5.7°: tangent is parallel
             * enough to N that the shader Gram-Schmidt will collapse. */
            if ((dotnt * dotnt) > 0.99f * t_len2) {
                tangents_ok = false;
                break;
            }
        }

        if (!tangents_ok) {
            ecs_vec_set_count_t(NULL, &mesh3->tangents, flecs_vec4_t, 0);
        }
    } else {
        ecs_vec_set_count_t(NULL, &mesh3->tangents, flecs_vec4_t, 0);
    }

    flecsEngine_gltf_readIndices(
        idx_acc, ecs_vec_first_t(&mesh3->indices, uint32_t));

    if (uv_acc) {
        flecs_vec2_t *uvs = ecs_vec_first_t(&mesh3->uvs, flecs_vec2_t);
        uint32_t *indices = ecs_vec_first_t(&mesh3->indices, uint32_t);
        const float eps = 1.0f / 4096.0f;
        bool *perturbed = ecs_os_calloc_n(bool, vert_count);

        for (int32_t t = 0; t + 2 < idx_count; t += 3) {
            uint32_t i0 = indices[t], i1 = indices[t + 1], i2 = indices[t + 2];
            float du1 = uvs[i1].x - uvs[i0].x;
            float dv1 = uvs[i1].y - uvs[i0].y;
            float du2 = uvs[i2].x - uvs[i0].x;
            float dv2 = uvs[i2].y - uvs[i0].y;
            float cross = du1 * dv2 - dv1 * du2;
            if (fabsf(cross) < 1e-6f) {
                if (!perturbed[i1]) {
                    uvs[i1].x += eps;
                    perturbed[i1] = true;
                }
                if (!perturbed[i2]) {
                    uvs[i2].y += eps;
                    perturbed[i2] = true;
                }
            }
        }

        ecs_os_free(perturbed);
    }

    return true;
}

static void flecsEngine_gltf_decomposeTransform(
    const float m[16],
    FlecsPosition3 *pos,
    FlecsRotation3 *rot,
    FlecsScale3 *scale)
{
    pos->x = m[12]; pos->y = m[13]; pos->z = m[14];

    scale->x = sqrtf(m[0]*m[0] + m[1]*m[1] + m[2]*m[2]);
    scale->y = sqrtf(m[4]*m[4] + m[5]*m[5] + m[6]*m[6]);
    scale->z = sqrtf(m[8]*m[8] + m[9]*m[9] + m[10]*m[10]);

    /* Detect reflection (negative determinant) */
    float det = m[0]*(m[5]*m[10] - m[6]*m[9])
              - m[4]*(m[1]*m[10] - m[2]*m[9])
              + m[8]*(m[1]*m[6] - m[2]*m[5]);
    if (det < 0) {
        scale->x = -scale->x;
    }

    float sx = scale->x, sy = scale->y, sz = scale->z;
    if (fabsf(sx) < 1e-6f || fabsf(sy) < 1e-6f || fabsf(sz) < 1e-6f) {
        rot->x = rot->y = rot->z = 0;
        return;
    }

    /* Extract euler angles (XYZ order) from normalized rotation matrix */
    float r02 = m[8] / sz;
    rot->y = asinf(fmaxf(-1.0f, fminf(1.0f, r02)));
    if (fabsf(r02) < 0.9999f) {
        rot->x = atan2f(-m[9] / sz, m[10] / sz);
        rot->z = atan2f(-m[4] / sy, m[0] / sx);
    } else {
        rot->x = atan2f(m[6] / sy, m[5] / sy);
        rot->z = 0;
    }
}

static ecs_entity_t flecsEngine_gltf_getImageEntity(
    ecs_world_t *world,
    ecs_entity_t *image_entities,
    const cgltf_data *data,
    const cgltf_texture_view *tv,
    const char *gltf_path)
{
    if (!tv || !tv->texture || !tv->texture->image || !tv->texture->image->uri) {
        return 0;
    }

    ptrdiff_t idx = tv->texture->image - data->images;
    if (image_entities[idx]) {
        return image_entities[idx];
    }

    char *path = flecsEngine_gltf_resolvePath(
        gltf_path, tv->texture->image->uri);
    if (!path) {
        return 0;
    }

    ecs_entity_t assets = ecs_lookup(world, "assets");
    if (!assets) {
        assets = ecs_entity(world, { .name = "assets" });
        ecs_add_id(world, assets, EcsModule);
    }

    ecs_entity_t e = ecs_entity(world, {
        .parent = assets,
        .name = path,
        .sep = "/"
    });

    ecs_add_id(world, e, EcsPrefab);
    ecs_set(world, e, FlecsTexture, { .path = path });
    ecs_os_free(path);

    image_entities[idx] = e;
    return e;
}

typedef struct {
    bool has_rgba;
    FlecsRgba rgba;
    bool has_pbr;
    FlecsPbrMaterial pbr;
    bool has_emissive;
    FlecsEmissive emissive;
    bool has_textures;
    FlecsPbrTextures textures;
    bool alpha_blend;
    bool has_transmission;
    FlecsTransmission transmission;
    bool has_texture_transform;
    FlecsTextureTransform texture_transform;
} flecsEngine_gltf_materialDesc;

static void flecsEngine_gltf_computeMaterial(
    flecsEngine_gltf_materialDesc *desc,
    ecs_world_t *world,
    const cgltf_material *mat,
    const char *gltf_path,
    ecs_entity_t *image_entities,
    const cgltf_data *data)
{
    memset(desc, 0, sizeof(*desc));

    float em_r = mat->emissive_factor[0];
    float em_g = mat->emissive_factor[1];
    float em_b = mat->emissive_factor[2];
    float em_max = fmaxf(em_r, fmaxf(em_g, em_b));
    if (em_max > 0.0f) {
        desc->has_emissive = true;
        desc->emissive.strength = em_max;
        desc->emissive.color = (flecs_rgba_t){
            .r = (uint8_t)(em_r / em_max * 255.0f),
            .g = (uint8_t)(em_g / em_max * 255.0f),
            .b = (uint8_t)(em_b / em_max * 255.0f),
            .a = 255
        };
    }

    const cgltf_texture_view *albedo_tv = NULL;
    const cgltf_texture_view *roughness_tv = NULL;

    if (mat->has_pbr_specular_glossiness) {
        const cgltf_pbr_specular_glossiness *sg =
            &mat->pbr_specular_glossiness;

        desc->has_rgba = true;
        desc->rgba = (FlecsRgba){
            (uint8_t)(sg->diffuse_factor[0] * 255.0f),
            (uint8_t)(sg->diffuse_factor[1] * 255.0f),
            (uint8_t)(sg->diffuse_factor[2] * 255.0f),
            (uint8_t)(sg->diffuse_factor[3] * 255.0f)
        };

        float max_spec = fmaxf(fmaxf(
            sg->specular_factor[0], sg->specular_factor[1]),
            sg->specular_factor[2]);
        desc->has_pbr = true;
        desc->pbr = (FlecsPbrMaterial){
            .metallic = 0.0f,
            .roughness = 1.0f - sg->glossiness_factor * sqrtf(max_spec)
        };

        albedo_tv = &sg->diffuse_texture;
    } else if (mat->has_pbr_metallic_roughness) {
        const cgltf_pbr_metallic_roughness *pbr =
            &mat->pbr_metallic_roughness;

        desc->has_rgba = true;
        desc->rgba = (FlecsRgba){
            (uint8_t)(pbr->base_color_factor[0] * 255.0f),
            (uint8_t)(pbr->base_color_factor[1] * 255.0f),
            (uint8_t)(pbr->base_color_factor[2] * 255.0f),
            (uint8_t)(pbr->base_color_factor[3] * 255.0f)
        };

        desc->has_pbr = true;
        desc->pbr = (FlecsPbrMaterial){
            .metallic = pbr->metallic_factor,
            .roughness = pbr->roughness_factor
        };

        albedo_tv = &pbr->base_color_texture;
        roughness_tv = &pbr->metallic_roughness_texture;
    }

    if (albedo_tv && albedo_tv->has_transform) {
        desc->has_texture_transform = true;
        desc->texture_transform = (FlecsTextureTransform){
            .scale_x = albedo_tv->transform.scale[0],
            .scale_y = albedo_tv->transform.scale[1],
            .offset_x = albedo_tv->transform.offset[0],
            .offset_y = albedo_tv->transform.offset[1]
        };
    }

    desc->textures.albedo = flecsEngine_gltf_getImageEntity(
        world, image_entities, data, albedo_tv, gltf_path);
    desc->textures.emissive = flecsEngine_gltf_getImageEntity(
        world, image_entities, data, &mat->emissive_texture, gltf_path);
    desc->textures.roughness = flecsEngine_gltf_getImageEntity(
        world, image_entities, data, roughness_tv, gltf_path);
    desc->textures.normal = flecsEngine_gltf_getImageEntity(
        world, image_entities, data, &mat->normal_texture, gltf_path);

    desc->has_textures = desc->textures.albedo || desc->textures.emissive ||
                         desc->textures.roughness || desc->textures.normal;

    if (mat->alpha_mode == cgltf_alpha_mode_blend) {
        desc->alpha_blend = true;
    }

    /* KHR_materials_transmission */
    if (mat->has_transmission &&
        mat->transmission.transmission_factor > 0.0f)
    {
        desc->has_transmission = true;
        desc->transmission.transmission_factor =
            mat->transmission.transmission_factor;
        desc->transmission.ior = mat->has_ior ? mat->ior.ior : 1.5f;
        if (mat->has_volume) {
            desc->transmission.thickness_factor =
                mat->volume.thickness_factor;
            desc->transmission.attenuation_distance =
                mat->volume.attenuation_distance;
            desc->transmission.attenuation_color = (flecs_rgba_t){
                (uint8_t)(mat->volume.attenuation_color[0] * 255.0f),
                (uint8_t)(mat->volume.attenuation_color[1] * 255.0f),
                (uint8_t)(mat->volume.attenuation_color[2] * 255.0f),
                255
            };
        } else {
            desc->transmission.thickness_factor = 0.0f;
            desc->transmission.attenuation_distance = 1e10f;
            desc->transmission.attenuation_color =
                (flecs_rgba_t){255, 255, 255, 255};
        }
    }
}

static bool flecsEngine_gltf_matchMaterial(
    ecs_world_t *world,
    ecs_entity_t entity,
    const flecsEngine_gltf_materialDesc *desc)
{
    const FlecsRgba *rgba = ecs_get(world, entity, FlecsRgba);
    if (desc->has_rgba) {
        if (!rgba) return false;
        if (rgba->r != desc->rgba.r || rgba->g != desc->rgba.g ||
            rgba->b != desc->rgba.b || rgba->a != desc->rgba.a) return false;
    } else {
        if (rgba) return false;
    }

    const FlecsPbrMaterial *pbr = ecs_get(world, entity, FlecsPbrMaterial);
    if (desc->has_pbr) {
        if (!pbr) return false;
        if (pbr->metallic != desc->pbr.metallic ||
            pbr->roughness != desc->pbr.roughness) return false;
    } else {
        if (pbr) return false;
    }

    const FlecsEmissive *em = ecs_get(world, entity, FlecsEmissive);
    if (desc->has_emissive) {
        if (!em) return false;
        if (em->strength != desc->emissive.strength) return false;
        if (em->color.r != desc->emissive.color.r ||
            em->color.g != desc->emissive.color.g ||
            em->color.b != desc->emissive.color.b ||
            em->color.a != desc->emissive.color.a) return false;
    } else {
        if (em) return false;
    }

    const FlecsPbrTextures *tex = ecs_get(world, entity, FlecsPbrTextures);
    if (desc->has_textures) {
        if (!tex) return false;
        if (tex->albedo != desc->textures.albedo ||
            tex->emissive != desc->textures.emissive ||
            tex->roughness != desc->textures.roughness ||
            tex->normal != desc->textures.normal) return false;
    } else {
        if (tex) return false;
    }

    const FlecsTextureTransform *tt =
        ecs_get(world, entity, FlecsTextureTransform);
    if (desc->has_texture_transform) {
        if (!tt) return false;
        if (tt->scale_x != desc->texture_transform.scale_x ||
            tt->scale_y != desc->texture_transform.scale_y ||
            tt->offset_x != desc->texture_transform.offset_x ||
            tt->offset_y != desc->texture_transform.offset_y) return false;
    } else {
        if (tt) return false;
    }

    bool has_alpha = ecs_has(world, entity, FlecsAlphaBlend);
    if (desc->alpha_blend != has_alpha) return false;

    return true;
}

static ecs_entity_t flecsEngine_gltf_findMaterial(
    ecs_world_t *world,
    ecs_entity_t materials_parent,
    const flecsEngine_gltf_materialDesc *desc)
{
    ecs_entity_t result = 0;
    ecs_iter_t it = ecs_children(world, materials_parent);
    while (ecs_children_next(&it)) {
        for (int i = 0; i < it.count; i++) {
            if (flecsEngine_gltf_matchMaterial(world, it.entities[i], desc)) {
                result = it.entities[i];
                ecs_iter_fini(&it);
                return result;
            }
        }
    }
    return 0;
}

static void flecsEngine_gltf_applyMaterial(
    ecs_world_t *world,
    ecs_entity_t entity,
    const flecsEngine_gltf_materialDesc *desc)
{
    if (desc->has_emissive) {
        ecs_set_ptr(world, entity, FlecsEmissive, &desc->emissive);
    }

    if (desc->has_rgba) {
        ecs_set_ptr(world, entity, FlecsRgba, &desc->rgba);
    }

    if (desc->has_pbr) {
        ecs_set_ptr(world, entity, FlecsPbrMaterial, &desc->pbr);
    }

    if (desc->has_textures) {
        ecs_set_ptr(world, entity, FlecsPbrTextures, &desc->textures);
    }

    if (desc->has_transmission) {
        ecs_set_ptr(world, entity, FlecsTransmission, &desc->transmission);
    }

    if (desc->has_texture_transform) {
        ecs_set_ptr(world, entity, FlecsTextureTransform,
            &desc->texture_transform);
    }

    if (desc->alpha_blend) {
        ecs_add(world, entity, FlecsAlphaBlend);
    }
}

static void flecsEngine_gltf_setParent(
    ecs_world_t *world,
    ecs_entity_t child,
    ecs_entity_t parent)
{
    if (!ecs_owns(world, child, EcsParent)) {
        ecs_set(world, child, EcsParent, {.value = parent});
    }
}

static void flecsEngine_gltf_normalizeChildren(
    ecs_world_t *world,
    ecs_entity_t parent)
{
    ecs_vec_t children;
    ecs_vec_init_t(NULL, &children, ecs_entity_t, 0);

    ecs_iter_t it = ecs_children(world, parent);
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            if (!ecs_owns(world, it.entities[i], EcsParent)) {
                *ecs_vec_append_t(NULL, &children, ecs_entity_t) =
                    it.entities[i];
            }
        }
    }

    int32_t count = ecs_vec_count(&children);
    ecs_entity_t *entities = ecs_vec_first_t(&children, ecs_entity_t);
    for (int32_t i = 0; i < count; i ++) {
        flecsEngine_gltf_setParent(world, entities[i], parent);
    }

    ecs_vec_fini_t(NULL, &children, ecs_entity_t);
}

static void FlecsGltfChildOfOnAdd(ecs_iter_t *it) {
    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t child = it->entities[i];
        ecs_entity_t parent = ecs_get_target(
            it->world, child, EcsChildOf, 0);
        if (parent &&
            ecs_owns(it->world, parent, FlecsGltf) &&
            ecs_has_id(it->world, parent, EcsPrefab))
        {
            flecsEngine_gltf_setParent(it->world, child, parent);
        }
    }
}

static ecs_entity_t flecsEngine_gltf_getMaterialEntity(
    ecs_world_t *world,
    ecs_entity_t *material_entities,
    const cgltf_data *data,
    const cgltf_material *mat,
    ecs_entity_t materials_parent,
    const char *gltf_path,
    ecs_entity_t *image_entities)
{
    ptrdiff_t mat_idx = mat - data->materials;
    if (material_entities[mat_idx]) {
        return material_entities[mat_idx];
    }

    flecsEngine_gltf_materialDesc desc;
    flecsEngine_gltf_computeMaterial(
        &desc, world, mat, gltf_path, image_entities, data);

    /* Check for existing duplicate in the materials scope */
    ecs_entity_t e = flecsEngine_gltf_findMaterial(
        world, materials_parent, &desc);

    if (!e) {
        e = ecs_entity(world, { .parent = materials_parent });
        ecs_add_id(world, e, EcsPrefab);
        if (mat->name) {
            ecs_doc_set_name(world, e, mat->name);
        }
        flecsEngine_gltf_applyMaterial(world, e, &desc);
    }

    material_entities[mat_idx] = e;
    return e;
}

static ecs_entity_t flecsEngine_gltf_getMeshEntity(
    ecs_world_t *world,
    ecs_entity_t **mesh_entities,
    const cgltf_data *data,
    const cgltf_mesh *mesh,
    cgltf_size prim_index,
    ecs_entity_t meshes_parent,
    const char *gltf_path,
    ecs_entity_t *image_entities,
    ecs_entity_t *material_entities,
    ecs_entity_t materials_parent)
{
    ptrdiff_t mesh_idx = mesh - data->meshes;
    if (mesh_entities[mesh_idx][prim_index]) {
        return mesh_entities[mesh_idx][prim_index];
    }

    const cgltf_primitive *prim = &mesh->primitives[prim_index];

    FlecsMesh3 mesh3 = {0};
    if (!flecsEngine_gltf_readMesh(&mesh3, prim)) {
        return 0;
    }

    ecs_entity_t e = ecs_entity(world, { .parent = meshes_parent });
    ecs_add_id(world, e, EcsPrefab);
    if (mesh->name) {
        ecs_doc_set_name(world, e, mesh->name);
    }
    ecs_set_ptr(world, e, FlecsMesh3, &mesh3);

    if (prim->material) {
        ecs_entity_t mat_e = flecsEngine_gltf_getMaterialEntity(
            world, material_entities, data, prim->material,
            materials_parent, gltf_path, image_entities);
        ecs_add_pair(world, e, EcsIsA, mat_e);
    }

    mesh_entities[mesh_idx][prim_index] = e;
    return e;
}

static void flecsEngine_gltf_load(
    ecs_world_t *world,
    ecs_entity_t root,
    const char *path)
{
    cgltf_options options = {0};
    cgltf_data *data = NULL;

    cgltf_result result = cgltf_parse_file(&options, path, &data);
    if (result != cgltf_result_success) {
        ecs_err("failed to parse GLTF file: %s (cgltf error %d)",
            path, (int)result);
        return;
    }

    result = cgltf_load_buffers(&options, data, path);
    if (result != cgltf_result_success) {
        ecs_err("failed to load GLTF buffers: %s (cgltf error %d)",
            path, (int)result);
        cgltf_free(data);
        return;
    }

    /* Create asset hierarchy: assets/<gltf_dir>/meshes */
    ecs_entity_t assets = ecs_lookup(world, "assets");
    if (!assets) {
        assets = ecs_entity(world, { .name = "assets" });
        ecs_add_id(world, assets, EcsModule);
    }

    const char *last_sep = strrchr(path, '/');
    if (!last_sep) {
        last_sep = strrchr(path, '\\');
    }

    char *dir_path = NULL;
    if (last_sep) {
        dir_path = ecs_os_malloc((ecs_size_t)(last_sep - path + 1));
        memcpy(dir_path, path, (size_t)(last_sep - path));
        dir_path[last_sep - path] = '\0';
    }

    ecs_entity_t gltf_e = ecs_entity(world, {
        .parent = assets,
        .name = dir_path ? dir_path : path,
        .sep = "/"
    });
    ecs_add_id(world, gltf_e, EcsPrefab);
    ecs_os_free(dir_path);

    ecs_entity_t meshes_e = ecs_entity(world, {
        .parent = gltf_e,
        .name = "meshes"
    });
    ecs_add_id(world, meshes_e, EcsPrefab);

    ecs_entity_t materials_e = ecs_entity(world, {
        .parent = gltf_e,
        .name = "materials"
    });
    ecs_add_id(world, materials_e, EcsPrefab);

    /* Pre-allocate lookup tables */
    ecs_entity_t *image_entities = NULL;
    if (data->images_count) {
        image_entities = ecs_os_calloc_n(
            ecs_entity_t, (int32_t)data->images_count);
    }

    ecs_entity_t *material_entities = NULL;
    if (data->materials_count) {
        material_entities = ecs_os_calloc_n(
            ecs_entity_t, (int32_t)data->materials_count);
    }

    ecs_entity_t **mesh_entities = NULL;
    if (data->meshes_count) {
        mesh_entities = ecs_os_calloc_n(
            ecs_entity_t*, (int32_t)data->meshes_count);
        for (cgltf_size mi = 0; mi < data->meshes_count; mi++) {
            if (data->meshes[mi].primitives_count) {
                mesh_entities[mi] = ecs_os_calloc_n(
                    ecs_entity_t,
                    (int32_t)data->meshes[mi].primitives_count);
            }
        }
    }

    int32_t prim_count = 0;
    bool use_parent_storage = ecs_has_id(world, root, EcsPrefab);

    /* Keep the root as the model's placement entity and create one child per
     * mesh primitive with the glTF node's composed transform. This prevents
     * asset transforms from overwriting transforms authored on the entity
     * that owns Gltf. Use ChildOf consistently for prefab children: mixing
     * EcsParent and ChildOf child records on one prefab can cause one of the
     * records to be skipped when the prefab hierarchy is instantiated. */

    for (cgltf_size ni = 0; ni < data->nodes_count; ni++) {
        const cgltf_node *node = &data->nodes[ni];
        if (!node->mesh) continue;

        float world_matrix[16];
        cgltf_node_transform_world(node, world_matrix);

        FlecsPosition3 pos = {0};
        FlecsRotation3 rot = {0};
        FlecsScale3 scale = {1, 1, 1};
        flecsEngine_gltf_decomposeTransform(
            world_matrix, &pos, &rot, &scale);

        const cgltf_mesh *mesh = node->mesh;
        for (cgltf_size pi = 0; pi < mesh->primitives_count; pi++) {
            if (mesh->primitives[pi].type !=
                cgltf_primitive_type_triangles)
            {
                continue;
            }

            ecs_entity_t mesh_prefab = flecsEngine_gltf_getMeshEntity(
                world, mesh_entities, data, mesh, pi,
                meshes_e, path, image_entities,
                material_entities, materials_e);
            if (!mesh_prefab) {
                ecs_warn("GLTF %s: dropped primitive %d of mesh '%s'",
                    path, (int)pi, mesh->name ? mesh->name : "?");
                continue;
            }

            ecs_entity_t prim_e;
            if (use_parent_storage) {
                prim_e = ecs_new(world);
                ecs_set(world, prim_e, EcsParent, {.value = root});
            } else {
                prim_e = ecs_entity(world, { .parent = root });
            }
            if (node->name) {
                ecs_doc_set_name(world, prim_e, node->name);
            }

            ecs_set_ptr(world, prim_e, FlecsPosition3, &pos);
            if (fabsf(rot.x) > 1e-6f || fabsf(rot.y) > 1e-6f ||
                fabsf(rot.z) > 1e-6f)
            {
                ecs_set_ptr(world, prim_e, FlecsRotation3, &rot);
            }
            if (fabsf(scale.x - 1.0f) > 1e-6f ||
                fabsf(scale.y - 1.0f) > 1e-6f ||
                fabsf(scale.z - 1.0f) > 1e-6f)
            {
                ecs_set_ptr(world, prim_e, FlecsScale3, &scale);
            }
            ecs_add_pair(world, prim_e, EcsIsA, mesh_prefab);
            prim_count ++;
        }
    }

    if (!prim_count) {
        ecs_err("GLTF file produced no renderable meshes: %s", path);
    }

    ecs_os_free(image_entities);
    ecs_os_free(material_entities);
    if (mesh_entities) {
        for (cgltf_size mi = 0; mi < data->meshes_count; mi++) {
            ecs_os_free(mesh_entities[mi]);
        }
        ecs_os_free(mesh_entities);
    }
    cgltf_free(data);

    ecs_dbg("loaded GLTF: %s", path);
}

static void FlecsGltf_on_set(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsGltf *gltf = ecs_field(it, FlecsGltf, 0);

    for (int i = 0; i < it->count; i++) {
        ecs_entity_t e = it->entities[i];
        if (!gltf[i].file) {
            continue;
        }

        if (ecs_has_id(world, e, EcsPrefab)) {
            flecsEngine_gltf_normalizeChildren(world, e);
        }
        flecsEngine_gltf_load(world, e, gltf[i].file);
    }
}

void FlecsEngineGltfImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineGltf);

    ecs_set_name_prefix(world, "Flecs");

    ECS_COMPONENT_DEFINE(world, FlecsGltf);
    ecs_struct(world, {
        .entity = ecs_id(FlecsGltf),
        .members = {
            { .name = "file", .type = ecs_id(ecs_string_t) }
        }
    });

    ecs_add_pair(world, ecs_id(FlecsGltf), EcsOnInstantiate, EcsDontInherit);

    ecs_set_hooks(world, FlecsGltf, {
        .ctor = flecs_default_ctor,
        .on_set = FlecsGltf_on_set
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_pair(EcsChildOf, EcsWildcard) }},
        .query.flags = EcsQueryMatchPrefab,
        .events = {EcsOnAdd},
        .callback = FlecsGltfChildOfOnAdd
    });
}
