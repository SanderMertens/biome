#include <math.h>
#include <string.h>

#include "atmosphere.h"
#include "../renderer/renderer.h"
#include "../renderer/ibl/ibl_internal.h"
#include "../../tracy_hooks.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsAtmosphere);
ECS_COMPONENT_DECLARE(FlecsAtmosphereImpl);

#define FLECS_ATMOS_TRANS_W (256u)
#define FLECS_ATMOS_TRANS_H (64u)
#define FLECS_ATMOS_MS_W    (32u)
#define FLECS_ATMOS_MS_H    (32u)
#define FLECS_ATMOS_SKYVIEW_W (192u)
#define FLECS_ATMOS_SKYVIEW_H (108u)
#define FLECS_ATMOS_AERIAL_W (32u)
#define FLECS_ATMOS_AERIAL_H (32u)
#define FLECS_ATMOS_AERIAL_SLICES (32u)

#define FLECS_ATMOS_FORMAT (WGPUTextureFormat_RGBA16Float)

typedef struct FlecsAtmosphereUniform {
    mat4 inv_vp;
    float camera_pos_world[4];
    float sun_direction[4];
    float sun_color[4];
    float ground_albedo[4];
    float atmosphere[4];
    float mie_params[4];
    float world_scale[4];
    float scat_rayleigh[4];
    float scat_mie[4];
    float ext_mie[4];
    float abs_ozone[4];
    float abs_strat_aerosol[4];
    float abs_haze[4];
    float misc[4];
    float aerial_params[4];
    float night_tint[4];
    float moon_direction[4];
    float moon_radiance[4];
    float star_rot_0[4];       /* xyz = rot matrix row 0, w = cells */
    float star_rot_1[4];       /* xyz = rot matrix row 1, w = density */
    float star_rot_2[4];       /* xyz = rot matrix row 2, w = size */
    float star_color[4];       /* rgb = toa_color * toa_intensity, a = unused */
} FlecsAtmosphereUniform;

typedef struct FlecsAtmosphereSliceUniform {
    uint32_t slice_index;
    uint32_t slice_count;
    float _pad[2];
} FlecsAtmosphereSliceUniform;

static const char *kAtmosphereCommonWgsl =
"const PI : f32 = 3.14159265358979323846;\n"
"const PLANET_OFFSET : f32 = 0.001;\n"
"struct AtmosUniforms {\n"
"  inv_vp : mat4x4<f32>,\n"
"  camera_pos_world : vec4<f32>,\n"
"  sun_direction : vec4<f32>,\n"
"  sun_color : vec4<f32>,\n"
"  ground_albedo : vec4<f32>,\n"
"  atmosphere : vec4<f32>,\n"
"  mie_params : vec4<f32>,\n"
"  world_scale : vec4<f32>,\n"
"  scat_rayleigh : vec4<f32>,\n"
"  scat_mie : vec4<f32>,\n"
"  ext_mie : vec4<f32>,\n"
"  abs_ozone : vec4<f32>,\n"
"  abs_strat_aerosol : vec4<f32>,\n"
"  abs_haze : vec4<f32>,\n"
"  misc : vec4<f32>,\n"
"  aerial_params : vec4<f32>,\n"
"  night_tint : vec4<f32>,\n"
"  moon_direction : vec4<f32>,\n"
"  moon_radiance : vec4<f32>,\n"
"  star_rot_0 : vec4<f32>,\n"
"  star_rot_1 : vec4<f32>,\n"
"  star_rot_2 : vec4<f32>,\n"
"  star_color : vec4<f32>,\n"
"};\n"
"struct Medium {\n"
"  scattering : vec3<f32>,\n"
"  extinction : vec3<f32>,\n"
"  scattering_rayleigh : vec3<f32>,\n"
"  scattering_mie : vec3<f32>,\n"
"};\n"
"fn bottomR(u : AtmosUniforms) -> f32 { return u.atmosphere.x; }\n"
"fn topR(u : AtmosUniforms) -> f32 { return u.atmosphere.y; }\n"
"fn sample_medium(alt_km : f32, u : AtmosUniforms) -> Medium {\n"
"  let density_ray = exp(-alt_km / u.atmosphere.z);\n"
"  let density_mie = exp(-alt_km / u.atmosphere.w);\n"
"  let density_ozone = max(0.0, 1.0 - abs(alt_km - 25.0) / 15.0);\n"
"  let density_strat = max(0.0, 1.0 - abs(alt_km - 20.0) / 5.0);\n"
"  let scat_ray = u.scat_rayleigh.xyz * density_ray;\n"
"  let scat_mie = u.scat_mie.xyz * density_mie;\n"
"  let ext_ray = scat_ray;\n"
"  let ext_mie = u.ext_mie.xyz * density_mie;\n"
"  let abs_ozone = u.abs_ozone.xyz * density_ozone;\n"
"  let abs_strat = u.abs_strat_aerosol.xyz * density_strat;\n"
"  let abs_haze = u.abs_haze.xyz * density_mie;\n"
"  var m : Medium;\n"
"  m.scattering_rayleigh = scat_ray;\n"
"  m.scattering_mie = scat_mie;\n"
"  m.scattering = scat_ray + scat_mie;\n"
"  m.extinction = ext_ray + ext_mie + abs_ozone + abs_strat + abs_haze;\n"
"  return m;\n"
"}\n"
"fn rayleigh_phase(c : f32) -> f32 {\n"
"  return (3.0 / (16.0 * PI)) * (1.0 + c * c);\n"
"}\n"
"fn mie_phase(c : f32, g : f32) -> f32 {\n"
"  let k = (3.0 / (8.0 * PI)) * (1.0 - g * g) / (2.0 + g * g);\n"
"  let denom = 1.0 + g * g - 2.0 * g * c;\n"
"  return k * (1.0 + c * c) / max(pow(max(denom, 1e-6), 1.5), 1e-6);\n"
"}\n"
"fn ray_sphere_nearest(ro : vec3<f32>, rd : vec3<f32>, r : f32) -> f32 {\n"
"  let b = dot(ro, rd);\n"
"  let c = dot(ro, ro) - r * r;\n"
"  let disc = b * b - c;\n"
"  if (disc < 0.0) { return -1.0; }\n"
"  let sd = sqrt(disc);\n"
"  let t0 = -b - sd;\n"
"  let t1 = -b + sd;\n"
"  if (t0 >= 0.0) { return t0; }\n"
"  if (t1 >= 0.0) { return t1; }\n"
"  return -1.0;\n"
"}\n"
"fn uv_to_trans_params(uv : vec2<f32>, u : AtmosUniforms) -> vec2<f32> {\n"
"  let Rb = bottomR(u);\n"
"  let Rt = topR(u);\n"
"  let H = sqrt(max(0.0, Rt * Rt - Rb * Rb));\n"
"  let rho = H * uv.y;\n"
"  let r = sqrt(rho * rho + Rb * Rb);\n"
"  let d_min = Rt - r;\n"
"  let d_max = rho + H;\n"
"  let d = d_min + uv.x * (d_max - d_min);\n"
"  var mu = 1.0;\n"
"  if (d > 0.0) {\n"
"    mu = (H * H - rho * rho - d * d) / (2.0 * r * d);\n"
"  }\n"
"  return vec2<f32>(r, clamp(mu, -1.0, 1.0));\n"
"}\n"
"fn trans_params_to_uv(r : f32, mu : f32, u : AtmosUniforms) -> vec2<f32> {\n"
"  let Rb = bottomR(u);\n"
"  let Rt = topR(u);\n"
"  let H = sqrt(max(0.0, Rt * Rt - Rb * Rb));\n"
"  let rho = sqrt(max(0.0, r * r - Rb * Rb));\n"
"  let disc = max(0.0, r * r * (mu * mu - 1.0) + Rt * Rt);\n"
"  let d = max(0.0, -r * mu + sqrt(disc));\n"
"  let d_min = Rt - r;\n"
"  let d_max = rho + H;\n"
"  var x_mu = 0.0;\n"
"  if (d_max > d_min + 1e-6) { x_mu = (d - d_min) / (d_max - d_min); }\n"
"  let x_r = rho / max(H, 1e-6);\n"
"  return vec2<f32>(clamp(x_mu, 0.0, 1.0), clamp(x_r, 0.0, 1.0));\n"
"}\n"
"fn sample_transmittance_lut(trans : texture_2d<f32>, smp : sampler, r : f32, mu : f32, u : AtmosUniforms) -> vec3<f32> {\n"
"  let uv = trans_params_to_uv(r, mu, u);\n"
"  return textureSampleLevel(trans, smp, uv, 0.0).rgb;\n"
"}\n"
"fn earth_shadow_factor(r : f32, mu_sun : f32, u : AtmosUniforms) -> f32 {\n"
"  let Rb = bottomR(u);\n"
"  let horizon = -sqrt(max(0.0, 1.0 - (Rb * Rb) / max(r * r, 1e-6)));\n"
"  return smoothstep(horizon - 0.02, horizon + 0.02, mu_sun);\n"
"}\n"
"fn ms_uv_to_params(uv : vec2<f32>, u : AtmosUniforms) -> vec2<f32> {\n"
"  let cos_sun = uv.x * 2.0 - 1.0;\n"
"  let alt = uv.y * (topR(u) - bottomR(u));\n"
"  return vec2<f32>(cos_sun, alt);\n"
"}\n"
"fn ms_params_to_uv(cos_sun : f32, r : f32, u : AtmosUniforms) -> vec2<f32> {\n"
"  let alt = r - bottomR(u);\n"
"  return vec2<f32>(clamp(0.5 + 0.5 * cos_sun, 0.0, 1.0),\n"
"                   clamp(alt / (topR(u) - bottomR(u)), 0.0, 1.0));\n"
"}\n"
"fn skyview_uv_to_dir(uv : vec2<f32>, view_r : f32, u : AtmosUniforms) -> vec3<f32> {\n"
"  let Rb = bottomR(u);\n"
"  let horizon_cos = -sqrt(max(0.0, view_r * view_r - Rb * Rb)) / max(view_r, 1e-4);\n"
"  let horizon_ang = acos(clamp(horizon_cos, -1.0, 1.0));\n"
"  var zenith = 0.0;\n"
"  if (uv.y < 0.5) {\n"
"    let t = uv.y / 0.5;\n"
"    zenith = horizon_ang * t * t;\n"
"  } else {\n"
"    let t = (uv.y - 0.5) / 0.5;\n"
"    zenith = horizon_ang + (PI - horizon_ang) * t * t;\n"
"  }\n"
"  let azim = (uv.x - 0.5) * 2.0 * PI;\n"
"  let sz = sin(zenith);\n"
"  return vec3<f32>(sz * sin(azim), cos(zenith), sz * cos(azim));\n"
"}\n"
"fn dir_to_skyview_uv(dir : vec3<f32>, view_r : f32, u : AtmosUniforms) -> vec2<f32> {\n"
"  let Rb = bottomR(u);\n"
"  let horizon_cos = -sqrt(max(0.0, view_r * view_r - Rb * Rb)) / max(view_r, 1e-4);\n"
"  let horizon_ang = acos(clamp(horizon_cos, -1.0, 1.0));\n"
"  let uu = atan2(dir.x, dir.z) / (2.0 * PI) + 0.5;\n"
"  let zenith = acos(clamp(dir.y, -1.0, 1.0));\n"
"  var vv = 0.0;\n"
"  if (zenith < horizon_ang) {\n"
"    let t = zenith / max(horizon_ang, 1e-6);\n"
"    vv = 0.5 * sqrt(clamp(t, 0.0, 1.0));\n"
"  } else {\n"
"    let t = (zenith - horizon_ang) / max(PI - horizon_ang, 1e-6);\n"
"    vv = 0.5 + 0.5 * sqrt(clamp(t, 0.0, 1.0));\n"
"  }\n"
"  return vec2<f32>(clamp(uu, 0.0, 1.0), clamp(vv, 0.0, 1.0));\n"
"}\n"
;

static const char *kTransShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "@group(0) @binding(0) var<uniform> u : AtmosUniforms;\n"
    "const TRANS_STEPS : i32 = 40;\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let p = uv_to_trans_params(in.uv, u);\n"
    "  let r = p.x;\n"
    "  let mu = p.y;\n"
    "  let ro = vec3<f32>(0.0, r, 0.0);\n"
    "  let rd = vec3<f32>(sqrt(max(0.0, 1.0 - mu * mu)), mu, 0.0);\n"
    "  let t_top = ray_sphere_nearest(ro, rd, topR(u));\n"
    "  let t_ground = ray_sphere_nearest(ro, rd, bottomR(u));\n"
    "  var t_max = t_top;\n"
    "  if (t_ground > 0.0 && t_ground < t_max) { t_max = t_ground; }\n"
    "  if (t_max <= 0.0) { return vec4<f32>(1.0, 1.0, 1.0, 1.0); }\n"
    "  let dt = t_max / f32(TRANS_STEPS);\n"
    "  var od = vec3<f32>(0.0);\n"
    "  for (var i : i32 = 0; i < TRANS_STEPS; i = i + 1) {\n"
    "    let t = (f32(i) + 0.5) * dt;\n"
    "    let pos = ro + rd * t;\n"
    "    let alt = length(pos) - bottomR(u);\n"
    "    let m = sample_medium(max(alt, 0.0), u);\n"
    "    od = od + m.extinction * dt;\n"
    "  }\n"
    "  return vec4<f32>(exp(-od), 1.0);\n"
    "}\n";

