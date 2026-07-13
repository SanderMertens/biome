#ifndef FLECS_ENGINE_IBL_HELPERS_WGSL_H
#define FLECS_ENGINE_IBL_HELPERS_WGSL_H

#define FLECS_ENGINE_IBL_HELPERS_WGSL \
    "const PI : f32 = 3.141592653589793;\n" \
    "fn cubeFaceUvToDir(face : u32, uv : vec2<f32>, face_size : f32) -> vec3<f32> {\n" \
    "  let scale = max(1.0 - (1.0 / max(face_size, 1.0)), 0.0);\n" \
    "  let st = (uv * 2.0 - vec2<f32>(1.0, 1.0)) * scale;\n" \
    "  let x = st.x;\n" \
    "  let y = -st.y;\n" \
    "  if (face == 0u) {\n" \
    "    return normalize(vec3<f32>(1.0, y, -x));\n" \
    "  }\n" \
    "  if (face == 1u) {\n" \
    "    return normalize(vec3<f32>(-1.0, y, x));\n" \
    "  }\n" \
    "  if (face == 2u) {\n" \
    "    return normalize(vec3<f32>(x, 1.0, -y));\n" \
    "  }\n" \
    "  if (face == 3u) {\n" \
    "    return normalize(vec3<f32>(x, -1.0, y));\n" \
    "  }\n" \
    "  if (face == 4u) {\n" \
    "    return normalize(vec3<f32>(x, y, 1.0));\n" \
    "  }\n" \
    "  return normalize(vec3<f32>(-x, y, -1.0));\n" \
    "}\n" \
    "fn radicalInverseVdC(bits_in : u32) -> f32 {\n" \
    "  var bits = bits_in;\n" \
    "  bits = (bits << 16u) | (bits >> 16u);\n" \
    "  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);\n" \
    "  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);\n" \
    "  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);\n" \
    "  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);\n" \
    "  return f32(bits) * 2.3283064365386963e-10;\n" \
    "}\n" \
    "fn hammersley(i : u32, n : u32) -> vec2<f32> {\n" \
    "  return vec2<f32>(f32(i) / f32(n), radicalInverseVdC(i));\n" \
    "}\n" \
    "fn importanceSampleGGX(xi : vec2<f32>, n : vec3<f32>, roughness : f32) -> vec3<f32> {\n" \
    "  let a = roughness * roughness;\n" \
    "  let phi = 2.0 * PI * xi.x;\n" \
    "  let cos_theta = sqrt((1.0 - xi.y) / (1.0 + ((a * a) - 1.0) * xi.y));\n" \
    "  let sin_theta = sqrt(max(1.0 - cos_theta * cos_theta, 0.0));\n" \
    "  let h = vec3<f32>(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);\n" \
    "  let up = select(vec3<f32>(0.0, 0.0, 1.0), vec3<f32>(1.0, 0.0, 0.0), abs(n.z) > 0.999);\n" \
    "  let tangent = normalize(cross(up, n));\n" \
    "  let bitangent = cross(n, tangent);\n" \
    "  return normalize(tangent * h.x + bitangent * h.y + n * h.z);\n" \
    "}\n" \
    "fn directionToEquirectUv(dir : vec3<f32>) -> vec2<f32> {\n" \
    "  let d = normalize(dir);\n" \
    "  let phi = atan2(d.z, d.x);\n" \
    "  let theta = asin(clamp(d.y, -1.0, 1.0));\n" \
    "  return vec2<f32>(phi / (2.0 * PI) + 0.5, 0.5 - theta / PI);\n" \
    "}\n"

#endif
