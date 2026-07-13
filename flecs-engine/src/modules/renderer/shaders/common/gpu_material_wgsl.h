#ifndef FLECS_ENGINE_SHADER_COMMON_GPU_MATERIAL_WGSL_H
#define FLECS_ENGINE_SHADER_COMMON_GPU_MATERIAL_WGSL_H

#define FLECS_ENGINE_SHADER_COMMON_GPU_MATERIAL_STRUCT_WGSL \
    "struct GpuMaterial {\n" \
    "  color : u32,\n" \
    "  metallic : f32,\n" \
    "  roughness : f32,\n" \
    "  emissive_strength : f32,\n" \
    "  emissive_color : u32,\n" \
    "  layer_albedo : u32,\n" \
    "  layer_emissive : u32,\n" \
    "  layer_mr : u32,\n" \
    "  layer_normal : u32,\n" \
    "  transmission_factor : f32,\n" \
    "  ior : f32,\n" \
    "  thickness_factor : f32,\n" \
    "  attenuation_distance : f32,\n" \
    "  attenuation_color : u32,\n" \
    "  uv_scale_x : f32,\n" \
    "  uv_scale_y : f32,\n" \
    "  uv_offset_x : f32,\n" \
    "  uv_offset_y : f32,\n" \
    "  tint : u32\n" \
    "};\n"

#define FLECS_ENGINE_SHADER_COMMON_GPU_MATERIAL_WGSL \
    FLECS_ENGINE_SHADER_COMMON_GPU_MATERIAL_STRUCT_WGSL \
    "@group(2) @binding(0) var<storage, read> materials : array<GpuMaterial>;\n"

#endif