static const char *kMSShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "@group(0) @binding(0) var<uniform> u : AtmosUniforms;\n"
    "@group(0) @binding(1) var trans_lut : texture_2d<f32>;\n"
    "@group(0) @binding(2) var lut_sampler : sampler;\n"
    "const MS_DIR_SQRT : i32 = 8;\n"
    "const MS_STEP_COUNT : i32 = 20;\n"
    "fn integrate_scattered(ro : vec3<f32>, rd : vec3<f32>, sun : vec3<f32>, u : AtmosUniforms) -> vec4<f32> {\n"
    "  let t_top = ray_sphere_nearest(ro, rd, topR(u));\n"
    "  let t_ground_raw = ray_sphere_nearest(ro, rd, bottomR(u));\n"
    "  var t_max = t_top;\n"
    "  var hit_ground = false;\n"
    "  if (t_ground_raw > 0.0 && t_ground_raw < t_max) {\n"
    "    t_max = t_ground_raw;\n"
    "    hit_ground = true;\n"
    "  }\n"
    "  if (t_max <= 0.0) { return vec4<f32>(0.0); }\n"
    "  let dt = t_max / f32(MS_STEP_COUNT);\n"
    "  var throughput = vec3<f32>(1.0);\n"
    "  var L = vec3<f32>(0.0);\n"
    "  var f_ms = vec3<f32>(0.0);\n"
    "  for (var i : i32 = 0; i < MS_STEP_COUNT; i = i + 1) {\n"
    "    let t = (f32(i) + 0.5) * dt;\n"
    "    let pos = ro + rd * t;\n"
    "    let r_p = length(pos);\n"
    "    let alt = r_p - bottomR(u);\n"
    "    let m = sample_medium(max(alt, 0.0), u);\n"
    "    let step_trans = exp(-m.extinction * dt);\n"
    "    let up = pos / max(r_p, 1e-4);\n"
    "    let mu_sun = clamp(dot(up, sun), -1.0, 1.0);\n"
    "    let shadow = earth_shadow_factor(r_p, mu_sun, u);\n"
    "    let sun_trans = sample_transmittance_lut(trans_lut, lut_sampler, r_p, mu_sun, u) * shadow;\n"
    "    let s_int = (m.scattering - m.scattering * step_trans) / max(m.extinction, vec3<f32>(1e-6));\n"
    "    let phase_iso = 1.0 / (4.0 * PI);\n"
    "    L = L + throughput * (sun_trans * phase_iso * m.scattering) * dt;\n"
    "    f_ms = f_ms + throughput * s_int;\n"
    "    throughput = throughput * step_trans;\n"
    "  }\n"
    "  if (hit_ground) {\n"
    "    let ground_point = ro + rd * t_max;\n"
    "    let up = ground_point / max(length(ground_point), 1e-4);\n"
    "    let mu_sun = clamp(dot(up, sun), -1.0, 1.0);\n"
    "    if (mu_sun > 0.0) {\n"
    "      let t_g = sample_transmittance_lut(trans_lut, lut_sampler,\n"
    "        bottomR(u), mu_sun, u);\n"
    "      L = L + throughput * t_g * u.ground_albedo.rgb * mu_sun / PI;\n"
    "    }\n"
    "  }\n"
    "  return vec4<f32>(L, (f_ms.x + f_ms.y + f_ms.z) / 3.0);\n"
    "}\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let p = ms_uv_to_params(in.uv, u);\n"
    "  let cos_sun = p.x;\n"
    "  let alt = p.y;\n"
    "  let r = bottomR(u) + max(alt, PLANET_OFFSET);\n"
    "  let ro = vec3<f32>(0.0, r, 0.0);\n"
    "  let sun = vec3<f32>(sqrt(max(0.0, 1.0 - cos_sun * cos_sun)), cos_sun, 0.0);\n"
    "  var L_2 = vec3<f32>(0.0);\n"
    "  var F = 0.0;\n"
    "  let sqrt_count = MS_DIR_SQRT;\n"
    "  let inv_samples = 1.0 / (f32(sqrt_count) * f32(sqrt_count));\n"
    "  for (var i : i32 = 0; i < sqrt_count; i = i + 1) {\n"
    "    for (var j : i32 = 0; j < sqrt_count; j = j + 1) {\n"
    "      let u01 = (f32(i) + 0.5) / f32(sqrt_count);\n"
    "      let v01 = (f32(j) + 0.5) / f32(sqrt_count);\n"
    "      let phi = 2.0 * PI * u01;\n"
    "      let cos_theta = 1.0 - 2.0 * v01;\n"
    "      let sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));\n"
    "      let dir = vec3<f32>(sin_theta * cos(phi), cos_theta, sin_theta * sin(phi));\n"
    "      let s = integrate_scattered(ro, dir, sun, u);\n"
    "      L_2 = L_2 + s.rgb * inv_samples;\n"
    "      F = F + s.w * inv_samples;\n"
    "    }\n"
    "  }\n"
    "  let psi_ms = L_2 / max(1.0 - F, 1e-4);\n"
    "  return vec4<f32>(psi_ms, 1.0);\n"
    "}\n";

static const char *kSkyViewShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "@group(0) @binding(0) var<uniform> u : AtmosUniforms;\n"
    "@group(0) @binding(1) var trans_lut : texture_2d<f32>;\n"
    "@group(0) @binding(2) var ms_lut : texture_2d<f32>;\n"
    "@group(0) @binding(3) var lut_sampler : sampler;\n"
    "const SV_STEPS : i32 = 30;\n"
    "fn integrate_skyview(ro : vec3<f32>, rd : vec3<f32>, u : AtmosUniforms) -> vec3<f32> {\n"
    "  let t_top = ray_sphere_nearest(ro, rd, topR(u));\n"
    "  let t_ground_raw = ray_sphere_nearest(ro, rd, bottomR(u));\n"
    "  var t_max = t_top;\n"
    "  var hit_ground = false;\n"
    "  if (t_ground_raw > 0.0 && t_ground_raw < t_max) {\n"
    "    t_max = t_ground_raw;\n"
    "    hit_ground = true;\n"
    "  }\n"
    "  if (t_max <= 0.0) { return vec3<f32>(0.0); }\n"
    "  let dt = t_max / f32(SV_STEPS);\n"
    "  let sun = u.sun_direction.xyz;\n"
    "  let moon = u.moon_direction.xyz;\n"
    "  let cos_sv = clamp(dot(rd, sun), -1.0, 1.0);\n"
    "  let cos_mv = clamp(dot(rd, moon), -1.0, 1.0);\n"
    "  let mie_g = u.mie_params.x;\n"
    "  var throughput = vec3<f32>(1.0);\n"
    "  var L = vec3<f32>(0.0);\n"
    "  for (var i : i32 = 0; i < SV_STEPS; i = i + 1) {\n"
    "    let t = (f32(i) + 0.5) * dt;\n"
    "    let pos = ro + rd * t;\n"
    "    let r_p = length(pos);\n"
    "    let alt = r_p - bottomR(u);\n"
    "    let m = sample_medium(max(alt, 0.0), u);\n"
    "    let step_trans = exp(-m.extinction * dt);\n"
    "    let up = pos / max(r_p, 1e-4);\n"
    "    let mu_sun = clamp(dot(up, sun), -1.0, 1.0);\n"
    "    let shadow_sun = earth_shadow_factor(r_p, mu_sun, u);\n"
    "    let sun_trans = sample_transmittance_lut(trans_lut, lut_sampler, r_p, mu_sun, u) * shadow_sun;\n"
    "    let scat_r_sun = m.scattering_rayleigh * rayleigh_phase(cos_sv);\n"
    "    let scat_m_sun = m.scattering_mie * mie_phase(cos_sv, mie_g);\n"
    "    let single_sun = (scat_r_sun + scat_m_sun) * sun_trans;\n"
    "    let ms_uv = ms_params_to_uv(mu_sun, r_p, u);\n"
    "    let psi_ms = textureSampleLevel(ms_lut, lut_sampler, ms_uv, 0.0).rgb;\n"
    "    let multi = psi_ms * m.scattering;\n"
    "    var in_scat = (single_sun + multi) * u.sun_color.rgb;\n"
    "    let mu_moon = clamp(dot(up, moon), -1.0, 1.0);\n"
    "    let shadow_moon = earth_shadow_factor(r_p, mu_moon, u);\n"
    "    let moon_trans = sample_transmittance_lut(trans_lut, lut_sampler, r_p, mu_moon, u) * shadow_moon;\n"
    "    let scat_r_moon = m.scattering_rayleigh * rayleigh_phase(cos_mv);\n"
    "    let scat_m_moon = m.scattering_mie * mie_phase(cos_mv, mie_g);\n"
    "    let single_moon = (scat_r_moon + scat_m_moon) * moon_trans;\n"
    "    in_scat = in_scat + single_moon * u.moon_radiance.rgb;\n"
    "    let s_int = (in_scat - in_scat * step_trans) / max(m.extinction, vec3<f32>(1e-6));\n"
    "    L = L + throughput * s_int;\n"
    "    throughput = throughput * step_trans;\n"
    "  }\n"
    "  return L;\n"
    "}\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let alt_km = u.camera_pos_world.w;\n"
    "  let r = bottomR(u) + max(alt_km, PLANET_OFFSET);\n"
    "  let ro = vec3<f32>(0.0, r, 0.0);\n"
    "  let rd = skyview_uv_to_dir(in.uv, r, u);\n"
    "  var L = integrate_skyview(ro, rd, u);\n"
    "  let night_amount = 1.0 - smoothstep(-0.05, 0.02, u.sun_direction.y);\n"
    "  L = L + u.night_tint.rgb * night_amount;\n"
    "  return vec4<f32>(L, 1.0);\n"
    "}\n";

static const char *kCubeFaceComputeShaderSource =
    "@group(0) @binding(0) var<uniform> u : AtmosUniforms;\n"
    "@group(0) @binding(1) var skyview_lut : texture_2d<f32>;\n"
    "@group(0) @binding(2) var lut_sampler : sampler;\n"
    "@group(0) @binding(3) var cube_out : texture_storage_2d_array<rgba16float, write>;\n"
    "fn face_direction(idx : u32, uv : vec2<f32>) -> vec3<f32> {\n"
    "  let x = uv.x * 2.0 - 1.0;\n"
    "  let y = 1.0 - uv.y * 2.0;\n"
    "  var d : vec3<f32>;\n"
    "  switch (idx) {\n"
    "    case 0u: { d = vec3<f32>( 1.0,  y, -x); }\n"
    "    case 1u: { d = vec3<f32>(-1.0,  y,  x); }\n"
    "    case 2u: { d = vec3<f32>( x,  1.0, -y); }\n"
    "    case 3u: { d = vec3<f32>( x, -1.0,  y); }\n"
    "    case 4u: { d = vec3<f32>( x,  y,  1.0); }\n"
    "    default: { d = vec3<f32>(-x,  y, -1.0); }\n"
    "  }\n"
    "  return normalize(d);\n"
    "}\n"
    "@compute @workgroup_size(8, 8, 1)\n"
    "fn cs_main(@builtin(global_invocation_id) gid : vec3<u32>) {\n"
    "  let dims = textureDimensions(cube_out);\n"
    "  if (gid.x >= dims.x || gid.y >= dims.y || gid.z >= 6u) { return; }\n"
    "  let uv = (vec2<f32>(f32(gid.x), f32(gid.y)) + vec2<f32>(0.5)) /\n"
    "    vec2<f32>(f32(dims.x), f32(dims.y));\n"
    "  let dir = face_direction(gid.z, uv);\n"
    "  let alt_km = u.camera_pos_world.w;\n"
    "  let view_r = bottomR(u) + max(alt_km, PLANET_OFFSET);\n"
    "  let sv_uv = dir_to_skyview_uv(dir, view_r, u);\n"
    "  let sky = textureSampleLevel(skyview_lut, lut_sampler, sv_uv, 0.0).rgb;\n"
    "  textureStore(cube_out, vec2<i32>(i32(gid.x), i32(gid.y)),\n"
    "    i32(gid.z), vec4<f32>(sky, 1.0));\n"
    "}\n";

