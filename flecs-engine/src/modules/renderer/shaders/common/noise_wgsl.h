#ifndef FLECS_ENGINE_SHADER_COMMON_NOISE_WGSL_H
#define FLECS_ENGINE_SHADER_COMMON_NOISE_WGSL_H

#define FLECS_ENGINE_SHADER_COMMON_NOISE_WGSL \
    "fn remap(v : f32, lo : f32, hi : f32, nlo : f32, nhi : f32) -> f32 {\n" \
    "  return nlo + (v - lo) * (nhi - nlo) / max(hi - lo, 1e-6);\n" \
    "}\n"

#endif
