#include <limits.h>
#include <string.h>

#include "renderer.h"
#include "../../tracy_hooks.h"
#include "flecs_engine.h"

static void flecsEngine_material_ensureBufferCapacity(
    FlecsEngineImpl *impl,
    uint32_t required_count)
{
    if (!required_count) {
        return;
    }

    if (impl->materials.buffer &&
        impl->materials.cpu_materials &&
        required_count <= impl->materials.buffer_capacity)
    {
        return;
    }

    uint32_t new_capacity = impl->materials.buffer_capacity;
    if (!new_capacity) {
        new_capacity = 64;
    }

    while (new_capacity < required_count) {
        if (new_capacity > (UINT32_MAX / 2u)) {
            new_capacity = required_count;
            break;
        }
        new_capacity *= 2u;
    }

    FlecsGpuMaterial *new_cpu_materials =
        ecs_os_malloc_n(FlecsGpuMaterial, new_capacity);
    ecs_assert(new_cpu_materials != NULL, ECS_OUT_OF_MEMORY, NULL);

    int8_t *new_cpu_buckets =
        ecs_os_malloc_n(int8_t, new_capacity);
    ecs_assert(new_cpu_buckets != NULL, ECS_OUT_OF_MEMORY, NULL);
    ecs_os_memset_n(new_cpu_buckets, FLECS_ENGINE_BUCKET_UNSET,
        int8_t, (int32_t)new_capacity);

    if (impl->materials.cpu_materials && impl->materials.buffer_capacity) {
        ecs_os_memcpy_n(new_cpu_materials, impl->materials.cpu_materials,
            FlecsGpuMaterial, (int32_t)impl->materials.buffer_capacity);
        ecs_os_memcpy_n(new_cpu_buckets, impl->materials.cpu_buckets,
            int8_t, (int32_t)impl->materials.buffer_capacity);
    }

    WGPUBuffer new_material_buffer = wgpuDeviceCreateBuffer(
        impl->device, &(WGPUBufferDescriptor){
            .usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)new_capacity * sizeof(FlecsGpuMaterial)
        });
    ecs_assert(new_material_buffer != NULL, ECS_INTERNAL_ERROR, NULL);

    if (impl->materials.buffer) {
        wgpuBufferRelease(impl->materials.buffer);
    }

    ecs_os_free(impl->materials.cpu_materials);
    ecs_os_free(impl->materials.cpu_buckets);

    impl->materials.buffer = new_material_buffer;
    impl->materials.cpu_materials = new_cpu_materials;
    impl->materials.cpu_buckets = new_cpu_buckets;
    impl->materials.buffer_capacity = new_capacity;

    impl->scene_bind_version ++;
}

void flecsEngine_material_ensureBuffer(
    FlecsEngineImpl *impl)
{
    if (!impl->materials.buffer) {
        flecsEngine_material_ensureBufferCapacity(impl, 1);
    }
}

void flecsEngine_material_releaseBuffer(
    FlecsEngineImpl *impl)
{
    FLECS_WGPU_RELEASE(impl->materials.buffer, wgpuBufferRelease);

    ecs_os_free(impl->materials.cpu_materials);
    impl->materials.cpu_materials = NULL;
    ecs_os_free(impl->materials.cpu_buckets);
    impl->materials.cpu_buckets = NULL;

    impl->materials.buffer_capacity = 0;
    impl->materials.count = 0;

    flecsEngine_materialBind_release(impl);

    flecsEngine_textureArray_release(impl);
    flecsEngine_textureBlit_release(impl);
}