static const char *kCubeDownsampleComputeShaderSource =
    "@group(0) @binding(0) var input_cube : texture_2d_array<f32>;\n"
    "@group(0) @binding(1) var lin_samp : sampler;\n"
    "@group(0) @binding(2) var output_cube : texture_storage_2d_array<rgba16float, write>;\n"
    "@compute @workgroup_size(8, 8, 1)\n"
    "fn cs_main(@builtin(global_invocation_id) gid : vec3<u32>) {\n"
    "  let dims = textureDimensions(output_cube);\n"
    "  if (gid.x >= dims.x || gid.y >= dims.y || gid.z >= 6u) { return; }\n"
    "  let uv = (vec2<f32>(f32(gid.x), f32(gid.y)) + vec2<f32>(0.5)) /\n"
    "    vec2<f32>(f32(dims.x), f32(dims.y));\n"
    "  let c = textureSampleLevel(input_cube, lin_samp, uv, i32(gid.z), 0.0);\n"
    "  textureStore(output_cube, vec2<i32>(i32(gid.x), i32(gid.y)),\n"
    "    i32(gid.z), c);\n"
    "}\n";

static const char *kAerialComputeShaderSource =
    "@group(0) @binding(0) var<uniform> u : AtmosUniforms;\n"
    "@group(0) @binding(1) var trans_lut : texture_2d<f32>;\n"
    "@group(0) @binding(2) var ms_lut : texture_2d<f32>;\n"
    "@group(0) @binding(3) var lut_sampler : sampler;\n"
    "@group(0) @binding(4) var aerial_out : texture_storage_2d_array<rgba16float, write>;\n"
    "@group(0) @binding(5) var skyview_lut : texture_2d<f32>;\n"
    "const AP_STEPS : i32 = 6;\n"
    "@compute @workgroup_size(8, 8, 1)\n"
    "fn cs_main(@builtin(global_invocation_id) gid : vec3<u32>) {\n"
    "  let dims = textureDimensions(aerial_out);\n"
    "  if (gid.x >= dims.x || gid.y >= dims.y || gid.z >= u32(u.misc.x)) { return; }\n"
    "  let uv = (vec2<f32>(f32(gid.x), f32(gid.y)) + vec2<f32>(0.5)) /\n"
    "    vec2<f32>(f32(dims.x), f32(dims.y));\n"
    "  let ndc = vec4<f32>(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, 1.0, 1.0);\n"
    "  let world_h = u.inv_vp * ndc;\n"
    "  var world_pos = world_h.xyz;\n"
    "  if (abs(world_h.w) > 1e-6) { world_pos = world_h.xyz / world_h.w; }\n"
    "  let view_dir_world = normalize(world_pos - u.camera_pos_world.xyz);\n"
    "  let alt_km = u.camera_pos_world.w;\n"
    "  let r0 = bottomR(u) + max(alt_km, PLANET_OFFSET);\n"
    "  let ro = vec3<f32>(0.0, r0, 0.0);\n"
    "  let rd = view_dir_world;\n"
    "  let max_dist_km = u.mie_params.y;\n"
    "  let slice_count = u.misc.x;\n"
    "  let slice_t = (f32(gid.z) + 1.0) / slice_count;\n"
    "  let t_end_km = slice_t * max_dist_km;\n"
    "  let t_top = ray_sphere_nearest(ro, rd, topR(u));\n"
    "  var t_max = t_end_km;\n"
    "  if (t_top > 0.0 && t_top < t_max) { t_max = t_top; }\n"
    "  if (t_max <= 0.0) {\n"
    "    textureStore(aerial_out, vec2<i32>(i32(gid.x), i32(gid.y)),\n"
    "      i32(gid.z), vec4<f32>(0.0, 0.0, 0.0, 1.0));\n"
    "    return;\n"
    "  }\n"
    "  let dt = t_max / f32(AP_STEPS);\n"
    "  let sun = u.sun_direction.xyz;\n"
    "  let moon = u.moon_direction.xyz;\n"
    "  let cos_sv = clamp(dot(rd, sun), -1.0, 1.0);\n"
    "  let cos_mv = clamp(dot(rd, moon), -1.0, 1.0);\n"
    "  let mie_g = u.mie_params.x;\n"
    "  let density_k = u.aerial_params.x;\n"
    "  var throughput = vec3<f32>(1.0);\n"
    "  var L = vec3<f32>(0.0);\n"
    "  for (var i : i32 = 0; i < AP_STEPS; i = i + 1) {\n"
    "    let t = (f32(i) + 0.5) * dt;\n"
    "    let pos = ro + rd * t;\n"
    "    let r_p = length(pos);\n"
    "    let alt = r_p - bottomR(u);\n"
    "    var m = sample_medium(max(alt, 0.0), u);\n"
    "    m.scattering = m.scattering * density_k;\n"
    "    m.scattering_rayleigh = m.scattering_rayleigh * density_k;\n"
    "    m.scattering_mie = m.scattering_mie * density_k;\n"
    "    m.extinction = m.extinction * density_k;\n"
    "    let step_trans = exp(-m.extinction * dt);\n"
    "    let up = pos / max(r_p, 1e-4);\n"
    "    let mu_sun = clamp(dot(up, sun), -1.0, 1.0);\n"
    "    let shadow_sun = earth_shadow_factor(r_p, mu_sun, u);\n"
    "    let sun_trans = sample_transmittance_lut(trans_lut, lut_sampler, r_p, mu_sun, u) * shadow_sun;\n"
    "    let scat_r_sun = m.scattering_rayleigh * rayleigh_phase(cos_sv);\n"
    "    let scat_m_sun = m.scattering_mie * mie_phase(cos_sv, mie_g);\n"
    "    let single_sun = (scat_r_sun + scat_m_sun) * sun_trans;\n"
    "    let ms_uv = ms_params_to_uv(mu_sun, r_p, u);\n"
    "    let psi_ms = textureSampleLevel(ms_lut, lut_sampler, ms_uv, 0.0).rgb;\n"
    "    let multi = psi_ms * m.scattering;\n"
    "    var in_scat = (single_sun + multi) * u.sun_color.rgb;\n"
    "    let mu_moon = clamp(dot(up, moon), -1.0, 1.0);\n"
    "    let shadow_moon = earth_shadow_factor(r_p, mu_moon, u);\n"
    "    let moon_trans = sample_transmittance_lut(trans_lut, lut_sampler, r_p, mu_moon, u) * shadow_moon;\n"
    "    let scat_r_moon = m.scattering_rayleigh * rayleigh_phase(cos_mv);\n"
    "    let scat_m_moon = m.scattering_mie * mie_phase(cos_mv, mie_g);\n"
    "    let single_moon = (scat_r_moon + scat_m_moon) * moon_trans;\n"
    "    in_scat = in_scat + single_moon * u.moon_radiance.rgb;\n"
    "    let s_int = (in_scat - in_scat * step_trans) / max(m.extinction, vec3<f32>(1e-6));\n"
    "    L = L + throughput * s_int;\n"
    "    throughput = throughput * step_trans;\n"
    "  }\n"
    "  let t_avg = (throughput.x + throughput.y + throughput.z) / 3.0;\n"
    "  let horizon_u = atan2(rd.x, rd.z) / (2.0 * PI) + 0.5;\n"
    "  let horizon_uv = vec2<f32>(clamp(horizon_u, 0.0, 1.0), 0.5);\n"
    "  let horizon_color = textureSampleLevel(skyview_lut, lut_sampler, horizon_uv, 0.0).rgb;\n"
    "  let fog_amount = clamp(1.0 - t_avg, 0.0, 1.0);\n"
    "  let L_out = mix(L, horizon_color, fog_amount);\n"
    "  textureStore(aerial_out, vec2<i32>(i32(gid.x), i32(gid.y)),\n"
    "    i32(gid.z), vec4<f32>(L_out, t_avg));\n"
    "}\n";

