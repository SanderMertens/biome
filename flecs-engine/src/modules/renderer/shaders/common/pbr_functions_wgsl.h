#ifndef FLECS_ENGINE_SHADER_COMMON_PBR_FUNCTIONS_WGSL_H
#define FLECS_ENGINE_SHADER_COMMON_PBR_FUNCTIONS_WGSL_H

#define FLECS_ENGINE_SHADER_COMMON_PBR_FUNCTIONS_WGSL \
    "const PI : f32 = 3.141592653589793;\n" \
    "fn saturate(v : f32) -> f32 {\n" \
    "  return clamp(v, 0.0, 1.0);\n" \
    "}\n" \
    "fn computeSplitSumSpecularTerm(f0 : vec3<f32>, brdf : vec2<f32>) -> vec3<f32> {\n" \
    "  return f0 * brdf.x + vec3<f32>(brdf.y);\n" \
    "}\n" \
    "fn fresnelSchlick(cos_theta : f32, f0 : vec3<f32>, f90 : f32) -> vec3<f32> {\n" \
    "  let x = 1.0 - cos_theta;\n" \
    "  let x2 = x * x;\n" \
    "  return f0 + (vec3<f32>(f90) - f0) * (x2 * x2 * x);\n" \
    "}\n" \
    "fn fresnelSchlickRoughness(cos_theta : f32, f0 : vec3<f32>, roughness : f32) -> vec3<f32> {\n" \
    "  let x = 1.0 - cos_theta;\n" \
    "  let x2 = x * x;\n" \
    "  let f90 = max(vec3<f32>(1.0 - roughness), f0);\n" \
    "  return f0 + (f90 - f0) * (x2 * x2 * x);\n" \
    "}\n" \
    "fn computeF90(f0 : vec3<f32>) -> f32 {\n" \
    "  return saturate(dot(f0, vec3<f32>(50.0 * 0.33)));\n" \
    "}\n" \
    "const PBR_DIV_EPSILON : f32 = 1e-7;\n" \
    "fn distributionGGX(n : vec3<f32>, h : vec3<f32>, roughness : f32) -> f32 {\n" \
    "  let a = roughness * roughness;\n" \
    "  let a2 = a * a;\n" \
    "  let ndoth = max(dot(n, h), 0.0);\n" \
    "  let ndoth2 = ndoth * ndoth;\n" \
    "  let denom = ndoth2 * (a2 - 1.0) + 1.0;\n" \
    "  return a2 / max(PI * denom * denom, PBR_DIV_EPSILON);\n" \
    "}\n" \
    "fn visibilitySmithGGXCorrelated(ndotv : f32, ndotl : f32, roughness : f32) -> f32 {\n" \
    "  let a = roughness * roughness;\n" \
    "  let a2 = a * a;\n" \
    "  let lambda_v = ndotl * sqrt((ndotv - a2 * ndotv) * ndotv + a2);\n" \
    "  let lambda_l = ndotv * sqrt((ndotl - a2 * ndotl) * ndotl + a2);\n" \
    "  return 0.5 / max(lambda_v + lambda_l, PBR_DIV_EPSILON);\n" \
    "}\n" \
    "struct DirectionSample {\n" \
    "  dir : vec3<f32>,\n" \
    "  enabled : bool\n" \
    "};\n" \
    "struct PbrLightingSplit {\n" \
    "  diffuse : vec3<f32>,\n" \
    "  specular : vec3<f32>\n" \
    "};\n" \
    "fn safeNormalize(v : vec3<f32>, fallback : vec3<f32>) -> vec3<f32> {\n" \
    "  let len2 = dot(v, v);\n" \
    "  return normalize(select(fallback, v, len2 > 1e-6));\n" \
    "}\n" \
    "fn getLightDirection() -> DirectionSample {\n" \
    "  let light_vec = -uniforms.light_ray_dir.xyz;\n" \
    "  let light_enabled = dot(light_vec, light_vec) > 1e-6;\n" \
    "  return DirectionSample(safeNormalize(light_vec, vec3<f32>(0.0, 1.0, 0.0)), light_enabled);\n" \
    "}\n" \
    "fn getViewDirection(world_pos : vec3<f32>) -> vec3<f32> {\n" \
    "  let view_vec = uniforms.camera_pos.xyz - world_pos;\n" \
    "  return safeNormalize(view_vec, vec3<f32>(0.0, 0.0, 1.0));\n" \
    "}\n" \
    "fn getHalfVector(v : vec3<f32>, l : vec3<f32>) -> vec3<f32> {\n" \
    "  return safeNormalize(v + l, v);\n" \
    "}\n" \
    "fn computeF0(albedo : vec3<f32>, metallic : f32) -> vec3<f32> {\n" \
    "  return mix(vec3<f32>(0.04), albedo, metallic);\n" \
    "}\n" \
    "fn computeDiffuseBurley(\n" \
    "  albedo : vec3<f32>,\n" \
    "  metallic : f32,\n" \
    "  roughness : f32,\n" \
    "  ndotv : f32,\n" \
    "  ndotl : f32,\n" \
    "  ldoth : f32) -> vec3<f32> {\n" \
    "  let f90 = 0.5 + 2.0 * roughness * ldoth * ldoth;\n" \
    "  let light_scatter = 1.0 + (f90 - 1.0) * pow(1.0 - ndotl, 5.0);\n" \
    "  let view_scatter  = 1.0 + (f90 - 1.0) * pow(1.0 - ndotv, 5.0);\n" \
    "  let fd = light_scatter * view_scatter / PI;\n" \
    "  return albedo * (1.0 - metallic) * fd;\n" \
    "}\n" \
    "fn computeSpecular(\n" \
    "  n : vec3<f32>,\n" \
    "  ndotv : f32,\n" \
    "  ndotl : f32,\n" \
    "  l : vec3<f32>,\n" \
    "  h : vec3<f32>,\n" \
    "  roughness : f32,\n" \
    "  f : vec3<f32>) -> vec3<f32> {\n" \
    "  let d = distributionGGX(n, h, roughness);\n" \
    "  let v = visibilitySmithGGXCorrelated(ndotv, ndotl, roughness);\n" \
    "  return d * v * f;\n" \
    "}\n" \
    "fn computeDirectLightingSplit(\n" \
    "  n : vec3<f32>,\n" \
    "  v : vec3<f32>,\n" \
    "  l : vec3<f32>,\n" \
    "  h : vec3<f32>,\n" \
    "  albedo : vec3<f32>,\n" \
    "  metallic : f32,\n" \
    "  roughness : f32,\n" \
    "  f0 : vec3<f32>,\n" \
    "  f90 : f32,\n" \
    "  ndotv : f32,\n" \
    "  light_active : bool) -> PbrLightingSplit {\n" \
    "  if (!light_active) {\n" \
    "    return PbrLightingSplit(vec3<f32>(0.0), vec3<f32>(0.0));\n" \
    "  }\n" \
    "  let ndotl = max(dot(n, l), 0.0);\n" \
    "  if (ndotl <= 0.0) {\n" \
    "    return PbrLightingSplit(vec3<f32>(0.0), vec3<f32>(0.0));\n" \
    "  }\n" \
    "  let ldoth = max(dot(l, h), 0.0);\n" \
    "  let f = fresnelSchlick(ldoth, f0, f90);\n" \
    "  let scale = uniforms.light_color.rgb * ndotl;\n" \
    "  let diffuse = computeDiffuseBurley(albedo, metallic, roughness, ndotv, ndotl, ldoth) * scale;\n" \
    "  let specular = computeSpecular(n, ndotv, ndotl, l, h, roughness, f) * scale;\n" \
    "  return PbrLightingSplit(diffuse, specular);\n" \
    "}\n" \
    "fn computeDirectLighting(\n" \
    "  n : vec3<f32>,\n" \
    "  v : vec3<f32>,\n" \
    "  l : vec3<f32>,\n" \
    "  h : vec3<f32>,\n" \
    "  albedo : vec3<f32>,\n" \
    "  metallic : f32,\n" \
    "  roughness : f32,\n" \
    "  f0 : vec3<f32>,\n" \
    "  f90 : f32,\n" \
    "  ndotv : f32,\n" \
    "  light_active : bool) -> vec3<f32> {\n" \
    "  let s = computeDirectLightingSplit(\n" \
    "    n, v, l, h, albedo, metallic, roughness, f0, f90, ndotv, light_active);\n" \
    "  return s.diffuse + s.specular;\n" \
    "}\n" \
    "fn computeClusterLightingSplit(\n" \
    "  n : vec3<f32>,\n" \
    "  v : vec3<f32>,\n" \
    "  world_pos : vec3<f32>,\n" \
    "  albedo : vec3<f32>,\n" \
    "  metallic : f32,\n" \
    "  roughness : f32,\n" \
    "  f0 : vec3<f32>,\n" \
    "  f90 : f32,\n" \
    "  ndotv : f32,\n" \
    "  cluster_idx : u32) -> PbrLightingSplit {\n" \
    "  var diffuse = vec3<f32>(0.0);\n" \
    "  var specular = vec3<f32>(0.0);\n" \
    "  let entry = cluster_grid[cluster_idx];\n" \
    "  for (var j = 0u; j < entry.light_count; j++) {\n" \
    "    let i = light_indices[entry.light_offset + j];\n" \
    "    let light_pos = lights[i].position.xyz;\n" \
    "    let light_range = lights[i].position.w;\n" \
    "    let light_dir = lights[i].direction.xyz;\n" \
    "    let outer_cos = lights[i].direction.w;\n" \
    "    let light_color = lights[i].color.rgb;\n" \
    "    let inner_cos = lights[i].color.w;\n" \
    "    let to_light = light_pos - world_pos;\n" \
    "    let dist = length(to_light);\n" \
    "    if (dist > light_range || dist < 0.001) {\n" \
    "      continue;\n" \
    "    }\n" \
    "    let l = to_light / dist;\n" \
    "    var spot_effect = 1.0;\n" \
    "    if (outer_cos > -1.5) {\n" \
    "      let theta = dot(l, -light_dir);\n" \
    "      if (theta < outer_cos) {\n" \
    "        continue;\n" \
    "      }\n" \
    "      spot_effect = clamp((theta - outer_cos) / max(inner_cos - outer_cos, PBR_DIV_EPSILON), 0.0, 1.0);\n" \
    "    }\n" \
    "    let ndotl = max(dot(n, l), 0.0);\n" \
    "    if (ndotl <= 0.0) {\n" \
    "      continue;\n" \
    "    }\n" \
    "    let h = normalize(v + l);\n" \
    "    let ratio = dist / light_range;\n" \
    "    let r2 = ratio * ratio;\n" \
    "    let attenuation = clamp(1.0 - r2 * r2, 0.0, 1.0) / (dist * dist + 1.0);\n" \
    "    let ldoth = max(dot(l, h), 0.0);\n" \
    "    let f = fresnelSchlick(ldoth, f0, f90);\n" \
    "    let scale = light_color * ndotl * attenuation * spot_effect;\n" \
    "    diffuse += computeDiffuseBurley(albedo, metallic, roughness, ndotv, ndotl, ldoth) * scale;\n" \
    "    specular += computeSpecular(n, ndotv, ndotl, l, h, roughness, f) * scale;\n" \
    "  }\n" \
    "  return PbrLightingSplit(diffuse, specular);\n" \
    "}\n" \
    "fn computeClusterLighting(\n" \
    "  n : vec3<f32>,\n" \
    "  v : vec3<f32>,\n" \
    "  world_pos : vec3<f32>,\n" \
    "  albedo : vec3<f32>,\n" \
    "  metallic : f32,\n" \
    "  roughness : f32,\n" \
    "  f0 : vec3<f32>,\n" \
    "  f90 : f32,\n" \
    "  ndotv : f32,\n" \
    "  cluster_idx : u32) -> vec3<f32> {\n" \
    "  let s = computeClusterLightingSplit(\n" \
    "    n, v, world_pos, albedo, metallic, roughness,\n" \
    "    f0, f90, ndotv, cluster_idx);\n" \
    "  return s.diffuse + s.specular;\n" \
    "}\n"

#endif
