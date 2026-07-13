#ifndef FLECS_ENGINE_SHADER_COMMON_SHADOW_WGSL_H
#define FLECS_ENGINE_SHADER_COMMON_SHADOW_WGSL_H

#define FLECS_ENGINE_SHADER_COMMON_SHADOW_WGSL \
    "@group(0) @binding(5) var shadow_map : texture_depth_2d_array;\n" \
    "@group(0) @binding(6) var shadow_sampler : sampler_comparison;\n" \
    "fn sampleShadowCascade(world_pos : vec3<f32>, normal : vec3<f32>, cascade : i32) -> f32 {\n" \
    "  let m = uniforms.light_vp[cascade];\n" \
    "  let map_size = f32(textureDimensions(shadow_map).x);\n" \
    "  let texel_world = 2.0 /\n" \
    "    (length(vec3<f32>(m[0].x, m[1].x, m[2].x)) * map_size);\n" \
    "  let ndotl = saturate(dot(normal, -uniforms.light_ray_dir.xyz));\n" \
    "  let offset = texel_world * (1.0 + 3.0 * (1.0 - ndotl));\n" \
    "  let sample_pos = world_pos + normal * offset;\n" \
    "  let light_clip = m * vec4<f32>(sample_pos, 1.0);\n" \
    "  let light_ndc = light_clip.xyz / light_clip.w;\n" \
    "  let shadow_uv = vec2<f32>(\n" \
    "    light_ndc.x * 0.5 + 0.5,\n" \
    "    light_ndc.y * -0.5 + 0.5\n" \
    "  );\n" \
    "  if (shadow_uv.x < 0.0 || shadow_uv.x > 1.0 ||\n" \
    "      shadow_uv.y < 0.0 || shadow_uv.y > 1.0 ||\n" \
    "      light_ndc.z < 0.0 || light_ndc.z > 1.0) {\n" \
    "    return -1.0;\n" \
    "  }\n" \
    "  let current_depth = light_ndc.z - uniforms.shadow_info.y;\n" \
    "  let shadow_size = vec2<f32>(textureDimensions(shadow_map));\n" \
    "  let texel_size = 1.0 / shadow_size;\n" \
    "  let uv = shadow_uv * shadow_size;\n" \
    "  var base_uv = vec2<f32>(floor(uv.x + 0.5), floor(uv.y + 0.5));\n" \
    "  let s = uv.x + 0.5 - base_uv.x;\n" \
    "  let t = uv.y + 0.5 - base_uv.y;\n" \
    "  base_uv = (base_uv - vec2<f32>(0.5)) * texel_size;\n" \
    "  let uw0 = 3.0 - 2.0 * s;\n" \
    "  let uw1 = 1.0 + 2.0 * s;\n" \
    "  let u0 = (2.0 - s) / uw0 - 1.0;\n" \
    "  let u1 = s / uw1 + 1.0;\n" \
    "  let vw0 = 3.0 - 2.0 * t;\n" \
    "  let vw1 = 1.0 + 2.0 * t;\n" \
    "  let v0 = (2.0 - t) / vw0 - 1.0;\n" \
    "  let v1 = t / vw1 + 1.0;\n" \
    "  var shadow = 0.0;\n" \
    "  shadow += uw0 * vw0 * textureSampleCompareLevel(\n" \
    "    shadow_map, shadow_sampler,\n" \
    "    base_uv + vec2<f32>(u0, v0) * texel_size, cascade, current_depth);\n" \
    "  shadow += uw1 * vw0 * textureSampleCompareLevel(\n" \
    "    shadow_map, shadow_sampler,\n" \
    "    base_uv + vec2<f32>(u1, v0) * texel_size, cascade, current_depth);\n" \
    "  shadow += uw0 * vw1 * textureSampleCompareLevel(\n" \
    "    shadow_map, shadow_sampler,\n" \
    "    base_uv + vec2<f32>(u0, v1) * texel_size, cascade, current_depth);\n" \
    "  shadow += uw1 * vw1 * textureSampleCompareLevel(\n" \
    "    shadow_map, shadow_sampler,\n" \
    "    base_uv + vec2<f32>(u1, v1) * texel_size, cascade, current_depth);\n" \
    "  return shadow / 16.0;\n" \
    "}\n" \
    "fn computeShadow(world_pos : vec3<f32>, normal : vec3<f32>) -> f32 {\n" \
    "  let clip = uniforms.vp * vec4<f32>(world_pos, 1.0);\n" \
    "  let view_depth = clip.w;\n" \
    "  if (view_depth > uniforms.cascade_splits.w) {\n" \
    "    return 1.0;\n" \
    "  }\n" \
    "  var cascade : i32 = 0;\n" \
    "  var near_split : f32 = 0.0;\n" \
    "  var far_split : f32 = uniforms.cascade_splits.x;\n" \
    "  if (view_depth > uniforms.cascade_splits.z) {\n" \
    "    cascade = 3; near_split = uniforms.cascade_splits.z; far_split = uniforms.cascade_splits.w;\n" \
    "  } else if (view_depth > uniforms.cascade_splits.y) {\n" \
    "    cascade = 2; near_split = uniforms.cascade_splits.y; far_split = uniforms.cascade_splits.z;\n" \
    "  } else if (view_depth > uniforms.cascade_splits.x) {\n" \
    "    cascade = 1; near_split = uniforms.cascade_splits.x; far_split = uniforms.cascade_splits.y;\n" \
    "  }\n" \
    "  var shadow = sampleShadowCascade(world_pos, normal, cascade);\n" \
    "  let range = max(far_split - near_split, 0.0001);\n" \
    "  let blend_start = far_split - range * 0.15;\n" \
    "  if (view_depth > blend_start && shadow >= 0.0) {\n" \
    "    let t = clamp((view_depth - blend_start) / max(far_split - blend_start, 0.0001), 0.0, 1.0);\n" \
    "    if (cascade < 3) {\n" \
    "      let next = sampleShadowCascade(world_pos, normal, cascade + 1);\n" \
    "      if (next >= 0.0) {\n" \
    "        shadow = mix(shadow, next, t);\n" \
    "      }\n" \
    "    } else {\n" \
    "      shadow = mix(shadow, 1.0, t);\n" \
    "    }\n" \
    "  }\n" \
    "  if (shadow < 0.0 && cascade < 3) {\n" \
    "    shadow = sampleShadowCascade(world_pos, normal, cascade + 1);\n" \
    "  }\n" \
    "  if (shadow < 0.0 && cascade > 0) {\n" \
    "    shadow = sampleShadowCascade(world_pos, normal, cascade - 1);\n" \
    "  }\n" \
    "  if (shadow < 0.0) { shadow = 1.0; }\n" \
    "  return shadow;\n" \
    "}\n"

#endif