FlecsGpuMaterial flecsEngine_material_pack(
    const FlecsEngineImpl *engine,
    const FlecsRgba *color,
    const FlecsPbrMaterial *pbr,
    const FlecsEmissive *emissive,
    const FlecsTransmission *transmission,
    const FlecsTextureTransform *tex_transform,
    const FlecsActualTint *tint)
{
    FlecsRgba c = color ? *color :
        *flecsEngine_defaultAttrCache_getColor(engine, 1);
    FlecsPbrMaterial p = pbr ? *pbr :
        *flecsEngine_defaultAttrCache_getMaterial(engine, 1);
    FlecsEmissive e = emissive ? *emissive :
        *flecsEngine_defaultAttrCache_getEmissive(engine, 1);

    FlecsGpuMaterial gm = {
        .color = c,
        .metallic = p.metallic,
        .roughness = p.roughness,
        .emissive_strength = e.strength,
        .emissive_color = e.color,
        .uv_scale_x = 1.0f,
        .uv_scale_y = 1.0f,
        .tint = 0u
    };

    if (tint) {
        gm.tint = (uint32_t)tint->r | ((uint32_t)tint->g << 8) |
            ((uint32_t)tint->b << 16) | ((uint32_t)tint->a << 24);
    }

    if (transmission) {
        gm.transmission_factor = transmission->transmission_factor;
        gm.ior = transmission->ior;
        gm.thickness_factor = transmission->thickness_factor;
        gm.attenuation_distance = transmission->attenuation_distance;
        flecs_rgba_t ac = transmission->attenuation_color;
        gm.attenuation_color =
            (uint32_t)ac.r | ((uint32_t)ac.g << 8) |
            ((uint32_t)ac.b << 16) | ((uint32_t)ac.a << 24);
    }

    if (tex_transform) {
        gm.uv_scale_x = tex_transform->scale_x;
        gm.uv_scale_y = tex_transform->scale_y;
        gm.uv_offset_x = tex_transform->offset_x;
        gm.uv_offset_y = tex_transform->offset_y;
    }

    return gm;
}

FlecsGpuMaterial flecsEngine_material_tintShared(
    const FlecsEngineImpl *engine,
    uint32_t material_id,
    const FlecsActualTint *tint)
{
    FlecsGpuMaterial gm;
    if (engine->materials.cpu_materials &&
        material_id < engine->materials.count)
    {
        gm = engine->materials.cpu_materials[material_id];
    } else {
        gm = flecsEngine_material_pack(
            engine, NULL, NULL, NULL, NULL, NULL, NULL);
    }

    if (tint) {
        gm.tint = (uint32_t)tint->r | ((uint32_t)tint->g << 8) |
            ((uint32_t)tint->b << 16) | ((uint32_t)tint->a << 24);
    } else {
        gm.tint = 0u;
    }

    return gm;
}

const FlecsRgba* flecsEngine_material_resolveRgba(
    const ecs_world_t *world,
    ecs_entity_t entity,
    const FlecsRgba *fallback,
    FlecsRgba *storage)
{
    const FlecsTransitionValue *value = flecs_transition_value_get(
        world, entity, ecs_id(FlecsRgba));
    if (!value) {
        return fallback;
    }
    *storage = (FlecsRgba){
        (uint8_t)glm_clamp(value->value[0] + .5f, 0, 255),
        (uint8_t)glm_clamp(value->value[1] + .5f, 0, 255),
        (uint8_t)glm_clamp(value->value[2] + .5f, 0, 255),
        (uint8_t)glm_clamp(value->value[3] + .5f, 0, 255)
    };
    return storage;
}

