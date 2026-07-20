#define FLECS_ENGINE_TERRAIN_IMPL
#include "terrain.h"

#include <math.h>
#include <float.h>

ECS_COMPONENT_DECLARE(FlecsTerrain);
ECS_COMPONENT_DECLARE(FlecsTerrainPosition);

typedef struct flecs_terrain_layer_t {
    const ecs_type_info_t *ti;
    ecs_vec_t data;
} flecs_terrain_layer_t;

static void FlecsTerrain_fini(
    FlecsTerrain *ptr)
{
    ecs_vec_fini_t(NULL, &ptr->heights, float);
    ecs_vec_fini_t(NULL, &ptr->colors, flecs_rgba_t);

    int32_t i, count = ecs_vec_count(&ptr->layers);
    flecs_terrain_layer_t *layers = ecs_vec_first_t(
        &ptr->layers, flecs_terrain_layer_t);
    for (i = 0; i < count; i ++) {
        if (layers[i].ti) {
            ecs_vec_fini(NULL, &layers[i].data, layers[i].ti->size);
        }
    }
    ecs_vec_fini_t(NULL, &ptr->layers, flecs_terrain_layer_t);
    ecs_vec_fini_t(NULL, &ptr->layerTypes, ecs_entity_t);
    ecs_vec_fini_t(NULL, &ptr->layer_scale, int8_t);
}

ECS_CTOR(FlecsTerrain, ptr, {
    ptr->width = 0;
    ptr->depth = 0;
    ptr->cell_size = 1.0f;
    ecs_vec_init_t(NULL, &ptr->heights, float, 0);
    ecs_vec_init_t(NULL, &ptr->colors, flecs_rgba_t, 0);
    ecs_vec_init_t(NULL, &ptr->layerTypes, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &ptr->layer_scale, int8_t, 0);
    ecs_vec_init_t(NULL, &ptr->layers, flecs_terrain_layer_t, 0);
})