static const char *kComposeShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "@group(0) @binding(0) var input_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var input_sampler : sampler;\n"
    "@group(0) @binding(2) var depth_texture : texture_depth_2d;\n"
    "@group(0) @binding(3) var<uniform> u : AtmosUniforms;\n"
    "@group(0) @binding(4) var trans_lut : texture_2d<f32>;\n"
    "@group(0) @binding(5) var skyview_lut : texture_2d<f32>;\n"
    "@group(0) @binding(6) var aerial_lut : texture_2d_array<f32>;\n"
    "@group(0) @binding(7) var lut_sampler : sampler;\n"
    "fn reconstruct_world_pos(uv : vec2<f32>, depth : f32) -> vec3<f32> {\n"
    "  let ndc = vec4<f32>(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0, depth, 1.0);\n"
    "  let h = u.inv_vp * ndc;\n"
    "  if (abs(h.w) > 1e-6) { return h.xyz / h.w; }\n"
    "  return h.xyz;\n"
    "}\n"
    "fn stars_hash31(p : vec3<f32>) -> f32 {\n"
    "  var p3 = fract(p * 0.1031);\n"
    "  p3 = p3 + dot(p3, p3.yzx + 33.33);\n"
    "  return fract((p3.x + p3.y) * p3.z);\n"
    "}\n"
    "fn stars_hash32(p : vec3<f32>) -> vec2<f32> {\n"
    "  var p3 = fract(p * vec3<f32>(0.1031, 0.1030, 0.0973));\n"
    "  p3 = p3 + dot(p3, p3.yxz + 33.33);\n"
    "  return fract(vec2<f32>((p3.x + p3.y) * p3.z,\n"
    "                         (p3.x + p3.z) * p3.y));\n"
    "}\n"
    "fn stars_cube_face_uv(rd : vec3<f32>) -> vec3<f32> {\n"
    "  let a = abs(rd);\n"
    "  var uv : vec2<f32>;\n"
    "  var face : f32;\n"
    "  if (a.x >= a.y && a.x >= a.z) {\n"
    "    if (rd.x > 0.0) { face = 0.0; uv = vec2<f32>(-rd.z, -rd.y) / a.x; }\n"
    "    else            { face = 1.0; uv = vec2<f32>( rd.z, -rd.y) / a.x; }\n"
    "  } else if (a.y >= a.z) {\n"
    "    if (rd.y > 0.0) { face = 2.0; uv = vec2<f32>( rd.x,  rd.z) / a.y; }\n"
    "    else            { face = 3.0; uv = vec2<f32>( rd.x, -rd.z) / a.y; }\n"
    "  } else {\n"
    "    if (rd.z > 0.0) { face = 4.0; uv = vec2<f32>( rd.x, -rd.y) / a.z; }\n"
    "    else            { face = 5.0; uv = vec2<f32>(-rd.x, -rd.y) / a.z; }\n"
    "  }\n"
    "  return vec3<f32>(uv * 0.5 + 0.5, face);\n"
    "}\n"
    "fn stars_palette(t : f32) -> vec3<f32> {\n"
    "  let o_type = vec3<f32>(0.35, 0.58, 1.00);\n"
    "  let a_type = vec3<f32>(0.80, 0.92, 1.00);\n"
    "  let f_type = vec3<f32>(1.00, 0.97, 0.92);\n"
    "  let g_type = vec3<f32>(1.00, 0.90, 0.65);\n"
    "  let k_type = vec3<f32>(1.00, 0.62, 0.28);\n"
    "  let m_type = vec3<f32>(1.00, 0.25, 0.10);\n"
    "  if (t < 0.20) { return mix(o_type, a_type, t * 5.0); }\n"
    "  if (t < 0.40) { return mix(a_type, f_type, (t - 0.20) * 5.0); }\n"
    "  if (t < 0.60) { return mix(f_type, g_type, (t - 0.40) * 5.0); }\n"
    "  if (t < 0.80) { return mix(g_type, k_type, (t - 0.60) * 5.0); }\n"
    "  return mix(k_type, m_type, (t - 0.80) * 5.0);\n"
    "}\n"
    "fn sample_stars(rd_world : vec3<f32>, u : AtmosUniforms) -> vec3<f32> {\n"
    "  let cells = u.star_rot_0.w;\n"
    "  let thresh = u.star_rot_1.w;\n"
    "  let size_k = u.star_rot_2.w;\n"
    "  let variation = clamp(u.star_color.a, 0.0, 1.0);\n"
    "  if (cells <= 0.0 || thresh >= 1.0) { return vec3<f32>(0.0); }\n"
    "  let rd = vec3<f32>(\n"
    "    dot(u.star_rot_0.xyz, rd_world),\n"
    "    dot(u.star_rot_1.xyz, rd_world),\n"
    "    dot(u.star_rot_2.xyz, rd_world));\n"
    "  let fc = stars_cube_face_uv(rd);\n"
    "  let face = fc.z;\n"
    "  let p = fc.xy * cells;\n"
    "  let cid = floor(p);\n"
    "  var col = vec3<f32>(0.0);\n"
    "  let inv_span = 1.0 / max(1.0 - thresh, 1e-4);\n"
    "  for (var dy : i32 = -1; dy <= 1; dy = dy + 1) {\n"
    "    for (var dx : i32 = -1; dx <= 1; dx = dx + 1) {\n"
    "      let nid = cid + vec2<f32>(f32(dx), f32(dy));\n"
    "      let seed = vec3<f32>(nid, face * 17.0);\n"
    "      let ex = stars_hash31(seed + vec3<f32>(0.17, 0.31, 0.53));\n"
    "      if (ex > thresh) {\n"
    "        let off = stars_hash32(seed + vec3<f32>(4.1, 7.3, 1.9));\n"
    "        let sp = nid + off;\n"
    "        let d = p - sp;\n"
    "        let d2 = dot(d, d);\n"
    "        let mag = (ex - thresh) * inv_span;\n"
    "        let bright = pow(mag, 3.0);\n"
    "        let profile = exp(-d2 * size_k);\n"
    "        let th_raw = stars_hash31(seed + vec3<f32>(9.7, 3.2, 6.6));\n"
    "        let th_signed = th_raw * 2.0 - 1.0;\n"
    "        let th_biased = sign(th_signed) * pow(abs(th_signed), 0.55);\n"
    "        let th = th_biased * 0.5 + 0.5;\n"
    "        let pal = stars_palette(th);\n"
    "        let tint = mix(vec3<f32>(1.0), pal, variation);\n"
    "        col = col + tint * profile * bright;\n"
    "      }\n"
    "    }\n"
    "  }\n"
    "  return col * u.star_color.rgb;\n"
    "}\n"
    "fn atmos_wobble(rd : vec3<f32>, t_in : f32, amount : f32, spatial : f32, speed : f32) -> vec3<f32> {\n"
    "  if (amount <= 0.0) { return rd; }\n"
    "  let t = t_in * speed;\n"
    "  let p = rd * spatial;\n"
    "  let s1 = sin(p + vec3<f32>(t * 0.73, t * 0.91, t * 1.13));\n"
    "  let s2 = sin(p.yzx * 1.37 + vec3<f32>(t * 1.21, t * 0.83, t * 0.97));\n"
    "  let d_iso = (s1 + 0.5 * s2) * 0.666;\n"
    "  let d = vec3<f32>(d_iso.x * 0.05, d_iso.y, d_iso.z * 0.05);\n"
    "  let horizon_boost = 0.3 + 0.7 * (1.0 - clamp(rd.y, -0.05, 1.0));\n"
    "  return normalize(rd + d * 0.008 * amount * horizon_boost);\n"
    "}\n"
    "fn sample_aerial(uv : vec2<f32>, d_km : f32, max_km : f32, slice_count : f32) -> vec4<f32> {\n"
    "  let sf = clamp(d_km / max_km, 0.0, 1.0) * slice_count - 1.0;\n"
    "  let lo = i32(floor(sf));\n"
    "  let hi = lo + 1;\n"
    "  let f = sf - f32(lo);\n"
    "  let identity = vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
    "  var a = identity;\n"
    "  var b = identity;\n"
    "  if (lo >= 0) {\n"
    "    a = textureSampleLevel(aerial_lut, lut_sampler, uv, lo, 0.0);\n"
    "  }\n"
    "  if (hi >= 0) {\n"
    "    b = textureSampleLevel(aerial_lut, lut_sampler, uv,\n"
    "      min(hi, i32(slice_count) - 1), 0.0);\n"
    "  }\n"
    "  return mix(a, b, f);\n"
    "}\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let src = textureSample(input_texture, input_sampler, in.uv);\n"
    "  let dims = textureDimensions(depth_texture);\n"
    "  let dims_f = vec2<f32>(f32(dims.x), f32(dims.y));\n"
    "  let clamped_uv = clamp(in.uv, vec2<f32>(0.0), vec2<f32>(0.9999));\n"
    "  let texel = vec2<i32>(clamped_uv * dims_f);\n"
    "  let depth = textureLoad(depth_texture, texel, 0);\n"
    "  let is_sky = depth >= 0.9999;\n"
    "  if (is_sky) {\n"
    "    let world_pos = reconstruct_world_pos(in.uv, 1.0);\n"
    "    let rd = normalize(world_pos - u.camera_pos_world.xyz);\n"
    "    let rd_w = atmos_wobble(rd, u.misc.z, u.misc.y, u.misc.w, u.aerial_params.y);\n"
    "    let alt_km = u.camera_pos_world.w;\n"
    "    let view_r = bottomR(u) + max(alt_km, PLANET_OFFSET);\n"
    "    var sv_uv = dir_to_skyview_uv(rd, view_r, u);\n"
    "    sv_uv.y = min(sv_uv.y, 0.5);\n"
    "    var sky = textureSampleLevel(skyview_lut, lut_sampler, sv_uv, 0.0).rgb;\n"
    "    let sun_cos = dot(rd_w, u.sun_direction.xyz);\n"
    "    let disk_cos = u.mie_params.z;\n"
    "    let disk_int = u.mie_params.w;\n"
    "    if (disk_int > 0.0 && sun_cos > disk_cos - 0.001) {\n"
    "      let ro = vec3<f32>(0.0, view_r, 0.0);\n"
    "      let t_ground = ray_sphere_nearest(ro, rd, bottomR(u));\n"
    "      if (t_ground < 0.0) {\n"
    "        let mu = clamp(u.sun_direction.y, -1.0, 1.0);\n"
    "        let t_sun = sample_transmittance_lut(trans_lut, lut_sampler, view_r, mu, u);\n"
    "        let aa_w = max(fwidth(sun_cos), 1e-6);\n"
    "        let aa = smoothstep(disk_cos - aa_w, disk_cos + aa_w, sun_cos);\n"
    "        sky = sky + t_sun * u.sun_color.rgb * disk_int * aa;\n"
    "      }\n"
    "    }\n"
    "    let moon = u.moon_direction.xyz;\n"
    "    let moon_cos = dot(rd_w, moon);\n"
    "    if (moon_cos > disk_cos - 0.001) {\n"
    "      let ro = vec3<f32>(0.0, view_r, 0.0);\n"
    "      let t_ground = ray_sphere_nearest(ro, rd, bottomR(u));\n"
    "      if (t_ground < 0.0) {\n"
    "        let mu = clamp(u.moon_direction.y, -1.0, 1.0);\n"
    "        let t_moon = sample_transmittance_lut(trans_lut, lut_sampler, view_r, mu, u);\n"
    "        let sin_r = sqrt(max(0.0, 1.0 - disk_cos * disk_cos));\n"
    "        let D = 1.0 / max(sin_r, 1e-6);\n"
    "        let b = D * moon_cos;\n"
    "        let disc = max(0.0, b * b - (D * D - 1.0));\n"
    "        let t_hit = b - sqrt(disc);\n"
    "        let p = t_hit * rd_w;\n"
    "        let n = normalize(p - D * moon);\n"
    "        let n_dot_l = max(0.0, dot(n, u.sun_direction.xyz));\n"
    "        let moon_albedo = 0.12;\n"
    "        let aa_w = max(fwidth(moon_cos), 1e-6);\n"
    "        let aa = smoothstep(disk_cos - aa_w, disk_cos + aa_w, moon_cos);\n"
    "        sky = sky + t_moon * u.sun_color.rgb * moon_albedo * n_dot_l * aa;\n"
    "      }\n"
    "    }\n"
    "    let mu_view = clamp(rd.y, -1.0, 1.0);\n"
    "    let t_view = sample_transmittance_lut(trans_lut, lut_sampler, view_r, mu_view, u);\n"
    "    let moon_fade = 1.0 - smoothstep(0.9975, 0.9998, moon_cos);\n"
    "    let stars = sample_stars(rd, u) * t_view * moon_fade;\n"
    "    sky = sky + stars;\n"
    "    return vec4<f32>(sky, 1.0);\n"
    "  }\n"
    "  let world_pos = reconstruct_world_pos(in.uv, depth);\n"
    "  let ray = world_pos - u.camera_pos_world.xyz;\n"
    "  let dist_world = length(ray);\n"
    "  let dist_km = dist_world * u.world_scale.y;\n"
    "  let max_km = u.mie_params.y;\n"
    "  let slice_count = u.misc.x;\n"
    "  let ap = sample_aerial(in.uv, dist_km, max_km, slice_count);\n"
    "  var composed = src.rgb * ap.a + ap.rgb;\n"
    "  return vec4<f32>(composed, src.a);\n"
    "}\n";

static void flecsEngine_atmos_releaseTextures(FlecsAtmosphereImpl *a)
{
    FLECS_WGPU_RELEASE(a->aerial_storage_view, wgpuTextureViewRelease);
    FLECS_WGPU_RELEASE(a->aerial_view, wgpuTextureViewRelease);
    FLECS_WGPU_RELEASE(a->aerial_texture, wgpuTextureRelease);
    FLECS_WGPU_RELEASE(a->skyview_view, wgpuTextureViewRelease);
    FLECS_WGPU_RELEASE(a->skyview_texture, wgpuTextureRelease);
    FLECS_WGPU_RELEASE(a->ms_view, wgpuTextureViewRelease);
    FLECS_WGPU_RELEASE(a->ms_texture, wgpuTextureRelease);
    FLECS_WGPU_RELEASE(a->trans_view, wgpuTextureViewRelease);
    FLECS_WGPU_RELEASE(a->trans_texture, wgpuTextureRelease);
}

static void flecsEngine_atmos_releaseResources(FlecsAtmosphereImpl *a)
{
    flecsEngine_atmos_releaseTextures(a);

    flecsEngine_bindGroup_release(&a->compose_bind_group);
    FLECS_WGPU_RELEASE(a->trans_bind_group, wgpuBindGroupRelease);
    FLECS_WGPU_RELEASE(a->ms_bind_group, wgpuBindGroupRelease);
    FLECS_WGPU_RELEASE(a->skyview_bind_group, wgpuBindGroupRelease);
    FLECS_WGPU_RELEASE(a->aerial_compute_bind_group, wgpuBindGroupRelease);
    FLECS_WGPU_RELEASE(a->cube_face_bind_group, wgpuBindGroupRelease);
    for (uint32_t m = 0; m < 8; m++) {
        FLECS_WGPU_RELEASE(a->cube_ds_bind_groups[m], wgpuBindGroupRelease);
        FLECS_WGPU_RELEASE(a->cube_mip_storage_views[m], wgpuTextureViewRelease);
        FLECS_WGPU_RELEASE(a->cube_mip_read_views[m], wgpuTextureViewRelease);
    }
    a->cube_mip_count = 0;
    FLECS_WGPU_RELEASE(a->cube_ds_pipeline, wgpuComputePipelineRelease);
    FLECS_WGPU_RELEASE(a->cube_ds_layout, wgpuBindGroupLayoutRelease);
    FLECS_WGPU_RELEASE(a->cube_face_pipeline, wgpuComputePipelineRelease);
    FLECS_WGPU_RELEASE(a->cube_face_layout, wgpuBindGroupLayoutRelease);
    FLECS_WGPU_RELEASE(a->aerial_compute_pipeline, wgpuComputePipelineRelease);
    FLECS_WGPU_RELEASE(a->aerial_compute_layout, wgpuBindGroupLayoutRelease);
    FLECS_WGPU_RELEASE(a->compose_pipeline_surface, wgpuRenderPipelineRelease);
    FLECS_WGPU_RELEASE(a->compose_pipeline_hdr, wgpuRenderPipelineRelease);
    FLECS_WGPU_RELEASE(a->skyview_pipeline, wgpuRenderPipelineRelease);
    FLECS_WGPU_RELEASE(a->ms_pipeline, wgpuRenderPipelineRelease);
    FLECS_WGPU_RELEASE(a->trans_pipeline, wgpuRenderPipelineRelease);
    FLECS_WGPU_RELEASE(a->compose_layout, wgpuBindGroupLayoutRelease);
    FLECS_WGPU_RELEASE(a->skyview_layout, wgpuBindGroupLayoutRelease);
    FLECS_WGPU_RELEASE(a->ms_layout, wgpuBindGroupLayoutRelease);
    FLECS_WGPU_RELEASE(a->trans_layout, wgpuBindGroupLayoutRelease);
    FLECS_WGPU_RELEASE(a->clamp_sampler, wgpuSamplerRelease);
    FLECS_WGPU_RELEASE(a->uniform_buffer, wgpuBufferRelease);
}

ECS_DTOR(FlecsAtmosphereImpl, ptr, {
    flecsEngine_atmos_releaseResources(ptr);
})

