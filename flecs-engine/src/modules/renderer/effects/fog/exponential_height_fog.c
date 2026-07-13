#include "../../renderer.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsHeightFog);
ECS_COMPONENT_DECLARE(FlecsHeightFogImpl);

typedef struct FlecsHeightFogUniform {
    mat4 inv_vp;
    float camera_pos[4];
    float fog_color_density[4];
    float fog_params[4];
    float atmos_params[4];
} FlecsHeightFogUniform;

static const char *kShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "struct FogUniforms {\n"
    "  inv_vp : mat4x4<f32>,\n"
    "  camera_pos : vec4<f32>,\n"
    "  fog_color_density : vec4<f32>,\n"
    "  fog_params : vec4<f32>,\n"
    "  atmos_params : vec4<f32>,\n"
    "};\n"
    "@group(0) @binding(0) var input_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var input_sampler : sampler;\n"
    "@group(0) @binding(2) var depth_texture : texture_depth_2d;\n"
    "@group(0) @binding(3) var<uniform> uniforms : FogUniforms;\n"
    "@group(0) @binding(4) var skyview_lut : texture_2d<f32>;\n"
    "const FOG_PI : f32 = 3.14159265358979323846;\n"
    "fn fog_skyview_uv(dir : vec3<f32>, view_r : f32, planet_r : f32) -> vec2<f32> {\n"
    "  let horizon_cos = -sqrt(max(0.0, view_r * view_r - planet_r * planet_r))\n"
    "                    / max(view_r, 1e-4);\n"
    "  let horizon_ang = acos(clamp(horizon_cos, -1.0, 1.0));\n"
    "  let uu = atan2(dir.x, dir.z) / (2.0 * FOG_PI) + 0.5;\n"
    "  let zenith = acos(clamp(dir.y, -1.0, 1.0));\n"
    "  var vv = 0.0;\n"
    "  if (zenith < horizon_ang) {\n"
    "    let t = zenith / max(horizon_ang, 1e-6);\n"
    "    vv = 0.5 * sqrt(clamp(t, 0.0, 1.0));\n"
    "  } else {\n"
    "    let t = (zenith - horizon_ang) / max(FOG_PI - horizon_ang, 1e-6);\n"
    "    vv = 0.5 + 0.5 * sqrt(clamp(t, 0.0, 1.0));\n"
    "  }\n"
    "  return vec2<f32>(clamp(uu, 0.0, 1.0), clamp(vv, 0.0, 1.0));\n"
    "}\n"
    "fn reconstruct_world_pos(uv : vec2<f32>, depth : f32) -> vec3<f32> {\n"
    "  let ndc = vec4<f32>(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);\n"
    "  let h = uniforms.inv_vp * ndc;\n"
    "  if (abs(h.w) > 1e-6) { return h.xyz / h.w; }\n"
    "  return h.xyz;\n"
    "}\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let src = textureSample(input_texture, input_sampler, in.uv);\n"
    "  let density = max(uniforms.fog_color_density.w, 0.0);\n"
    "  if (density <= 0.0) { return src; }\n"
    "  let dims = textureDimensions(depth_texture);\n"
    "  let clamped_uv = clamp(in.uv, vec2<f32>(0.0), vec2<f32>(0.9999));\n"
    "  let texel = vec2<i32>(clamped_uv * vec2<f32>(f32(dims.x), f32(dims.y)));\n"
    "  let depth = textureLoad(depth_texture, texel, 0);\n"
    "  let is_sky = depth >= 0.9999;\n"
    "  let world_pos = reconstruct_world_pos(in.uv, depth);\n"
    "  let ray = world_pos - uniforms.camera_pos.xyz;\n"
    "  let distance = length(ray);\n"
    "  let falloff = max(uniforms.fog_params.x, 1e-6);\n"
    "  let base_height = uniforms.fog_params.y;\n"
    "  let max_opacity = clamp(uniforms.fog_params.z, 0.0, 1.0);\n"
    "  let camera_height = uniforms.camera_pos.y - base_height;\n"
    "  let e0 = exp(clamp(-falloff * camera_height, -80.0, 80.0));\n"
    "  var integral : f32;\n"
    "  if (is_sky) {\n"
    "    let view_dir_y = ray.y / max(distance, 1e-6);\n"
    "    if (view_dir_y > 1e-4) {\n"
    "      integral = e0 / (falloff * view_dir_y);\n"
    "    } else {\n"
    "      integral = 1e6;\n"
    "    }\n"
    "  } else {\n"
    "    let world_height = world_pos.y - base_height;\n"
    "    let e1 = exp(clamp(-falloff * world_height, -80.0, 80.0));\n"
    "    let dy = world_pos.y - uniforms.camera_pos.y;\n"
    "    if (abs(dy) > 1e-5) {\n"
    "      integral = distance * (e0 - e1) / (falloff * dy);\n"
    "    } else {\n"
    "      integral = distance * e0;\n"
    "    }\n"
    "  }\n"
    "  let optical_depth = max(0.0, density * integral);\n"
    "  let fog_factor = min(max_opacity, 1.0 - exp(clamp(-optical_depth, -80.0, 0.0)));\n"
    "  var fog_color = uniforms.fog_color_density.rgb;\n"
    "  if (uniforms.fog_params.w > 0.5) {\n"
    "    let view_dir = ray / max(distance, 1e-6);\n"
    "    let planet_r = uniforms.atmos_params.x;\n"
    "    let view_r = planet_r + max(uniforms.atmos_params.y, 0.001);\n"
    "    let xz_len = max(sqrt(view_dir.x * view_dir.x + view_dir.z * view_dir.z), 1e-6);\n"
    "    let sample_dir = vec3<f32>(view_dir.x / xz_len, 0.0, view_dir.z / xz_len);\n"
    "    let sv_uv = fog_skyview_uv(sample_dir, view_r, planet_r);\n"
    "    fog_color = textureSampleLevel(skyview_lut, input_sampler, sv_uv, 0.0).rgb;\n"
    "  }\n"
    "  let fogged = mix(src.rgb, fog_color, fog_factor);\n"
    "  return vec4<f32>(fogged, src.a);\n"
    "}\n";