void flecsEngine_material_uploadBuffer(
    const ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    FLECS_TRACY_ZONE_BEGIN("MaterialUpload");

    flecsEngine_material_ensureBuffer(impl);

    if (impl->materials.dirty_version == impl->materials.uploaded_version) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    impl->materials.count = 0;
    if (!impl->materials.query || !impl->queue) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    uint32_t snapshot_version = impl->materials.dirty_version;

    ecs_trace("upload materials buffer (last material id = %u)",
        impl->materials.next_id);

redo: {
        uint32_t required_count = 0;
        uint32_t capacity = impl->materials.buffer_capacity;

        if (capacity) {
            ecs_os_memset_n(
                impl->materials.cpu_materials, 0, FlecsGpuMaterial, capacity);
            ecs_os_memset_n(
                impl->materials.cpu_buckets, FLECS_ENGINE_BUCKET_UNSET,
                int8_t, (int32_t)capacity);
        }

        ecs_iter_t it = ecs_query_iter(world, impl->materials.query);
        while (ecs_query_next(&it)) {
            const FlecsRgba *colors = ecs_field(&it, FlecsRgba, 0);
            const FlecsPbrMaterial *materials =
                ecs_field(&it, FlecsPbrMaterial, 1);
            const FlecsMaterialId *material_ids =
                ecs_field(&it, FlecsMaterialId, 2);
            const FlecsEmissive *emissives =
                ecs_field(&it, FlecsEmissive, 3);
            const FlecsTransmission *transmissions =
                ecs_field(&it, FlecsTransmission, 4);
            const FlecsTextureTransform *tex_transforms =
                ecs_field(&it, FlecsTextureTransform, 5);

            bool colors_self = ecs_field_is_self(&it, 0);
            bool materials_self = materials && ecs_field_is_self(&it, 1);
            bool emissives_self = ecs_field_is_self(&it, 3);
            bool transmissions_self = ecs_field_is_self(&it, 4);
            bool tex_transforms_self = ecs_field_is_self(&it, 5);

            for (int32_t i = 0; i < it.count; i ++) {
                uint32_t index = material_ids[i].value;
                if ((index + 1u) > required_count) {
                    required_count = index + 1u;
                }

                if (index >= capacity) {
                    continue;
                }

                int32_t ci = colors_self ? i : 0;
                int32_t mi = materials_self ? i : 0;
                int32_t ei = emissives_self ? i : 0;
                int32_t ti = transmissions_self ? i : 0;
                int32_t tti = tex_transforms_self ? i : 0;
                ecs_entity_t entity = it.entities[i];
                FlecsRgba color_storage;
                const FlecsRgba *actual_color =
                    flecsEngine_material_resolveRgba(
                        world, entity, &colors[ci], &color_storage);
                const FlecsPbrMaterial *actual_material =
                    flecs_transition_get(
                        world, entity, ecs_id(FlecsPbrMaterial));
                const FlecsEmissive *actual_emissive = flecs_transition_get(
                    world, entity, ecs_id(FlecsEmissive));
                const FlecsTransmission *actual_transmission =
                    flecs_transition_get(
                        world, entity, ecs_id(FlecsTransmission));
                const FlecsTextureTransform *actual_tex_transform =
                    flecs_transition_get(
                        world, entity, ecs_id(FlecsTextureTransform));

                impl->materials.cpu_materials[index] = flecsEngine_material_pack(
                    impl,
                    actual_color,
                    actual_material ? actual_material :
                        (materials ? &materials[mi] : NULL),
                    actual_emissive ? actual_emissive :
                        (emissives ? &emissives[ei] : NULL),
                    actual_transmission ? actual_transmission :
                        (transmissions ? &transmissions[ti] : NULL),
                    actual_tex_transform ? actual_tex_transform :
                        (tex_transforms ? &tex_transforms[tti] : NULL),
                    NULL);
            }
        }

        if (required_count > capacity) {
            flecsEngine_material_ensureBufferCapacity(impl, required_count);
            goto redo;
        }

        if (!required_count) {
            FLECS_TRACY_ZONE_END;
            return;
        }

        wgpuQueueWriteBuffer(
            impl->queue,
            impl->materials.buffer,
            0,
            impl->materials.cpu_materials,
            (uint64_t)required_count * sizeof(FlecsGpuMaterial));
        impl->materials.count = required_count;

        flecsEngine_textureArray_release(impl);

        impl->materials.uploaded_version = snapshot_version;
    }
    FLECS_TRACY_ZONE_END;
}