ECS_MOVE(FlecsAtmosphereImpl, dst, src, {
    flecsEngine_atmos_releaseResources(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

ECS_CTOR(FlecsAtmosphere, ptr, {
    *ptr = (FlecsAtmosphere){
        .sun_disk_intensity = 1.0f,
        .sun_disk_angular_radius = 0.00465f,
        .aerial_perspective_distance_km = 32.0f,
        .aerial_perspective_intensity = 1.0f,
        .sea_level_y = 0.0f,
        .world_units_per_km = 1000.0f,
        .ground_altitude_km = 0.5f,
        .planet_radius_km = 6360.0f,
        .atmosphere_thickness_km = 100.0f,
        .rayleigh = {
            .scale_height_km = 8.0f,
            .scattering_scale = 1.0f,
        },
        .mie = {
            .scale_height_km = 1.2f,
            .anisotropy = 0.8f,
            .scattering_scale = 1.0f,
            .tint = { 255, 255, 255, 255 },
        },
        .ozone_scale = 1.0f,
        .stratospheric_aerosol_scale = 0.0f,
        .haze_absorption = 0.0f,
        .ground_albedo = { 77, 77, 77, 255 },
        .night_tint = { 70, 110, 230, 255 },
        .night_intensity = 0.003f,
        .turbulence = 0.0f,
        .turbulence_scale = 40.0f,
        .turbulence_speed = 1.0f
    };
})

static void flecsEngine_atmos_fillUniform(
    const ecs_world_t *world,
    ecs_entity_t view_entity,
    const FlecsAtmosphere *s,
    FlecsAtmosphereUniform *out)
{
    memset(out, 0, sizeof(*out));
    glm_mat4_identity(out->inv_vp);

    float planet_r = s->planet_radius_km;
    float top_r = planet_r + s->atmosphere_thickness_km;

    out->atmosphere[0] = planet_r;
    out->atmosphere[1] = top_r;
    out->atmosphere[2] = s->rayleigh.scale_height_km;
    out->atmosphere[3] = s->mie.scale_height_km;

    float rayleigh_scale = s->rayleigh.scattering_scale >= 0.0f
        ? s->rayleigh.scattering_scale : 1.0f;
    out->scat_rayleigh[0] = 0.005802f * rayleigh_scale;
    out->scat_rayleigh[1] = 0.013558f * rayleigh_scale;
    out->scat_rayleigh[2] = 0.033100f * rayleigh_scale;
    out->scat_rayleigh[3] = 0.0f;

    float mie_scale = s->mie.scattering_scale > 0.0f ? s->mie.scattering_scale : 1.0f;
    float mie_tint_r = flecsEngine_colorChannelToFloat(s->mie.tint.r);
    float mie_tint_g = flecsEngine_colorChannelToFloat(s->mie.tint.g);
    float mie_tint_b = flecsEngine_colorChannelToFloat(s->mie.tint.b);

    out->scat_mie[0] = 0.003996f * mie_scale * mie_tint_r;
    out->scat_mie[1] = 0.003996f * mie_scale * mie_tint_g;
    out->scat_mie[2] = 0.003996f * mie_scale * mie_tint_b;
    out->scat_mie[3] = 0.0f;
    out->ext_mie[0] = 0.004400f * mie_scale * mie_tint_r;
    out->ext_mie[1] = 0.004400f * mie_scale * mie_tint_g;
    out->ext_mie[2] = 0.004400f * mie_scale * mie_tint_b;
    out->ext_mie[3] = 0.0f;

    float ozone_scale = s->ozone_scale >= 0.0f ? s->ozone_scale : 1.0f;
    out->abs_ozone[0] = 0.000650f * ozone_scale;
    out->abs_ozone[1] = 0.001881f * ozone_scale;
    out->abs_ozone[2] = 0.000085f * ozone_scale;
    out->abs_ozone[3] = 0.0f;

    float strat = s->stratospheric_aerosol_scale > 0.0f
        ? s->stratospheric_aerosol_scale : 0.0f;
    out->abs_strat_aerosol[0] = 0.001950f * strat;
    out->abs_strat_aerosol[1] = 0.005643f * strat;
    out->abs_strat_aerosol[2] = 0.000255f * strat;
    out->abs_strat_aerosol[3] = 0.0f;

    float haze = s->haze_absorption > 0.0f ? s->haze_absorption : 0.0f;
    out->abs_haze[0] = 0.005f * haze;
    out->abs_haze[1] = 0.005f * haze;
    out->abs_haze[2] = 0.005f * haze;
    out->abs_haze[3] = 0.0f;

    out->ground_albedo[0] = flecsEngine_colorChannelToFloat(s->ground_albedo.r);
    out->ground_albedo[1] = flecsEngine_colorChannelToFloat(s->ground_albedo.g);
    out->ground_albedo[2] = flecsEngine_colorChannelToFloat(s->ground_albedo.b);
    out->ground_albedo[3] = 1.0f;

    float disk_half_angle = s->sun_disk_angular_radius > 1e-6f
        ? s->sun_disk_angular_radius : 0.00465f;
    out->mie_params[0] = s->mie.anisotropy;
    out->mie_params[1] = s->aerial_perspective_distance_km;
    out->mie_params[2] = cosf(disk_half_angle);
    out->mie_params[3] = s->sun_disk_intensity;

    float units_per_km = s->world_units_per_km > 1e-6f ? s->world_units_per_km : 1000.0f;
    out->world_scale[0] = units_per_km;
    out->world_scale[1] = 1.0f / units_per_km;
    out->world_scale[2] = s->sea_level_y;
    out->world_scale[3] = s->ground_altitude_km;

    out->misc[0] = (float)FLECS_ATMOS_AERIAL_SLICES;
    out->misc[1] = s->turbulence > 0.0f ? s->turbulence : 0.0f;
    const ecs_world_info_t *wi = ecs_get_world_info(world);
    out->misc[2] = wi ? (float)wi->world_time_total : 0.0f;
    out->misc[3] = s->turbulence_scale > 0.0f ? s->turbulence_scale : 40.0f;

    out->aerial_params[1] = s->turbulence_speed > 0.0f ? s->turbulence_speed : 1.0f;
    out->aerial_params[0] = s->aerial_perspective_intensity >= 0.0f
        ? s->aerial_perspective_intensity : 1.0f;

    float night_intensity = s->night_intensity >= 0.0f ? s->night_intensity : 0.0f;
    out->night_tint[0] =
        flecsEngine_colorChannelToFloat(s->night_tint.r) * night_intensity;
    out->night_tint[1] =
        flecsEngine_colorChannelToFloat(s->night_tint.g) * night_intensity;
    out->night_tint[2] =
        flecsEngine_colorChannelToFloat(s->night_tint.b) * night_intensity;
    out->night_tint[3] = 0.0f;

    out->camera_pos_world[3] = s->ground_altitude_km;

    float sun_toa = 0.0f;
    float sun_toa_r = 1.0f, sun_toa_g = 1.0f, sun_toa_b = 1.0f;
    if (s->sun) {
        const FlecsCelestialLight *cl = ecs_get(
            world, s->sun, FlecsCelestialLight);
        if (cl) {
            sun_toa = cl->toa_intensity;
            sun_toa_r = flecsEngine_colorChannelToFloat(cl->toa_color.r);
            sun_toa_g = flecsEngine_colorChannelToFloat(cl->toa_color.g);
            sun_toa_b = flecsEngine_colorChannelToFloat(cl->toa_color.b);
        }
    }

    out->sun_direction[0] = 0.0f;
    out->sun_direction[1] = 1.0f;
    out->sun_direction[2] = 0.0f;
    out->sun_direction[3] = sun_toa;

    out->sun_color[0] = sun_toa * sun_toa_r;
    out->sun_color[1] = sun_toa * sun_toa_g;
    out->sun_color[2] = sun_toa * sun_toa_b;
    out->sun_color[3] = 1.0f;

    if (!view_entity) return;
    const FlecsRenderView *view = ecs_get(world, view_entity, FlecsRenderView);
    if (!view) return;

    if (view->camera) {
        const FlecsCameraImpl *cam = ecs_get(world, view->camera, FlecsCameraImpl);
        if (cam) {
            mat4 mvp;
            glm_mat4_copy((vec4*)cam->mvp, mvp);
            glm_mat4_inv(mvp, out->inv_vp);
        }
        const FlecsWorldTransform3 *cam_tf = ecs_get(
            world, view->camera, FlecsWorldTransform3);
        if (cam_tf) {
            out->camera_pos_world[0] = cam_tf->m[3][0];
            out->camera_pos_world[1] = cam_tf->m[3][1];
            out->camera_pos_world[2] = cam_tf->m[3][2];
            float alt_km = s->ground_altitude_km +
                (cam_tf->m[3][1] - s->sea_level_y) / units_per_km;
            if (alt_km < 0.001f) alt_km = 0.001f;
            out->camera_pos_world[3] = alt_km;
        }
    }

    if (s->sun) {
        const FlecsRotation3 *rot = ecs_get(world, s->sun, FlecsRotation3);
        if (rot) {
            float ray_dir[3];
            if (flecsEngine_lightDirFromRotation(rot, ray_dir)) {
                out->sun_direction[0] = -ray_dir[0];
                out->sun_direction[1] = -ray_dir[1];
                out->sun_direction[2] = -ray_dir[2];
            }
        }
    }

    out->moon_direction[0] = 0.0f;
    out->moon_direction[1] = -1.0f;
    out->moon_direction[2] = 0.0f;
    out->moon_direction[3] = 0.0f;
    out->moon_radiance[0] = 0.0f;
    out->moon_radiance[1] = 0.0f;
    out->moon_radiance[2] = 0.0f;
    out->moon_radiance[3] = 0.0f;

    if (s->moon) {
        const FlecsRotation3 *mrot = ecs_get(
            world, s->moon, FlecsRotation3);
        if (mrot) {
            float ray_dir[3];
            if (flecsEngine_lightDirFromRotation(mrot, ray_dir)) {
                out->moon_direction[0] = -ray_dir[0];
                out->moon_direction[1] = -ray_dir[1];
                out->moon_direction[2] = -ray_dir[2];
            }
        }

        const FlecsCelestialLight *mcl = ecs_get(
            world, s->moon, FlecsCelestialLight);
        float moon_toa = mcl ? mcl->toa_intensity : 0.0f;
        float cr = mcl ? flecsEngine_colorChannelToFloat(mcl->toa_color.r) : 1.0f;
        float cg = mcl ? flecsEngine_colorChannelToFloat(mcl->toa_color.g) : 1.0f;
        float cb = mcl ? flecsEngine_colorChannelToFloat(mcl->toa_color.b) : 1.0f;

        out->moon_radiance[0] = moon_toa * cr;
        out->moon_radiance[1] = moon_toa * cg;
        out->moon_radiance[2] = moon_toa * cb;
        out->moon_radiance[3] = 0.0f;
    }

    mat4 star_rot;
    glm_mat4_identity(star_rot);
    float star_cells = 0.0f;
    float star_density = 1.0f;
    float star_size = 0.0f;
    float star_variation = 0.0f;
    float star_col_r = 0.0f, star_col_g = 0.0f, star_col_b = 0.0f;

    if (s->stars) {
        const FlecsStars *st = ecs_get(world, s->stars, FlecsStars);
        if (st) {
            star_cells = st->cells;
            star_density = st->density;
            star_size = st->size;
            star_variation = st->color_variation;
        }

        const FlecsRotation3 *srot = ecs_get(
            world, s->stars, FlecsRotation3);
        if (srot) {
            /* x = latitude, y = hour angle (around polar axis),
             * z = north offset (world yaw pointing to true north). */
            float lat = srot->x;
            float hour_ang = srot->y;
            float north = srot->z;
            float cos_lat = cosf(lat), sin_lat = sinf(lat);
            float cos_n = cosf(north), sin_n = sinf(north);
            vec3 axis = {
                cos_lat * sin_n,
                sin_lat,
                cos_lat * cos_n
            };
            glm_rotate_make(star_rot, hour_ang, axis);
        }

        const FlecsCelestialLight *scl = ecs_get(
            world, s->stars, FlecsCelestialLight);
        if (scl) {
            float toa = scl->toa_intensity;
            star_col_r = toa * flecsEngine_colorChannelToFloat(scl->toa_color.r);
            star_col_g = toa * flecsEngine_colorChannelToFloat(scl->toa_color.g);
            star_col_b = toa * flecsEngine_colorChannelToFloat(scl->toa_color.b);
        }
    }

    out->star_rot_0[0] = star_rot[0][0];
    out->star_rot_0[1] = star_rot[1][0];
    out->star_rot_0[2] = star_rot[2][0];
    out->star_rot_0[3] = star_cells;
    out->star_rot_1[0] = star_rot[0][1];
    out->star_rot_1[1] = star_rot[1][1];
    out->star_rot_1[2] = star_rot[2][1];
    out->star_rot_1[3] = star_density;
    out->star_rot_2[0] = star_rot[0][2];
    out->star_rot_2[1] = star_rot[1][2];
    out->star_rot_2[2] = star_rot[2][2];
    out->star_rot_2[3] = star_size;
    out->star_color[0] = star_col_r;
    out->star_color[1] = star_col_g;
    out->star_color[2] = star_col_b;
    out->star_color[3] = star_variation;
}

static bool flecsEngine_atmos_createSimpleTexture(
    const FlecsEngineImpl *engine,
    uint32_t w, uint32_t h,
    WGPUTextureFormat format,
    WGPUTexture *out_tex,
    WGPUTextureView *out_view)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { w, h, 1 },
        .format = format,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    WGPUTexture tex = wgpuDeviceCreateTexture(engine->device, &desc);
    if (!tex) return false;

    WGPUTextureViewDescriptor view_desc = {
        .format = format,
        .dimension = WGPUTextureViewDimension_2D,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    };
    WGPUTextureView view = wgpuTextureCreateView(tex, &view_desc);
    if (!view) {
        wgpuTextureRelease(tex);
        return false;
    }

    *out_tex = tex;
    *out_view = view;
    return true;
}

static bool flecsEngine_atmos_createAerialTexture(
    const FlecsEngineImpl *engine,
    FlecsAtmosphereImpl *a)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = { FLECS_ATMOS_AERIAL_W, FLECS_ATMOS_AERIAL_H, FLECS_ATMOS_AERIAL_SLICES },
        .format = FLECS_ATMOS_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    };
    a->aerial_texture = wgpuDeviceCreateTexture(engine->device, &desc);
    if (!a->aerial_texture) return false;

    WGPUTextureViewDescriptor array_desc = {
        .format = FLECS_ATMOS_FORMAT,
        .dimension = WGPUTextureViewDimension_2DArray,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = FLECS_ATMOS_AERIAL_SLICES,
        .aspect = WGPUTextureAspect_All
    };
    a->aerial_view = wgpuTextureCreateView(a->aerial_texture, &array_desc);
    if (!a->aerial_view) return false;

    a->aerial_storage_view = wgpuTextureCreateView(a->aerial_texture, &array_desc);
    return a->aerial_storage_view != NULL;
}

