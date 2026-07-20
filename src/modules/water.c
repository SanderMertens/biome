#define BIOME_WATER_IMPL

#include "biome.h"
#include "water_shader.h"

typedef struct WaterMeshState {
    ecs_entity_t asset;
    ecs_entity_t instance;
    ecs_entity_t shader;
    uint64_t hash;
    ecs_vec_t flow_surface;
} WaterMeshState;

ECS_COMPONENT_DECLARE(WaterMeshState);

typedef struct WaterRenderState {
    ecs_entity_t shader;
} WaterRenderState;

ECS_COMPONENT_DECLARE(WaterRenderState);

static void WaterMeshState_fini(
    WaterMeshState *ptr)
{
    ecs_vec_fini_t(NULL, &ptr->flow_surface, float);
}

ECS_CTOR(WaterMeshState, ptr, {
    ecs_os_zeromem(ptr);
    ecs_vec_init_t(NULL, &ptr->flow_surface, float, 0);
})

ECS_MOVE(WaterMeshState, dst, src, {
    WaterMeshState_fini(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_COPY(WaterMeshState, dst, src, {
    WaterMeshState_fini(dst);
    *dst = *src;
    dst->flow_surface = ecs_vec_copy_t(
        NULL, &src->flow_surface, float);
})

ECS_DTOR(WaterMeshState, ptr, {
    WaterMeshState_fini(ptr);
})

static uint64_t biomeWaterHashBytes(
    uint64_t hash,
    const void *ptr,
    ecs_size_t size)
{
    const uint8_t *bytes = ptr;
    for (ecs_size_t i = 0; i < size; i ++) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

static uint64_t biomeWaterHash(
    const FlecsTerrain *terrain,
    const WeatherWaterTile *water,
    int32_t cell_count,
    const WaterConfig *config)
{
    uint64_t hash = 1469598103934665603ull;
    const float *heights = ecs_vec_first_t(&terrain->heights, float);
    hash = biomeWaterHashBytes(hash, heights,
        ecs_vec_count(&terrain->heights) * ECS_SIZEOF(float));
    for (int32_t i = 0; i < cell_count; i ++) {
        hash = biomeWaterHashBytes(
            hash, &water[i].water_amount, ECS_SIZEOF(float));
        hash = biomeWaterHashBytes(
            hash, &water[i].temperature, ECS_SIZEOF(float));
    }
    return biomeWaterHashBytes(
        hash, config, ECS_SIZEOF(WaterConfig));
}

static float biomeWaterTerrainCellHeight(
    const float *height,
    int32_t stride,
    int32_t x,
    int32_t z)
{
    int32_t i = z * stride + x;
    return 0.25f * (
        height[i] + height[i + 1] +
        height[i + stride] + height[i + stride + 1]);
}

static void biomeWaterNormal(
    const float *height,
    int32_t width,
    int32_t depth,
    int32_t x,
    int32_t z,
    flecs_vec3_t *result)
{
    int32_t stride = width + 1;
    int32_t left = x > 0 ? x - 1 : x;
    int32_t right = x < width ? x + 1 : x;
    int32_t down = z > 0 ? z - 1 : z;
    int32_t up = z < depth ? z + 1 : z;
    float dx = (float)(right - left) * TerrainCellSize;
    float dz = (float)(up - down) * TerrainCellSize;
    float nx = -(
        height[z * stride + right] -
        height[z * stride + left]) / dx;
    float nz = -(
        height[up * stride + x] -
        height[down * stride + x]) / dz;
    float length = sqrtf(nx * nx + 1.0f + nz * nz);
    *result = (flecs_vec3_t){nx / length, 1.0f / length, nz / length};
}

static void biomeWaterEnsureAsset(
    ecs_world_t *world,
    ecs_entity_t terrain,
    WaterMeshState *state)
{
    if (!state->asset) {
        state->asset = ecs_entity(world, {
            .name = "water_mesh",
            .parent = terrain,
            .add = ecs_ids(EcsPrefab)
        });
    }
}

static void biomeWaterEnsureInstance(
    ecs_world_t *world,
    ecs_entity_t terrain,
    WaterMeshState *state,
    ecs_entity_t shader)
{
    if (!state->instance) {
        state->instance = ecs_entity(world, {
            .name = "water",
            .parent = terrain
        });
        ecs_add_pair(world, state->instance, EcsIsA, state->asset);
        ecs_set(world, state->instance, FlecsPosition3, {0, 0, 0});
        ecs_set(world, state->instance, FlecsRgba, {30, 100, 170, 255});
        ecs_set(world, state->instance, FlecsPbrMaterial, {
            .metallic = 0,
            .roughness = 0.08f
        });
        ecs_set(world, state->instance, FlecsTransmission, {
            .transmission_factor = 0.82f,
            .ior = 1.333f,
            .thickness_factor = 0.5f,
            .attenuation_distance = 5.0f,
            .attenuation_color = {20, 100, 190, 255}
        });
        ecs_add_pair(
            world, state->instance, FlecsCustomShader, shader);
        state->shader = shader;
    } else if (state->shader != shader) {
        if (state->shader) {
            ecs_remove_pair(
                world, state->instance, FlecsCustomShader, state->shader);
        }
        ecs_add_pair(
            world, state->instance, FlecsCustomShader, shader);
        state->shader = shader;
    } else {
        ecs_modified(world, state->instance, FlecsPosition3);
    }
}

static void biomeWaterBuildMesh(
    ecs_world_t *world,
    ecs_entity_t terrain_entity,
    const FlecsTerrain *terrain,
    const WeatherWaterTile *water,
    const WaterConfig *config,
    WaterMeshState *state,
    ecs_entity_t shader)
{
    int32_t width = terrain->width;
    int32_t depth = terrain->depth;
    int32_t stride = width + 1;
    int32_t corner_count = stride * (depth + 1);
    float density = config->density > 0 ? config->density : 997.0f;
    float tile_area = (float)(TerrainCellSize * TerrainCellSize);
    float *surface = ecs_os_malloc_n(float, corner_count);
    float *shore_depth = ecs_os_malloc_n(float, corner_count);
    float *temperature = ecs_os_malloc_n(float, corner_count);
    const float *terrain_height = ecs_vec_first_t(
        &terrain->heights, float);

    for (int32_t z = 0; z <= depth; z ++) {
        for (int32_t x = 0; x <= width; x ++) {
            float surface_sum = 0;
            float temperature_sum = 0;
            float edge_depth = 1000000.0f;
            int32_t samples = 0;
            int32_t adjacent = 0;
            for (int32_t dz = -1; dz <= 0; dz ++) {
                int32_t cz = z + dz;
                if (cz < 0 || cz >= depth) {
                    continue;
                }
                for (int32_t dx = -1; dx <= 0; dx ++) {
                    int32_t cx = x + dx;
                    if (cx < 0 || cx >= width) {
                        continue;
                    }
                    float amount = water[cz * width + cx].water_amount;
                    float water_depth = amount > 0
                        ? amount / (density * tile_area)
                        : 0;
                    if (water_depth < edge_depth) {
                        edge_depth = water_depth;
                    }
                    adjacent ++;
                    if (water_depth > 0) {
                        surface_sum += biomeWaterTerrainCellHeight(
                            terrain_height, stride, cx, cz) +
                            water_depth;
                        temperature_sum +=
                            water[cz * width + cx].temperature;
                        samples ++;
                    }
                }
            }
            int32_t i = z * stride + x;
            surface[i] = samples
                ? surface_sum / (float)samples
                : terrain_height[z * stride + x];
            shore_depth[i] = adjacent ? edge_depth : 0;
            temperature[i] = samples
                ? temperature_sum / (float)samples
                : 0;
        }
    }

    int32_t wet_count = 0;
    for (int32_t i = 0; i < width * depth; i ++) {
        float amount = water[i].water_amount;
        if (amount > 0) {
            wet_count ++;
        }
    }

    biomeWaterEnsureAsset(world, terrain_entity, state);
    FlecsMesh3 *mesh = ecs_ensure(world, state->asset, FlecsMesh3);
    ecs_vec_set_count_t(
        NULL, &mesh->vertices, flecs_vec3_t, corner_count);
    ecs_vec_set_count_t(
        NULL, &mesh->normals, flecs_vec3_t, corner_count);
    ecs_vec_set_count_t(
        NULL, &mesh->uvs, flecs_vec2_t, corner_count);
    ecs_vec_set_count_t(
        NULL, &mesh->indices, uint32_t, wet_count * 6);
    ecs_vec_set_count_t(NULL, &mesh->colors, flecs_rgba_t, 0);

    flecs_vec3_t *vertices = ecs_vec_first_t(
        &mesh->vertices, flecs_vec3_t);
    flecs_vec3_t *normals = ecs_vec_first_t(
        &mesh->normals, flecs_vec3_t);
    flecs_vec2_t *uvs = ecs_vec_first_t(
        &mesh->uvs, flecs_vec2_t);
    uint32_t *indices = ecs_vec_first_t(
        &mesh->indices, uint32_t);

    for (int32_t z = 0; z <= depth; z ++) {
        for (int32_t x = 0; x <= width; x ++) {
            int32_t i = z * stride + x;
            vertices[i] = (flecs_vec3_t){
                (float)x * TerrainCellSize,
                surface[i],
                (float)z * TerrainCellSize
            };
            biomeWaterNormal(
                surface, width, depth, x, z, &normals[i]);
            uvs[i] = (flecs_vec2_t){
                shore_depth[i],
                temperature[i]
            };
        }
    }
    ecs_os_free(shore_depth);
    ecs_os_free(temperature);

    int32_t index = 0;
    for (int32_t z = 0; z < depth; z ++) {
        for (int32_t x = 0; x < width; x ++) {
            float amount = water[z * width + x].water_amount;
            if (amount <= 0) {
                continue;
            }
            uint32_t i00 = (uint32_t)(z * stride + x);
            uint32_t i10 = i00 + 1;
            uint32_t i01 = i00 + (uint32_t)stride;
            uint32_t i11 = i01 + 1;
            float c00 = terrain_height[i00];
            float c10 = terrain_height[i10];
            float c01 = terrain_height[i01];
            float c11 = terrain_height[i11];
            if (fabsf(c00 - c11) <= fabsf(c10 - c01)) {
                indices[index ++] = i00;
                indices[index ++] = i11;
                indices[index ++] = i10;
                indices[index ++] = i00;
                indices[index ++] = i01;
                indices[index ++] = i11;
            } else {
                indices[index ++] = i00;
                indices[index ++] = i01;
                indices[index ++] = i10;
                indices[index ++] = i10;
                indices[index ++] = i01;
                indices[index ++] = i11;
            }
        }
    }

    ecs_os_free(surface);
    ecs_modified(world, state->asset, FlecsMesh3);
    biomeWaterEnsureInstance(world, terrain_entity, state, shader);
}

static float biomeWaterFlowPotential(
    float surface_a,
    float surface_b,
    float capacity,
    float flow_rate)
{
    float difference = surface_a - surface_b;
    if (difference < 0) {
        difference = -difference;
    }
    return difference * capacity * 0.5f * flow_rate;
}

static void biomeWaterAccumulateOutflow(
    int32_t a,
    int32_t b,
    const float *surface,
    WeatherWaterTile *next,
    float capacity,
    float flow_rate,
    float min_flow)
{
    float difference = surface[a] - surface[b];
    if (difference == 0) {
        return;
    }
    int32_t source = difference > 0 ? a : b;
    float flow = biomeWaterFlowPotential(
        surface[a], surface[b], capacity, flow_rate);
    if (flow >= min_flow) {
        next[source].temperature += flow;
    }
}

static void biomeWaterApplyFlow(
    int32_t a,
    int32_t b,
    const float *surface,
    const WeatherWaterTile *water,
    WeatherWaterTile *next,
    float capacity,
    float flow_rate,
    float min_flow)
{
    float difference = surface[a] - surface[b];
    if (difference == 0) {
        return;
    }
    int32_t source = difference > 0 ? a : b;
    int32_t destination = difference > 0 ? b : a;
    float flow = biomeWaterFlowPotential(
        surface[a], surface[b], capacity, flow_rate);
    if (flow < min_flow) {
        return;
    }
    float requested = next[source].temperature;
    float available = water[source].water_amount;
    float scale = requested > available && requested > 0
        ? available / requested
        : 1.0f;
    flow *= scale;
    next[source].water_amount -= flow;
    next[destination].water_amount += flow;
}

static void WaterFlow(ecs_iter_t *it) {
    ecs_world_t *world = it->world;
    const FlecsTerrain *terrain = ecs_field(it, FlecsTerrain, 0);
    const WaterConfig *config = ecs_field(it, WaterConfig, 1);
    WeatherBuffers *buffers = ecs_field(it, WeatherBuffers, 2);
    WaterMeshState *state = ecs_field(it, WaterMeshState, 3);

    for (int32_t n = 0; n < it->count; n ++) {
        ecs_entity_t entity = it->entities[n];
        int32_t width = terrain[n].width;
        int32_t depth = terrain[n].depth;
        int32_t count = width * depth;
        if (width <= 0 || depth <= 0 ||
            ecs_vec_count(&terrain[n].heights) !=
                (width + 1) * (depth + 1))
        {
            continue;
        }

        WeatherWaterTile *water = flecsEngine_terrain_getLayer(
            world, entity, TerrainWaterIndex, WeatherWaterTile);
        if (!water) {
            continue;
        }

        ecs_vec_set_count_t(
            NULL, &buffers[n].water_buffer, WeatherWaterTile, count);
        ecs_vec_set_count_t(
            NULL, &state[n].flow_surface, float, count);
        WeatherWaterTile *next = ecs_vec_first_t(
            &buffers[n].water_buffer, WeatherWaterTile);
        float *surface = ecs_vec_first_t(
            &state[n].flow_surface, float);
        const float *height = ecs_vec_first_t(
            &terrain[n].heights, float);
        int32_t stride = width + 1;
        float density = config->density > 0
            ? config->density
            : 997.0f;
        float capacity = density *
            (float)(TerrainCellSize * TerrainCellSize);
        float flow_rate = config->flow_rate;
        if (flow_rate < 0) flow_rate = 0;
        if (flow_rate > 1) flow_rate = 1;
        float min_flow = config->min_flow > 0
            ? config->min_flow
            : 0;

        for (int32_t z = 0; z < depth; z ++) {
            for (int32_t x = 0; x < width; x ++) {
                int32_t i = z * width + x;
                float amount = water[i].water_amount;
                if (amount < 0) amount = 0;
                surface[i] = biomeWaterTerrainCellHeight(
                    height, stride, x, z) +
                    amount / capacity;
                next[i] = water[i];
                next[i].water_amount = amount;
                next[i].temperature = 0;
            }
        }

        for (int32_t z = 0; z < depth; z ++) {
            int32_t row = z * width;
            for (int32_t x = 0; x < width; x ++) {
                int32_t i = row + x;
                if (x + 1 < width) {
                    biomeWaterAccumulateOutflow(
                        i, i + 1, surface, next,
                        capacity, flow_rate, min_flow);
                }
                if (z + 1 < depth) {
                    biomeWaterAccumulateOutflow(
                        i, i + width, surface, next,
                        capacity, flow_rate, min_flow);
                }
            }
        }

        for (int32_t z = 0; z < depth; z ++) {
            int32_t row = z * width;
            for (int32_t x = 0; x < width; x ++) {
                int32_t i = row + x;
                if (x + 1 < width) {
                    biomeWaterApplyFlow(
                        i, i + 1, surface, water, next,
                        capacity, flow_rate, min_flow);
                }
                if (z + 1 < depth) {
                    biomeWaterApplyFlow(
                        i, i + width, surface, water, next,
                        capacity, flow_rate, min_flow);
                }
            }
        }

        for (int32_t i = 0; i < count; i ++) {
            water[i].water_amount = next[i].water_amount > 0
                ? next[i].water_amount
                : 0;
        }
    }
}

static void WaterUpdate(ecs_iter_t *it) {
    ecs_world_t *world = it->world;
    const FlecsTerrain *terrain = ecs_field(it, FlecsTerrain, 0);
    const WaterConfig *config = ecs_field(it, WaterConfig, 1);
    WaterMeshState *state = ecs_field(it, WaterMeshState, 2);
    const WaterRenderState *render = ecs_field(it, WaterRenderState, 3);

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t entity = it->entities[i];
        int32_t width;
        int32_t depth;
        flecsEngine_terrainLayerDimensions(
            &terrain[i], TerrainWaterIndex, &width, &depth);
        if (width != terrain[i].width || depth != terrain[i].depth) {
            continue;
        }
        const WeatherWaterTile *water = flecsEngine_terrain_getLayer(
            world, entity, TerrainWaterIndex, WeatherWaterTile);
        if (!water) {
            continue;
        }
        uint64_t hash = biomeWaterHash(
            &terrain[i], water, width * depth, config);
        if (hash == state[i].hash) {
            continue;
        }
        biomeWaterBuildMesh(
            world, entity, &terrain[i], water, config, &state[i],
            render->shader);
        state[i].hash = hash;
    }
}

void biomeWaterConfigureRenderer(
    ecs_world_t *world)
{
    const WaterRenderState *render = ecs_singleton_get(
        world, WaterRenderState);
    if (!render || !render->shader) {
        return;
    }
    ecs_iter_t it = ecs_each(world, FlecsRenderView);
    while (ecs_each_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            ecs_entity_t geometry = ecs_lookup_child(
                world, it.entities[i], "geometry");
            if (!geometry) {
                continue;
            }
            ecs_entity_t batch = ecs_lookup_child(
                world, geometry, "WaterShader");
            if (!batch) {
                batch = flecsEngine_createMeshShaderBatch(
                    world, geometry, "WaterShader",
                    render->shader, true);
            }
            flecsEngine_renderBatchSetAppend(
                world, geometry, batch);
        }
    }
}

void biomeWaterImport(ecs_world_t *world) {
    ECS_MODULE(world, biomeWater);

    ECS_IMPORT(world, biomeWeather);

    ecs_set_name_prefix(world, "Water");
    ECS_META_COMPONENT(world, WaterConfig);
    ECS_COMPONENT_DEFINE(world, WaterMeshState);
    ECS_COMPONENT_DEFINE(world, WaterRenderState);

    ecs_add_id(world, ecs_id(WaterConfig), EcsSingleton);
    ecs_add_id(world, ecs_id(WaterRenderState), EcsSingleton);
    ecs_singleton_set(world, WaterConfig, {
        .density = 997.0f,
        .flow_rate = 0.25f,
        .min_flow = 0.1f
    });
    ecs_add_pair(
        world, ecs_id(Terrain), EcsWith, ecs_id(WaterMeshState));

    ecs_entity_t shader = flecsEngine_createShader(
        world, ecs_id(biomeWater), "WaterShader",
        &(FlecsShader){
            .source = biomeWaterShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
    ecs_singleton_set(world, WaterRenderState, {
        .shader = shader
    });

    ecs_set_hooks(world, WaterMeshState, {
        .ctor = ecs_ctor(WaterMeshState),
        .move = ecs_move(WaterMeshState),
        .copy = ecs_copy(WaterMeshState),
        .dtor = ecs_dtor(WaterMeshState)
    });

    ECS_SYSTEM(world, WaterFlow, EcsPostUpdate,
        [in] FlecsTerrain,
        [in] WaterConfig,
        [inout] WeatherBuffers,
        [inout] WaterMeshState);

    ECS_SYSTEM(world, WaterUpdate, EcsPreStore,
        [in] FlecsTerrain,
        [in] WaterConfig,
        [inout] WaterMeshState,
        [in] WaterRenderState);

    ecs_set_interval(world, WaterFlow, 0.1);
    ecs_set_interval(world, WaterUpdate, 0.1);
}