static ecs_entity_t flecsEngine_heightFog_shader(
    ecs_world_t *world)
{
    return flecsEngine_shader_ensure(world, "HeightFogShader",
        &(FlecsShader){
            .source = kShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}

static void flecsEngine_heightFog_releaseResources(
    FlecsHeightFogImpl *impl)
{
    FLECS_WGPU_RELEASE(impl->uniform_buffer, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(impl->fallback_view, wgpuTextureViewRelease);
    FLECS_WGPU_RELEASE(impl->fallback_texture, wgpuTextureRelease);
}

ECS_DTOR(FlecsHeightFogImpl, ptr, {
    flecsEngine_heightFog_releaseResources(ptr);
})

ECS_MOVE(FlecsHeightFogImpl, dst, src, {
    flecsEngine_heightFog_releaseResources(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

static void flecsEngine_heightFog_fillUniform(
    const ecs_world_t *world,
    const FlecsRenderViewImpl *view_impl,
    const FlecsHeightFog *fog,
    FlecsHeightFogUniform *uniform)
{
    glm_mat4_identity(uniform->inv_vp);

    uniform->camera_pos[0] = 0.0f;
    uniform->camera_pos[1] = 0.0f;
    uniform->camera_pos[2] = 0.0f;
    uniform->camera_pos[3] = 1.0f;

    uniform->fog_color_density[0] = flecsEngine_colorChannelToFloat(fog->color.r);
    uniform->fog_color_density[1] = flecsEngine_colorChannelToFloat(fog->color.g);
    uniform->fog_color_density[2] = flecsEngine_colorChannelToFloat(fog->color.b);
    uniform->fog_color_density[3] = fog->density;
    uniform->fog_params[0] = fog->falloff;
    uniform->fog_params[1] = fog->base_height;
    uniform->fog_params[2] = fog->max_opacity;
    uniform->fog_params[3] = 0.0f;   /* use_atmosphere flag; set below */

    uniform->atmos_params[0] = 0.0f;
    uniform->atmos_params[1] = 0.0f;
    uniform->atmos_params[2] = 0.0f;
    uniform->atmos_params[3] = 0.0f;

    if (!view_impl || !view_impl->camera_view_proj_valid) return;

    mat4 mvp;
    glm_mat4_copy((vec4*)view_impl->camera_view_proj, mvp);
    glm_mat4_inv(mvp, uniform->inv_vp);

    uniform->camera_pos[0] = view_impl->camera_pos[0];
    uniform->camera_pos[1] = view_impl->camera_pos[1];
    uniform->camera_pos[2] = view_impl->camera_pos[2];

    if (fog->atmosphere) {
        const FlecsAtmosphere *atm = ecs_get(
            world, fog->atmosphere, FlecsAtmosphere);
        const FlecsAtmosphereImpl *atm_impl = ecs_get(
            world, fog->atmosphere, FlecsAtmosphereImpl);
        if (atm && atm_impl && atm_impl->skyview_view) {
            float planet_r = atm->planet_radius_km > 0.0f
                ? atm->planet_radius_km : 6360.0f;
            float units_per_km = atm->world_units_per_km > 1e-6f
                ? atm->world_units_per_km : 1000.0f;
            float alt_km = atm->ground_altitude_km +
                (uniform->camera_pos[1] - atm->sea_level_y) / units_per_km;
            if (alt_km < 0.001f) alt_km = 0.001f;
            uniform->atmos_params[0] = planet_r;
            uniform->atmos_params[1] = alt_km;
            uniform->fog_params[3] = 1.0f;
        }
    }
}

static bool flecsEngine_heightFog_setup(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t effect_entity,
    const FlecsRenderEffectKind *kind,
    FlecsRenderEffectImpl *effect_impl,
    WGPUBindGroupLayoutEntry *layout_entries,
    uint32_t *entry_count)
{
    (void)kind;
    (void)effect_impl;

    ecs_assert(layout_entries != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(entry_count != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(*entry_count == 2, ECS_INVALID_PARAMETER, NULL);

    FlecsHeightFogImpl fog_impl = {0};

    fog_impl.uniform_buffer = flecsEngine_createUniformBuffer(
        engine->device, sizeof(FlecsHeightFogUniform));
    if (!fog_impl.uniform_buffer) {
        return false;
    }

    WGPUTextureDescriptor fallback_desc = {
        .usage = WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { 1, 1, 1 },
        .format = WGPUTextureFormat_RGBA8Unorm,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    fog_impl.fallback_texture = wgpuDeviceCreateTexture(
        engine->device, &fallback_desc);
    if (!fog_impl.fallback_texture) {
        wgpuBufferRelease(fog_impl.uniform_buffer);
        return false;
    }
    WGPUTextureViewDescriptor fallback_view_desc = {
        .format = WGPUTextureFormat_RGBA8Unorm,
        .dimension = WGPUTextureViewDimension_2D,
        .mipLevelCount = 1,
        .arrayLayerCount = 1
    };
    fog_impl.fallback_view = wgpuTextureCreateView(
        fog_impl.fallback_texture, &fallback_view_desc);

    layout_entries[2] = (WGPUBindGroupLayoutEntry){
        .binding = 2,
        .visibility = WGPUShaderStage_Fragment,
        .texture = {
            .sampleType = WGPUTextureSampleType_Depth,
            .viewDimension = WGPUTextureViewDimension_2D,
            .multisampled = false
        }
    };

    layout_entries[3] = (WGPUBindGroupLayoutEntry){
        .binding = 3,
        .visibility = WGPUShaderStage_Fragment,
        .buffer = {
            .type = WGPUBufferBindingType_Uniform,
            .minBindingSize = sizeof(FlecsHeightFogUniform)
        }
    };

    layout_entries[4] = (WGPUBindGroupLayoutEntry){
        .binding = 4,
        .visibility = WGPUShaderStage_Fragment,
        .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_2D,
            .multisampled = false
        }
    };

    FlecsHeightFogImpl *old = ecs_get_mut(
        (ecs_world_t*)world, effect_entity, FlecsHeightFogImpl);
    if (old) {
        flecsEngine_heightFog_releaseResources(old);
        ecs_os_zeromem(old);
    }
    ecs_set_ptr((ecs_world_t*)world, effect_entity, FlecsHeightFogImpl, &fog_impl);

    *entry_count = 5;
    return true;
}

static bool flecsEngine_heightFog_bind(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    ecs_entity_t effect_entity,
    const FlecsRenderEffectKind *kind,
    const FlecsRenderEffectImpl *impl,
    WGPUBindGroupEntry *entries,
    uint32_t *entry_count)
{
    (void)kind;
    (void)impl;

    ecs_assert(entries != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(entry_count != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(*entry_count == 2, ECS_INVALID_PARAMETER, NULL);

    if (!view_impl || !view_impl->depth_texture_view) {
        return false;
    }

    const FlecsHeightFogImpl *fog_impl = ecs_get(
        world, effect_entity, FlecsHeightFogImpl);
    if (!fog_impl || !fog_impl->uniform_buffer) {
        return false;
    }

    const FlecsHeightFog *fog = ecs_get(world, effect_entity, FlecsHeightFog);
    if (!fog) {
        return false;
    }

    FlecsHeightFogUniform uniform = {0};
    flecsEngine_heightFog_fillUniform(world, view_impl, fog, &uniform);
    wgpuQueueWriteBuffer(
        engine->queue,
        fog_impl->uniform_buffer,
        0,
        &uniform,
        sizeof(uniform));

    entries[2] = (WGPUBindGroupEntry){
        .binding = 2,
        .textureView = view_impl->depth_texture_view
    };

    entries[3] = (WGPUBindGroupEntry){
        .binding = 3,
        .buffer = fog_impl->uniform_buffer,
        .offset = 0,
        .size = sizeof(FlecsHeightFogUniform)
    };

    WGPUTextureView skyview = fog_impl->fallback_view;
    if (fog->atmosphere) {
        const FlecsAtmosphereImpl *atm_impl = ecs_get(
            world, fog->atmosphere, FlecsAtmosphereImpl);
        if (atm_impl && atm_impl->skyview_view) {
            skyview = atm_impl->skyview_view;
        }
    }
    entries[4] = (WGPUBindGroupEntry){
        .binding = 4,
        .textureView = skyview
    };

    *entry_count = 5;
    return true;
}

ECS_CTOR(FlecsHeightFog, ptr, {
    ptr->density = 0.1f;
    ptr->falloff = 0.3f;
    ptr->base_height = 0.0f;
    ptr->max_opacity = 1.0f;
    ptr->color = (flecs_rgba_t){191, 158, 140, 255};
    ptr->atmosphere = 0;
})

static void FlecsHeightFog_on_set(
    ecs_iter_t *it)
{
    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        ecs_set(it->world, e, FlecsRenderEffectKind, {
            .shader = flecsEngine_heightFog_shader(it->world),
            .setup_callback = flecsEngine_heightFog_setup,
            .bind_callback = flecsEngine_heightFog_bind
        });
    }
}

void flecsEngine_heightFog_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsHeightFog);
    ECS_COMPONENT_DEFINE(world, FlecsHeightFogImpl);

    ecs_set_hooks(world, FlecsHeightFog, {
        .ctor = ecs_ctor(FlecsHeightFog)
    });

    ecs_set_hooks(world, FlecsHeightFogImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsHeightFogImpl),
        .dtor = ecs_dtor(FlecsHeightFogImpl)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsHeightFog),
        .members = {
            { .name = "density", .type = ecs_id(ecs_f32_t) },
            { .name = "falloff", .type = ecs_id(ecs_f32_t) },
            { .name = "base_height", .type = ecs_id(ecs_f32_t) },
            { .name = "max_opacity", .type = ecs_id(ecs_f32_t) },
            { .name = "color", .type = ecs_id(flecs_rgba_t) },
            { .name = "atmosphere", .type = ecs_id(ecs_entity_t) }
        }
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(FlecsHeightFog) }},
        .events = { EcsOnSet },
        .callback = FlecsHeightFog_on_set
    });
}
