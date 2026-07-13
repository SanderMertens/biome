#include "shaders.h"
#include "../renderer.h"
#include "common/uniforms_wgsl.h"
#include "common/ibl_bindings_wgsl.h"

static const char *kShaderSource =
    FLECS_ENGINE_SHADER_COMMON_UNIFORMS_WGSL
    FLECS_ENGINE_SHADER_COMMON_IBL_BINDINGS_WGSL
    "struct VertexInput {\n"
    "  @location(0) pos : vec3<f32>\n"
    "};\n"
    "struct VertexOutput {\n"
    "  @builtin(position) pos : vec4<f32>,\n"
    "  @location(0) view_ray : vec3<f32>\n"
    "};\n"
    "@vertex fn vs_main(input : VertexInput) -> VertexOutput {\n"
    "  var out : VertexOutput;\n"
    "  let ndc_xy = input.pos.xy * 2.0;\n"
    "  out.pos = vec4<f32>(ndc_xy, 1.0, 1.0);\n"
    "  let world_far_h = uniforms.inv_vp * vec4<f32>(ndc_xy, 1.0, 1.0);\n"
    "  out.view_ray = world_far_h.xyz / world_far_h.w - uniforms.camera_pos.xyz;\n"
    "  return out;\n"
    "}\n"
    "@fragment fn fs_main(input : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let ray_dir = normalize(input.view_ray);\n"
    "  let sky = textureSampleLevel(ibl_prefiltered_env, ibl_sampler, ray_dir, 0.0).rgb;\n"
    "  return vec4<f32>(sky, 1.0);\n"
    "}\n";

ecs_entity_t flecsEngine_shader_skybox(
    ecs_world_t *world)
{
    return flecsEngine_shader_ensure(world, "SkyboxShader",
        &(FlecsShader){
            .source = kShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}
