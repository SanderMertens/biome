#ifndef FLECS_ENGINE_SHADER_COMMON_IBL_BINDINGS_WGSL_H
#define FLECS_ENGINE_SHADER_COMMON_IBL_BINDINGS_WGSL_H

#define FLECS_ENGINE_SHADER_COMMON_IBL_BINDINGS_WGSL \
    "@group(0) @binding(1) var ibl_prefiltered_env : texture_cube<f32>;\n" \
    "@group(0) @binding(2) var ibl_sampler : sampler;\n" \
    "@group(0) @binding(4) var ibl_irradiance_env : texture_cube<f32>;\n" \
    "fn envBRDFApprox(roughness : f32, ndotv : f32) -> vec2<f32> {\n" \
    "  let c0 = vec4<f32>(-1.0, -0.0275, -0.572, 0.022);\n" \
    "  let c1 = vec4<f32>(1.0, 0.0425, 1.04, -0.04);\n" \
    "  let r = roughness * c0 + c1;\n" \
    "  let a004 = min(r.x * r.x, exp2(-9.28 * ndotv)) * r.x + r.y;\n" \
    "  return vec2<f32>(-1.04, 1.04) * a004 + r.zw;\n" \
    "}\n"

#endif
