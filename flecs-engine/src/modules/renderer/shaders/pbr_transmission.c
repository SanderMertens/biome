#include "shaders.h"
#include "../renderer.h"
#include "common/uniforms_wgsl.h"
#include "common/shared_vertex_wgsl.h"
#include "common/cluster_wgsl.h"
#include "common/pbr_functions_wgsl.h"
#include "common/shadow_wgsl.h"
#include "common/cloud_shadow_wgsl.h"
#include "common/pbr_lighting_wgsl.h"
#include "common/ibl_bindings_wgsl.h"
#include "common/gpu_material_wgsl.h"
#include "common/pbr_textures_wgsl.h"
#include "common/gpu_transform_wgsl.h"

/* PBR shader with transmission support. Mirrors pbr.c's structure (reads
 * per-instance material from the storage buffer at @group(2) @binding(0)
 * and samples PBR texture arrays at @group(1)), and additionally samples
 * the opaque scene snapshot at @group(0) @binding(3) for screen-space
 * refraction. The transmission math is gated on mat.transmission_factor
 * being > 0, so opaque entities in a transmission batch still render
 * correctly. */
static const char *kShaderSource =
    FLECS_ENGINE_SHADER_COMMON_UNIFORMS_WGSL
    FLECS_ENGINE_SHADER_COMMON_IBL_BINDINGS_WGSL
    FLECS_ENGINE_SHADER_COMMON_SHADOW_WGSL
    FLECS_ENGINE_SHADER_COMMON_CLOUD_SHADOW_WGSL
    FLECS_ENGINE_SHADER_COMMON_CLUSTER_WGSL
    FLECS_ENGINE_SHADER_COMMON_PBR_TEXTURES_WGSL
    FLECS_ENGINE_SHADER_COMMON_GPU_MATERIAL_WGSL
    FLECS_ENGINE_SHADER_COMMON_GPU_TRANSFORM_WGSL

    "@group(0) @binding(3) var opaque_snapshot : texture_2d<f32>;\n"

    "struct VertexInput {\n"
    "  @location(0) pos : vec3<f32>,\n"
    "  @location(1) nrm : vec3<f32>,\n"
    "  @location(2) uv : vec2<f32>,\n"
    "  @location(3) tan : vec4<f32>,\n"
    "  @location(4) slot : u32\n"
    "};\n"

    "struct VertexOutput {\n"
    "  @builtin(position) pos : vec4<f32>,\n"
    "  @location(0) normal : vec3<f32>,\n"
    "  @location(1) world_pos : vec3<f32>,\n"
    "  @location(2) uv : vec2<f32>,\n"
    "  @location(3) tangent : vec3<f32>,\n"
    "  @location(4) bitangent_sign : f32,\n"
    "  @location(5) @interpolate(flat) material_id : u32,\n"
    "  @location(6) model_scale : vec3<f32>\n"
    "};\n"

    FLECS_ENGINE_SHADER_COMMON_SHARED_VERTEX_WGSL

    "@vertex fn vs_main(input : VertexInput) -> VertexOutput {\n"
    "  var out : VertexOutput;\n"
    "  let t = instance_transforms[input.slot];\n"
    "  let m0 = t.c0.xyz;\n"
    "  let m1 = t.c1.xyz;\n"
    "  let m2 = t.c2.xyz;\n"
    "  let m3 = t.c3.xyz;\n"
    "  let vertex = buildPbrVertexState(\n"
    "    input.pos, input.nrm, m0, m1, m2, m3);\n"
    "  out.pos = vertex.clip_pos;\n"
    "  out.normal = vertex.world_normal;\n"
    "  out.world_pos = vertex.world_pos;\n"
    "  out.uv = input.uv;\n"
    "  let model_3x3 = mat3x3<f32>(m0, m1, m2);\n"
    "  out.tangent = model_3x3 * input.tan.xyz;\n"
    "  out.bitangent_sign = input.tan.w;\n"
    "  out.material_id = instance_material_ids[input.slot];\n"
    "  out.model_scale = vec3<f32>(length(m0), length(m1), length(m2));\n"
    "  return out;\n"
    "}\n"

    FLECS_ENGINE_SHADER_COMMON_PBR_FUNCTIONS_WGSL
    FLECS_ENGINE_SHADER_COMMON_PBR_LIGHTING_WGSL

    "@fragment fn fs_main(input : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let material = materials[input.material_id];\n"
    "  let uv = input.uv * vec2<f32>(material.uv_scale_x, material.uv_scale_y)\n"
    "         + vec2<f32>(material.uv_offset_x, material.uv_offset_y);\n"
    "  let dx = dpdx(uv);\n"
    "  let dy = dpdy(uv);\n"
    "  let mat_color = unpack4x8unorm(material.color);\n"

    "  var albedo = mat_color.rgb;\n"
    "  var alpha = mat_color.a;\n"
    "  if (material.layer_albedo != 0u) {\n"
    "    let base_color = sample_albedo(uv, material.layer_albedo, dx, dy);\n"
    "    albedo = base_color.rgb * mat_color.rgb;\n"
    "    alpha = base_color.a * mat_color.a;\n"
    "  }\n"

    "  var roughness = material.roughness;\n"
    "  var metallic = material.metallic;\n"
    "  if (material.layer_mr != 0u) {\n"
    "    let mr = sample_roughness(uv, material.layer_mr, dx, dy);\n"
    "    roughness = mr.g * material.roughness;\n"
    "    metallic = mr.b * material.metallic;\n"
    "  }\n"

    "  let em_color = unpack4x8unorm(material.emissive_color).rgb;\n"
    "  let em_has_color = dot(em_color, em_color) > 0.0;\n"
    "  let em_base = select(albedo, em_color, em_has_color);\n"
    "  var emissive = vec3<f32>(0.0);\n"
    "  if (material.emissive_strength > 0.0) {\n"
    "    var em_sample = vec3<f32>(1.0);\n"
    "    if (material.layer_emissive != 0u) {\n"
    "      em_sample = sample_emissive(uv, material.layer_emissive, dx, dy).rgb;\n"
    "    }\n"
    "    emissive = em_sample * em_base * max(material.emissive_strength, 0.0);\n"
    "  }\n"

    "  var mapped_normal = normalize(input.normal);\n"
    "  if (material.layer_normal != 0u) {\n"
    "    let normal_sample = sample_normal(uv, material.layer_normal, dx, dy).rgb;\n"
    "    let tangent_normal = normal_sample * 2.0 - 1.0;\n"
    "    let T_raw = input.tangent;\n"
    "    let T = normalize(T_raw - mapped_normal * dot(mapped_normal, T_raw));\n"
    "    let B = cross(mapped_normal, T) * input.bitangent_sign;\n"
    "    let tbn = mat3x3<f32>(T, B, mapped_normal);\n"
    "    mapped_normal = normalize(tbn * tangent_normal);\n"
    "  }\n"

    /* No transmission: identical to the pbr shader. */
    "  if (material.transmission_factor <= 0.0) {\n"
    "    let lit = computePbrLighting(\n"
    "      albedo, metallic, roughness, emissive,\n"
    "      input.world_pos, mapped_normal, input.pos);\n"
    "    return vec4<f32>(lit, alpha);\n"
    "  }\n"

    /* Transmission path: screen-space refraction through opaque snapshot. */
    "  let v = getViewDirection(input.world_pos);\n"
    "  let ndotv = saturate(dot(mapped_normal, v));\n"

    /* Fresnel reflection (Schlick with IOR-derived F0). */
    "  let ior_ratio = (1.0 - material.ior) / (1.0 + material.ior);\n"
    "  let f0_ior = ior_ratio * ior_ratio;\n"
    "  let fx = 1.0 - ndotv;\n"
    "  let fx2 = fx * fx;\n"
    "  let F = f0_ior + (1.0 - f0_ior) * (fx2 * fx2 * fx);\n"

    /* Screen-space refraction: project refracted exit point through VP. */
    "  let refracted = refract(-v, mapped_normal, 1.0 / max(material.ior, 0.001));\n"
    "  let exit_pos = input.world_pos + normalize(refracted)\n"
    "    * material.thickness_factor * input.model_scale;\n"
    "  let exit_clip = uniforms.vp * vec4<f32>(exit_pos, 1.0);\n"
    "  var screen_uv = exit_clip.xy / exit_clip.w * vec2<f32>(0.5, -0.5)\n"
    "    + vec2<f32>(0.5, 0.5);\n"
    "  screen_uv = clamp(screen_uv, vec2<f32>(0.0), vec2<f32>(1.0));\n"

    /* Sample opaque scene at refracted UV with roughness-dependent LOD. */
    "  let max_mip = max(f32(textureNumLevels(opaque_snapshot)) - 1.0, 0.0);\n"
    "  let bg_lod = roughness * max_mip;\n"
    "  let background = textureSampleLevel(\n"
    "    opaque_snapshot, ibl_sampler, screen_uv, bg_lod).rgb;\n"

    /* Beer-Lambert absorption. */
    "  let att_color = max(unpack4x8unorm(material.attenuation_color).rgb,\n"
    "    vec3<f32>(0.001));\n"
    "  let att_ratio = material.thickness_factor\n"
    "    / max(material.attenuation_distance, 0.001);\n"
    "  let transmittance = pow(att_color, vec3<f32>(att_ratio));\n"
    "  let transmitted = (1.0 - F) * background * transmittance * albedo;\n"

    /* Reflected light split into diffuse and specular. */
    "  let reflected = computePbrLightingSplit(\n"
    "    albedo, metallic, roughness, emissive,\n"
    "    input.world_pos, mapped_normal, input.pos);\n"

    /* Blend: transmission replaces diffuse, specular always added on top. */
    "  let final_color = mix(reflected.diffuse, transmitted, material.transmission_factor)\n"
    "    + reflected.specular;\n"
    "  return vec4<f32>(final_color, 1.0);\n"
    "}\n";

ecs_entity_t flecsEngine_shader_pbrTransmission(
    ecs_world_t *world)
{
    return flecsEngine_shader_ensure(world, "PbrTransmissionShader",
        &(FlecsShader){
            .source = kShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}
