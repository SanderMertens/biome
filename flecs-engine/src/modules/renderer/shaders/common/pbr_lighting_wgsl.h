#ifndef FLECS_ENGINE_SHADER_COMMON_PBR_LIGHTING_WGSL_H
#define FLECS_ENGINE_SHADER_COMMON_PBR_LIGHTING_WGSL_H

#define FLECS_ENGINE_SHADER_COMMON_PBR_LIGHTING_WGSL \
    "fn computePbrLightingSplit(\n" \
    "  albedo : vec3<f32>,\n" \
    "  metallic : f32,\n" \
    "  roughness : f32,\n" \
    "  emissive : vec3<f32>,\n" \
    "  world_pos : vec3<f32>,\n" \
    "  normal : vec3<f32>,\n" \
    "  frag_coord : vec4<f32>\n" \
    ") -> PbrLightingSplit {\n" \
    "  let direct_roughness = max(roughness, 0.045);\n" \
    "  let n = normal;\n" \
    "  let light = getLightDirection();\n" \
    "  let v = getViewDirection(world_pos);\n" \
    "  let h = getHalfVector(v, light.dir);\n" \
    "  let f0 = computeF0(albedo, metallic);\n" \
    "  let f90 = computeF90(f0);\n" \
    "  let ndotv = saturate(dot(n, v));\n" \
    "  let shadow = min(computeShadow(world_pos, n), computeCloudShadow(world_pos));\n" \
    "  let direct = computeDirectLightingSplit(\n" \
    "    n, v, light.dir, h,\n" \
    "    albedo, metallic, direct_roughness,\n" \
    "    f0, f90, ndotv,\n" \
    "    light.enabled);\n" \
    "  let r = reflect(-v, n);\n" \
    "  let max_mip = max(f32(textureNumLevels(ibl_prefiltered_env)) - 1.0, 0.0);\n" \
    "  let lod = roughness * max_mip;\n" \
    "  let prefiltered_color = textureSampleLevel(ibl_prefiltered_env, ibl_sampler, r, lod).rgb;\n" \
    "  let brdf = envBRDFApprox(roughness, ndotv);\n" \
    "  let F = fresnelSchlickRoughness(ndotv, f0, roughness);\n" \
    "  let specular_single = computeSplitSumSpecularTerm(f0, brdf);\n" \
    "  let energy_comp = vec3<f32>(1.0) + f0 * (1.0 / max(brdf.x + brdf.y, PBR_DIV_EPSILON) - 1.0);\n" \
    "  let specular_ibl = prefiltered_color * specular_single * energy_comp * uniforms.ambient_light.w;\n" \
    "  let irradiance = textureSampleLevel(ibl_irradiance_env, ibl_sampler, n, 0.0).rgb;\n" \
    "  let kD = (vec3<f32>(1.0) - F) * (1.0 - metallic);\n" \
    "  let diffuse_ibl = irradiance * albedo * kD * uniforms.ambient_light.w;\n" \
    "  let cluster_idx = getClusterIndex(frag_coord);\n" \
    "  let cluster = computeClusterLightingSplit(\n" \
    "    n, v, world_pos, albedo, metallic, direct_roughness,\n" \
    "    f0, f90, ndotv, cluster_idx);\n" \
    "  let diffuse_total = diffuse_ibl + direct.diffuse * shadow + cluster.diffuse;\n" \
    "  let specular_total = specular_ibl + direct.specular * shadow * energy_comp + cluster.specular * energy_comp + emissive;\n" \
    "  return PbrLightingSplit(diffuse_total, specular_total);\n" \
    "}\n" \
    "fn computePbrLighting(\n" \
    "  albedo : vec3<f32>,\n" \
    "  metallic : f32,\n" \
    "  roughness : f32,\n" \
    "  emissive : vec3<f32>,\n" \
    "  world_pos : vec3<f32>,\n" \
    "  normal : vec3<f32>,\n" \
    "  frag_coord : vec4<f32>\n" \
    ") -> vec3<f32> {\n" \
    "  let s = computePbrLightingSplit(\n" \
    "    albedo, metallic, roughness, emissive,\n" \
    "    world_pos, normal, frag_coord);\n" \
    "  return s.diffuse + s.specular;\n" \
    "}\n"

#endif
