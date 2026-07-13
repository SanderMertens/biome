#ifndef FLECS_ENGINE_SHADER_COMMON_PBR_TEXTURES_WGSL_H
#define FLECS_ENGINE_SHADER_COMMON_PBR_TEXTURES_WGSL_H

#define FLECS_ENGINE_SHADER_COMMON_PBR_TEXTURES_WGSL \
    "@group(1) @binding(0) var albedo_tex        : texture_2d_array<f32>;\n" \
    "@group(1) @binding(1) var emissive_tex      : texture_2d_array<f32>;\n" \
    "@group(1) @binding(2) var roughness_tex     : texture_2d_array<f32>;\n" \
    "@group(1) @binding(3) var normal_tex        : texture_2d_array<f32>;\n" \
    "@group(1) @binding(4) var tex_sampler_aniso : sampler;\n" \
    "@group(1) @binding(5) var tex_sampler_low   : sampler;\n" \
    "fn sample_albedo(uv : vec2<f32>, layer : u32,\n" \
    "                 dx : vec2<f32>, dy : vec2<f32>) -> vec4<f32> {\n" \
    "  return textureSampleGrad(albedo_tex, tex_sampler_aniso, uv, layer, dx, dy);\n" \
    "}\n" \
    "fn sample_emissive(uv : vec2<f32>, layer : u32,\n" \
    "                   dx : vec2<f32>, dy : vec2<f32>) -> vec4<f32> {\n" \
    "  return textureSampleGrad(emissive_tex, tex_sampler_low, uv, layer, dx, dy);\n" \
    "}\n" \
    "fn sample_roughness(uv : vec2<f32>, layer : u32,\n" \
    "                    dx : vec2<f32>, dy : vec2<f32>) -> vec4<f32> {\n" \
    "  return textureSampleGrad(roughness_tex, tex_sampler_low, uv, layer, dx, dy);\n" \
    "}\n" \
    "fn sample_normal(uv : vec2<f32>, layer : u32,\n" \
    "                 dx : vec2<f32>, dy : vec2<f32>) -> vec4<f32> {\n" \
    "  return textureSampleGrad(normal_tex, tex_sampler_aniso, uv, layer, dx, dy);\n" \
    "}\n"

#endif
