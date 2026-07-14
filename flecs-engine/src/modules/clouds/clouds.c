#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../renderer/renderer.h"
#include "../renderer/shaders/common/noise_wgsl.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsClouds);

/* Decoupled from clouds->appearance.density so overcast (high coverage, low density)
 * still casts full shadows. */
#define FLECS_CLOUDS_SHADOW_OD_SCALE 4.0f

typedef struct FlecsCloudsImpl {
    WGPUBuffer uniform_buffer;
    WGPUTexture weather_texture;
    WGPUTextureView weather_view;
    WGPUTexture noise_texture;
    WGPUTextureView noise_view;
    WGPUSampler repeat_sampler;
    /* Double for long-session precision; wrapped mod tile size before upload. */
    double wind_offset_x;
    double wind_offset_z;
    uint32_t frame_counter;
    /* Hillaire 2016 §5.9 baked cloud shadow. R8 transmittance per ground
     * point, sun-direction projection baked in so PBR samples by world XZ. */
    WGPUTexture shadow_texture;
    WGPUTextureView shadow_view;
    uint32_t shadow_size;
    WGPUBuffer shadow_uniform_buffer;
    WGPUBindGroupLayout shadow_bake_layout;
    WGPURenderPipeline shadow_bake_pipeline;
    WGPUBindGroup shadow_bake_bind_group;
    float shadow_origin_x;
    float shadow_origin_z;
    float shadow_inv_footprint;
    /* Low-res cloud target used when render_scale > 1. Upsample pass keeps
     * foreground pixels full-res via depth check. */
    WGPUTexture lowres_texture;
    WGPUTextureView lowres_view;
    uint32_t lowres_width;
    uint32_t lowres_height;
    WGPUTextureFormat lowres_format;
    WGPUBindGroupLayout upsample_layout;
    WGPURenderPipeline upsample_pipeline;
    WGPUBindGroup upsample_bind_group;
    WGPUTextureView upsample_bind_input_view;
    WGPUTextureView upsample_bind_depth_view;
    WGPUTextureView upsample_bind_cloud_view;
    WGPUTextureFormat upsample_pipeline_format;
    WGPUSampler upsample_sampler;
} FlecsCloudsImpl;

ECS_COMPONENT_DECLARE(FlecsCloudsImpl);

typedef struct FlecsCloudsUniform {
    mat4 inv_vp;
    float camera_pos[4];
    float sun_dir[4];        /* xyz dir, w intensity */
    float sun_color[4];
    float params0[4];        /* low_y, high_y, density_scale, jitter_seed */
    float params1[4];        /* coverage_bias, weather_inv_scale, noise_inv_scale, cloud_type_bias */
    float params2[4];        /* weather_uv_x, weather_uv_z, ambient_intensity, max_dist */
    float params3[4];        /* noise_uv_x, noise_uv_z, detail_strength, anisotropy */
    float params4[4];        /* powder_strength, multi_scatter, march_steps, light_steps */
    float ambient_top[4];
    float ambient_bottom[4];
} FlecsCloudsUniform;

#define FLECS_CLOUDS_WEATHER_SIZE 256u
#define FLECS_CLOUDS_NOISE_SIZE 64u
#define FLECS_CLOUDS_SHADOW_FORMAT WGPUTextureFormat_R8Unorm

typedef struct FlecsShadowBakeUniform {
    float sun_dir[4];        /* xyz, w unused; sun.y must be > 0 to bake */
    float params[4];         /* origin_x, origin_z, footprint, slab_low_y */
    float params2[4];        /* slab_high_y, weather_inv_scale, _, _ */
    float params3[4];        /* weather_uv_x, weather_uv_z, coverage_bias, density_scale */
} FlecsShadowBakeUniform;

static void flecs_clouds_publishShadow(
    FlecsEngineImpl *engine,
    WGPUTextureView view,
    float origin_x,
    float origin_z,
    float inv_footprint,
    float strength)
{
    bool view_changed = engine->clouds.shadow_source_view != view;
    engine->clouds.shadow_source_view = view;
    engine->clouds.origin_x = origin_x;
    engine->clouds.origin_z = origin_z;
    engine->clouds.strength = strength;
    engine->clouds.inv_footprint = inv_footprint;
    if (view_changed) {
        engine->clouds.version++;
        engine->scene_bind_version++;
    }
}

static const char *kShadowBakeShader =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "struct ShadowBakeUniforms {\n"
    "  sun_dir : vec4<f32>,\n"
    "  params : vec4<f32>,\n"
    "  params2 : vec4<f32>,\n"
    "  params3 : vec4<f32>,\n"
    "};\n"
    "@group(0) @binding(0) var<uniform> u : ShadowBakeUniforms;\n"
    "@group(0) @binding(1) var weather_texture : texture_2d<f32>;\n"
    "@group(0) @binding(2) var repeat_sampler : sampler;\n"
    FLECS_ENGINE_SHADER_COMMON_NOISE_WGSL
    /* height_density evaluated at slab midpoint (h = 0.5). */
    "fn h_density_mid(cloud_type : f32) -> f32 {\n"
    "  let stratus = saturate(remap(0.5, 0.0, 0.10, 0.0, 1.0))\n"
    "              * saturate(remap(0.5, 0.20, 0.30, 1.0, 0.0));\n"
    "  let cumulus = saturate(remap(0.5, 0.05, 0.20, 0.0, 1.0))\n"
    "              * saturate(remap(0.5, 0.55, 0.85, 1.0, 0.0));\n"
    "  let cnb     = saturate(remap(0.5, 0.05, 0.30, 0.0, 1.0))\n"
    "              * saturate(remap(0.5, 0.80, 1.00, 1.0, 0.0));\n"
    "  let t = saturate(cloud_type);\n"
    "  let a = mix(stratus, cumulus, saturate(t * 2.0));\n"
    "  return mix(a, cnb, saturate(t * 2.0 - 1.0));\n"
    "}\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  if (u.sun_dir.y < 0.05) { return vec4<f32>(1.0, 0.0, 0.0, 1.0); }\n"
    "  let footprint = u.params.z;\n"
    "  let ground_xz = vec2<f32>(u.params.x, u.params.y) + in.uv * footprint;\n"
    /* Project ground point along the sun ray to slab midpoint — this is the
     * cloud column whose shadow falls on this ground point. */
    "  let slab_low = u.params.w;\n"
    "  let slab_high = u.params2.x;\n"
    "  let h_mid = (slab_low + slab_high) * 0.5;\n"
    "  let t = h_mid / max(u.sun_dir.y, 1e-3);\n"
    "  let cloud_xz = ground_xz + u.sun_dir.xz * t;\n"
    "  let weather_uv_offset = vec2<f32>(u.params3.x, u.params3.y);\n"
    "  let w_uv = cloud_xz * u.params2.y - weather_uv_offset;\n"
    "  let weather = textureSampleLevel(weather_texture, repeat_sampler, w_uv, 0.0);\n"
    "  let coverage = saturate(weather.r + u.params3.z);\n"
    "  if (coverage < 0.001) { return vec4<f32>(1.0, 0.0, 0.0, 1.0); }\n"
    /* Coverage without noise erosion: overcast (coverage ≈ 1) should cast
     * uniform full shadow; noise erosion would leave sunlight pinholes. */
    "  let d = coverage;\n"
    "  let od = d * u.params3.w;\n"
    "  let trans = exp(-od);\n"
    "  return vec4<f32>(trans, 0.0, 0.0, 1.0);\n"
    "}\n";

