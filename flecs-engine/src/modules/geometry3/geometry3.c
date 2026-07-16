#define FLECS_ENGINE_GEOMETRY_MESH_IMPL
#define FLECS_ENGINE_GEOMETRY_PRIMITIVES3_IMPL
#include "geometry3.h"

#include <math.h>

ECS_COMPONENT_DECLARE(FlecsMesh3);
ECS_COMPONENT_DECLARE(FlecsBox);
ECS_COMPONENT_DECLARE(FlecsCone);
ECS_COMPONENT_DECLARE(FlecsQuad);
ECS_COMPONENT_DECLARE(FlecsTriangle);
ECS_COMPONENT_DECLARE(FlecsRightTriangle);
ECS_COMPONENT_DECLARE(FlecsTrianglePrism);
ECS_COMPONENT_DECLARE(FlecsRightTrianglePrism);
ECS_COMPONENT_DECLARE(FlecsSphere);
ECS_COMPONENT_DECLARE(FlecsHemiSphere);
ECS_COMPONENT_DECLARE(FlecsIcoSphere);
ECS_COMPONENT_DECLARE(FlecsNGon);
ECS_COMPONENT_DECLARE(FlecsCylinder);
ECS_COMPONENT_DECLARE(FlecsBevel);
ECS_COMPONENT_DECLARE(FlecsBevelCorner);
ECS_COMPONENT_DECLARE(FlecsMesh3Impl);
ECS_COMPONENT_DECLARE(FlecsGeometry3Cache);

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void FlecsMesh3_fini(
    FlecsMesh3 *ptr)
{
    ecs_vec_fini_t(NULL, &ptr->vertices, flecs_vec3_t);
    ecs_vec_fini_t(NULL, &ptr->normals, flecs_vec3_t);
    ecs_vec_fini_t(NULL, &ptr->uvs, flecs_vec2_t);
    ecs_vec_fini_t(NULL, &ptr->tangents, flecs_vec4_t);
    ecs_vec_fini_t(NULL, &ptr->indices, uint32_t);
    ecs_vec_fini_t(NULL, &ptr->colors, flecs_rgba_t);
}

static void flecsEngine_objectWorldAABB_add(
    const ecs_world_t *world,
    ecs_entity_t entity,
    FlecsAABB *out,
    bool *found)
{
    const FlecsMesh3Impl *mesh = ecs_get(world, entity, FlecsMesh3Impl);
    const FlecsWorldTransform3 *transform = ecs_get(
        world, entity, FlecsWorldTransform3);
    if (mesh && transform) {
        FlecsAABB bounds;
        for (int32_t i = 0; i < 3; i ++) {
            bounds.min[i] = bounds.max[i] = transform->m[3][i];
            for (int32_t j = 0; j < 3; j ++) {
                float a = transform->m[j][i] * mesh->aabb.min[j];
                float b = transform->m[j][i] * mesh->aabb.max[j];
                bounds.min[i] += a < b ? a : b;
                bounds.max[i] += a > b ? a : b;
            }
        }
        if (!*found) {
            *out = bounds;
            *found = true;
        } else {
            for (int32_t i = 0; i < 3; i ++) {
                if (bounds.min[i] < out->min[i]) {
                    out->min[i] = bounds.min[i];
                }
                if (bounds.max[i] > out->max[i]) {
                    out->max[i] = bounds.max[i];
                }
            }
        }
    }

    ecs_iter_t it = ecs_children(world, entity);
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            flecsEngine_objectWorldAABB_add(
                world, it.entities[i], out, found);
        }
    }
}

bool flecsEngine_objectWorldAABB(
    const ecs_world_t *world,
    ecs_entity_t entity,
    FlecsAABB *out)
{
    if (!out || !entity || !ecs_is_alive(world, entity)) {
        return false;
    }
    bool found = false;
    flecsEngine_objectWorldAABB_add(world, entity, out, &found);
    return found;
}

