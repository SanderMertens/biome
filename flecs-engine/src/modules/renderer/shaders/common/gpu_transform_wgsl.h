#ifndef FLECS_ENGINE_SHADER_COMMON_GPU_TRANSFORM_WGSL_H
#define FLECS_ENGINE_SHADER_COMMON_GPU_TRANSFORM_WGSL_H

#define FLECS_ENGINE_SHADER_COMMON_GPU_TRANSFORM_WGSL \
    "struct GpuTransform {\n" \
    "  c0 : vec4<f32>,\n" \
    "  c1 : vec4<f32>,\n" \
    "  c2 : vec4<f32>,\n" \
    "  c3 : vec4<f32>\n" \
    "};\n" \
    "@group(3) @binding(0) var<storage, read> instance_transforms : array<GpuTransform>;\n" \
    "@group(3) @binding(1) var<storage, read> instance_material_ids : array<u32>;\n"

#define FLECS_ENGINE_SHADER_COMMON_GPU_TRANSFORM_SHADOW_WGSL \
    "struct GpuTransform {\n" \
    "  c0 : vec4<f32>,\n" \
    "  c1 : vec4<f32>,\n" \
    "  c2 : vec4<f32>,\n" \
    "  c3 : vec4<f32>\n" \
    "};\n" \
    "@group(1) @binding(0) var<storage, read> instance_transforms : array<GpuTransform>;\n" \
    "@group(1) @binding(1) var<storage, read> instance_material_ids : array<u32>;\n"

#endif