static const char *kCloudsShader =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "struct CloudsUniforms {\n"
    "  inv_vp : mat4x4<f32>,\n"
    "  camera_pos : vec4<f32>,\n"
    "  sun_dir : vec4<f32>,\n"
    "  sun_color : vec4<f32>,\n"
    "  params0 : vec4<f32>,\n"
    "  params1 : vec4<f32>,\n"
    "  params2 : vec4<f32>,\n"
    "  params3 : vec4<f32>,\n"
    "  params4 : vec4<f32>,\n"
    "  ambient_top : vec4<f32>,\n"
    "  ambient_bottom : vec4<f32>,\n"
    "};\n"
    "@group(0) @binding(0) var input_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var input_sampler : sampler;\n"
    "@group(0) @binding(2) var depth_texture : texture_depth_2d;\n"
    "@group(0) @binding(3) var<uniform> u : CloudsUniforms;\n"
    "@group(0) @binding(4) var weather_texture : texture_2d<f32>;\n"
    "@group(0) @binding(5) var noise_texture : texture_3d<f32>;\n"
    "@group(0) @binding(6) var repeat_sampler : sampler;\n"
    "const MAX_STEPS : i32 = 256;\n"
    "const MAX_LIGHT_STEPS : i32 = 16;\n"
    "fn reconstruct_world_pos(uv : vec2<f32>, depth : f32) -> vec3<f32> {\n"
    "  let ndc = vec4<f32>(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);\n"
    "  let h = u.inv_vp * ndc;\n"
    "  if (abs(h.w) > 1e-6) { return h.xyz / h.w; }\n"
    "  return h.xyz;\n"
    "}\n"
    FLECS_ENGINE_SHADER_COMMON_NOISE_WGSL
    /* Hard-coded type-density gradients (Schneider). x = stratus, y = cumulus,
     * z = cumulonimbus. h is normalized slab height in [0,1]. */
    "fn height_density(h : f32, cloud_type : f32) -> f32 {\n"
    "  let stratus = saturate(remap(h, 0.0, 0.10, 0.0, 1.0))\n"
    "              * saturate(remap(h, 0.20, 0.30, 1.0, 0.0));\n"
    "  let cumulus = saturate(remap(h, 0.05, 0.20, 0.0, 1.0))\n"
    "              * saturate(remap(h, 0.55, 0.85, 1.0, 0.0));\n"
    "  let cnb     = saturate(remap(h, 0.05, 0.30, 0.0, 1.0))\n"
    "              * saturate(remap(h, 0.80, 1.00, 1.0, 0.0));\n"
    "  let t = saturate(cloud_type);\n"
    "  let a = mix(stratus, cumulus, saturate(t * 2.0));\n"
    "  return mix(a, cnb, saturate(t * 2.0 - 1.0));\n"
    "}\n"
    "fn sample_density(p : vec3<f32>, low_y : f32, high_y : f32) -> vec2<f32> {\n"
    /* returns (density, cloud_type) */
    "  let h = saturate((p.y - low_y) / max(high_y - low_y, 1e-3));\n"
    "  let w_uv = p.xz * u.params1.y - vec2<f32>(u.params2.x, u.params2.y);\n"
    "  let n_coord = vec3<f32>(\n"
    "    p.x * u.params1.z - u.params3.x,\n"
    "    p.y * u.params1.z,\n"
    "    p.z * u.params1.z - u.params3.y);\n"
    "  let weather = textureSampleLevel(weather_texture, repeat_sampler, w_uv, 0.0);\n"
    "  let noise = textureSampleLevel(noise_texture, repeat_sampler, n_coord, 0.0);\n"
    "  let coverage = saturate(weather.r + u.params1.x);\n"
    "  if (coverage <= 0.001) { return vec2<f32>(0.0, weather.b); }\n"
    "  let cloud_type = saturate(weather.b + u.params1.w);\n"
    "  let hgrad = height_density(h, cloud_type);\n"
    /* Combine octaves: r = base low-freq, gba contribute as detail erosion. */
    "  let base = noise.r;\n"
    "  let detail = noise.g * 0.625 + noise.b * 0.25 + noise.a * 0.125;\n"
    "  var d = base * hgrad;\n"
    /* Schneider coverage remap (density threshold → silhouette). */
    "  d = saturate(remap(d, 1.0 - coverage, 1.0, 0.0, 1.0)) * coverage;\n"
    "  let erode_mask = saturate(remap(h, 0.0, 0.4, 1.0, 0.0)) * 0.5 + 0.5;\n"
    "  d = saturate(d - (1.0 - detail) * u.params3.z * erode_mask);\n"
    "  return vec2<f32>(d, cloud_type);\n"
    "}\n"
    "fn hg(cos_t : f32, g : f32) -> f32 {\n"
    "  let g2 = g * g;\n"
    "  let denom = 1.0 + g2 - 2.0 * g * cos_t;\n"
    "  return (1.0 - g2) / (12.566370614 * pow(max(denom, 1e-4), 1.5));\n"
    "}\n"
    /* Dual-lobe HG, Hillaire 2016 §5.7. `scale` is Wrenninge c^n. `aniso` is
     * the forward-lobe g; back lobe locked at -0.25 * aniso to preserve the
     * Schneider 0.8 / -0.2 ratio. */
    "fn dual_hg(cos_t : f32, scale : f32, aniso : f32) -> f32 {\n"
    "  let g0 = aniso * scale;\n"
    "  let g1 = -0.25 * aniso * scale;\n"
    "  return mix(hg(cos_t, g0), hg(cos_t, g1), 0.5);\n"
    "}\n"
    /* Schneider 2015 §57 cone-sampled light march: short steps toward the
     * sun with widening cone offsets + one long-distance sample. */
    "fn light_march(p_in : vec3<f32>, low_y : f32, high_y : f32) -> f32 {\n"
    "  var od : f32 = 0.0;\n"
    "  var p = p_in;\n"
    "  let step : f32 = 120.0;\n"
    "  let cone_r : f32 = 30.0;\n"
    "  let light_steps = max(1, i32(u.params4.w));\n"
    "  for (var i : i32 = 0; i < MAX_LIGHT_STEPS; i = i + 1) {\n"
    "    if (i >= light_steps) { break; }\n"
    "    let fi = f32(i);\n"
    "    let h1 = fract(sin(fi * 12.9898 + 4.1414) * 43758.5453) * 2.0 - 1.0;\n"
    "    let h2 = fract(sin(fi * 78.233 + 27.182) * 43758.5453) * 2.0 - 1.0;\n"
    "    let h3 = fract(sin(fi * 37.719 + 3.14159) * 43758.5453) * 2.0 - 1.0;\n"
    "    let cone = vec3<f32>(h1, h2, h3) * cone_r * fi;\n"
    "    p = p + u.sun_dir.xyz * step + cone;\n"
    "    if (p.y > high_y || p.y < low_y) { break; }\n"
    "    let s = sample_density(p, low_y, high_y);\n"
    "    od = od + s.x * step;\n"
    "  }\n"
    "  let far_p = p_in + u.sun_dir.xyz * 2000.0;\n"
    "  if (far_p.y < high_y && far_p.y > low_y) {\n"
    "    let s = sample_density(far_p, low_y, high_y);\n"
    "    od = od + s.x * 500.0;\n"
    "  }\n"
    "  return od;\n"
    "}\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let src = textureSample(input_texture, input_sampler, in.uv);\n"
    "  let dims = textureDimensions(depth_texture);\n"
    "  let dims_f = vec2<f32>(f32(dims.x), f32(dims.y));\n"
    "  let clamped_uv = clamp(in.uv, vec2<f32>(0.0), vec2<f32>(0.9999));\n"
    "  let texel = vec2<i32>(clamped_uv * dims_f);\n"
    "  let depth = textureLoad(depth_texture, texel, 0);\n"
    "  if (depth < 0.9999) { return src; }\n"
    "  let world_pos = reconstruct_world_pos(in.uv, 1.0);\n"
    "  let cam = u.camera_pos.xyz;\n"
    "  let d = normalize(world_pos - cam);\n"
    "  let low_y = u.params0.x;\n"
    "  let high_y = u.params0.y;\n"
    "  let max_dist = u.params2.w;\n"
    /* Sphere-slab intersect on a virtual planet so clouds curve below the
     * horizon. Planet center at (cam.x, -planet_r, cam.z). */
    "  let planet_r : f32 = 6360000.0;\n"
    "  let pc = vec3<f32>(0.0, planet_r + cam.y, 0.0);\n"
    "  let bdotd = pc.y * d.y;\n"
    "  let pc2 = pc.y * pc.y;\n"
    "  let r_low = planet_r + low_y;\n"
    "  let r_high = planet_r + high_y;\n"
    "  let disc_low = bdotd * bdotd - pc2 + r_low * r_low;\n"
    "  let disc_high = bdotd * bdotd - pc2 + r_high * r_high;\n"
    "  if (disc_high < 0.0) { return src; }\n"
    "  let sqrt_high = sqrt(disc_high);\n"
    "  var t_enter : f32 = 0.0;\n"
    "  var t_exit : f32 = -bdotd + sqrt_high;\n"
    /* Camera below / above / inside the slab — three cases. */
    "  if (cam.y < low_y) {\n"
    "    if (disc_low < 0.0 || d.y <= 0.0) { return src; }\n"
    "    t_enter = -bdotd + sqrt(disc_low);\n"
    "  } else if (cam.y > high_y) {\n"
    "    if (d.y >= 0.0) { return src; }\n"
    "    t_enter = -bdotd - sqrt_high;\n"
    "    if (disc_low >= 0.0) {\n"
    "      t_exit = -bdotd - sqrt(disc_low);\n"
    "    }\n"
    "  } else {\n"
    "    if (d.y > 0.0) {\n"
    "      t_exit = -bdotd + sqrt_high;\n"
    "    } else if (disc_low >= 0.0) {\n"
    "      t_exit = -bdotd - sqrt(disc_low);\n"
    "    }\n"
    "  }\n"
    "  t_exit = min(t_exit, max_dist);\n"
    "  if (t_exit <= t_enter + 1.0) { return src; }\n"
    "  let horizon_fade = smoothstep(-0.02, 0.05, d.y);\n"
    "  let extinction = max(u.params0.z, 1e-4);\n"
    /* march_steps directly sets the sample count along the ray. Lower values
     * trade horizon banding for perf; raise it for cleaner long-distance views. */
    "  let march_steps = max(1.0, u.params4.z);\n"
    "  let dt = (t_exit - t_enter) / march_steps;\n"
    "  let cos_sun = dot(d, u.sun_dir.xyz);\n"
    /* Wrenninge multi-scattering octaves, Hillaire 2016 §5.8. a = in-scatter
     * attenuation, b = extinction attenuation (a <= b for energy conservation),
     * c = phase widening. */
    "  let MS_OCT : i32 = 3;\n"
    "  let MS_A : f32 = u.params4.y;\n"
    "  let MS_B : f32 = 0.6;\n"
    "  let MS_C : f32 = 0.5;\n"
    "  let aniso = u.params3.w;\n"
    "  var ms_phase : array<f32, 3>;\n"
    "  var ms_a_pow : array<f32, 3>;\n"
    "  var ms_b_pow : array<f32, 3>;\n"
    "  for (var k : i32 = 0; k < MS_OCT; k = k + 1) {\n"
    "    let kf = f32(k);\n"
    "    ms_phase[k] = dual_hg(cos_sun, pow(MS_C, kf), aniso);\n"
    "    ms_a_pow[k] = pow(MS_A, kf);\n"
    "    ms_b_pow[k] = pow(MS_B, kf);\n"
    "  }\n"
    "  var transmittance : f32 = 1.0;\n"
    "  var scattered : vec3<f32> = vec3<f32>(0.0);\n"
    /* Per-pixel per-frame jitter to break step banding. */
    "  let seed = u.params0.w;\n"
    "  let jitter = fract(sin(dot(in.uv * dims_f, vec2<f32>(12.9898, 78.233)) + seed * 1.2345) * 43758.5453);\n"
    "  var t = t_enter + dt * jitter;\n"
    "  for (var i : i32 = 0; i < MAX_STEPS; i = i + 1) {\n"
    "    if (transmittance < 0.01 || t >= t_exit) { break; }\n"
    "    let p = cam + d * t;\n"
    "    let s = sample_density(p, low_y, high_y);\n"
    "    let dens = s.x;\n"
    "    if (dens > 0.001) {\n"
    "      let sigma_t = dens * extinction;\n"
    "      let light_od = light_march(p, low_y, high_y) * extinction;\n"
    "      let sun_t = exp(-light_od);\n"
    /* Powder term, Schneider 2015 §57; floor at 0.5 so thin clouds don't
     * darken to zero. */
    "      let powder = mix(1.0, 1.0 - exp(-light_od * 2.0), u.params4.x);\n"
    "      let h_norm = saturate((p.y - low_y) / max(high_y - low_y, 1e-3));\n"
    "      let ambient = mix(u.ambient_bottom.rgb, u.ambient_top.rgb, h_norm)\n"
    "                  * u.params2.z;\n"
    /* Hillaire 2016 Eq. 17 with albedo ≈ 1 (sigma_s ≈ sigma_t) collapses to
     * Lscat * (1 - exp(-sigma_t*dt)). Sum MS_OCT octaves for multi-scatter. */
    "      var Lscat = vec3<f32>(0.0);\n"
    "      for (var k : i32 = 0; k < MS_OCT; k = k + 1) {\n"
    "        let sun_t_k = pow(sun_t, ms_b_pow[k]);\n"
    "        let sun_term = u.sun_color.rgb * sun_t_k * ms_phase[k];\n"
    "        Lscat = Lscat + (sun_term + ambient) * ms_a_pow[k];\n"
    "      }\n"
    "      Lscat = Lscat * powder;\n"
    "      let trans_step = exp(-sigma_t * dt);\n"
    "      let integ = Lscat * (1.0 - trans_step);\n"
    "      scattered = scattered + transmittance * integ;\n"
    "      transmittance = transmittance * trans_step;\n"
    "    }\n"
    "    t = t + dt;\n"
    "  }\n"
    "  let alpha = (1.0 - transmittance) * horizon_fade;\n"
    "  let composed = mix(src.rgb, src.rgb * transmittance + scattered, horizon_fade);\n"
    "  return vec4<f32>(composed, src.a + alpha * (1.0 - src.a));\n"
    "}\n";