ECS_CTOR(FlecsMesh3, ptr, {
    ecs_vec_init_t(NULL, &ptr->vertices, flecs_vec3_t, 0);
    ecs_vec_init_t(NULL, &ptr->normals, flecs_vec3_t, 0);
    ecs_vec_init_t(NULL, &ptr->uvs, flecs_vec2_t, 0);
    ecs_vec_init_t(NULL, &ptr->tangents, flecs_vec4_t, 0);
    ecs_vec_init_t(NULL, &ptr->indices, uint32_t, 0);
    ecs_vec_init_t(NULL, &ptr->colors, flecs_rgba_t, 0);
    ptr->shadow_sink = 0;
})

ECS_MOVE(FlecsMesh3, dst, src, {
    FlecsMesh3_fini(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_COPY(FlecsMesh3, dst, src, {
    FlecsMesh3_fini(dst);
    dst->vertices = ecs_vec_copy_t(NULL, &src->vertices, flecs_vec3_t);
    dst->normals = ecs_vec_copy_t(NULL, &src->normals, flecs_vec3_t);
    dst->uvs = ecs_vec_copy_t(NULL, &src->uvs, flecs_vec2_t);
    dst->tangents = ecs_vec_copy_t(NULL, &src->tangents, flecs_vec4_t);
    dst->indices = ecs_vec_copy_t(NULL, &src->indices, uint32_t);
    dst->colors = ecs_vec_copy_t(NULL, &src->colors, flecs_rgba_t);
    dst->shadow_sink = src->shadow_sink;
})

static void FlecsMesh3Impl_fini(
    FlecsMesh3Impl *ptr)
{
    FLECS_WGPU_RELEASE(ptr->vertex_buffer, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(ptr->vertex_uv_buffer, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(ptr->vertex_color_buffer, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(ptr->index_buffer, wgpuBufferRelease);
}

ECS_MOVE(FlecsMesh3Impl, dst, src, {
    FlecsMesh3Impl_fini(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_DTOR(FlecsMesh3Impl, ptr, {
    FlecsMesh3Impl_fini(ptr);
})

ECS_DTOR(FlecsMesh3, ptr, {
    FlecsMesh3_fini(ptr);
})

ECS_CTOR(FlecsGeometry3Cache, ptr, {
    ecs_map_init(&ptr->sphere_cache, NULL);
    ecs_map_init(&ptr->hemisphere_cache, NULL);
    ecs_map_init(&ptr->icosphere_cache, NULL);
    ecs_map_init(&ptr->cone_cache, NULL);
    ecs_map_init(&ptr->ngon_cache, NULL);
    ecs_map_init(&ptr->cylinder_cache, NULL);
    ecs_map_init(&ptr->bevel_cache, NULL);
    ecs_map_init(&ptr->bevel_corner_cache, NULL);
    ptr->unit_box_asset = 0;
    ptr->unit_quad_asset = 0;
    ptr->unit_triangle_asset = 0;
    ptr->unit_right_triangle_asset = 0;
    ptr->unit_triangle_prism_asset = 0;
    ptr->unit_right_triangle_prism_asset = 0;
})

ECS_DTOR(FlecsGeometry3Cache, ptr, {
    ecs_map_fini(&ptr->sphere_cache);
    ecs_map_fini(&ptr->hemisphere_cache);
    ecs_map_fini(&ptr->icosphere_cache);
    ecs_map_fini(&ptr->cone_cache);
    ecs_map_fini(&ptr->ngon_cache);
    ecs_map_fini(&ptr->cylinder_cache);
    ecs_map_fini(&ptr->bevel_cache);
    ecs_map_fini(&ptr->bevel_corner_cache);
})

static void flecsEngine_mesh_computeTangents(
    int32_t vert_count,
    int32_t idx_count,
    const flecs_vec3_t *positions,
    const flecs_vec3_t *normals,
    const flecs_vec2_t *uvs,
    const uint32_t *indices,
    flecs_vec4_t *out)
{
    flecs_vec3_t *tan1 = ecs_os_calloc_n(flecs_vec3_t, vert_count);
    flecs_vec3_t *tan2 = ecs_os_calloc_n(flecs_vec3_t, vert_count);

    for (int32_t t = 0; t + 2 < idx_count; t += 3) {
        uint32_t i0 = indices[t];
        uint32_t i1 = indices[t + 1];
        uint32_t i2 = indices[t + 2];

        const flecs_vec3_t *v0 = &positions[i0];
        const flecs_vec3_t *v1 = &positions[i1];
        const flecs_vec3_t *v2 = &positions[i2];
        const flecs_vec2_t *w0 = &uvs[i0];
        const flecs_vec2_t *w1 = &uvs[i1];
        const flecs_vec2_t *w2 = &uvs[i2];

        float ex1 = v1->x - v0->x;
        float ey1 = v1->y - v0->y;
        float ez1 = v1->z - v0->z;
        float ex2 = v2->x - v0->x;
        float ey2 = v2->y - v0->y;
        float ez2 = v2->z - v0->z;

        float du1 = w1->x - w0->x;
        float dv1 = w1->y - w0->y;
        float du2 = w2->x - w0->x;
        float dv2 = w2->y - w0->y;

        float det = du1 * dv2 - du2 * dv1;
        if (fabsf(det) < 1e-20f) {
            continue;
        }
        float r = 1.0f / det;

        flecs_vec3_t sdir = {
            (dv2 * ex1 - dv1 * ex2) * r,
            (dv2 * ey1 - dv1 * ey2) * r,
            (dv2 * ez1 - dv1 * ez2) * r
        };
        flecs_vec3_t tdir = {
            (du1 * ex2 - du2 * ex1) * r,
            (du1 * ey2 - du2 * ey1) * r,
            (du1 * ez2 - du2 * ez1) * r
        };

        tan1[i0].x += sdir.x; tan1[i0].y += sdir.y; tan1[i0].z += sdir.z;
        tan1[i1].x += sdir.x; tan1[i1].y += sdir.y; tan1[i1].z += sdir.z;
        tan1[i2].x += sdir.x; tan1[i2].y += sdir.y; tan1[i2].z += sdir.z;

        tan2[i0].x += tdir.x; tan2[i0].y += tdir.y; tan2[i0].z += tdir.z;
        tan2[i1].x += tdir.x; tan2[i1].y += tdir.y; tan2[i1].z += tdir.z;
        tan2[i2].x += tdir.x; tan2[i2].y += tdir.y; tan2[i2].z += tdir.z;
    }

    for (int32_t i = 0; i < vert_count; i++) {
        float nx = normals[i].x, ny = normals[i].y, nz = normals[i].z;
        float tx = tan1[i].x, ty = tan1[i].y, tz = tan1[i].z;

        /* Gram-Schmidt orthonormalize against the vertex normal. */
        float ndt = nx * tx + ny * ty + nz * tz;
        tx -= nx * ndt;
        ty -= ny * ndt;
        tz -= nz * ndt;

        float len2 = tx * tx + ty * ty + tz * tz;
        if (len2 > 1e-20f) {
            float inv = 1.0f / sqrtf(len2);
            tx *= inv; ty *= inv; tz *= inv;
        } else {
            /* Pick any axis perpendicular to N. */
            if (fabsf(nx) > 0.9f) {
                tx = 0.0f; ty = 1.0f; tz = 0.0f;
            } else {
                tx = 1.0f; ty = 0.0f; tz = 0.0f;
            }
            float ndt2 = nx * tx + ny * ty + nz * tz;
            tx -= nx * ndt2;
            ty -= ny * ndt2;
            tz -= nz * ndt2;
            float l = sqrtf(tx * tx + ty * ty + tz * tz);
            if (l > 0.0f) {
                tx /= l; ty /= l; tz /= l;
            }
        }

        /* Bitangent sign: positive when (N x T) aligns with the accumulated
         * binormal direction, negative otherwise. Matches glTF convention. */
        float bx = ny * tz - nz * ty;
        float by = nz * tx - nx * tz;
        float bz = nx * ty - ny * tx;
        float w = (bx * tan2[i].x + by * tan2[i].y + bz * tan2[i].z) < 0.0f
            ? -1.0f : 1.0f;

        out[i].x = tx;
        out[i].y = ty;
        out[i].z = tz;
        out[i].w = w;
    }

    ecs_os_free(tan1);
    ecs_os_free(tan2);
}

static void FlecsMesh3_on_set(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsMesh3 *mesh = ecs_field(it, FlecsMesh3, 0);

    const FlecsEngineImpl *impl = ecs_singleton_get(world, FlecsEngineImpl);

    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        FlecsMesh3Impl *mesh_impl = ecs_ensure(world, e, FlecsMesh3Impl);

        FLECS_WGPU_RELEASE(mesh_impl->vertex_buffer, wgpuBufferRelease);
        FLECS_WGPU_RELEASE(mesh_impl->vertex_uv_buffer, wgpuBufferRelease);
        FLECS_WGPU_RELEASE(mesh_impl->vertex_color_buffer, wgpuBufferRelease);
        FLECS_WGPU_RELEASE(mesh_impl->index_buffer, wgpuBufferRelease);

        int32_t vert_count = ecs_vec_count(&mesh[i].vertices);
        int32_t ind_count = ecs_vec_count(&mesh[i].indices);
        int32_t uv_count = ecs_vec_count(&mesh[i].uvs);
        bool has_uvs = uv_count == vert_count && uv_count > 0;
        mesh_impl->vertex_count = vert_count;
        mesh_impl->index_count = ind_count;

        if (!vert_count || !ind_count) {
            continue;
        }

        flecs_vec3_t *mesh_vertices = ecs_vec_first_t(&mesh[i].vertices, flecs_vec3_t);
        flecs_vec3_t *mesh_normals = ecs_vec_first_t(&mesh[i].normals, flecs_vec3_t);

        /* Compute local-space AABB independently from GPU initialization so
         * meshes and glTF assets can be loaded in headless worlds. */
        float *bb_min = mesh_impl->aabb.min;
        float *bb_max = mesh_impl->aabb.max;
        bb_min[0] = bb_min[1] = bb_min[2] =  1e18f;
        bb_max[0] = bb_max[1] = bb_max[2] = -1e18f;
        for (int v = 0; v < vert_count; v ++) {
            float px = mesh_vertices[v].x;
            float py = mesh_vertices[v].y;
            float pz = mesh_vertices[v].z;
            if (px < bb_min[0]) bb_min[0] = px;
            if (py < bb_min[1]) bb_min[1] = py;
            if (pz < bb_min[2]) bb_min[2] = pz;
            if (px > bb_max[0]) bb_max[0] = px;
            if (py > bb_max[1]) bb_max[1] = py;
            if (pz > bb_max[2]) bb_max[2] = pz;
        }

        if (!impl || !impl->device || !impl->queue) {
            continue;
        }

        /* Position-only buffer for the shadow depth pass. */
        {
            int32_t vert_size = vert_count * (int32_t)sizeof(FlecsGpuVertex);
            WGPUBufferDescriptor vert_desc = {
                .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
                .size = (uint64_t)vert_size
            };

            FlecsGpuVertex *verts = ecs_os_malloc_n(FlecsGpuVertex, vert_count);
            float sink = mesh[i].shadow_sink;
            bool has_normals =
                ecs_vec_count(&mesh[i].normals) == vert_count;
            for (int v = 0; v < vert_count; v ++) {
                verts[v].p = mesh_vertices[v];
                if (sink != 0 && has_normals) {
                    verts[v].p.x -= mesh_normals[v].x * sink;
                    verts[v].p.y -= mesh_normals[v].y * sink;
                    verts[v].p.z -= mesh_normals[v].z * sink;
                }
            }

            mesh_impl->vertex_buffer = wgpuDeviceCreateBuffer(
                impl->device, &vert_desc);
            wgpuQueueWriteBuffer(impl->queue,
                mesh_impl->vertex_buffer, 0, verts, vert_size);
            ecs_os_free(verts);
        }

        /* Always build vertex_uv_buffer. When the source has no UVs the PBR
         * shader's layer-index branches skip texture sampling, so the zero
         * UVs + zero tangents don't matter. This lets one unified shader
         * handle both textured and non-textured geometry. */
        {
            int32_t vert_uv_size = vert_count * (int32_t)sizeof(FlecsGpuVertexLitUv);
            WGPUBufferDescriptor vert_uv_desc = {
                .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
                .size = (uint64_t)vert_uv_size
            };

            FlecsGpuVertexLitUv *uv_verts = ecs_os_malloc_n(FlecsGpuVertexLitUv, vert_count);

            if (has_uvs) {
                flecs_vec2_t *mesh_uvs = ecs_vec_first_t(&mesh[i].uvs, flecs_vec2_t);

                int32_t tan_count = ecs_vec_count(&mesh[i].tangents);
                flecs_vec4_t *tangents;
                bool owns_tangents = false;
                if (tan_count == vert_count) {
                    tangents = ecs_vec_first_t(&mesh[i].tangents, flecs_vec4_t);
                } else {
                    tangents = ecs_os_malloc_n(flecs_vec4_t, vert_count);
                    owns_tangents = true;
                    flecsEngine_mesh_computeTangents(
                        vert_count,
                        ind_count,
                        mesh_vertices,
                        mesh_normals,
                        mesh_uvs,
                        ecs_vec_first_t(&mesh[i].indices, uint32_t),
                        tangents);
                }

                for (int v = 0; v < vert_count; v ++) {
                    uv_verts[v].p = mesh_vertices[v];
                    uv_verts[v].n = mesh_normals[v];
                    uv_verts[v].uv = mesh_uvs[v];
                    uv_verts[v].t = tangents[v];
                }

                if (owns_tangents) {
                    ecs_os_free(tangents);
                }
            } else {
                for (int v = 0; v < vert_count; v ++) {
                    uv_verts[v].p = mesh_vertices[v];
                    uv_verts[v].n = mesh_normals[v];
                    uv_verts[v].uv = (flecs_vec2_t){0};
                    uv_verts[v].t = (flecs_vec4_t){0};
                }
            }

            mesh_impl->vertex_uv_buffer = wgpuDeviceCreateBuffer(impl->device, &vert_uv_desc);
            wgpuQueueWriteBuffer(impl->queue, mesh_impl->vertex_uv_buffer, 0, uv_verts, vert_uv_size);
            ecs_os_free(uv_verts);
        }

        int32_t color_count = ecs_vec_count(&mesh[i].colors);
        if (color_count > 0 && color_count != vert_count) {
            char *name = ecs_get_path(world, e);
            ecs_warn("mesh '%s' has %d colors for %d vertices; "
                "ignoring colors", name, color_count, vert_count);
            ecs_os_free(name);
        } else if (color_count == vert_count) {
            int32_t color_size = vert_count * (int32_t)sizeof(flecs_rgba_t);
            WGPUBufferDescriptor color_desc = {
                .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
                .size = (uint64_t)color_size
            };
            mesh_impl->vertex_color_buffer = wgpuDeviceCreateBuffer(
                impl->device, &color_desc);
            wgpuQueueWriteBuffer(impl->queue, mesh_impl->vertex_color_buffer,
                0, ecs_vec_first_t(&mesh[i].colors, flecs_rgba_t),
                (size_t)color_size);
        }

        int32_t ind_size = ind_count * (int32_t)sizeof(uint32_t);
        WGPUBufferDescriptor ind_desc = {
            .usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst,
            .size = (uint64_t)ind_size
        };

        mesh_impl->index_buffer = wgpuDeviceCreateBuffer(impl->device, &ind_desc);
        uint32_t *indices = ecs_vec_first_t(&mesh[i].indices, uint32_t);
        wgpuQueueWriteBuffer(
            impl->queue, mesh_impl->index_buffer, 0, indices, ind_size);

    }
}

void flecsEngine_mesh3_updateColors(
    ecs_world_t *world,
    ecs_entity_t mesh_entity)
{
    const FlecsMesh3 *mesh = ecs_get(world, mesh_entity, FlecsMesh3);
    if (!mesh) {
        return;
    }

    FlecsMesh3Impl *mesh_impl = ecs_get_mut(
        world, mesh_entity, FlecsMesh3Impl);
    int32_t color_count = ecs_vec_count(&mesh->colors);
    if (!mesh_impl || !mesh_impl->vertex_color_buffer ||
        color_count != mesh_impl->vertex_count)
    {
        ecs_modified(world, mesh_entity, FlecsMesh3);
        return;
    }

    const FlecsEngineImpl *impl = ecs_singleton_get(world, FlecsEngineImpl);
    ecs_assert(impl != NULL, ECS_INVALID_OPERATION, NULL);
    wgpuQueueWriteBuffer(impl->queue, mesh_impl->vertex_color_buffer, 0,
        ecs_vec_first_t(&mesh->colors, flecs_rgba_t),
        (size_t)color_count * sizeof(flecs_rgba_t));
}

ecs_entity_t flecsEngine_geometry3_createAsset(
    ecs_world_t *world,
    FlecsGeometry3Cache *ctx,
    const char *name)
{
    ecs_entity_t asset = ecs_entity(world, {
        .name = name,
        .parent = ecs_entity(world, {
            .name = "flecs.engine.geometry3"
        })
    });

    ecs_add_id(world, asset, EcsPrefab);

    return asset;
}

void FlecsEngineGeometry3Import(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineGeometry3);

    ecs_set_name_prefix(world, "Flecs");

    ECS_COMPONENT_DEFINE(world, FlecsMesh3);
    ECS_COMPONENT_DEFINE(world, FlecsMesh3Impl);
    ECS_COMPONENT_DEFINE(world, FlecsBox);
    ECS_COMPONENT_DEFINE(world, FlecsCone);
    ECS_COMPONENT_DEFINE(world, FlecsQuad);
    ECS_COMPONENT_DEFINE(world, FlecsTriangle);
    ECS_COMPONENT_DEFINE(world, FlecsRightTriangle);
    ECS_COMPONENT_DEFINE(world, FlecsTrianglePrism);
    ECS_COMPONENT_DEFINE(world, FlecsRightTrianglePrism);
    ECS_COMPONENT_DEFINE(world, FlecsSphere);
    ECS_COMPONENT_DEFINE(world, FlecsHemiSphere);
    ECS_COMPONENT_DEFINE(world, FlecsIcoSphere);
    ECS_COMPONENT_DEFINE(world, FlecsNGon);
    ECS_COMPONENT_DEFINE(world, FlecsCylinder);
    ECS_COMPONENT_DEFINE(world, FlecsBevel);
    ECS_COMPONENT_DEFINE(world, FlecsBevelCorner);
    ECS_COMPONENT_DEFINE(world, FlecsGeometry3Cache);

    ecs_struct(world, {
        .entity = ecs_id(FlecsMesh3),
        .members = {
            { .name = "vertices", .type = flecsEngine_vecVec3(world) },
            { .name = "normals", .type = flecsEngine_vecVec3(world) },
            { .name = "uvs", .type = flecsEngine_vecVec2(world) },
            { .name = "indices", .type = flecsEngine_vecU16(world) },
            { .name = "colors", .type = flecsEngine_vecRgba(world),
              .offset = offsetof(FlecsMesh3, colors) },
            { .name = "shadow_sink", .type = ecs_id(ecs_f32_t),
              .offset = offsetof(FlecsMesh3, shadow_sink) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsBox),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) },
            { .name = "z", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsCone),
        .members = {
            { .name = "segments", .type = ecs_id(ecs_i32_t) },
            { .name = "smooth", .type = ecs_id(ecs_bool_t) },
            { .name = "length", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsSphere),
        .members = {
            { .name = "segments", .type = ecs_id(ecs_i32_t) },
            { .name = "smooth", .type = ecs_id(ecs_bool_t) },
            { .name = "radius", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsHemiSphere),
        .members = {
            { .name = "segments", .type = ecs_id(ecs_i32_t) },
            { .name = "smooth", .type = ecs_id(ecs_bool_t) },
            { .name = "radius", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsIcoSphere),
        .members = {
            { .name = "segments", .type = ecs_id(ecs_i32_t) },
            { .name = "smooth", .type = ecs_id(ecs_bool_t) },
            { .name = "radius", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsNGon),
        .members = {
            { .name = "sides", .type = ecs_id(ecs_i32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsQuad),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsTriangle),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsRightTriangle),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsTrianglePrism),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) },
            { .name = "z", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsRightTrianglePrism),
        .members = {
            { .name = "x", .type = ecs_id(ecs_f32_t) },
            { .name = "y", .type = ecs_id(ecs_f32_t) },
            { .name = "z", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsCylinder),
        .members = {
            { .name = "segments", .type = ecs_id(ecs_i32_t) },
            { .name = "smooth", .type = ecs_id(ecs_bool_t) },
            { .name = "length", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsBevel),
        .members = {
            { .name = "segments", .type = ecs_id(ecs_i32_t) },
            { .name = "smooth", .type = ecs_id(ecs_bool_t) },
            { .name = "radius", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsBevelCorner),
        .members = {
            { .name = "segments", .type = ecs_id(ecs_i32_t) },
            { .name = "smooth", .type = ecs_id(ecs_bool_t) }
        }
    });

    ecs_set_hooks(world, FlecsMesh3, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsMesh3),
        .copy = ecs_copy(FlecsMesh3),
        .dtor = ecs_dtor(FlecsMesh3),
        .on_set = FlecsMesh3_on_set
    });

    ecs_set_hooks(world, FlecsMesh3Impl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsMesh3Impl),
        .dtor = ecs_dtor(FlecsMesh3Impl)
    });

    ecs_set_hooks(world, FlecsSphere, {
        .on_replace = FlecsSphere_on_replace
    });

    ecs_set_hooks(world, FlecsHemiSphere, {
        .on_replace = FlecsHemiSphere_on_replace
    });

    ecs_set_hooks(world, FlecsIcoSphere, {
        .on_replace = FlecsIcoSphere_on_replace
    });

    ecs_set_hooks(world, FlecsCone, {
        .on_replace = FlecsCone_on_replace
    });

    ecs_set_hooks(world, FlecsNGon, {
        .on_replace = FlecsNGon_on_replace
    });

    ecs_set_hooks(world, FlecsCylinder, {
        .on_replace = FlecsCylinder_on_replace
    });

    ecs_set_hooks(world, FlecsBevel, {
        .on_replace = FlecsBevel_on_replace
    });

    ecs_set_hooks(world, FlecsBevelCorner, {
        .on_replace = FlecsBevelCorner_on_replace
    });

    ecs_set_hooks(world, FlecsGeometry3Cache, {
        .ctor = ecs_ctor(FlecsGeometry3Cache),
        .dtor = ecs_dtor(FlecsGeometry3Cache)
    });

    ecs_add_pair(world, ecs_id(FlecsMesh3), EcsWith, ecs_id(FlecsMesh3Impl));
    ecs_add_pair(world, ecs_id(FlecsMesh3), EcsOnInstantiate, EcsInherit);
    ecs_add_pair(world, ecs_id(FlecsMesh3Impl), EcsOnInstantiate, EcsInherit);

    ecs_singleton_ensure(world, FlecsGeometry3Cache);
}