static WGPUShaderModule flecsEngine_atmos_createModule(
    ecs_world_t *world,
    const char *name,
    const char *body)
{
    size_t common_len = strlen(kAtmosphereCommonWgsl);
    size_t body_len = strlen(body);
    char *buf = (char*)ecs_os_malloc(common_len + body_len + 1);
    memcpy(buf, kAtmosphereCommonWgsl, common_len);
    memcpy(buf + common_len, body, body_len);
    buf[common_len + body_len] = '\0';
    WGPUShaderModule module = flecsEngine_shader_ensureModule(world, name, buf);
    ecs_os_free(buf);
    return module;
}

static WGPURenderPipeline flecsEngine_atmos_createPipeline(
    const FlecsEngineImpl *engine,
    WGPUShaderModule module,
    WGPUBindGroupLayout bind_layout,
    const char *vertex_entry,
    const char *fragment_entry,
    WGPUTextureFormat color_format)
{
    WGPUColorTargetState color_target = {
        .format = color_format,
        .writeMask = WGPUColorWriteMask_All
    };
    return flecsEngine_createFullscreenPipeline(
        engine, module, bind_layout,
        vertex_entry, fragment_entry, &color_target, NULL);
}


typedef enum {
    ATMOS_BIND_UNIFORM,
    ATMOS_BIND_TEX2D,
    ATMOS_BIND_TEX2DARRAY,
    ATMOS_BIND_TEX_DEPTH,
    ATMOS_BIND_SAMPLER,
    ATMOS_BIND_STORAGE2DARRAY
} flecsEngine_atmos_bind_kind_t;

static WGPUBindGroupLayout flecsEngine_atmos_makeLayout(
    const FlecsEngineImpl *engine,
    WGPUShaderStage stage,
    const flecsEngine_atmos_bind_kind_t *kinds,
    uint32_t count)
{
    WGPUBindGroupLayoutEntry entries[16] = {0};
    ecs_assert(count <= 16, ECS_INTERNAL_ERROR, NULL);
    for (uint32_t i = 0; i < count; i ++) {
        entries[i].binding = i;
        entries[i].visibility = stage;
        switch (kinds[i]) {
        case ATMOS_BIND_UNIFORM:
            entries[i].buffer.type = WGPUBufferBindingType_Uniform;
            entries[i].buffer.minBindingSize = sizeof(FlecsAtmosphereUniform);
            break;
        case ATMOS_BIND_TEX2D:
            entries[i].texture.sampleType = WGPUTextureSampleType_Float;
            entries[i].texture.viewDimension = WGPUTextureViewDimension_2D;
            break;
        case ATMOS_BIND_TEX2DARRAY:
            entries[i].texture.sampleType = WGPUTextureSampleType_Float;
            entries[i].texture.viewDimension = WGPUTextureViewDimension_2DArray;
            break;
        case ATMOS_BIND_TEX_DEPTH:
            entries[i].texture.sampleType = WGPUTextureSampleType_Depth;
            entries[i].texture.viewDimension = WGPUTextureViewDimension_2D;
            break;
        case ATMOS_BIND_SAMPLER:
            entries[i].sampler.type = WGPUSamplerBindingType_Filtering;
            break;
        case ATMOS_BIND_STORAGE2DARRAY:
            entries[i].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
            entries[i].storageTexture.format = FLECS_ATMOS_FORMAT;
            entries[i].storageTexture.viewDimension = WGPUTextureViewDimension_2DArray;
            break;
        }
    }
    WGPUBindGroupLayoutDescriptor d = { .entryCount = count, .entries = entries };
    return wgpuDeviceCreateBindGroupLayout(engine->device, &d);
}

static WGPUBindGroupLayout flecsEngine_atmos_layoutUniformOnly(
    const FlecsEngineImpl *engine)
{
    static const flecsEngine_atmos_bind_kind_t k[] = { ATMOS_BIND_UNIFORM };
    return flecsEngine_atmos_makeLayout(engine, WGPUShaderStage_Fragment, k, 1);
}

static WGPUBindGroupLayout flecsEngine_atmos_layoutMS(
    const FlecsEngineImpl *engine)
{
    static const flecsEngine_atmos_bind_kind_t k[] = {
        ATMOS_BIND_UNIFORM, ATMOS_BIND_TEX2D, ATMOS_BIND_SAMPLER
    };
    return flecsEngine_atmos_makeLayout(engine, WGPUShaderStage_Fragment, k, 3);
}

static WGPUBindGroupLayout flecsEngine_atmos_layoutSkyView(
    const FlecsEngineImpl *engine)
{
    static const flecsEngine_atmos_bind_kind_t k[] = {
        ATMOS_BIND_UNIFORM, ATMOS_BIND_TEX2D, ATMOS_BIND_TEX2D, ATMOS_BIND_SAMPLER
    };
    return flecsEngine_atmos_makeLayout(engine, WGPUShaderStage_Fragment, k, 4);
}

static WGPUBindGroupLayout flecsEngine_atmos_layoutAerialCompute(
    const FlecsEngineImpl *engine)
{
    static const flecsEngine_atmos_bind_kind_t k[] = {
        ATMOS_BIND_UNIFORM, ATMOS_BIND_TEX2D, ATMOS_BIND_TEX2D,
        ATMOS_BIND_SAMPLER, ATMOS_BIND_STORAGE2DARRAY, ATMOS_BIND_TEX2D
    };
    return flecsEngine_atmos_makeLayout(engine, WGPUShaderStage_Compute, k, 6);
}

static WGPUBindGroupLayout flecsEngine_atmos_layoutCubeFaceCompute(
    const FlecsEngineImpl *engine)
{
    static const flecsEngine_atmos_bind_kind_t k[] = {
        ATMOS_BIND_UNIFORM, ATMOS_BIND_TEX2D,
        ATMOS_BIND_SAMPLER, ATMOS_BIND_STORAGE2DARRAY
    };
    return flecsEngine_atmos_makeLayout(engine, WGPUShaderStage_Compute, k, 4);
}

static WGPUBindGroupLayout flecsEngine_atmos_layoutCubeDsCompute(
    const FlecsEngineImpl *engine)
{
    static const flecsEngine_atmos_bind_kind_t k[] = {
        ATMOS_BIND_TEX2DARRAY, ATMOS_BIND_SAMPLER, ATMOS_BIND_STORAGE2DARRAY
    };
    return flecsEngine_atmos_makeLayout(engine, WGPUShaderStage_Compute, k, 3);
}

static WGPUBindGroupLayout flecsEngine_atmos_layoutCompose(
    const FlecsEngineImpl *engine)
{
    static const flecsEngine_atmos_bind_kind_t k[] = {
        ATMOS_BIND_TEX2D, ATMOS_BIND_SAMPLER, ATMOS_BIND_TEX_DEPTH,
        ATMOS_BIND_UNIFORM, ATMOS_BIND_TEX2D, ATMOS_BIND_TEX2D,
        ATMOS_BIND_TEX2DARRAY, ATMOS_BIND_SAMPLER
    };
    return flecsEngine_atmos_makeLayout(engine, WGPUShaderStage_Fragment, k, 8);
}