ECS_MOVE(FlecsTerrain, dst, src, {
    FlecsTerrain_fini(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_COPY(FlecsTerrain, dst, src, {
    FlecsTerrain_fini(dst);
    dst->width = src->width;
    dst->depth = src->depth;
    dst->cell_size = src->cell_size;
    dst->heights = ecs_vec_copy_t(NULL, &src->heights, float);
    dst->colors = ecs_vec_copy_t(NULL, &src->colors, flecs_rgba_t);
    dst->layerTypes = ecs_vec_copy_t(NULL, &src->layerTypes, ecs_entity_t);
    dst->layer_scale = ecs_vec_copy_t(NULL, &src->layer_scale, int8_t);

    int32_t i, count = ecs_vec_count(&src->layers);
    dst->layers = ecs_vec_copy_t(NULL, &src->layers, flecs_terrain_layer_t);
    flecs_terrain_layer_t *dst_layers = ecs_vec_first_t(
        &dst->layers, flecs_terrain_layer_t);
    const flecs_terrain_layer_t *src_layers = ecs_vec_first_t(
        &src->layers, flecs_terrain_layer_t);
    for (i = 0; i < count; i ++) {
        if (src_layers[i].ti) {
            dst_layers[i].data = ecs_vec_copy(
                NULL, &src_layers[i].data, src_layers[i].ti->size);
        }
    }
})

ECS_DTOR(FlecsTerrain, ptr, {
    FlecsTerrain_fini(ptr);
})

static bool flecsEngine_terrain_validate(
    ecs_world_t *world,
    ecs_entity_t e,
    const FlecsTerrain *t)
{
    if (t->width <= 0 || t->depth <= 0 || t->cell_size <= 0) {
        char *name = ecs_get_path(world, e);
        ecs_err("terrain '%s' has invalid dimensions (%d x %d, cell %f)",
            name, t->width, t->depth, (double)t->cell_size);
        ecs_os_free(name);
        return false;
    }

    int32_t corner_count = (t->width + 1) * (t->depth + 1);
    if (ecs_vec_count(&t->heights) != corner_count) {
        char *name = ecs_get_path(world, e);
        ecs_err("terrain '%s' has %d heights, expected %d "
            "((width + 1) * (depth + 1) corner samples)",
            name, ecs_vec_count(&t->heights), corner_count);
        ecs_os_free(name);
        return false;
    }

    int32_t color_count = ecs_vec_count(&t->colors);
    if (color_count && color_count != t->width * t->depth) {
        char *name = ecs_get_path(world, e);
        ecs_err("terrain '%s' has %d colors, expected %d (width * depth)",
            name, color_count, t->width * t->depth);
        ecs_os_free(name);
        return false;
    }

    return true;
}

static void flecsEngine_terrain_cornerNormal(
    const float *h,
    int32_t w,
    int32_t d,
    float cs,
    int32_t x,
    int32_t z,
    float out[3])
{
    int32_t stride = w + 1;
    int32_t xl = x > 0 ? x - 1 : x;
    int32_t xr = x < w ? x + 1 : x;
    int32_t zd = z > 0 ? z - 1 : z;
    int32_t zu = z < d ? z + 1 : z;

    float hl = h[z * stride + xl];
    float hr = h[z * stride + xr];
    float hdn = h[zd * stride + x];
    float hup = h[zu * stride + x];

    float dx = (float)(xr - xl) * cs;
    float dz = (float)(zu - zd) * cs;

    float nx = -(hr - hl) / dx;
    float ny = 1.0f;
    float nz = -(hup - hdn) / dz;
    float len = sqrtf(nx * nx + ny * ny + nz * nz);

    out[0] = nx / len;
    out[1] = ny / len;
    out[2] = nz / len;
}

static void flecsEngine_terrain_emitTriangle(
    FlecsMesh3 *m,
    int32_t v,
    const float pa[3],
    const float pb[3],
    const float pc[3],
    const float na[3],
    const float nb[3],
    const float nc[3],
    float inv_extent_x,
    float inv_extent_z,
    flecs_rgba_t color)
{
    const float *p1 = pb, *p2 = pc;
    const float *n1 = nb, *n2 = nc;

    float e1x = pb[0] - pa[0], e1z = pb[2] - pa[2];
    float e2x = pc[0] - pa[0], e2z = pc[2] - pa[2];
    float fny = e1z * e2x - e1x * e2z;
    if (fny < 0) {
        p1 = pc; p2 = pb;
        n1 = nc; n2 = nb;
    }

    flecs_vec3_t *verts = ecs_vec_first_t(&m->vertices, flecs_vec3_t);
    flecs_vec3_t *normals = ecs_vec_first_t(&m->normals, flecs_vec3_t);
    flecs_vec2_t *uvs = ecs_vec_first_t(&m->uvs, flecs_vec2_t);
    flecs_rgba_t *colors = ecs_vec_first_t(&m->colors, flecs_rgba_t);
    uint32_t *indices = ecs_vec_first_t(&m->indices, uint32_t);

    const float *p[3] = { pa, p1, p2 };
    const float *n[3] = { na, n1, n2 };
    for (int32_t k = 0; k < 3; k ++) {
        verts[v + k] = (flecs_vec3_t){ p[k][0], p[k][1], p[k][2] };
        normals[v + k] = (flecs_vec3_t){ n[k][0], n[k][1], n[k][2] };
        uvs[v + k] = (flecs_vec2_t){
            p[k][0] * inv_extent_x, p[k][2] * inv_extent_z };
        colors[v + k] = color;
        indices[v + k] = (uint32_t)(v + k);
    }
}

static flecs_rgba_t flecsEngine_terrain_cornerColor(
    const flecs_rgba_t *colors,
    int32_t width,
    int32_t depth,
    int32_t x,
    int32_t z)
{
    uint32_t r = 0, g = 0, b = 0, a = 0, count = 0;
    for (int32_t dz = -1; dz <= 0; dz ++) {
        int32_t cell_z = z + dz;
        if (cell_z < 0 || cell_z >= depth) {
            continue;
        }
        for (int32_t dx = -1; dx <= 0; dx ++) {
            int32_t cell_x = x + dx;
            if (cell_x < 0 || cell_x >= width) {
                continue;
            }
            flecs_rgba_t color = colors[cell_z * width + cell_x];
            r += color.r;
            g += color.g;
            b += color.b;
            a += color.a;
            count ++;
        }
    }
    return (flecs_rgba_t){
        (uint8_t)((r + count / 2) / count),
        (uint8_t)((g + count / 2) / count),
        (uint8_t)((b + count / 2) / count),
        (uint8_t)((a + count / 2) / count)
    };
}

static void flecsEngine_terrain_setCellVertexColors(
    const FlecsTerrain *terrain,
    flecs_rgba_t *colors,
    int32_t x,
    int32_t z)
{
    int32_t width = terrain->width;
    int32_t depth = terrain->depth;
    const flecs_rgba_t *cell_colors = ecs_vec_first_t(
        &terrain->colors, flecs_rgba_t);
    flecs_rgba_t c00 = flecsEngine_terrain_cornerColor(
        cell_colors, width, depth, x, z);
    flecs_rgba_t c10 = flecsEngine_terrain_cornerColor(
        cell_colors, width, depth, x + 1, z);
    flecs_rgba_t c01 = flecsEngine_terrain_cornerColor(
        cell_colors, width, depth, x, z + 1);
    flecs_rgba_t c11 = flecsEngine_terrain_cornerColor(
        cell_colors, width, depth, x + 1, z + 1);
    const float *heights = ecs_vec_first_t(&terrain->heights, float);
    int32_t stride = width + 1;
    float h00 = heights[z * stride + x];
    float h10 = heights[z * stride + x + 1];
    float h01 = heights[(z + 1) * stride + x];
    float h11 = heights[(z + 1) * stride + x + 1];
    int32_t v = (z * width + x) * 6;

    if (fabsf(h00 - h11) <= fabsf(h10 - h01)) {
        colors[v] = c00;
        colors[v + 1] = c11;
        colors[v + 2] = c10;
        colors[v + 3] = c00;
        colors[v + 4] = c01;
        colors[v + 5] = c11;
    } else {
        colors[v] = c00;
        colors[v + 1] = c01;
        colors[v + 2] = c10;
        colors[v + 3] = c10;
        colors[v + 4] = c01;
        colors[v + 5] = c11;
    }
}

static float flecsEngine_terrain_stratumHeight(
    float base,
    float top,
    float x,
    float z,
    int32_t layer,
    int32_t layer_count)
{
    float t = (float)layer / (float)layer_count;
    float phase = (float)layer * 1.713f;
    float warp = sinf(x * 0.031f + z * 0.023f + phase) * 0.018f;
    warp += sinf(x * 0.011f - z * 0.017f + phase * 1.91f) * 0.012f;
    t += warp * sinf(t * 3.14159265f);
    return base + (top - base) * t;
}

static flecs_rgba_t flecsEngine_terrain_stratumColor(
    int32_t layer,
    int32_t segment,
    int32_t side)
{
    static const flecs_rgba_t colors[] = {
        { 96, 95, 93, 255 },
        { 137, 107, 87, 255 },
        { 166, 116, 91, 255 },
        { 124, 125, 124, 255 },
        { 178, 135, 103, 255 },
        { 108, 106, 105, 255 },
        { 148, 105, 87, 255 }
    };
    flecs_rgba_t color = colors[layer % 7];
    int32_t variation = (int32_t)(
        sinf((float)segment * 0.29f + (float)side * 1.7f) * 7.0f);
    color.r = (uint8_t)((int32_t)color.r + variation);
    color.g = (uint8_t)((int32_t)color.g + variation);
    color.b = (uint8_t)((int32_t)color.b + variation);
    return color;
}

static void flecsEngine_terrain_emitEdgeTriangle(
    FlecsMesh3 *m,
    int32_t v,
    const float pa[3],
    const float pb[3],
    const float pc[3],
    const float normal[3],
    flecs_rgba_t color)
{
    const float *p1 = pb;
    const float *p2 = pc;
    float abx = pb[0] - pa[0];
    float aby = pb[1] - pa[1];
    float abz = pb[2] - pa[2];
    float acx = pc[0] - pa[0];
    float acy = pc[1] - pa[1];
    float acz = pc[2] - pa[2];
    float nx = aby * acz - abz * acy;
    float ny = abz * acx - abx * acz;
    float nz = abx * acy - aby * acx;
    if (nx * normal[0] + ny * normal[1] + nz * normal[2] < 0) {
        p1 = pc;
        p2 = pb;
    }

    flecs_vec3_t *verts = ecs_vec_first_t(&m->vertices, flecs_vec3_t);
    flecs_vec3_t *normals = ecs_vec_first_t(&m->normals, flecs_vec3_t);
    flecs_vec2_t *uvs = ecs_vec_first_t(&m->uvs, flecs_vec2_t);
    flecs_rgba_t *colors = ecs_vec_first_t(&m->colors, flecs_rgba_t);
    uint32_t *indices = ecs_vec_first_t(&m->indices, uint32_t);
    const float *p[3] = { pa, p1, p2 };
    for (int32_t k = 0; k < 3; k ++) {
        verts[v + k] = (flecs_vec3_t){ p[k][0], p[k][1], p[k][2] };
        normals[v + k] = (flecs_vec3_t){
            normal[0], normal[1], normal[2] };
        uvs[v + k] = (flecs_vec2_t){ p[k][0], p[k][1] };
        colors[v + k] = color;
        indices[v + k] = (uint32_t)(v + k);
    }
}

static int32_t flecsEngine_terrain_emitEdge(
    FlecsMesh3 *m,
    int32_t v,
    const float *h,
    int32_t w,
    int32_t d,
    float cs,
    float base,
    int32_t side,
    int32_t layer_count)
{
    int32_t segment_count = side < 2 ? w : d;
    float normal[3] = { 0, 0, 0 };
    if (side == 0) normal[2] = -1;
    if (side == 1) normal[2] = 1;
    if (side == 2) normal[0] = -1;
    if (side == 3) normal[0] = 1;

    for (int32_t segment = 0; segment < segment_count; segment ++) {
        int32_t x0, z0, x1, z1;
        if (side < 2) {
            x0 = segment;
            x1 = segment + 1;
            z0 = z1 = side == 0 ? 0 : d;
        } else {
            z0 = segment;
            z1 = segment + 1;
            x0 = x1 = side == 2 ? 0 : w;
        }

        float px0 = (float)x0 * cs;
        float pz0 = (float)z0 * cs;
        float px1 = (float)x1 * cs;
        float pz1 = (float)z1 * cs;
        float top0 = h[z0 * (w + 1) + x0];
        float top1 = h[z1 * (w + 1) + x1];

        for (int32_t layer = 0; layer < layer_count; layer ++) {
            float y00 = flecsEngine_terrain_stratumHeight(
                base, top0, px0, pz0, layer, layer_count);
            float y10 = flecsEngine_terrain_stratumHeight(
                base, top1, px1, pz1, layer, layer_count);
            float y01 = flecsEngine_terrain_stratumHeight(
                base, top0, px0, pz0, layer + 1, layer_count);
            float y11 = flecsEngine_terrain_stratumHeight(
                base, top1, px1, pz1, layer + 1, layer_count);
            float p00[3] = { px0, y00, pz0 };
            float p10[3] = { px1, y10, pz1 };
            float p01[3] = { px0, y01, pz0 };
            float p11[3] = { px1, y11, pz1 };
            flecs_rgba_t color = flecsEngine_terrain_stratumColor(
                layer, segment, side);

            flecsEngine_terrain_emitEdgeTriangle(
                m, v, p01, p11, p10, normal, color);
            flecsEngine_terrain_emitEdgeTriangle(
                m, v + 3, p01, p10, p00, normal, color);
            v += 6;
        }
    }

    return v;
}

static void flecsEngine_terrain_generateMesh(
    const FlecsTerrain *t,
    FlecsMesh3 *m)
{
    int32_t w = t->width, d = t->depth;
    float cs = t->cell_size;
    int32_t layer_count = 7;
    int32_t surface_vert_count = w * d * 6;
    int32_t edge_vert_count = (w + d) * 2 * layer_count * 6;
    int32_t vert_count = surface_vert_count + edge_vert_count;

    m->shadow_sink = 0.25f * cs;

    ecs_vec_set_count_t(NULL, &m->vertices, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &m->normals, flecs_vec3_t, vert_count);
    ecs_vec_set_count_t(NULL, &m->uvs, flecs_vec2_t, vert_count);
    ecs_vec_set_count_t(NULL, &m->colors, flecs_rgba_t, vert_count);
    ecs_vec_set_count_t(NULL, &m->indices, uint32_t, vert_count);

    const float *h = ecs_vec_first_t(&t->heights, float);
    const flecs_rgba_t *cell_colors = ecs_vec_count(&t->colors)
        ? ecs_vec_first_t(&t->colors, flecs_rgba_t) : NULL;
    float min_height = h[0];
    for (int32_t i = 1; i < (w + 1) * (d + 1); i ++) {
        if (h[i] < min_height) {
            min_height = h[i];
        }
    }
    float base_height = min_height - cs * 6.0f;

    float inv_extent_x = 1.0f / ((float)w * cs);
    float inv_extent_z = 1.0f / ((float)d * cs);

    int32_t corner_count = (w + 1) * (d + 1);
    float *cn = ecs_os_malloc_n(float, corner_count * 3);
    for (int32_t z = 0; z <= d; z ++) {
        for (int32_t x = 0; x <= w; x ++) {
            flecsEngine_terrain_cornerNormal(
                h, w, d, cs, x, z, &cn[(z * (w + 1) + x) * 3]);
        }
    }

    for (int32_t z = 0; z < d; z ++) {
        for (int32_t x = 0; x < w; x ++) {
            float c00 = h[z * (w + 1) + x];
            float c10 = h[z * (w + 1) + x + 1];
            float c01 = h[(z + 1) * (w + 1) + x];
            float c11 = h[(z + 1) * (w + 1) + x + 1];

            float x0 = (float)x * cs, x1 = (float)(x + 1) * cs;
            float z0 = (float)z * cs, z1 = (float)(z + 1) * cs;
            float p00[3] = { x0, c00, z0 };
            float p10[3] = { x1, c10, z0 };
            float p01[3] = { x0, c01, z1 };
            float p11[3] = { x1, c11, z1 };

            const float *n00 = &cn[(z * (w + 1) + x) * 3];
            const float *n10 = &cn[(z * (w + 1) + x + 1) * 3];
            const float *n01 = &cn[((z + 1) * (w + 1) + x) * 3];
            const float *n11 = &cn[((z + 1) * (w + 1) + x + 1) * 3];

            flecs_rgba_t color = cell_colors
                ? cell_colors[z * w + x]
                : (flecs_rgba_t){ 255, 255, 255, 255 };

            /* Split along the diagonal with the smallest height difference
             * so ridges/valleys follow the terrain shape. */
            int32_t v = (z * w + x) * 6;
            if (fabsf(c00 - c11) <= fabsf(c10 - c01)) {
                flecsEngine_terrain_emitTriangle(m, v, p00, p10, p11,
                    n00, n10, n11, inv_extent_x, inv_extent_z, color);
                flecsEngine_terrain_emitTriangle(m, v + 3, p00, p11, p01,
                    n00, n11, n01, inv_extent_x, inv_extent_z, color);
            } else {
                flecsEngine_terrain_emitTriangle(m, v, p00, p10, p01,
                    n00, n10, n01, inv_extent_x, inv_extent_z, color);
                flecsEngine_terrain_emitTriangle(m, v + 3, p10, p11, p01,
                    n10, n11, n01, inv_extent_x, inv_extent_z, color);
            }
            if (cell_colors) {
                flecsEngine_terrain_setCellVertexColors(
                    t, ecs_vec_first_t(&m->colors, flecs_rgba_t), x, z);
            }
        }
    }

    int32_t edge_v = surface_vert_count;
    for (int32_t side = 0; side < 4; side ++) {
        edge_v = flecsEngine_terrain_emitEdge(
            m, edge_v, h, w, d, cs, base_height, side, layer_count);
    }

    ecs_os_free(cn);
}

static ecs_entity_t flecsEngine_terrain_meshEntity(
    ecs_world_t *world,
    ecs_entity_t e)
{
    return ecs_lookup_child(world, e, "mesh");
}

static void flecsEngine_terrain_refreshPositions(
    ecs_world_t *world,
    ecs_entity_t terrain);

static void flecsEngine_terrain_syncLayers(
    ecs_world_t *world,
    ecs_entity_t e,
    FlecsTerrain *t)
{
    int32_t i, count = ecs_vec_count(&t->layerTypes);
    const ecs_entity_t *types = ecs_vec_first_t(&t->layerTypes, ecs_entity_t);

    int32_t existing = ecs_vec_count(&t->layers);
    flecs_terrain_layer_t *layers = ecs_vec_first_t(
        &t->layers, flecs_terrain_layer_t);

    bool rebuild = existing != count;
    for (i = 0; !rebuild && i < count; i ++) {
        if (!layers[i].ti || layers[i].ti->component != types[i]) {
            rebuild = true;
        }
    }

    if (rebuild) {
        for (i = 0; i < existing; i ++) {
            if (layers[i].ti) {
                ecs_vec_fini(NULL, &layers[i].data, layers[i].ti->size);
            }
        }
        ecs_vec_set_count_t(NULL, &t->layers, flecs_terrain_layer_t, count);
        layers = ecs_vec_first_t(&t->layers, flecs_terrain_layer_t);
        for (i = 0; i < count; i ++) {
            const ecs_type_info_t *ti = ecs_get_type_info(world, types[i]);
            layers[i].ti = ti;
            ecs_vec_init(NULL, &layers[i].data, ti ? ti->size : 0, 0);
            if (!ti) {
                char *name = ecs_get_path(world, e);
                char *type_name = ecs_get_path(world, types[i]);
                ecs_err("terrain '%s': layer type '%s' has no type info",
                    name, type_name);
                ecs_os_free(type_name);
                ecs_os_free(name);
            }
        }
    }

    layers = ecs_vec_first_t(&t->layers, flecs_terrain_layer_t);
    for (i = 0; i < count; i ++) {
        const ecs_type_info_t *ti = layers[i].ti;
        if (ti) {
            int32_t w, d;
            flecsEngine_terrainLayerDimensions(t, i, &w, &d);
            int32_t cells = w * d;
            int32_t prev = ecs_vec_count(&layers[i].data);
            ecs_vec_set_count_w_type_info(
                NULL, &layers[i].data, ti->size, cells, ti);
            if (!ti->hooks.ctor_move_dtor && cells > prev) {
                ecs_os_memset(
                    ECS_ELEM(ecs_vec_first(&layers[i].data), ti->size, prev),
                    0, (cells - prev) * ti->size);
            }
        }
    }
}

static void FlecsTerrain_on_set(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsTerrain *t = ecs_field(it, FlecsTerrain, 0);

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        if (!flecsEngine_terrain_validate(world, e, &t[i])) {
            continue;
        }

        flecsEngine_terrain_syncLayers(world, e, &t[i]);

        ecs_entity_t mesh_e = flecsEngine_terrain_meshEntity(world, e);
        if (!mesh_e) {
            mesh_e = ecs_entity(world, { .name = "mesh", .parent = e });
            ecs_add_id(world, mesh_e, EcsPrefab);
        }

        FlecsMesh3 *mesh = ecs_ensure(world, mesh_e, FlecsMesh3);
        flecsEngine_terrain_generateMesh(&t[i], mesh);
        ecs_modified(world, mesh_e, FlecsMesh3);

        ecs_add_pair(world, e, EcsIsA, mesh_e);
        ecs_add_id(world, e, FlecsVertexColors);
        if (!ecs_owns(world, e, FlecsRgba)) {
            ecs_set(world, e, FlecsRgba, { 255, 255, 255, 255 });
        }
        if (!ecs_owns(world, e, FlecsPbrMaterial)) {
            ecs_set(world, e, FlecsPbrMaterial,
                { .metallic = 0, .roughness = 1.0f });
        }
        if (!ecs_owns(world, e, FlecsPosition3)) {
            ecs_set(world, e, FlecsPosition3, { 0, 0, 0 });
        }

        flecsEngine_terrain_refreshPositions(world, e);
    }
}

void flecsEngine_terrain_setHeight(
    ecs_world_t *world,
    ecs_entity_t terrainEntity,
    int32_t x,
    int32_t z,
    int32_t width,
    int32_t depth,
    float targetHeight)
{
    FlecsTerrain *t = ecs_get_mut(world, terrainEntity, FlecsTerrain);
    if (!t || x < 0 || z < 0 || width <= 0 || depth <= 0 ||
        x >= t->width || z >= t->depth ||
        width > t->width - x || depth > t->depth - z)
    {
        return;
    }

    if (ecs_vec_count(&t->heights) != (t->width + 1) * (t->depth + 1)) {
        return;
    }

    float *heights = ecs_vec_first_t(&t->heights, float);
    int32_t stride = t->width + 1;
    for (int32_t cz = z; cz <= z + depth; cz ++) {
        for (int32_t cx = x; cx <= x + width; cx ++) {
            heights[cz * stride + cx] = targetHeight;
        }
    }

    ecs_modified(world, terrainEntity, FlecsTerrain);
}

float flecsEngine_terrainCellHeight(
    const FlecsTerrain *t,
    int32_t x,
    int32_t y)
{
    if (!t || x < 0 || y < 0 || x >= t->width || y >= t->depth) {
        return 0;
    }
    if (ecs_vec_count(&t->heights) != (t->width + 1) * (t->depth + 1)) {
        return 0;
    }
    const float *h = ecs_vec_first_t(&t->heights, float);
    int32_t stride = t->width + 1;
    return 0.25f * (
        h[y * stride + x] + h[y * stride + x + 1] +
        h[(y + 1) * stride + x] + h[(y + 1) * stride + x + 1]);
}

float flecsEngine_terrainSampleHeight(
    const FlecsTerrain *t,
    float x,
    float z)
{
    if (!t || t->width <= 0 || t->depth <= 0 || t->cell_size <= 0) {
        return 0;
    }
    if (ecs_vec_count(&t->heights) != (t->width + 1) * (t->depth + 1)) {
        return 0;
    }

    float fx = x / t->cell_size;
    float fz = z / t->cell_size;
    if (fx < 0) fx = 0;
    if (fz < 0) fz = 0;
    if (fx > (float)t->width) fx = (float)t->width;
    if (fz > (float)t->depth) fz = (float)t->depth;

    int32_t cx = (int32_t)fx;
    int32_t cz = (int32_t)fz;
    if (cx >= t->width) cx = t->width - 1;
    if (cz >= t->depth) cz = t->depth - 1;

    float u = fx - (float)cx;
    float v = fz - (float)cz;

    const float *h = ecs_vec_first_t(&t->heights, float);
    int32_t stride = t->width + 1;
    float h00 = h[cz * stride + cx];
    float h10 = h[cz * stride + cx + 1];
    float h01 = h[(cz + 1) * stride + cx];
    float h11 = h[(cz + 1) * stride + cx + 1];

    float h0 = h00 + (h10 - h00) * u;
    float h1 = h01 + (h11 - h01) * u;
    return h0 + (h1 - h0) * v;
}

void* flecsEngine_terrainGetLayer(
    ecs_world_t *world,
    ecs_entity_t terrain,
    int16_t index,
    ecs_entity_t type)
{
    const FlecsTerrain *t = ecs_get(world, terrain, FlecsTerrain);
    if (!t) {
        return NULL;
    }

    int32_t i, count = ecs_vec_count(&t->layerTypes);
    ecs_assert(index < count, ECS_INVALID_PARAMETER, "invalid layer index");

    const ecs_entity_t *types = ecs_vec_first_t(&t->layerTypes, ecs_entity_t);
    ecs_assert(types[index] == type, ECS_INVALID_PARAMETER, "invalid layer type");

    const flecs_terrain_layer_t *layers = ecs_vec_first_t(
        &t->layers, flecs_terrain_layer_t);

    return ecs_vec_first(&layers[index].data);
}

int32_t flecsEngine_terrainLayerScale(
    const FlecsTerrain *t,
    int16_t index)
{
    if (!t || index < 0 || index >= ecs_vec_count(&t->layerTypes) ||
        index >= ecs_vec_count(&t->layer_scale))
    {
        return 1;
    }

    const int8_t *scales = ecs_vec_first_t(&t->layer_scale, int8_t);
    return scales[index] > 1 ? scales[index] : 1;
}

void flecsEngine_terrainLayerDimensions(
    const FlecsTerrain *t,
    int16_t index,
    int32_t *width_out,
    int32_t *depth_out)
{
    int32_t scale = flecsEngine_terrainLayerScale(t, index);
    if (width_out) {
        *width_out = t && t->width > 0 ? (t->width + scale - 1) / scale : 0;
    }
    if (depth_out) {
        *depth_out = t && t->depth > 0 ? (t->depth + scale - 1) / scale : 0;
    }
}

static bool flecsEngine_terrain_isLocalTo(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_entity_t terrain)
{
    if (ecs_get_parent(world, e) == terrain) {
        return true;
    }
    const EcsParent *ep = ecs_get(world, e, EcsParent);
    return ep && ep->value == terrain;
}

bool flecsEngine_terrainTileToPosition(
    const ecs_world_t *world,
    ecs_entity_t terrain,
    int32_t x,
    int32_t y,
    int32_t span_x,
    int32_t span_y,
    const flecs_vec3_t *offset,
    FlecsPosition3 *out)
{
    if (!out || !terrain || !ecs_is_alive(world, terrain)) {
        return false;
    }
    const FlecsTerrain *t = ecs_get(world, terrain, FlecsTerrain);
    if (!t || t->width <= 0 || t->depth <= 0 ||
        ecs_vec_count(&t->heights) != (t->width + 1) * (t->depth + 1))
    {
        return false;
    }

    int32_t sx = span_x > 1 ? span_x : 1;
    int32_t sy = span_y > 1 ? span_y : 1;
    if (x < 0 || y < 0 || x + sx > t->width || y + sy > t->depth) {
        return false;
    }

    float height = -FLT_MAX;
    for (int32_t cy = y; cy < y + sy; cy ++) {
        for (int32_t cx = x; cx < x + sx; cx ++) {
            float cell_height = flecsEngine_terrainCellHeight(t, cx, cy);
            if (cell_height > height) {
                height = cell_height;
            }
        }
    }

    *out = (FlecsPosition3){
        ((float)x + (float)sx * 0.5f) * t->cell_size,
        height,
        ((float)y + (float)sy * 0.5f) * t->cell_size
    };
    const FlecsPosition3 *terrain_position = ecs_get(
        world, terrain, FlecsPosition3);
    if (terrain_position) {
        out->x += terrain_position->x;
        out->y += terrain_position->y;
        out->z += terrain_position->z;
    }
    if (offset) {
        out->x += offset->x;
        out->y += offset->y;
        out->z += offset->z;
    }
    return true;
}

static void flecsEngine_terrain_applyPosition(
    ecs_world_t *world,
    ecs_entity_t e,
    const FlecsTerrainPosition *tp)
{
    if (!tp->terrain || !ecs_is_alive(world, tp->terrain)) {
        return;
    }
    const FlecsTerrain *t = ecs_get(world, tp->terrain, FlecsTerrain);
    if (!t || t->width <= 0 || t->depth <= 0) {
        return;
    }
    if (ecs_vec_count(&t->heights) != (t->width + 1) * (t->depth + 1)) {
        return;
    }

    int32_t sx = tp->span_x > 1 ? tp->span_x : 1;
    int32_t sy = tp->span_y > 1 ? tp->span_y : 1;
    if (tp->x < 0 || tp->y < 0 ||
        tp->x + sx > t->width || tp->y + sy > t->depth)
    {
        char *name = ecs_get_path(world, e);
        ecs_err("terrain position '%s' out of bounds "
            "(tile %d, %d, span %d x %d on %d x %d terrain)",
            name, tp->x, tp->y, sx, sy, t->width, t->depth);
        ecs_os_free(name);
        return;
    }

    float height = -FLT_MAX;
    for (int32_t cy = tp->y; cy < tp->y + sy; cy ++) {
        for (int32_t cx = tp->x; cx < tp->x + sx; cx ++) {
            float ch = flecsEngine_terrainCellHeight(t, cx, cy);
            if (ch > height) {
                height = ch;
            }
        }
    }

    float px = ((float)tp->x + (float)sx * 0.5f) * t->cell_size;
    float pz = ((float)tp->y + (float)sy * 0.5f) * t->cell_size;

    if (!flecsEngine_terrain_isLocalTo(world, e, tp->terrain)) {
        const FlecsPosition3 *terrain_pos = ecs_get(
            world, tp->terrain, FlecsPosition3);
        if (terrain_pos) {
            px += terrain_pos->x;
            height += terrain_pos->y;
            pz += terrain_pos->z;
        }
    }

    ecs_set(world, e, FlecsPosition3, { px, height, pz });
    ecs_set(world, e, FlecsRotation3, { 0, tp->yaw, 0 });
}

static void FlecsTerrainPosition_on_set(
    ecs_iter_t *it)
{
    FlecsTerrainPosition *tp = ecs_field(it, FlecsTerrainPosition, 0);
    for (int32_t i = 0; i < it->count; i ++) {
        flecsEngine_terrain_applyPosition(
            it->world, it->entities[i], &tp[i]);
    }
}

static void flecsEngine_terrain_refreshPositions(
    ecs_world_t *world,
    ecs_entity_t terrain)
{
    ecs_iter_t it = ecs_each(world, FlecsTerrainPosition);
    while (ecs_each_next(&it)) {
        FlecsTerrainPosition *tp = ecs_field(&it, FlecsTerrainPosition, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            if (tp[i].terrain == terrain) {
                flecsEngine_terrain_applyPosition(
                    world, it.entities[i], &tp[i]);
            }
        }
    }
}

void flecsEngine_terrainColorsModified(
    ecs_world_t *world,
    ecs_entity_t terrain)
{
    const FlecsTerrain *t = ecs_get(world, terrain, FlecsTerrain);
    if (!t) {
        return;
    }

    ecs_entity_t mesh_e = flecsEngine_terrain_meshEntity(world, terrain);
    if (!mesh_e) {
        return;
    }

    int32_t cell_count = t->width * t->depth;
    if (ecs_vec_count(&t->colors) != cell_count) {
        return;
    }

    FlecsMesh3 *mesh = ecs_get_mut(world, mesh_e, FlecsMesh3);
    if (!mesh || ecs_vec_count(&mesh->colors) < cell_count * 6) {
        return;
    }

    flecs_rgba_t *vert_colors = ecs_vec_first_t(&mesh->colors, flecs_rgba_t);
    for (int32_t z = 0; z < t->depth; z ++) {
        for (int32_t x = 0; x < t->width; x ++) {
            flecsEngine_terrain_setCellVertexColors(
                t, vert_colors, x, z);
        }
    }

    flecsEngine_mesh3_updateColorRange(world, mesh_e, 0, cell_count * 6);
}

void flecsEngine_terrain_computeSlope(
    ecs_world_t *world,
    ecs_entity_t entity,
    flecs_vec2_t *slope)
{
    const FlecsTerrain *cfg = ecs_get(world, entity, FlecsTerrain);
    int32_t w = cfg->width, d = cfg->depth;
    const float *h = ecs_vec_first_t(&cfg->heights, float);
    int32_t stride = w + 1;
    float cs = cfg->cell_size > 0 ? cfg->cell_size : 1.0f;

    for (int32_t z = 0; z < d; z ++) {
        for (int32_t x = 0; x < w; x ++) {
            float c00 = h[z * stride + x];
            float c10 = h[z * stride + x + 1];
            float c01 = h[(z + 1) * stride + x];
            float c11 = h[(z + 1) * stride + x + 1];

            float dhdx = ((c10 + c11) - (c00 + c01)) * 0.5f / cs;
            float dhdz = ((c01 + c11) - (c00 + c10)) * 0.5f / cs;

            slope[z * w + x] = (flecs_vec2_t){ dhdx, dhdz };
        }
    }
}

void FlecsEngineTerrainImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineTerrain);

    ecs_set_name_prefix(world, "Flecs");

    ECS_COMPONENT_DEFINE(world, FlecsTerrain);
    ECS_COMPONENT_DEFINE(world, FlecsTerrainPosition);

    ecs_struct(world, {
        .entity = ecs_id(FlecsTerrain),
        .members = {
            { .name = "width", .type = ecs_id(ecs_i32_t) },
            { .name = "depth", .type = ecs_id(ecs_i32_t) },
            { .name = "cell_size", .type = ecs_id(ecs_f32_t) },
            { .name = "layerTypes", .type = flecsEngine_vecEntity(world),
              .offset = offsetof(FlecsTerrain, layerTypes) },
            { .name = "layer_scale", .type = flecsEngine_vecI8(world),
              .offset = offsetof(FlecsTerrain, layer_scale) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsTerrainPosition),
        .members = {
            { .name = "terrain", .type = ecs_id(ecs_entity_t) },
            { .name = "x", .type = ecs_id(ecs_i32_t) },
            { .name = "y", .type = ecs_id(ecs_i32_t) },
            { .name = "span_x", .type = ecs_id(ecs_i32_t) },
            { .name = "span_y", .type = ecs_id(ecs_i32_t) },
            { .name = "yaw", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_set_hooks(world, FlecsTerrain, {
        .ctor = ecs_ctor(FlecsTerrain),
        .move = ecs_move(FlecsTerrain),
        .copy = ecs_copy(FlecsTerrain),
        .dtor = ecs_dtor(FlecsTerrain),
        .on_set = FlecsTerrain_on_set
    });

    ecs_set_hooks(world, FlecsTerrainPosition, {
        .on_set = FlecsTerrainPosition_on_set
    });
}