static ecs_entity_t flecsEngine_clouds_shader(
    ecs_world_t *world)
{
    return flecsEngine_shader_ensure(world, "CloudsShader",
        &(FlecsShader){
            .source = kCloudsShader,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}

/* Upsample composite: full-res for foreground (geometry stays sharp),
 * bilinear-upsampled low-res cloud for sky pixels. */
static const char *kCloudUpsampleShader =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "@group(0) @binding(0) var input_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var input_sampler : sampler;\n"
    "@group(0) @binding(2) var depth_texture : texture_depth_2d;\n"
    "@group(0) @binding(3) var cloud_texture : texture_2d<f32>;\n"
    "@group(0) @binding(4) var cloud_sampler : sampler;\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let dims = textureDimensions(depth_texture);\n"
    "  let dims_f = vec2<f32>(f32(dims.x), f32(dims.y));\n"
    "  let clamped_uv = clamp(in.uv, vec2<f32>(0.0), vec2<f32>(0.9999));\n"
    "  let texel = vec2<i32>(clamped_uv * dims_f);\n"
    "  let depth = textureLoad(depth_texture, texel, 0);\n"
    "  if (depth < 0.9999) {\n"
    "    return textureSample(input_texture, input_sampler, in.uv);\n"
    "  }\n"
    "  return textureSample(cloud_texture, cloud_sampler, in.uv);\n"
    "}\n";

static void flecsEngine_clouds_releaseResources(
    FlecsCloudsImpl *impl)
{
    FLECS_WGPU_RELEASE(impl->uniform_buffer, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(impl->weather_view, wgpuTextureViewRelease);
    FLECS_WGPU_RELEASE(impl->weather_texture, wgpuTextureRelease);
    FLECS_WGPU_RELEASE(impl->noise_view, wgpuTextureViewRelease);
    FLECS_WGPU_RELEASE(impl->noise_texture, wgpuTextureRelease);
    FLECS_WGPU_RELEASE(impl->repeat_sampler, wgpuSamplerRelease);
    FLECS_WGPU_RELEASE(impl->shadow_view, wgpuTextureViewRelease);
    FLECS_WGPU_RELEASE(impl->shadow_texture, wgpuTextureRelease);
    FLECS_WGPU_RELEASE(impl->shadow_uniform_buffer, wgpuBufferRelease);
    FLECS_WGPU_RELEASE(impl->shadow_bake_bind_group, wgpuBindGroupRelease);
    FLECS_WGPU_RELEASE(impl->shadow_bake_pipeline, wgpuRenderPipelineRelease);
    FLECS_WGPU_RELEASE(impl->shadow_bake_layout, wgpuBindGroupLayoutRelease);
    FLECS_WGPU_RELEASE(impl->lowres_view, wgpuTextureViewRelease);
    FLECS_WGPU_RELEASE(impl->lowres_texture, wgpuTextureRelease);
    FLECS_WGPU_RELEASE(impl->upsample_bind_group, wgpuBindGroupRelease);
    FLECS_WGPU_RELEASE(impl->upsample_pipeline, wgpuRenderPipelineRelease);
    FLECS_WGPU_RELEASE(impl->upsample_layout, wgpuBindGroupLayoutRelease);
    FLECS_WGPU_RELEASE(impl->upsample_sampler, wgpuSamplerRelease);
}

ECS_DTOR(FlecsCloudsImpl, ptr, {
    flecsEngine_clouds_releaseResources(ptr);
})

ECS_MOVE(FlecsCloudsImpl, dst, src, {
    flecsEngine_clouds_releaseResources(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

static uint32_t flecs_clouds_hash(int32_t x, int32_t y, uint32_t seed)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static float flecs_clouds_value_noise(float x, float y, int32_t period, uint32_t seed)
{
    int32_t xi = (int32_t)floorf(x);
    int32_t yi = (int32_t)floorf(y);
    float fx = x - (float)xi;
    float fy = y - (float)yi;
    float sx = fx * fx * (3.0f - 2.0f * fx);
    float sy = fy * fy * (3.0f - 2.0f * fy);
    int32_t x0 = ((xi % period) + period) % period;
    int32_t y0 = ((yi % period) + period) % period;
    int32_t x1 = (x0 + 1) % period;
    int32_t y1 = (y0 + 1) % period;
    float n00 = (float)(flecs_clouds_hash(x0, y0, seed) & 0xFFFF) / 65535.0f;
    float n10 = (float)(flecs_clouds_hash(x1, y0, seed) & 0xFFFF) / 65535.0f;
    float n01 = (float)(flecs_clouds_hash(x0, y1, seed) & 0xFFFF) / 65535.0f;
    float n11 = (float)(flecs_clouds_hash(x1, y1, seed) & 0xFFFF) / 65535.0f;
    float a = n00 * (1.0f - sx) + n10 * sx;
    float b = n01 * (1.0f - sx) + n11 * sx;
    return a * (1.0f - sy) + b * sy;
}

static float flecs_clouds_fbm(float x, float y, int32_t base_period, int32_t octaves, uint32_t seed)
{
    float sum = 0.0f;
    float amp = 0.5f;
    float total = 0.0f;
    int32_t period = base_period;
    for (int32_t i = 0; i < octaves; i++) {
        sum += amp * flecs_clouds_value_noise(x * (float)period / (float)base_period,
            y * (float)period / (float)base_period, period, seed + (uint32_t)i);
        total += amp;
        amp *= 0.5f;
        period *= 2;
    }
    return sum / total;
}

/* Tileable Worley: output = 1 - dist_to_nearest jittered point. Schneider
 * 2015 base for cumulus billow. */
static float flecs_clouds_worley(float x, float y, int32_t period, uint32_t seed)
{
    int32_t xi = (int32_t)floorf(x);
    int32_t yi = (int32_t)floorf(y);
    float fx = x - (float)xi;
    float fy = y - (float)yi;
    float min_d2 = 1e10f;
    for (int32_t dy = -1; dy <= 1; dy++) {
        for (int32_t dx = -1; dx <= 1; dx++) {
            int32_t cx = ((xi + dx) % period + period) % period;
            int32_t cy = ((yi + dy) % period + period) % period;
            uint32_t h = flecs_clouds_hash(cx, cy, seed);
            float jx = (float)(h & 0xFFFF) / 65535.0f;
            float jy = (float)((h >> 16) & 0xFFFF) / 65535.0f;
            float ex = (float)dx + jx - fx;
            float ey = (float)dy + jy - fy;
            float d2 = ex * ex + ey * ey;
            if (d2 < min_d2) min_d2 = d2;
        }
    }
    float d = sqrtf(min_d2);
    float r = 1.0f - d;
    if (r < 0.0f) r = 0.0f;
    if (r > 1.0f) r = 1.0f;
    return r;
}

static float flecs_clouds_worley_fbm(float x, float y, int32_t base_period, int32_t octaves, uint32_t seed)
{
    float sum = 0.0f;
    float amp = 0.5f;
    float total = 0.0f;
    int32_t period = base_period;
    for (int32_t i = 0; i < octaves; i++) {
        sum += amp * flecs_clouds_worley(
            x * (float)period / (float)base_period,
            y * (float)period / (float)base_period,
            period, seed + (uint32_t)i);
        total += amp;
        amp *= 0.5f;
        period *= 2;
    }
    return sum / total;
}

/* Stateless weather field: neutral-bias FBM with the silhouette stretch baked
 * in. Coverage is applied at sample time via the shader uniform, so the
 * texture stays stable and artists see the exact value they set. Channels:
 * R=base coverage, G=unused, B=cloud_type. */
static void flecs_clouds_bakeWeather(
    uint8_t *data, uint32_t seed)
{
    const uint32_t s = FLECS_CLOUDS_WEATHER_SIZE;
    /* Type seed offset > coverage octave count so per-octave hashes never
     * collide between channels. */
    uint32_t cov_seed = seed ? seed : 11u;
    uint32_t type_seed = cov_seed + 100u;
    for (uint32_t y = 0; y < s; y++) {
        for (uint32_t x = 0; x < s; x++) {
            float u = (float)x / (float)s * 4.0f;
            float v = (float)y / (float)s * 4.0f;
            float cov_raw = flecs_clouds_fbm(u, v, 4, 4, cov_seed);
            float cov = (cov_raw - 0.5f) * 2.5f + 0.5f;
            if (cov < 0.0f) cov = 0.0f;
            if (cov > 1.0f) cov = 1.0f;
            /* Multiplier must divide base_period (4) so the FBM stays periodic
             * across the texture's wrap boundary; 0.7 produced an x-axis seam. */
            float type_raw = flecs_clouds_fbm(
                u * 0.5f + 3.0f, v * 0.5f + 3.0f, 4, 3, type_seed);
            float type = type_raw;
            if (type < 0.0f) type = 0.0f;
            if (type > 1.0f) type = 1.0f;
            uint8_t *px = &data[(y * s + x) * 4];
            px[0] = (uint8_t)(cov * 255.0f + 0.5f);
            px[1] = 0u;
            px[2] = (uint8_t)(type * 255.0f + 0.5f);
            px[3] = 255u;
        }
    }
}

static uint32_t flecs_clouds_hash3(
    int32_t x, int32_t y, int32_t z, uint32_t seed)
{
    uint32_t h = (uint32_t)x * 374761393u
               + (uint32_t)y * 668265263u
               + (uint32_t)z * 2147483647u
               + seed * 1597334677u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static float flecs_clouds_value_noise_3d(
    float x, float y, float z, int32_t period, uint32_t seed)
{
    int32_t xi = (int32_t)floorf(x);
    int32_t yi = (int32_t)floorf(y);
    int32_t zi = (int32_t)floorf(z);
    float fx = x - (float)xi;
    float fy = y - (float)yi;
    float fz = z - (float)zi;
    float sx = fx * fx * (3.0f - 2.0f * fx);
    float sy = fy * fy * (3.0f - 2.0f * fy);
    float sz = fz * fz * (3.0f - 2.0f * fz);
    int32_t x0 = ((xi % period) + period) % period;
    int32_t y0 = ((yi % period) + period) % period;
    int32_t z0 = ((zi % period) + period) % period;
    int32_t x1 = (x0 + 1) % period;
    int32_t y1 = (y0 + 1) % period;
    int32_t z1 = (z0 + 1) % period;
    float c000 = (float)(flecs_clouds_hash3(x0, y0, z0, seed) & 0xFFFF) / 65535.0f;
    float c100 = (float)(flecs_clouds_hash3(x1, y0, z0, seed) & 0xFFFF) / 65535.0f;
    float c010 = (float)(flecs_clouds_hash3(x0, y1, z0, seed) & 0xFFFF) / 65535.0f;
    float c110 = (float)(flecs_clouds_hash3(x1, y1, z0, seed) & 0xFFFF) / 65535.0f;
    float c001 = (float)(flecs_clouds_hash3(x0, y0, z1, seed) & 0xFFFF) / 65535.0f;
    float c101 = (float)(flecs_clouds_hash3(x1, y0, z1, seed) & 0xFFFF) / 65535.0f;
    float c011 = (float)(flecs_clouds_hash3(x0, y1, z1, seed) & 0xFFFF) / 65535.0f;
    float c111 = (float)(flecs_clouds_hash3(x1, y1, z1, seed) & 0xFFFF) / 65535.0f;
    float a00 = c000 * (1.0f - sx) + c100 * sx;
    float a10 = c010 * (1.0f - sx) + c110 * sx;
    float a01 = c001 * (1.0f - sx) + c101 * sx;
    float a11 = c011 * (1.0f - sx) + c111 * sx;
    float b0 = a00 * (1.0f - sy) + a10 * sy;
    float b1 = a01 * (1.0f - sy) + a11 * sy;
    return b0 * (1.0f - sz) + b1 * sz;
}

static float flecs_clouds_fbm_3d(
    float x, float y, float z, int32_t base_period, int32_t octaves, uint32_t seed)
{
    float sum = 0.0f;
    float amp = 0.5f;
    float total = 0.0f;
    int32_t period = base_period;
    for (int32_t i = 0; i < octaves; i++) {
        float s = (float)period / (float)base_period;
        sum += amp * flecs_clouds_value_noise_3d(
            x * s, y * s, z * s, period, seed + (uint32_t)i);
        total += amp;
        amp *= 0.5f;
        period *= 2;
    }
    return sum / total;
}

static float flecs_clouds_worley_3d(
    float x, float y, float z, int32_t period, uint32_t seed)
{
    int32_t xi = (int32_t)floorf(x);
    int32_t yi = (int32_t)floorf(y);
    int32_t zi = (int32_t)floorf(z);
    float fx = x - (float)xi;
    float fy = y - (float)yi;
    float fz = z - (float)zi;
    float min_d2 = 1e10f;
    for (int32_t dz = -1; dz <= 1; dz++) {
        for (int32_t dy = -1; dy <= 1; dy++) {
            for (int32_t dx = -1; dx <= 1; dx++) {
                int32_t cx = ((xi + dx) % period + period) % period;
                int32_t cy = ((yi + dy) % period + period) % period;
                int32_t cz = ((zi + dz) % period + period) % period;
                uint32_t h = flecs_clouds_hash3(cx, cy, cz, seed);
                float jx = (float)(h & 0xFF) / 255.0f;
                float jy = (float)((h >> 8) & 0xFF) / 255.0f;
                float jz = (float)((h >> 16) & 0xFF) / 255.0f;
                float ex = (float)dx + jx - fx;
                float ey = (float)dy + jy - fy;
                float ez = (float)dz + jz - fz;
                float d2 = ex * ex + ey * ey + ez * ez;
                if (d2 < min_d2) min_d2 = d2;
            }
        }
    }
    float d = sqrtf(min_d2);
    float r = 1.0f - d;
    if (r < 0.0f) r = 0.0f;
    if (r > 1.0f) r = 1.0f;
    return r;
}

static float flecs_clouds_worley_fbm_3d(
    float x, float y, float z, int32_t base_period, int32_t octaves, uint32_t seed)
{
    float sum = 0.0f;
    float amp = 0.5f;
    float total = 0.0f;
    int32_t period = base_period;
    for (int32_t i = 0; i < octaves; i++) {
        float s = (float)period / (float)base_period;
        sum += amp * flecs_clouds_worley_3d(
            x * s, y * s, z * s, period, seed + (uint32_t)i);
        total += amp;
        amp *= 0.5f;
        period *= 2;
    }
    return sum / total;
}

/* Schneider 2015 §27 packed 3D noise:
 *   R = Perlin-Worley base (value-noise FBM raised by inverted Worley FBM)
 *   G/B/A = Worley FBM at rising frequencies for detail erosion.
 * 3D (not 2D) because flat noise produces visible disk layering: with no
 * y-variation, every height sees the same silhouette scaled by height_density. */
static void flecs_clouds_bakeNoise(uint8_t *data)
{
    const uint32_t s = FLECS_CLOUDS_NOISE_SIZE;
    for (uint32_t z = 0; z < s; z++) {
        for (uint32_t y = 0; y < s; y++) {
            for (uint32_t x = 0; x < s; x++) {
                float u = (float)x / (float)s;
                float v = (float)y / (float)s;
                float w = (float)z / (float)s;
                float perlin = flecs_clouds_fbm_3d(
                    u * 4.0f, v * 4.0f, w * 4.0f, 4, 4, 41u);
                float worley = flecs_clouds_worley_fbm_3d(
                    u * 4.0f, v * 4.0f, w * 4.0f, 4, 3, 43u);
                /* remap(perlin, -worley, 1, 0, 1) = (perlin+worley)/(1+worley) */
                float r = (perlin + worley) / (1.0f + worley);
                if (r < 0.0f) r = 0.0f;
                if (r > 1.0f) r = 1.0f;
                float g = flecs_clouds_worley_fbm_3d(
                    u * 8.0f, v * 8.0f, w * 8.0f, 8, 2, 53u);
                float b = flecs_clouds_worley_fbm_3d(
                    u * 16.0f, v * 16.0f, w * 16.0f, 16, 2, 67u);
                float a = flecs_clouds_worley_fbm_3d(
                    u * 16.0f + 1.3f, v * 16.0f + 2.7f, w * 16.0f + 0.9f,
                    16, 1, 79u);
                uint8_t *px = &data[((z * s + y) * s + x) * 4];
                px[0] = (uint8_t)(r * 255.0f);
                px[1] = (uint8_t)(g * 255.0f);
                px[2] = (uint8_t)(b * 255.0f);
                px[3] = (uint8_t)(a * 255.0f);
            }
        }
    }
}

static bool flecs_clouds_uploadTexture(
    const FlecsEngineImpl *engine,
    uint32_t size,
    const uint8_t *data,
    WGPUTexture *out_texture,
    WGPUTextureView *out_view)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = { size, size, 1 },
        .format = WGPUTextureFormat_RGBA8Unorm,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    WGPUTexture tex = wgpuDeviceCreateTexture(engine->device, &desc);
    if (!tex) return false;
    WGPUTexelCopyTextureInfo dst = {
        .texture = tex,
        .mipLevel = 0,
        .origin = { 0, 0, 0 },
        .aspect = WGPUTextureAspect_All
    };
    WGPUTexelCopyBufferLayout layout = {
        .offset = 0,
        .bytesPerRow = size * 4u,
        .rowsPerImage = size
    };
    WGPUExtent3D extent = { size, size, 1 };
    wgpuQueueWriteTexture(
        engine->queue, &dst, data, (size_t)size * size * 4u, &layout, &extent);
    WGPUTextureView view = wgpuTextureCreateView(tex, &(WGPUTextureViewDescriptor){
        .format = WGPUTextureFormat_RGBA8Unorm,
        .dimension = WGPUTextureViewDimension_2D,
        .mipLevelCount = 1,
        .arrayLayerCount = 1
    });
    if (!view) {
        wgpuTextureRelease(tex);
        return false;
    }
    *out_texture = tex;
    *out_view = view;
    return true;
}

static bool flecs_clouds_uploadTexture3D(
    const FlecsEngineImpl *engine,
    uint32_t size,
    const uint8_t *data,
    WGPUTexture *out_texture,
    WGPUTextureView *out_view)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_3D,
        .size = { size, size, size },
        .format = WGPUTextureFormat_RGBA8Unorm,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    WGPUTexture tex = wgpuDeviceCreateTexture(engine->device, &desc);
    if (!tex) return false;
    WGPUTexelCopyTextureInfo dst = {
        .texture = tex,
        .mipLevel = 0,
        .origin = { 0, 0, 0 },
        .aspect = WGPUTextureAspect_All
    };
    WGPUTexelCopyBufferLayout layout = {
        .offset = 0,
        .bytesPerRow = size * 4u,
        .rowsPerImage = size
    };
    WGPUExtent3D extent = { size, size, size };
    wgpuQueueWriteTexture(
        engine->queue, &dst, data,
        (size_t)size * size * size * 4u, &layout, &extent);
    WGPUTextureView view = wgpuTextureCreateView(tex, &(WGPUTextureViewDescriptor){
        .format = WGPUTextureFormat_RGBA8Unorm,
        .dimension = WGPUTextureViewDimension_3D,
        .mipLevelCount = 1,
        .arrayLayerCount = 1
    });
    if (!view) {
        wgpuTextureRelease(tex);
        return false;
    }
    *out_texture = tex;
    *out_view = view;
    return true;
}

static WGPUSampler flecs_clouds_createRepeatSampler(WGPUDevice device)
{
    return wgpuDeviceCreateSampler(device, &(WGPUSamplerDescriptor){
        .addressModeU = WGPUAddressMode_Repeat,
        .addressModeV = WGPUAddressMode_Repeat,
        .addressModeW = WGPUAddressMode_Repeat,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Linear,
        .maxAnisotropy = 1
    });
}

static bool flecsEngine_clouds_setup(
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

    const FlecsClouds *cfg = ecs_get(world, effect_entity, FlecsClouds);

    FlecsCloudsImpl impl = {0};
    impl.uniform_buffer = flecsEngine_createUniformBuffer(
        engine->device, sizeof(FlecsCloudsUniform));
    if (!impl.uniform_buffer) return false;

    uint8_t *weather_data = ecs_os_malloc(
        FLECS_CLOUDS_WEATHER_SIZE * FLECS_CLOUDS_WEATHER_SIZE * 4u);
    flecs_clouds_bakeWeather(weather_data, cfg->appearance.seed);
    bool ok = flecs_clouds_uploadTexture(
        engine, FLECS_CLOUDS_WEATHER_SIZE, weather_data,
        &impl.weather_texture, &impl.weather_view);
    ecs_os_free(weather_data);
    if (!ok) {
        flecsEngine_clouds_releaseResources(&impl);
        return false;
    }

    uint8_t *noise_data = ecs_os_malloc(
        FLECS_CLOUDS_NOISE_SIZE * FLECS_CLOUDS_NOISE_SIZE *
        FLECS_CLOUDS_NOISE_SIZE * 4u);
    flecs_clouds_bakeNoise(noise_data);
    ok = flecs_clouds_uploadTexture3D(
        engine, FLECS_CLOUDS_NOISE_SIZE, noise_data,
        &impl.noise_texture, &impl.noise_view);
    ecs_os_free(noise_data);
    if (!ok) {
        flecsEngine_clouds_releaseResources(&impl);
        return false;
    }

    impl.repeat_sampler = flecs_clouds_createRepeatSampler(engine->device);
    if (!impl.repeat_sampler) {
        flecsEngine_clouds_releaseResources(&impl);
        return false;
    }

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
            .minBindingSize = sizeof(FlecsCloudsUniform)
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
    layout_entries[5] = (WGPUBindGroupLayoutEntry){
        .binding = 5,
        .visibility = WGPUShaderStage_Fragment,
        .texture = {
            .sampleType = WGPUTextureSampleType_Float,
            .viewDimension = WGPUTextureViewDimension_3D,
            .multisampled = false
        }
    };
    layout_entries[6] = (WGPUBindGroupLayoutEntry){
        .binding = 6,
        .visibility = WGPUShaderStage_Fragment,
        .sampler = { .type = WGPUSamplerBindingType_Filtering }
    };

    {
        impl.shadow_size = (uint32_t)cfg->shadows.size;
        WGPUTextureDescriptor sd = {
            .usage = WGPUTextureUsage_RenderAttachment
                   | WGPUTextureUsage_TextureBinding,
            .dimension = WGPUTextureDimension_2D,
            .size = { impl.shadow_size, impl.shadow_size, 1 },
            .format = FLECS_CLOUDS_SHADOW_FORMAT,
            .mipLevelCount = 1,
            .sampleCount = 1
        };
        impl.shadow_texture = wgpuDeviceCreateTexture(engine->device, &sd);
        if (!impl.shadow_texture) {
            flecsEngine_clouds_releaseResources(&impl);
            return false;
        }
        impl.shadow_view = wgpuTextureCreateView(impl.shadow_texture,
            &(WGPUTextureViewDescriptor){
                .format = FLECS_CLOUDS_SHADOW_FORMAT,
                .dimension = WGPUTextureViewDimension_2D,
                .mipLevelCount = 1,
                .arrayLayerCount = 1
            });
        if (!impl.shadow_view) {
            flecsEngine_clouds_releaseResources(&impl);
            return false;
        }

        impl.shadow_uniform_buffer = flecsEngine_createUniformBuffer(
            engine->device, sizeof(FlecsShadowBakeUniform));
        if (!impl.shadow_uniform_buffer) {
            flecsEngine_clouds_releaseResources(&impl);
            return false;
        }

        WGPUBindGroupLayoutEntry bake_entries[3] = {
            {
                .binding = 0,
                .visibility = WGPUShaderStage_Fragment,
                .buffer = {
                    .type = WGPUBufferBindingType_Uniform,
                    .minBindingSize = sizeof(FlecsShadowBakeUniform)
                }
            },
            {
                .binding = 1,
                .visibility = WGPUShaderStage_Fragment,
                .texture = {
                    .sampleType = WGPUTextureSampleType_Float,
                    .viewDimension = WGPUTextureViewDimension_2D
                }
            },
            {
                .binding = 2,
                .visibility = WGPUShaderStage_Fragment,
                .sampler = { .type = WGPUSamplerBindingType_Filtering }
            }
        };
        impl.shadow_bake_layout = wgpuDeviceCreateBindGroupLayout(
            engine->device, &(WGPUBindGroupLayoutDescriptor){
                .entryCount = 3, .entries = bake_entries
            });
        if (!impl.shadow_bake_layout) {
            flecsEngine_clouds_releaseResources(&impl);
            return false;
        }

        WGPUShaderModule bake_mod = flecsEngine_shader_ensureModule(
            (ecs_world_t*)world, "CloudsShadowBakeShader", kShadowBakeShader);
        if (!bake_mod) {
            flecsEngine_clouds_releaseResources(&impl);
            return false;
        }
        WGPUColorTargetState bake_target = {
            .format = FLECS_CLOUDS_SHADOW_FORMAT,
            .writeMask = WGPUColorWriteMask_All
        };
        impl.shadow_bake_pipeline = flecsEngine_createFullscreenPipeline(
            engine, bake_mod, impl.shadow_bake_layout,
            "vs_main", "fs_main", &bake_target, NULL);
        if (!impl.shadow_bake_pipeline) {
            flecsEngine_clouds_releaseResources(&impl);
            return false;
        }

        WGPUBindGroupEntry bake_bind[3] = {
            { .binding = 0, .buffer = impl.shadow_uniform_buffer,
              .size = sizeof(FlecsShadowBakeUniform) },
            { .binding = 1, .textureView = impl.weather_view },
            { .binding = 2, .sampler = impl.repeat_sampler }
        };
        impl.shadow_bake_bind_group = wgpuDeviceCreateBindGroup(
            engine->device, &(WGPUBindGroupDescriptor){
                .layout = impl.shadow_bake_layout,
                .entryCount = 3, .entries = bake_bind
            });
        if (!impl.shadow_bake_bind_group) {
            flecsEngine_clouds_releaseResources(&impl);
            return false;
        }
    }

    /* Shadow view is registered by the first render_callback, not here, so
     * frame 1 falls back to "no shadow" rather than baking with a fake sun. */
    ecs_set_ptr((ecs_world_t*)world, effect_entity, FlecsCloudsImpl, &impl);
    *entry_count = 7;
    return true;
}

static void flecs_clouds_fillUniform(
    const ecs_world_t *world,
    ecs_entity_t effect_entity,
    const FlecsRenderViewImpl *view_impl,
    const FlecsClouds *clouds,
    FlecsCloudsImpl *impl,
    FlecsCloudsUniform *uniform)
{
    glm_mat4_identity(uniform->inv_vp);

    if (view_impl && view_impl->camera_view_proj_valid) {
        mat4 mvp;
        glm_mat4_copy((vec4*)view_impl->camera_view_proj, mvp);
        glm_mat4_inv(mvp, uniform->inv_vp);
    }

    if (view_impl) {
        uniform->camera_pos[0] = view_impl->camera_pos[0];
        uniform->camera_pos[1] = view_impl->camera_pos[1];
        uniform->camera_pos[2] = view_impl->camera_pos[2];
        uniform->camera_pos[3] = 1.0f;
    }

    /* Shader expects sun_dir FROM surface TOWARD sun. */
    uniform->sun_dir[0] = 0.0f;
    uniform->sun_dir[1] = 1.0f;
    uniform->sun_dir[2] = 0.0f;
    uniform->sun_color[0] = 5.0f;
    uniform->sun_color[1] = 5.0f;
    uniform->sun_color[2] = 5.0f;
    if (clouds->appearance.atmosphere) {
        const FlecsAtmosphere *atm = ecs_get(world, clouds->appearance.atmosphere, FlecsAtmosphere);
        if (atm && atm->sun) {
            const FlecsRotation3 *rot = ecs_get(world, atm->sun, FlecsRotation3);
            if (rot) {
                vec3 light_ray;
                if (flecsEngine_lightDirFromRotation(rot, light_ray)) {
                    /* light_ray points from sun toward scene; flip for sun_dir. */
                    uniform->sun_dir[0] = -light_ray[0];
                    uniform->sun_dir[1] = -light_ray[1];
                    uniform->sun_dir[2] = -light_ray[2];
                }
            }
            /* Atmosphere-attenuated intensity matches what PBR sees for
             * surface lighting (FlecsRgba * DirectionalLight.intensity). */
            const FlecsRgba *sun_rgb = ecs_get(world, atm->sun, FlecsRgba);
            const FlecsDirectionalLight *dl = ecs_get(
                world, atm->sun, FlecsDirectionalLight);
            if (sun_rgb && dl) {
                FlecsRgba storage;
                sun_rgb = flecsEngine_material_resolveRgba(
                    world, atm->sun, sun_rgb, &storage);
                float scale = dl->intensity;
                uniform->sun_color[0] =
                    flecsEngine_colorChannelToFloat(sun_rgb->r) * scale;
                uniform->sun_color[1] =
                    flecsEngine_colorChannelToFloat(sun_rgb->g) * scale;
                uniform->sun_color[2] =
                    flecsEngine_colorChannelToFloat(sun_rgb->b) * scale;
            }
        }
    }

    float low_y = clouds->appearance.low_altitude_km * 1000.0f;
    float high_y = clouds->appearance.high_altitude_km * 1000.0f;
    if (clouds->appearance.atmosphere) {
        const FlecsAtmosphere *atm = ecs_get(world, clouds->appearance.atmosphere, FlecsAtmosphere);
        if (atm && atm->world_units_per_km > 1e-3f) {
            low_y = atm->sea_level_y +
                clouds->appearance.low_altitude_km * atm->world_units_per_km;
            high_y = atm->sea_level_y +
                clouds->appearance.high_altitude_km * atm->world_units_per_km;
        }
    }
    uniform->params0[0] = low_y;
    uniform->params0[1] = high_y;
    uniform->params0[2] = clouds->appearance.density;
    /* Bounded jitter seed: phase variation with no long-session drift. */
    uniform->params0[3] = (float)(impl->frame_counter & 1023u);

    /* Weather texture stores neutral-bias FBM; coverage is applied at
     * sample time as an additive shift on weather.r. */
    uniform->params1[0] = clouds->appearance.coverage - 0.5f;
    float weather_scale_m = clouds->appearance.weather_scale_km > 0.001f
        ? clouds->appearance.weather_scale_km * 1000.0f : 20000.0f;
    float noise_scale_m = clouds->appearance.noise_scale_km > 0.001f
        ? clouds->appearance.noise_scale_km * 1000.0f : 4000.0f;
    uniform->params1[1] = 1.0f / weather_scale_m;
    uniform->params1[2] = 1.0f / noise_scale_m;
    uniform->params1[3] = clouds->appearance.cloud_type_bias;

    /* Wind as pre-wrapped UV offsets (double-precision CPU fmod → [0,1)
     * float), so the shader sidesteps (wind*time) float blowup. */
    double wx_uv = fmod(impl->wind_offset_x / (double)weather_scale_m, 1.0);
    double wz_uv = fmod(impl->wind_offset_z / (double)weather_scale_m, 1.0);
    double nx_uv = fmod(impl->wind_offset_x / (double)noise_scale_m, 1.0);
    double nz_uv = fmod(impl->wind_offset_z / (double)noise_scale_m, 1.0);
    uniform->params2[0] = (float)wx_uv;
    uniform->params2[1] = (float)wz_uv;
    uniform->params2[2] = 1.0f;
    float max_dist_m = clouds->performance.max_distance_km > 0.001f
        ? clouds->performance.max_distance_km * 1000.0f : 200000.0f;
    uniform->params2[3] = max_dist_m;

    uniform->params3[0] = (float)nx_uv;
    uniform->params3[1] = (float)nz_uv;
    uniform->params3[2] = clouds->appearance.detail_strength;
    uniform->params3[3] = clouds->appearance.anisotropy;

    uniform->params4[0] = clouds->appearance.powder_strength;
    uniform->params4[1] = clouds->appearance.multi_scatter;
    uniform->params4[2] = clouds->performance.march_steps > 0
        ? (float)clouds->performance.march_steps : 32.0f;
    uniform->params4[3] = clouds->performance.light_steps > 0
        ? (float)clouds->performance.light_steps : 4.0f;

    /* Atmosphere-derived ambient: crown leans blue (sees the full sky dome),
     * base picks up warm sun + ground bounce. Night floor from atm->night_tint.
     * User ambient_top/bottom act as per-channel tints on this. */
    bool ambient_from_atmos = false;
    if (clouds->appearance.atmosphere) {
        const FlecsAtmosphere *atm = ecs_get(
            world, clouds->appearance.atmosphere, FlecsAtmosphere);
        if (atm) {
            float sun_cos_z = uniform->sun_dir[1];
            if (sun_cos_z < -1.0f) sun_cos_z = -1.0f;
            if (sun_cos_z > 1.0f) sun_cos_z = 1.0f;
            float sun_t[3] = {0};
            flecsEngine_atmos_sunTransmittance(atm, sun_cos_z, sun_t);

            float day = sun_cos_z > 0.0f ? sun_cos_z : 0.0f;
            float night = 1.0f - day;
            const float sky_blue[3] = { 0.45f, 0.65f, 1.0f };

            float night_scale = atm->night_intensity > 0.0f
                ? atm->night_intensity : 0.0f;
            float nr = flecsEngine_colorChannelToFloat(atm->night_tint.r)
                * night_scale * night;
            float ng = flecsEngine_colorChannelToFloat(atm->night_tint.g)
                * night_scale * night;
            float nb = flecsEngine_colorChannelToFloat(atm->night_tint.b)
                * night_scale * night;

            float tt_r = flecsEngine_colorChannelToFloat(clouds->appearance.ambient_top.r);
            float tt_g = flecsEngine_colorChannelToFloat(clouds->appearance.ambient_top.g);
            float tt_b = flecsEngine_colorChannelToFloat(clouds->appearance.ambient_top.b);
            float tb_r = flecsEngine_colorChannelToFloat(clouds->appearance.ambient_bottom.r);
            float tb_g = flecsEngine_colorChannelToFloat(clouds->appearance.ambient_bottom.g);
            float tb_b = flecsEngine_colorChannelToFloat(clouds->appearance.ambient_bottom.b);

            uniform->ambient_top[0] =
                (sky_blue[0] * day + sun_t[0] * 0.25f * day + nr) * tt_r;
            uniform->ambient_top[1] =
                (sky_blue[1] * day + sun_t[1] * 0.25f * day + ng) * tt_g;
            uniform->ambient_top[2] =
                (sky_blue[2] * day + sun_t[2] * 0.25f * day + nb) * tt_b;
            uniform->ambient_top[3] = 1.0f;
            uniform->ambient_bottom[0] =
                (sky_blue[0] * 0.35f * day + sun_t[0] * 0.5f * day + nr * 0.4f) * tb_r;
            uniform->ambient_bottom[1] =
                (sky_blue[1] * 0.35f * day + sun_t[1] * 0.5f * day + ng * 0.4f) * tb_g;
            uniform->ambient_bottom[2] =
                (sky_blue[2] * 0.30f * day + sun_t[2] * 0.5f * day + nb * 0.4f) * tb_b;
            uniform->ambient_bottom[3] = 1.0f;
            ambient_from_atmos = true;
        }
    }
    if (!ambient_from_atmos) {
        uniform->ambient_top[0] =
            flecsEngine_colorChannelToFloat(clouds->appearance.ambient_top.r);
        uniform->ambient_top[1] =
            flecsEngine_colorChannelToFloat(clouds->appearance.ambient_top.g);
        uniform->ambient_top[2] =
            flecsEngine_colorChannelToFloat(clouds->appearance.ambient_top.b);
        uniform->ambient_top[3] = 1.0f;
        uniform->ambient_bottom[0] =
            flecsEngine_colorChannelToFloat(clouds->appearance.ambient_bottom.r);
        uniform->ambient_bottom[1] =
            flecsEngine_colorChannelToFloat(clouds->appearance.ambient_bottom.g);
        uniform->ambient_bottom[2] =
            flecsEngine_colorChannelToFloat(clouds->appearance.ambient_bottom.b);
        uniform->ambient_bottom[3] = 1.0f;
    }
}

/* Per-frame: uniforms + shadow publish. Runs before the bake
 * pass so the bake reads fresh uniforms; bind_callback only wires entries. */
static bool flecs_clouds_updateState(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    ecs_entity_t effect_entity,
    FlecsCloudsImpl *impl,
    const FlecsClouds *clouds)
{
    float real_dt = (float)ecs_get_world_info(world)->delta_time;
    if (real_dt < 0.0f) real_dt = 0.0f;
    if (real_dt > 0.1f) real_dt = 0.1f;
    impl->wind_offset_x += (double)clouds->appearance.wind_x * (double)real_dt;
    impl->wind_offset_z += (double)clouds->appearance.wind_z * (double)real_dt;
    impl->frame_counter++;

    FlecsCloudsUniform uniform = {0};
    flecs_clouds_fillUniform(world, effect_entity, view_impl, clouds, impl, &uniform);
    wgpuQueueWriteBuffer(engine->queue, impl->uniform_buffer, 0,
        &uniform, sizeof(uniform));

    /* Shadow bake uniform for this frame; bake pass itself runs in the
     * render_callback. */
    {
        float footprint = clouds->shadows.scale_km > 0.001f
            ? clouds->shadows.scale_km * 1000.0f : 4000.0f;
        float texel = footprint / (float)impl->shadow_size;
        float cx = uniform.camera_pos[0];
        float cz = uniform.camera_pos[2];
        /* Snap to texel grid to avoid sub-texel shadow crawling when camera
         * moves — texels still cover the same world XZ until a full crossing. */
        float snap_x = floorf(cx / texel) * texel;
        float snap_z = floorf(cz / texel) * texel;
        float origin_x = snap_x - footprint * 0.5f;
        float origin_z = snap_z - footprint * 0.5f;
        impl->shadow_origin_x = origin_x;
        impl->shadow_origin_z = origin_z;
        impl->shadow_inv_footprint = 1.0f / footprint;

        FlecsShadowBakeUniform sb = {0};
        sb.sun_dir[0] = uniform.sun_dir[0];
        sb.sun_dir[1] = uniform.sun_dir[1];
        sb.sun_dir[2] = uniform.sun_dir[2];
        sb.params[0] = origin_x;
        sb.params[1] = origin_z;
        sb.params[2] = footprint;
        sb.params[3] = uniform.params0[0];          /* slab_low_y */
        sb.params2[0] = uniform.params0[1];         /* slab_high_y */
        sb.params2[1] = uniform.params1[1];         /* weather_inv_scale */
        sb.params2[2] = 0.0f;                        /* unused */
        sb.params2[3] = 0.0f;                        /* unused */
        sb.params3[0] = uniform.params2[0];         /* weather_uv_x */
        sb.params3[1] = uniform.params2[1];         /* weather_uv_z */
        sb.params3[2] = uniform.params1[0];         /* coverage_bias */
        sb.params3[3] = FLECS_CLOUDS_SHADOW_OD_SCALE;
        wgpuQueueWriteBuffer(engine->queue, impl->shadow_uniform_buffer, 0,
            &sb, sizeof(sb));

        flecs_clouds_publishShadow(
            engine, impl->shadow_view,
            origin_x, origin_z, impl->shadow_inv_footprint,
            clouds->shadows.strength);
    }

    return true;
}

static bool flecsEngine_clouds_bind(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    ecs_entity_t effect_entity,
    const FlecsRenderEffectKind *kind,
    const FlecsRenderEffectImpl *effect_impl,
    WGPUBindGroupEntry *entries,
    uint32_t *entry_count)
{
    (void)kind;
    (void)effect_impl;
    (void)engine;

    if (!view_impl || !view_impl->depth_texture_view) return false;

    const FlecsCloudsImpl *impl = ecs_get(
        world, effect_entity, FlecsCloudsImpl);
    if (!impl || !impl->uniform_buffer) return false;

    entries[2] = (WGPUBindGroupEntry){
        .binding = 2, .textureView = view_impl->depth_texture_view };
    entries[3] = (WGPUBindGroupEntry){
        .binding = 3, .buffer = impl->uniform_buffer,
        .offset = 0, .size = sizeof(FlecsCloudsUniform) };
    entries[4] = (WGPUBindGroupEntry){
        .binding = 4, .textureView = impl->weather_view };
    entries[5] = (WGPUBindGroupEntry){
        .binding = 5, .textureView = impl->noise_view };
    entries[6] = (WGPUBindGroupEntry){
        .binding = 6, .sampler = impl->repeat_sampler };

    *entry_count = 7;
    return true;
}

/* Build/recycle the low-res cloud target + upsample pipeline when
 * render_scale > 1. Textures rebuild when dims/format/scale change. */
static bool flecs_clouds_ensureUpsampleResources(
    ecs_world_t *world,
    const FlecsEngineImpl *engine,
    FlecsCloudsImpl *impl,
    const FlecsRenderViewImpl *view_impl,
    WGPUTextureView input_view,
    WGPUTextureFormat output_format,
    float render_scale)
{
    uint32_t full_w = view_impl->effect_target_width;
    uint32_t full_h = view_impl->effect_target_height;
    if (full_w == 0 || full_h == 0) return false;

    uint32_t lw = (uint32_t)((float)full_w / render_scale + 0.5f);
    uint32_t lh = (uint32_t)((float)full_h / render_scale + 0.5f);
    if (lw < 1) lw = 1;
    if (lh < 1) lh = 1;

    WGPUTextureFormat lowres_format = output_format;

    if (impl->lowres_texture == NULL ||
        impl->lowres_width != lw || impl->lowres_height != lh ||
        impl->lowres_format != lowres_format)
    {
        FLECS_WGPU_RELEASE(impl->lowres_view, wgpuTextureViewRelease);
        FLECS_WGPU_RELEASE(impl->lowres_texture, wgpuTextureRelease);
        WGPUTextureDescriptor td = {
            .usage = WGPUTextureUsage_RenderAttachment
                   | WGPUTextureUsage_TextureBinding,
            .dimension = WGPUTextureDimension_2D,
            .size = { lw, lh, 1 },
            .format = lowres_format,
            .mipLevelCount = 1,
            .sampleCount = 1
        };
        impl->lowres_texture = wgpuDeviceCreateTexture(engine->device, &td);
        if (!impl->lowres_texture) return false;
        impl->lowres_view = wgpuTextureCreateView(impl->lowres_texture,
            &(WGPUTextureViewDescriptor){
                .format = lowres_format,
                .dimension = WGPUTextureViewDimension_2D,
                .mipLevelCount = 1,
                .arrayLayerCount = 1
            });
        if (!impl->lowres_view) return false;
        impl->lowres_width = lw;
        impl->lowres_height = lh;
        impl->lowres_format = lowres_format;
        FLECS_WGPU_RELEASE(impl->upsample_bind_group, wgpuBindGroupRelease);
    }

    if (!impl->upsample_sampler) {
        impl->upsample_sampler = wgpuDeviceCreateSampler(engine->device,
            &(WGPUSamplerDescriptor){
                .addressModeU = WGPUAddressMode_ClampToEdge,
                .addressModeV = WGPUAddressMode_ClampToEdge,
                .addressModeW = WGPUAddressMode_ClampToEdge,
                .magFilter = WGPUFilterMode_Linear,
                .minFilter = WGPUFilterMode_Linear,
                .mipmapFilter = WGPUMipmapFilterMode_Nearest,
                .maxAnisotropy = 1
            });
        if (!impl->upsample_sampler) return false;
    }

    if (!impl->upsample_layout) {
        WGPUBindGroupLayoutEntry entries[5] = {
            { .binding = 0, .visibility = WGPUShaderStage_Fragment,
              .texture = { .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D } },
            { .binding = 1, .visibility = WGPUShaderStage_Fragment,
              .sampler = { .type = WGPUSamplerBindingType_Filtering } },
            { .binding = 2, .visibility = WGPUShaderStage_Fragment,
              .texture = { .sampleType = WGPUTextureSampleType_Depth,
                .viewDimension = WGPUTextureViewDimension_2D } },
            { .binding = 3, .visibility = WGPUShaderStage_Fragment,
              .texture = { .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D } },
            { .binding = 4, .visibility = WGPUShaderStage_Fragment,
              .sampler = { .type = WGPUSamplerBindingType_Filtering } }
        };
        impl->upsample_layout = wgpuDeviceCreateBindGroupLayout(
            engine->device, &(WGPUBindGroupLayoutDescriptor){
                .entryCount = 5, .entries = entries
            });
        if (!impl->upsample_layout) return false;
    }

    if (!impl->upsample_pipeline ||
        impl->upsample_pipeline_format != output_format)
    {
        FLECS_WGPU_RELEASE(impl->upsample_pipeline, wgpuRenderPipelineRelease);
        WGPUShaderModule mod = flecsEngine_shader_ensureModule(
            world, "CloudsUpsampleShader", kCloudUpsampleShader);
        if (!mod) return false;
        WGPUColorTargetState target = {
            .format = output_format,
            .writeMask = WGPUColorWriteMask_All
        };
        impl->upsample_pipeline = flecsEngine_createFullscreenPipeline(
            engine, mod, impl->upsample_layout,
            "vs_main", "fs_main", &target, NULL);
        if (!impl->upsample_pipeline) return false;
        impl->upsample_pipeline_format = output_format;
    }

    if (!impl->upsample_bind_group ||
        impl->upsample_bind_input_view != input_view ||
        impl->upsample_bind_depth_view != view_impl->depth_texture_view ||
        impl->upsample_bind_cloud_view != impl->lowres_view)
    {
        FLECS_WGPU_RELEASE(impl->upsample_bind_group, wgpuBindGroupRelease);
        WGPUBindGroupEntry entries[5] = {
            { .binding = 0, .textureView = input_view },
            { .binding = 1, .sampler = engine->pipelines.passthrough_sampler },
            { .binding = 2, .textureView = view_impl->depth_texture_view },
            { .binding = 3, .textureView = impl->lowres_view },
            { .binding = 4, .sampler = impl->upsample_sampler }
        };
        impl->upsample_bind_group = wgpuDeviceCreateBindGroup(engine->device,
            &(WGPUBindGroupDescriptor){
                .layout = impl->upsample_layout,
                .entryCount = 5, .entries = entries
            });
        if (!impl->upsample_bind_group) return false;
        impl->upsample_bind_input_view = input_view;
        impl->upsample_bind_depth_view = view_impl->depth_texture_view;
        impl->upsample_bind_cloud_view = impl->lowres_view;
    }

    return true;
}

/* Bake cloud shadow then run the cloud composite. The bake stores per-
 * ground-point transmittance so PBR samples directly by world XZ. */
static bool flecsEngine_clouds_render(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    WGPUCommandEncoder encoder,
    ecs_entity_t effect_entity,
    const FlecsRenderEffectKind *kind,
    FlecsRenderEffectImpl *effect_impl,
    WGPUTextureView input_view,
    WGPUTextureFormat input_format,
    WGPUTextureView output_view,
    WGPUTextureFormat output_format,
    WGPULoadOp output_load_op)
{
    (void)input_format;

    FlecsCloudsImpl *impl = ecs_get_mut(
        (ecs_world_t*)world, effect_entity, FlecsCloudsImpl);
    if (!impl || !impl->shadow_bake_pipeline ||
        !impl->shadow_bake_bind_group || !impl->shadow_view)
    {
        return false;
    }

    const FlecsClouds *clouds = ecs_get(world, effect_entity, FlecsClouds);
    if (!clouds) return false;

    /* Update state first so the bake below reads fresh uniforms. */
    if (!flecs_clouds_updateState(world, engine, view_impl, effect_entity, impl, clouds)) {
        return false;
    }

    ((FlecsRenderViewImpl*)view_impl)->cloud_shadow_available =
        clouds->shadows.strength > 0.0f;

    if (!flecsEngine_fullscreenPass(
            encoder, impl->shadow_view,
            WGPULoadOp_Clear, (WGPUColor){ 1.0, 1.0, 1.0, 1.0 },
            impl->shadow_bake_pipeline, impl->shadow_bake_bind_group,
            engine, "CloudsShadowBake", NULL))
    {
        return false;
    }

    float scale = clouds->performance.render_scale;
    if (scale < 1.0f) scale = 1.0f;

    if (scale <= 1.001f) {
        return flecsEngine_renderEffect_render(
            world, engine, view_impl, encoder,
            output_view, output_load_op, (WGPUColor){0, 0, 0, 1},
            effect_entity, kind, effect_impl,
            input_view, output_format,
            "Clouds", NULL);
    }

    if (!flecs_clouds_ensureUpsampleResources((ecs_world_t*)world, engine, impl,
            view_impl, input_view, output_format, scale))
    {
        return false;
    }

    if (!flecsEngine_renderEffect_render(
            world, engine, view_impl, encoder,
            impl->lowres_view, WGPULoadOp_Clear, (WGPUColor){0, 0, 0, 1},
            effect_entity, kind, effect_impl,
            input_view, impl->lowres_format,
            "CloudsLowRes", NULL))
    {
        return false;
    }

    return flecsEngine_fullscreenPass(
        encoder, output_view,
        output_load_op, (WGPUColor){0, 0, 0, 1},
        impl->upsample_pipeline, impl->upsample_bind_group,
        engine, "CloudsUpsample", NULL);
}

ECS_CTOR(FlecsClouds, ptr, {
    *ptr = (FlecsClouds){
        .appearance = {
            .atmosphere = 0,
            .low_altitude_km = 0.1f,
            .high_altitude_km = 2.0f,
            .coverage = 0.55f,
            .cloud_type_bias = 0.0f,
            .density = 0.04f,
            .detail_strength = 0.4f,
            .anisotropy = 0.8f,
            .powder_strength = 0.5f,
            .multi_scatter = 0.5f,
            .wind_x = 8.0f,
            .wind_z = 4.0f,
            .weather_scale_km = 10.0f,
            .noise_scale_km = 2.0f,
            .seed = 0u,
            .ambient_top = {220, 230, 255, 255},
            .ambient_bottom = {110, 120, 140, 255}
        },
        .shadows = {
            .strength = 1.0f,
            .scale_km = 2.0f,
            .size = 1024
        },
        .performance = {
            .render_scale = 0.25f,
            .march_steps = 32,
            .light_steps = 4,
            .max_distance_km = 200.0f
        }
    };
})

static void FlecsClouds_on_set(
    ecs_iter_t *it)
{
    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        ecs_set(it->world, e, FlecsRenderEffectKind, {
            .shader = flecsEngine_clouds_shader(it->world),
            .setup_callback = flecsEngine_clouds_setup,
            .bind_callback = flecsEngine_clouds_bind,
            .render_callback = flecsEngine_clouds_render
        });
    }
}

void flecsEngine_clouds_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsClouds);
    ECS_COMPONENT_DEFINE(world, FlecsCloudsImpl);

    ecs_set_hooks(world, FlecsClouds, {
        .ctor = ecs_ctor(FlecsClouds)
    });

    ecs_set_hooks(world, FlecsCloudsImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsCloudsImpl),
        .dtor = ecs_dtor(FlecsCloudsImpl)
    });

    ecs_entity_t appearance_t = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "FlecsCloudsAppearance" }),
        .members = {
            { .name = "atmosphere", .type = ecs_id(ecs_entity_t) },
            { .name = "low_altitude_km", .type = ecs_id(ecs_f32_t) },
            { .name = "high_altitude_km", .type = ecs_id(ecs_f32_t) },
            { .name = "coverage", .type = ecs_id(ecs_f32_t) },
            { .name = "cloud_type_bias", .type = ecs_id(ecs_f32_t) },
            { .name = "density", .type = ecs_id(ecs_f32_t) },
            { .name = "detail_strength", .type = ecs_id(ecs_f32_t) },
            { .name = "anisotropy", .type = ecs_id(ecs_f32_t) },
            { .name = "powder_strength", .type = ecs_id(ecs_f32_t) },
            { .name = "multi_scatter", .type = ecs_id(ecs_f32_t) },
            { .name = "wind_x", .type = ecs_id(ecs_f32_t) },
            { .name = "wind_z", .type = ecs_id(ecs_f32_t) },
            { .name = "weather_scale_km", .type = ecs_id(ecs_f32_t) },
            { .name = "noise_scale_km", .type = ecs_id(ecs_f32_t) },
            { .name = "seed", .type = ecs_id(ecs_u32_t) },
            { .name = "ambient_top", .type = ecs_id(flecs_rgba_t) },
            { .name = "ambient_bottom", .type = ecs_id(flecs_rgba_t) }
        }
    });

    ecs_entity_t shadows_t = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "FlecsCloudsShadows" }),
        .members = {
            { .name = "strength", .type = ecs_id(ecs_f32_t) },
            { .name = "scale_km", .type = ecs_id(ecs_f32_t) },
            { .name = "size", .type = ecs_id(ecs_i32_t) }
        }
    });

    ecs_entity_t performance_t = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "FlecsCloudsPerformance" }),
        .members = {
            { .name = "render_scale", .type = ecs_id(ecs_f32_t) },
            { .name = "march_steps", .type = ecs_id(ecs_i32_t) },
            { .name = "light_steps", .type = ecs_id(ecs_i32_t) },
            { .name = "max_distance_km", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsClouds),
        .members = {
            { .name = "appearance", .type = appearance_t },
            { .name = "shadows", .type = shadows_t },
            { .name = "performance", .type = performance_t }
        }
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(FlecsClouds) }},
        .events = { EcsOnSet },
        .callback = FlecsClouds_on_set
    });
}