bool flecsEngine_atmosphere_ensureImpl(
    ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t atmosphere_entity)
{
    bool was_deferred = ecs_is_deferred(world);
    if (was_deferred) {
        ecs_defer_suspend(world);
    }

    FlecsAtmosphereImpl *existing = ecs_ensure(
        world, atmosphere_entity, FlecsAtmosphereImpl);
    if (existing->uniform_buffer) {
        if (was_deferred) ecs_defer_resume(world);
        return true;
    }

    FlecsAtmosphereImpl a = {0};

    a.uniform_buffer = flecsEngine_createUniformBuffer(
        engine->device, sizeof(FlecsAtmosphereUniform));
    if (!a.uniform_buffer) { ecs_err("atmos: uniform_buffer create failed"); flecsEngine_atmos_releaseResources(&a); return false; }

    a.clamp_sampler = flecsEngine_createLinearClampSampler(engine->device);
    if (!a.clamp_sampler) { ecs_err("atmos setup failed at %s:%d", __FILE__, __LINE__); flecsEngine_atmos_releaseResources(&a); return false; }

    if (!flecsEngine_atmos_createSimpleTexture(engine,
        FLECS_ATMOS_TRANS_W, FLECS_ATMOS_TRANS_H, FLECS_ATMOS_FORMAT,
        &a.trans_texture, &a.trans_view))
    { ecs_err("atmos setup failed at %s:%d", __FILE__, __LINE__); flecsEngine_atmos_releaseResources(&a); return false; }

    if (!flecsEngine_atmos_createSimpleTexture(engine,
        FLECS_ATMOS_MS_W, FLECS_ATMOS_MS_H, FLECS_ATMOS_FORMAT,
        &a.ms_texture, &a.ms_view))
    { ecs_err("atmos setup failed at %s:%d", __FILE__, __LINE__); flecsEngine_atmos_releaseResources(&a); return false; }

    if (!flecsEngine_atmos_createSimpleTexture(engine,
        FLECS_ATMOS_SKYVIEW_W, FLECS_ATMOS_SKYVIEW_H, FLECS_ATMOS_FORMAT,
        &a.skyview_texture, &a.skyview_view))
    { ecs_err("atmos setup failed at %s:%d", __FILE__, __LINE__); flecsEngine_atmos_releaseResources(&a); return false; }

    if (!flecsEngine_atmos_createAerialTexture(engine, &a))
    { ecs_err("atmos setup failed at %s:%d", __FILE__, __LINE__); flecsEngine_atmos_releaseResources(&a); return false; }

    a.trans_layout = flecsEngine_atmos_layoutUniformOnly(engine);
    a.ms_layout = flecsEngine_atmos_layoutMS(engine);
    a.skyview_layout = flecsEngine_atmos_layoutSkyView(engine);
    a.aerial_compute_layout = flecsEngine_atmos_layoutAerialCompute(engine);
    a.cube_face_layout = flecsEngine_atmos_layoutCubeFaceCompute(engine);
    a.cube_ds_layout = flecsEngine_atmos_layoutCubeDsCompute(engine);
    a.compose_layout = flecsEngine_atmos_layoutCompose(engine);
    if (!a.trans_layout || !a.ms_layout || !a.skyview_layout ||
        !a.aerial_compute_layout || !a.cube_face_layout ||
        !a.cube_ds_layout || !a.compose_layout)
    { ecs_err("atmos setup failed at %s:%d", __FILE__, __LINE__); flecsEngine_atmos_releaseResources(&a); return false; }

    WGPUShaderModule trans_mod = flecsEngine_atmos_createModule(
        world, "AtmosphereTransShader", kTransShaderSource);
    WGPUShaderModule ms_mod = flecsEngine_atmos_createModule(
        world, "AtmosphereMSShader", kMSShaderSource);
    WGPUShaderModule sv_mod = flecsEngine_atmos_createModule(
        world, "AtmosphereSkyViewShader", kSkyViewShaderSource);
    WGPUShaderModule ap_mod = flecsEngine_atmos_createModule(
        world, "AtmosphereAerialComputeShader", kAerialComputeShaderSource);
    WGPUShaderModule cs_mod = flecsEngine_atmos_createModule(
        world, "AtmosphereComposeShader", kComposeShaderSource);
    WGPUShaderModule cf_mod = flecsEngine_atmos_createModule(
        world, "AtmosphereCubeFaceShader", kCubeFaceComputeShaderSource);
    WGPUShaderModule ds_mod = flecsEngine_atmos_createModule(
        world, "AtmosphereCubeDsShader", kCubeDownsampleComputeShaderSource);
    if (!trans_mod || !ms_mod || !sv_mod || !ap_mod || !cs_mod || !cf_mod || !ds_mod) {
        flecsEngine_atmos_releaseResources(&a);
        return false;
    }

    a.trans_pipeline = flecsEngine_atmos_createPipeline(
        engine, trans_mod, a.trans_layout, "vs_main", "fs_main", FLECS_ATMOS_FORMAT);
    a.ms_pipeline = flecsEngine_atmos_createPipeline(
        engine, ms_mod, a.ms_layout, "vs_main", "fs_main", FLECS_ATMOS_FORMAT);
    a.skyview_pipeline = flecsEngine_atmos_createPipeline(
        engine, sv_mod, a.skyview_layout, "vs_main", "fs_main", FLECS_ATMOS_FORMAT);

    a.aerial_compute_pipeline = flecsEngine_createComputePipeline(
        engine, ap_mod, a.aerial_compute_layout, "cs_main");
    a.cube_face_pipeline = flecsEngine_createComputePipeline(
        engine, cf_mod, a.cube_face_layout, "cs_main");
    a.cube_ds_pipeline = flecsEngine_createComputePipeline(
        engine, ds_mod, a.cube_ds_layout, "cs_main");

    WGPUTextureFormat hdr_format = flecsEngine_getHdrFormat(engine);
    WGPUTextureFormat surface_format = flecsEngine_getViewTargetFormat(engine);
    a.compose_pipeline_hdr = flecsEngine_atmos_createPipeline(
        engine, cs_mod, a.compose_layout, "vs_main", "fs_main", hdr_format);
    a.compose_pipeline_surface = flecsEngine_atmos_createPipeline(
        engine, cs_mod, a.compose_layout, "vs_main", "fs_main", surface_format);

    if (!a.trans_pipeline || !a.ms_pipeline || !a.skyview_pipeline ||
        !a.compose_pipeline_hdr || !a.compose_pipeline_surface ||
        !a.aerial_compute_pipeline || !a.cube_face_pipeline || !a.cube_ds_pipeline)
    { ecs_err("atmos setup failed at %s:%d", __FILE__, __LINE__); flecsEngine_atmos_releaseResources(&a); return false; }

    *existing = a;
    ecs_modified(world, atmosphere_entity, FlecsAtmosphereImpl);

    FlecsHdriImpl *hdri = ecs_ensure(world, atmosphere_entity, FlecsHdriImpl);
    existing = ecs_ensure(world, atmosphere_entity, FlecsAtmosphereImpl);
    if (!hdri->ibl_prefiltered_cubemap) {
        const uint32_t cube_size = 128u;
        uint32_t mips = flecsIblComputeMipCount(cube_size);
        if (mips > 8u) mips = 8u;
        existing->cube_mip_count = mips;
        hdri->ibl_prefilter_mip_count = mips;

        WGPUTextureDescriptor desc = {
            .usage = WGPUTextureUsage_StorageBinding |
                     WGPUTextureUsage_TextureBinding,
            .dimension = WGPUTextureDimension_2D,
            .size = { cube_size, cube_size, 6 },
            .format = WGPUTextureFormat_RGBA16Float,
            .mipLevelCount = mips,
            .sampleCount = 1
        };
        hdri->ibl_prefiltered_cubemap = wgpuDeviceCreateTexture(
            engine->device, &desc);
        hdri->ibl_prefiltered_cubemap_view = wgpuTextureCreateView(
            hdri->ibl_prefiltered_cubemap, &(WGPUTextureViewDescriptor){
                .format = WGPUTextureFormat_RGBA16Float,
                .dimension = WGPUTextureViewDimension_Cube,
                .baseMipLevel = 0, .mipLevelCount = mips,
                .baseArrayLayer = 0, .arrayLayerCount = 6
            });

        hdri->ibl_irradiance_cubemap = NULL;
        hdri->ibl_irradiance_cubemap_view = wgpuTextureCreateView(
            hdri->ibl_prefiltered_cubemap, &(WGPUTextureViewDescriptor){
                .format = WGPUTextureFormat_RGBA16Float,
                .dimension = WGPUTextureViewDimension_Cube,
                .baseMipLevel = mips - 1u, .mipLevelCount = 1u,
                .baseArrayLayer = 0, .arrayLayerCount = 6
            });
        flecsIblCreateSampler(engine, hdri);

        for (uint32_t m = 0; m < mips; m++) {
            existing->cube_mip_storage_views[m] = wgpuTextureCreateView(
                hdri->ibl_prefiltered_cubemap,
                &(WGPUTextureViewDescriptor){
                    .format = WGPUTextureFormat_RGBA16Float,
                    .dimension = WGPUTextureViewDimension_2DArray,
                    .baseMipLevel = m, .mipLevelCount = 1u,
                    .baseArrayLayer = 0, .arrayLayerCount = 6u
                });
            existing->cube_mip_read_views[m] = wgpuTextureCreateView(
                hdri->ibl_prefiltered_cubemap,
                &(WGPUTextureViewDescriptor){
                    .format = WGPUTextureFormat_RGBA16Float,
                    .dimension = WGPUTextureViewDimension_2DArray,
                    .baseMipLevel = m, .mipLevelCount = 1u,
                    .baseArrayLayer = 0, .arrayLayerCount = 6u
                });
        }

        /* Bump the scene bind version so every view rebuilds its group 0
         * with the new atmosphere IBL cubemap on next render. */
        ((FlecsEngineImpl*)engine)->scene_bind_version ++;
        ecs_modified(world, atmosphere_entity, FlecsHdriImpl);
    }

    {
        WGPUBindGroupEntry t_entries[1] = {
            { .binding = 0, .buffer = existing->uniform_buffer,
              .offset = 0, .size = sizeof(FlecsAtmosphereUniform) }
        };
        existing->trans_bind_group = wgpuDeviceCreateBindGroup(engine->device,
            &(WGPUBindGroupDescriptor){
                .layout = existing->trans_layout,
                .entryCount = 1, .entries = t_entries });

        WGPUBindGroupEntry ms_entries[3] = {
            { .binding = 0, .buffer = existing->uniform_buffer,
              .offset = 0, .size = sizeof(FlecsAtmosphereUniform) },
            { .binding = 1, .textureView = existing->trans_view },
            { .binding = 2, .sampler = existing->clamp_sampler }
        };
        existing->ms_bind_group = wgpuDeviceCreateBindGroup(engine->device,
            &(WGPUBindGroupDescriptor){
                .layout = existing->ms_layout,
                .entryCount = 3, .entries = ms_entries });

        WGPUBindGroupEntry sv_entries[4] = {
            { .binding = 0, .buffer = existing->uniform_buffer,
              .offset = 0, .size = sizeof(FlecsAtmosphereUniform) },
            { .binding = 1, .textureView = existing->trans_view },
            { .binding = 2, .textureView = existing->ms_view },
            { .binding = 3, .sampler = existing->clamp_sampler }
        };
        existing->skyview_bind_group = wgpuDeviceCreateBindGroup(engine->device,
            &(WGPUBindGroupDescriptor){
                .layout = existing->skyview_layout,
                .entryCount = 4, .entries = sv_entries });

        WGPUBindGroupEntry a_entries[6] = {
            { .binding = 0, .buffer = existing->uniform_buffer,
              .offset = 0, .size = sizeof(FlecsAtmosphereUniform) },
            { .binding = 1, .textureView = existing->trans_view },
            { .binding = 2, .textureView = existing->ms_view },
            { .binding = 3, .sampler = existing->clamp_sampler },
            { .binding = 4, .textureView = existing->aerial_storage_view },
            { .binding = 5, .textureView = existing->skyview_view }
        };
        existing->aerial_compute_bind_group = wgpuDeviceCreateBindGroup(
            engine->device, &(WGPUBindGroupDescriptor){
                .layout = existing->aerial_compute_layout,
                .entryCount = 6, .entries = a_entries });

        WGPUBindGroupEntry cf_entries[4] = {
            { .binding = 0, .buffer = existing->uniform_buffer,
              .offset = 0, .size = sizeof(FlecsAtmosphereUniform) },
            { .binding = 1, .textureView = existing->skyview_view },
            { .binding = 2, .sampler = existing->clamp_sampler },
            { .binding = 3, .textureView = existing->cube_mip_storage_views[0] }
        };
        existing->cube_face_bind_group = wgpuDeviceCreateBindGroup(
            engine->device, &(WGPUBindGroupDescriptor){
                .layout = existing->cube_face_layout,
                .entryCount = 4, .entries = cf_entries });

        for (uint32_t m = 1; m < existing->cube_mip_count; m++) {
            WGPUBindGroupEntry ds_entries[3] = {
                { .binding = 0,
                  .textureView = existing->cube_mip_read_views[m - 1] },
                { .binding = 1, .sampler = existing->clamp_sampler },
                { .binding = 2,
                  .textureView = existing->cube_mip_storage_views[m] }
            };
            existing->cube_ds_bind_groups[m] = wgpuDeviceCreateBindGroup(
                engine->device, &(WGPUBindGroupDescriptor){
                    .layout = existing->cube_ds_layout,
                    .entryCount = 3, .entries = ds_entries });
        }
    }

    if (was_deferred) ecs_defer_resume(world);
    return true;
}

static bool flecsEngine_atmos_runPassBG(
    const FlecsEngineImpl *engine,
    WGPUCommandEncoder encoder,
    WGPURenderPipeline pipeline,
    WGPUBindGroup bind_group,
    WGPUTextureView target_view)
{
    return flecsEngine_fullscreenPass(
        encoder, target_view, WGPULoadOp_Clear, (WGPUColor){0},
        pipeline, bind_group, NULL, NULL, NULL);
}

static bool flecsEngine_atmos_runTrans(
    const FlecsEngineImpl *engine,
    FlecsAtmosphereImpl *a,
    WGPUCommandEncoder encoder)
{
    return flecsEngine_atmos_runPassBG(
        engine, encoder, a->trans_pipeline,
        a->trans_bind_group, a->trans_view);
}

static bool flecsEngine_atmos_runMS(
    const FlecsEngineImpl *engine,
    FlecsAtmosphereImpl *a,
    WGPUCommandEncoder encoder)
{
    return flecsEngine_atmos_runPassBG(
        engine, encoder, a->ms_pipeline,
        a->ms_bind_group, a->ms_view);
}

static bool flecsEngine_atmos_runSkyView(
    const FlecsEngineImpl *engine,
    FlecsAtmosphereImpl *a,
    WGPUCommandEncoder encoder)
{
    return flecsEngine_atmos_runPassBG(
        engine, encoder, a->skyview_pipeline,
        a->skyview_bind_group, a->skyview_view);
}

static bool flecsEngine_atmos_runComputePass(
    WGPUCommandEncoder encoder,
    WGPUComputePipeline pipeline,
    WGPUBindGroup bind_group,
    uint32_t gx, uint32_t gy, uint32_t gz)
{
    WGPUComputePassDescriptor desc = {0};
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(
        encoder, &desc);
    if (!pass) return false;
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);
    wgpuComputePassEncoderDispatchWorkgroups(pass, gx, gy, gz);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    return true;
}

static bool flecsEngine_atmos_runAerial(
    const FlecsEngineImpl *engine,
    FlecsAtmosphereImpl *a,
    WGPUCommandEncoder encoder)
{
    (void)engine;
    FLECS_TRACY_ZONE_BEGIN("AtmosAerial");

    bool ok = flecsEngine_atmos_runComputePass(
        encoder, a->aerial_compute_pipeline, a->aerial_compute_bind_group,
        (FLECS_ATMOS_AERIAL_W + 7) / 8,
        (FLECS_ATMOS_AERIAL_H + 7) / 8,
        FLECS_ATMOS_AERIAL_SLICES);
    FLECS_TRACY_ZONE_END;
    return ok;
}

static bool flecsEngine_atmos_runCompose(
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    FlecsAtmosphereImpl *a,
    WGPUCommandEncoder encoder,
    WGPUTextureView input_view,
    WGPUTextureView output_view,
    WGPUTextureFormat output_format,
    WGPULoadOp output_load_op)
{
    FLECS_TRACY_ZONE_BEGIN("AtmosCompose");
    if (!view_impl->depth_texture_view) {
        FLECS_TRACY_ZONE_END;
        return false;
    }
    WGPUBindGroupEntry entries[8] = {
        { .binding = 0, .textureView = input_view },
        { .binding = 1, .sampler = a->clamp_sampler },
        { .binding = 2, .textureView = view_impl->depth_texture_view },
        { .binding = 3, .buffer = a->uniform_buffer,
          .offset = 0, .size = sizeof(FlecsAtmosphereUniform) },
        { .binding = 4, .textureView = a->trans_view },
        { .binding = 5, .textureView = a->skyview_view },
        { .binding = 6, .textureView = a->aerial_view },
        { .binding = 7, .sampler = a->clamp_sampler }
    };
    WGPUBindGroup bg = flecsEngine_bindGroup_ensure(
        &a->compose_bind_group, engine->device, a->compose_layout, entries, 8);
    if (!bg) {
        FLECS_TRACY_ZONE_END;
        return false;
    }

    WGPURenderPipeline pipeline =
        output_format == flecsEngine_getViewTargetFormat(engine)
            ? a->compose_pipeline_surface
            : a->compose_pipeline_hdr;

    bool ok = flecsEngine_fullscreenPass(
        encoder, output_view, output_load_op, (WGPUColor){0}, pipeline, bg,
        NULL, NULL, NULL);
    FLECS_TRACY_ZONE_END;
    return ok;
}

