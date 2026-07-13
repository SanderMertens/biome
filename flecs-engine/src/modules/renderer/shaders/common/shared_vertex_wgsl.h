#ifndef FLECS_ENGINE_SHADER_COMMON_SHARED_VERTEX_WGSL_H
#define FLECS_ENGINE_SHADER_COMMON_SHARED_VERTEX_WGSL_H

#define FLECS_ENGINE_SHADER_COMMON_SHARED_VERTEX_WGSL \
    "struct PbrVertexState {\n" \
    "  clip_pos : vec4<f32>,\n" \
    "  world_pos : vec3<f32>,\n" \
    "  world_normal : vec3<f32>\n" \
    "};\n" \
    "fn buildPbrVertexState(\n" \
    "  pos : vec3<f32>,\n" \
    "  nrm : vec3<f32>,\n" \
    "  m0 : vec3<f32>,\n" \
    "  m1 : vec3<f32>,\n" \
    "  m2 : vec3<f32>,\n" \
    "  m3 : vec3<f32>) -> PbrVertexState {\n" \
    "  let model = mat4x4<f32>(\n" \
    "    vec4<f32>(m0, 0.0),\n" \
    "    vec4<f32>(m1, 0.0),\n" \
    "    vec4<f32>(m2, 0.0),\n" \
    "    vec4<f32>(m3, 1.0)\n" \
    "  );\n" \
    "  let c0 = m0;\n" \
    "  let c1 = m1;\n" \
    "  let c2 = m2;\n" \
    "  let normal_matrix = mat3x3<f32>(\n" \
    "    cross(c1, c2),\n" \
    "    cross(c2, c0),\n" \
    "    cross(c0, c1)\n" \
    "  );\n" \
    "  let world_pos = model * vec4<f32>(pos, 1.0);\n" \
    "  let world_normal = normalize(normal_matrix * nrm);\n" \
    "  return PbrVertexState(uniforms.vp * world_pos, world_pos.xyz, world_normal);\n" \
    "}\n"

#endif