bool flecsEngine_atmosphere_renderLuts(
    ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t atmosphere_entity,
    ecs_entity_t view_entity,
    WGPUCommandEncoder encoder)
{
    FlecsAtmosphereImpl *a = ecs_get_mut(
        world, atmosphere_entity, FlecsAtmosphereImpl);
    if (!a || !a->uniform_buffer) return false;
    const FlecsAtmosphere *s = ecs_get(
        world, atmosphere_entity, FlecsAtmosphere);
    if (!s) return false;

    FLECS_TRACY_ZONE_BEGIN("AtmosLuts");

    FlecsAtmosphereUniform uniform;
    flecsEngine_atmos_fillUniform(world, view_entity, s, &uniform);
    wgpuQueueWriteBuffer(
        engine->queue, a->uniform_buffer, 0, &uniform, sizeof(uniform));

    FlecsAtmosphereLutKey key;
    memcpy(key.atmosphere,         uniform.atmosphere,         sizeof(key.atmosphere));
    memcpy(key.scat_rayleigh,      uniform.scat_rayleigh,      sizeof(key.scat_rayleigh));
    memcpy(key.scat_mie,           uniform.scat_mie,           sizeof(key.scat_mie));
    memcpy(key.ext_mie,            uniform.ext_mie,            sizeof(key.ext_mie));
    memcpy(key.abs_ozone,          uniform.abs_ozone,          sizeof(key.abs_ozone));
    memcpy(key.abs_strat_aerosol,  uniform.abs_strat_aerosol,  sizeof(key.abs_strat_aerosol));
    memcpy(key.abs_haze,           uniform.abs_haze,           sizeof(key.abs_haze));
    memcpy(key.ground_albedo,      uniform.ground_albedo,      sizeof(key.ground_albedo));

    bool luts_dirty = !a->luts_cached ||
        memcmp(&key, &a->lut_key_cached, sizeof(key)) != 0;

    bool ok = false;
    if (luts_dirty) {
        ok = flecsEngine_atmos_runTrans(engine, a, encoder);
        if (!ok) { FLECS_TRACY_ZONE_END; return false; }

        ok = flecsEngine_atmos_runMS(engine, a, encoder);
        if (!ok) { FLECS_TRACY_ZONE_END; return false; }

        a->lut_key_cached = key;
        a->luts_cached = true;
    }

    ok = flecsEngine_atmos_runSkyView(engine, a, encoder);
    if (!ok) { FLECS_TRACY_ZONE_END; return false; }

    ok = flecsEngine_atmos_runAerial(engine, a, encoder);
    FLECS_TRACY_ZONE_END;
    return ok;
}

bool flecsEngine_atmosphere_renderIbl(
    ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t atmosphere_entity,
    WGPUCommandEncoder encoder)
{
    FLECS_TRACY_ZONE_BEGIN("AtmosIbl");
    const FlecsAtmosphereImpl *a = ecs_get(
        world, atmosphere_entity, FlecsAtmosphereImpl);
    if (!a || !a->cube_face_pipeline || !a->cube_ds_pipeline ||
        a->cube_mip_count == 0)
    {
        FLECS_TRACY_ZONE_END;
        return false;
    }

    uint32_t top_size = 128u;
    bool ok = flecsEngine_atmos_runComputePass(
        encoder, a->cube_face_pipeline, a->cube_face_bind_group,
        (top_size + 7) / 8, (top_size + 7) / 8, 6);
    if (!ok) { FLECS_TRACY_ZONE_END; return false; }

    for (uint32_t m = 1; m < a->cube_mip_count; m++) {
        uint32_t size = top_size >> m;
        if (size < 1u) size = 1u;
        uint32_t gx = (size + 7) / 8;
        uint32_t gy = gx;
        if (!flecsEngine_atmos_runComputePass(encoder,
            a->cube_ds_pipeline, a->cube_ds_bind_groups[m], gx, gy, 6))
        {
            FLECS_TRACY_ZONE_END;
            return false;
        }
    }
    (void)world;
    FLECS_TRACY_ZONE_END;
    return true;
}

bool flecsEngine_atmosphere_renderCompose(
    ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    ecs_entity_t atmosphere_entity,
    WGPUCommandEncoder encoder,
    WGPUTextureView input_view,
    WGPUTextureView output_view,
    WGPUTextureFormat output_format,
    WGPULoadOp output_load_op)
{
    FlecsAtmosphereImpl *a = ecs_get_mut(
        world, atmosphere_entity, FlecsAtmosphereImpl);
    if (!a) return false;
    return flecsEngine_atmos_runCompose(
        engine, view_impl, a, encoder, input_view, output_view, output_format,
        output_load_op);
}

/* CPU transmittance function for calculating light color from time of day */
void flecsEngine_atmos_sunTransmittance(
    const FlecsAtmosphere *atm,
    float sun_cos_zenith,
    float out_rgb[3])
{
    if (!atm || !out_rgb) {
        return;
    }

    if (sun_cos_zenith > 1.0f) sun_cos_zenith = 1.0f;
    if (sun_cos_zenith < -1.0f) sun_cos_zenith = -1.0f;

    if (sun_cos_zenith <= 0.0f) {
        out_rgb[0] = out_rgb[1] = out_rgb[2] = 0.0f;
        return;
    }

    float planet_r = atm->planet_radius_km > 0.0f
        ? atm->planet_radius_km : 6360.0f;
    float thickness = atm->atmosphere_thickness_km > 0.0f
        ? atm->atmosphere_thickness_km : 100.0f;
    float top_r = planet_r + thickness;
    float alt_km = atm->ground_altitude_km > 0.0f
        ? atm->ground_altitude_km : 0.0f;
    float view_r = planet_r + alt_km;

    float b = view_r * sun_cos_zenith;
    float c = view_r * view_r - top_r * top_r;
    float disc = b * b - c;
    if (disc <= 0.0f) {
        out_rgb[0] = out_rgb[1] = out_rgb[2] = 1.0f;
        return;
    }
    float t_max = -b + sqrtf(disc);
    if (t_max <= 0.0f) {
        out_rgb[0] = out_rgb[1] = out_rgb[2] = 1.0f;
        return;
    }

    float rayleigh_scale = atm->rayleigh.scattering_scale >= 0.0f
        ? atm->rayleigh.scattering_scale : 1.0f;
    float ray_r = 0.005802f * rayleigh_scale;
    float ray_g = 0.013558f * rayleigh_scale;
    float ray_b = 0.033100f * rayleigh_scale;

    float mie_scale = atm->mie.scattering_scale > 0.0f
        ? atm->mie.scattering_scale : 1.0f;
    float tint_r = (float)atm->mie.tint.r * (1.0f / 255.0f);
    float tint_g = (float)atm->mie.tint.g * (1.0f / 255.0f);
    float tint_b = (float)atm->mie.tint.b * (1.0f / 255.0f);
    float mie_ext_r = 0.004400f * mie_scale * tint_r;
    float mie_ext_g = 0.004400f * mie_scale * tint_g;
    float mie_ext_b = 0.004400f * mie_scale * tint_b;

    float ozone_scale = atm->ozone_scale >= 0.0f ? atm->ozone_scale : 1.0f;
    float oz_r = 0.000650f * ozone_scale;
    float oz_g = 0.001881f * ozone_scale;
    float oz_b = 0.000085f * ozone_scale;

    float strat = atm->stratospheric_aerosol_scale > 0.0f
        ? atm->stratospheric_aerosol_scale : 0.0f;
    float st_r = 0.001950f * strat;
    float st_g = 0.005643f * strat;
    float st_b = 0.000255f * strat;

    float haze = atm->haze_absorption > 0.0f ? atm->haze_absorption : 0.0f;
    float hz_r = 0.005f * haze;
    float hz_g = 0.005f * haze;
    float hz_b = 0.005f * haze;

    float ray_h = atm->rayleigh.scale_height_km > 0.0f
        ? atm->rayleigh.scale_height_km : 8.0f;
    float mie_h = atm->mie.scale_height_km > 0.0f
        ? atm->mie.scale_height_km : 1.2f;

    const int STEPS = 40;
    float dt = t_max / (float)STEPS;
    float rd_x = sqrtf(fmaxf(0.0f, 1.0f - sun_cos_zenith * sun_cos_zenith));
    float rd_y = sun_cos_zenith;

    float od_r = 0.0f, od_g = 0.0f, od_b = 0.0f;

    for (int i = 0; i < STEPS; i++) {
        float t = ((float)i + 0.5f) * dt;
        float px = rd_x * t;
        float py = view_r + rd_y * t;
        float r_p = sqrtf(px * px + py * py);
        float alt = r_p - planet_r;
        if (alt < 0.0f) alt = 0.0f;

        float d_ray = expf(-alt / ray_h);
        float d_mie = expf(-alt / mie_h);
        float d_oz = 1.0f - fabsf(alt - 25.0f) / 15.0f;
        if (d_oz < 0.0f) d_oz = 0.0f;
        float d_st = 1.0f - fabsf(alt - 20.0f) / 5.0f;
        if (d_st < 0.0f) d_st = 0.0f;

        float ext_r = ray_r * d_ray + mie_ext_r * d_mie + oz_r * d_oz + st_r * d_st + hz_r * d_mie;
        float ext_g = ray_g * d_ray + mie_ext_g * d_mie + oz_g * d_oz + st_g * d_st + hz_g * d_mie;
        float ext_b = ray_b * d_ray + mie_ext_b * d_mie + oz_b * d_oz + st_b * d_st + hz_b * d_mie;

        od_r += ext_r * dt;
        od_g += ext_g * dt;
        od_b += ext_b * dt;
    }

    out_rgb[0] = expf(-od_r);
    out_rgb[1] = expf(-od_g);
    out_rgb[2] = expf(-od_b);
}

void FlecsEngineAtmosphereImport(ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineAtmosphere);

    ecs_set_name_prefix(world, "Flecs");

    ECS_COMPONENT_DEFINE(world, FlecsAtmosphere);
    ECS_COMPONENT_DEFINE(world, FlecsAtmosphereImpl);

    ecs_set_hooks(world, FlecsAtmosphere, {
        .ctor = ecs_ctor(FlecsAtmosphere)
    });

    ecs_set_hooks(world, FlecsAtmosphereImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsAtmosphereImpl),
        .dtor = ecs_dtor(FlecsAtmosphereImpl)
    });

    ecs_entity_t rayleigh_t = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "FlecsAtmosphereRayleigh" }),
        .members = {
            { .name = "scale_height_km", .type = ecs_id(ecs_f32_t) },
            { .name = "scattering_scale", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_entity_t mie_t = ecs_struct(world, {
        .entity = ecs_entity(world, { .name = "FlecsAtmosphereMie" }),
        .members = {
            { .name = "scale_height_km", .type = ecs_id(ecs_f32_t) },
            { .name = "anisotropy", .type = ecs_id(ecs_f32_t) },
            { .name = "scattering_scale", .type = ecs_id(ecs_f32_t) },
            { .name = "tint", .type = ecs_id(flecs_rgba_t) }
        }
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsAtmosphere),
        .members = {
            { .name = "sun", .type = ecs_id(ecs_entity_t) },
            { .name = "moon", .type = ecs_id(ecs_entity_t) },
            { .name = "stars", .type = ecs_id(ecs_entity_t) },
            { .name = "sun_disk_intensity", .type = ecs_id(ecs_f32_t) },
            { .name = "sun_disk_angular_radius", .type = ecs_id(ecs_f32_t) },
            { .name = "aerial_perspective_distance_km", .type = ecs_id(ecs_f32_t) },
            { .name = "aerial_perspective_intensity", .type = ecs_id(ecs_f32_t) },
            { .name = "sea_level_y", .type = ecs_id(ecs_f32_t) },
            { .name = "world_units_per_km", .type = ecs_id(ecs_f32_t) },
            { .name = "ground_altitude_km", .type = ecs_id(ecs_f32_t) },
            { .name = "planet_radius_km", .type = ecs_id(ecs_f32_t) },
            { .name = "atmosphere_thickness_km", .type = ecs_id(ecs_f32_t) },
            { .name = "rayleigh", .type = rayleigh_t },
            { .name = "mie", .type = mie_t },
            { .name = "ozone_scale", .type = ecs_id(ecs_f32_t) },
            { .name = "stratospheric_aerosol_scale", .type = ecs_id(ecs_f32_t) },
            { .name = "haze_absorption", .type = ecs_id(ecs_f32_t) },
            { .name = "ground_albedo", .type = ecs_id(flecs_rgba_t) },
            { .name = "night_tint", .type = ecs_id(flecs_rgba_t) },
            { .name = "night_intensity", .type = ecs_id(ecs_f32_t) },
            { .name = "turbulence", .type = ecs_id(ecs_f32_t) },
            { .name = "turbulence_scale", .type = ecs_id(ecs_f32_t) },
            { .name = "turbulence_speed", .type = ecs_id(ecs_f32_t) }
        }
    });
}
