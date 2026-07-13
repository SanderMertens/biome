#include "../../renderer.h"
#include "../../gpu_timing.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsSSAO);
ECS_COMPONENT_DECLARE(FlecsSSAOImpl);

typedef struct FlecsSSAOUniform {
    mat4 proj;
    mat4 inv_proj;
    float params[4];    /* radius, bias, intensity, unused */
    float viewport[4];  /* width, height, 1/width, 1/height */
} FlecsSSAOUniform;

static const char *kShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "struct SsaoUniforms {\n"
    "  proj : mat4x4<f32>,\n"
    "  inv_proj : mat4x4<f32>,\n"
    "  params : vec4<f32>,\n"
    "  viewport : vec4<f32>,\n"
    "};\n"
    "@group(0) @binding(0) var input_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var input_sampler : sampler;\n"
    "@group(0) @binding(2) var depth_texture : texture_depth_2d;\n"
    "@group(0) @binding(3) var<uniform> uniforms : SsaoUniforms;\n"

    "fn reconstruct_view_pos(uv : vec2<f32>, depth : f32) -> vec3<f32> {\n"
    "  let ndc = vec4<f32>(\n"
    "    uv.x * 2.0 - 1.0,\n"
    "    (1.0 - uv.y) * 2.0 - 1.0,\n"
    "    depth,\n"
    "    1.0);\n"
    "  let view_h = uniforms.inv_proj * ndc;\n"
    "  return view_h.xyz / view_h.w;\n"
    "}\n"

    /* Fast path: recover only view-space Z from depth.
     * Equivalent to (inv_proj * vec4(0,0,depth,1)).z / .w, but precomputes
     * the constant matrix entries so the per-sample cost is 4 muls +
     * 2 adds + 1 div. p_z = (inv_proj[2].z, inv_proj[3].z),
     * p_w = (inv_proj[2].w, inv_proj[3].w). */
    "fn fast_view_z(depth : f32, p_z : vec2<f32>, p_w : vec2<f32>) -> f32 {\n"
    "  let z = p_z.x * depth + p_z.y;\n"
    "  let w = p_w.x * depth + p_w.y;\n"
    "  return z / w;\n"
    "}\n"

    "fn hash22(p : vec2<f32>) -> vec2<f32> {\n"
    "  var p3 = fract(vec3<f32>(p.xyx) * vec3<f32>(0.1031, 0.1030, 0.0973));\n"
    "  p3 += dot(p3, p3.yzx + 33.33);\n"
    "  return fract((p3.xx + p3.yz) * p3.zy);\n"
    "}\n"

    "const SAMPLE_COUNT = 16u;\n"
    "const kernel = array<vec3<f32>, 16>(\n"
    "  vec3<f32>( 0.0380,  0.0510,  0.0146),\n"
    "  vec3<f32>(-0.0447,  0.0118,  0.0327),\n"
    "  vec3<f32>( 0.0264, -0.0423,  0.0581),\n"
    "  vec3<f32>(-0.0307,  0.0576,  0.0421),\n"
    "  vec3<f32>( 0.0765,  0.0167,  0.0604),\n"
    "  vec3<f32>(-0.0579,  0.0611,  0.0527),\n"
    "  vec3<f32>( 0.0948, -0.0388,  0.0688),\n"
    "  vec3<f32>(-0.0635,  0.0906,  0.0784),\n"
    "  vec3<f32>( 0.1291, -0.0206,  0.1064),\n"
    "  vec3<f32>(-0.0598,  0.1399,  0.0862),\n"
    "  vec3<f32>( 0.1106,  0.1161,  0.1203),\n"
    "  vec3<f32>(-0.1588, -0.0539,  0.1040),\n"
    "  vec3<f32>( 0.1728,  0.1124,  0.1365),\n"
    "  vec3<f32>(-0.0756, -0.1867,  0.1226),\n"
    "  vec3<f32>( 0.2300, -0.0827,  0.1510),\n"
    "  vec3<f32>(-0.1842,  0.1689,  0.2032)\n"
    ");\n"

    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let src = textureSample(input_texture, input_sampler, in.uv);\n"
    "  let dims = textureDimensions(depth_texture);\n"
    "  let dims_f = vec2<f32>(f32(dims.x), f32(dims.y));\n"
    "  let clamped_uv = clamp(in.uv, vec2<f32>(0.0), vec2<f32>(0.999999));\n"
    "  let texel = vec2<i32>(clamped_uv * dims_f);\n"
    "  let depth = textureLoad(depth_texture, texel, 0);\n"
    "  if (depth >= 0.999999) {\n"
    "    return src;\n"
    "  }\n"

    "  let view_pos = reconstruct_view_pos(in.uv, depth);\n"

    /* Reconstruct neighbour view positions on the depth-texel grid so the
     * finite-difference stride matches the depth lookup, even when SSAO
     * runs at a lower resolution than the depth buffer. */
    "  let inv_dims = vec2<f32>(1.0) / dims_f;\n"
    "  let uv_r = (vec2<f32>(texel + vec2<i32>(1, 0)) + vec2<f32>(0.5)) * inv_dims;\n"
    "  let uv_u = (vec2<f32>(texel + vec2<i32>(0, -1)) + vec2<f32>(0.5)) * inv_dims;\n"
    "  let vp_r = reconstruct_view_pos(\n"
    "    uv_r,\n"
    "    textureLoad(depth_texture, texel + vec2<i32>(1, 0), 0));\n"
    "  let vp_u = reconstruct_view_pos(\n"
    "    uv_u,\n"
    "    textureLoad(depth_texture, texel + vec2<i32>(0, -1), 0));\n"
    "  let normal = normalize(cross(vp_r - view_pos, vp_u - view_pos));\n"
    "  let noise = hash22(vec2<f32>(f32(texel.x), f32(texel.y)));\n"
    "  var random_vec = vec3<f32>(noise.x * 2.0 - 1.0, noise.y * 2.0 - 1.0, 0.0);\n"
    "  random_vec = normalize(random_vec - normal * dot(random_vec, normal));\n"
    "  let tangent = random_vec;\n"
    "  let bitangent = cross(normal, tangent);\n"

    "  let radius = uniforms.params.x;\n"
    "  let bias = uniforms.params.y;\n"
    "  let intensity = uniforms.params.z;\n"

    /* Hoist the constant inv_proj entries used by fast_view_z out of the
     * inner loop. inv_proj is column-major; column [2] is NDC z's column
     * and column [3] is the constant (w=1) column. We need their row 2
     * (view z) and row 3 (view w) entries. */
    "  let inv_p_z_zw = vec2<f32>(uniforms.inv_proj[2].z, uniforms.inv_proj[3].z);\n"
    "  let inv_p_w_zw = vec2<f32>(uniforms.inv_proj[2].w, uniforms.inv_proj[3].w);\n"

    "  var occlusion = 0.0;\n"
    "  for (var i = 0u; i < SAMPLE_COUNT; i++) {\n"
    "    let k = kernel[i];\n"
    "    let sample_dir = tangent * k.x + bitangent * k.y + normal * k.z;\n"
    "    let sample_pos = view_pos + sample_dir * radius;\n"

    "    let sample_clip = uniforms.proj * vec4<f32>(sample_pos, 1.0);\n"
    "    var sample_uv = sample_clip.xy / sample_clip.w;\n"
    "    sample_uv = sample_uv * vec2<f32>(0.5, -0.5) + vec2<f32>(0.5, 0.5);\n"

    "    if (sample_uv.x < 0.0 || sample_uv.x > 1.0 ||\n"
    "        sample_uv.y < 0.0 || sample_uv.y > 1.0) {\n"
    "      continue;\n"
    "    }\n"

    "    let s_texel = vec2<i32>(sample_uv * dims_f);\n"
    "    let s_depth = textureLoad(depth_texture, s_texel, 0);\n"
    "    let sample_view_z = fast_view_z(s_depth, inv_p_z_zw, inv_p_w_zw);\n"

    "    let range_check = smoothstep(\n"
    "      0.0, 1.0, radius / max(abs(view_pos.z - sample_view_z), 1e-6));\n"
    "    occlusion += select(0.0, 1.0,\n"
    "      sample_view_z >= sample_pos.z + bias) * range_check;\n"
    "  }\n"

    "  let fade = 1.0 - smoothstep(radius * 40.0, radius * 100.0, -view_pos.z);\n"
    "  let ao = clamp(1.0 - (occlusion / f32(SAMPLE_COUNT)) * intensity * fade, 0.0, 1.0);\n"

    "  return vec4<f32>(src.rgb * ao, src.a);\n"
    "}\n";

static ecs_entity_t flecsEngine_ssao_shader(
    ecs_world_t *world)
{
    return flecsEngine_shader_ensure(world, "SSAOPostShader",
        &(FlecsShader){
            .source = kShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}

static void flecsEngine_ssao_releaseResources(
    FlecsSSAOImpl *impl)
{
    FLECS_WGPU_RELEASE(impl->uniform_buffer, wgpuBufferRelease);
}

ECS_DTOR(FlecsSSAOImpl, ptr, {
    flecsEngine_ssao_releaseResources(ptr);
})

ECS_MOVE(FlecsSSAOImpl, dst, src, {
    flecsEngine_ssao_releaseResources(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

static void flecsEngine_ssao_fillUniform(
    const FlecsRenderViewImpl *view_impl,
    const FlecsSSAO *ssao,
    FlecsSSAOUniform *uniform)
{
    glm_mat4_identity(uniform->proj);
    glm_mat4_identity(uniform->inv_proj);

    uniform->params[0] = ssao->radius;
    uniform->params[1] = ssao->bias;
    uniform->params[2] = ssao->intensity;
    uniform->params[3] = 0.0f;

    uniform->viewport[0] = 0.0f;
    uniform->viewport[1] = 0.0f;
    uniform->viewport[2] = 0.0f;
    uniform->viewport[3] = 0.0f;

    if (!view_impl || !view_impl->camera_view_proj_valid) {
        return;
    }

    glm_mat4_copy((vec4*)view_impl->camera_proj, uniform->proj);

    mat4 proj_copy;
    glm_mat4_copy((vec4*)view_impl->camera_proj, proj_copy);
    glm_mat4_inv(proj_copy, uniform->inv_proj);

    if (view_impl->effect_target_width > 0) {
        float w = (float)view_impl->effect_target_width;
        float h = (float)view_impl->effect_target_height;
        uniform->viewport[0] = w;
        uniform->viewport[1] = h;
        uniform->viewport[2] = 1.0f / w;
        uniform->viewport[3] = 1.0f / h;
    }
}

static bool flecsEngine_ssao_setup(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t effect_entity,
    const FlecsRenderEffectKind *kind,
    FlecsRenderEffectImpl *effect_impl,
    WGPUBindGroupLayoutEntry *layout_entries,
    uint32_t *entry_count)
{
    (void)kind;
    (void)effect_impl;

    ecs_assert(layout_entries != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(entry_count != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(*entry_count == 2, ECS_INVALID_PARAMETER, NULL);

    FlecsSSAOImpl ssao_impl = {0};

    ssao_impl.uniform_buffer = flecsEngine_createUniformBuffer(
        engine->device, sizeof(FlecsSSAOUniform));
    if (!ssao_impl.uniform_buffer) {
        return false;
    }

    /* SSAO pass layout entries */
    layout_entries[2] = (WGPUBindGroupLayoutEntry){
        .binding = 2,
        .visibility = WGPUShaderStage_Fragment,
        .texture = {
            .sampleType = WGPUTextureSampleType_Depth,
            .viewDimension = WGPUTextureViewDimension_2D,
            .multisampled = false
        }
    };

    layout_entries[3] = (WGPUBindGroupLayoutEntry){
        .binding = 3,
        .visibility = WGPUShaderStage_Fragment,
        .buffer = {
            .type = WGPUBufferBindingType_Uniform,
            .minBindingSize = sizeof(FlecsSSAOUniform)
        }
    };

    *entry_count = 4;

    ecs_set_ptr((ecs_world_t*)world, effect_entity, FlecsSSAOImpl, &ssao_impl);
    return true;
}

static bool flecsEngine_ssao_bind(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    ecs_entity_t effect_entity,
    const FlecsRenderEffectKind *kind,
    const FlecsRenderEffectImpl *impl,
    WGPUBindGroupEntry *entries,
    uint32_t *entry_count)
{
    (void)kind;
    (void)impl;

    ecs_assert(entries != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(entry_count != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(*entry_count == 2, ECS_INVALID_PARAMETER, NULL);

    if (!view_impl || !view_impl->depth_texture_view) {
        return false;
    }

    const FlecsSSAO *ssao = ecs_get(world, effect_entity, FlecsSSAO);
    const FlecsSSAOImpl *ssao_impl = ecs_get(
        world, effect_entity, FlecsSSAOImpl);
    if (!ssao || !ssao_impl || !ssao_impl->uniform_buffer) {
        return false;
    }

    FlecsSSAOUniform uniform = {0};
    flecsEngine_ssao_fillUniform(view_impl, ssao, &uniform);
    wgpuQueueWriteBuffer(
        engine->queue,
        ssao_impl->uniform_buffer,
        0,
        &uniform,
        sizeof(uniform));

    entries[2] = (WGPUBindGroupEntry){
        .binding = 2,
        .textureView = view_impl->depth_texture_view
    };

    entries[3] = (WGPUBindGroupEntry){
        .binding = 3,
        .buffer = ssao_impl->uniform_buffer,
        .offset = 0,
        .size = sizeof(FlecsSSAOUniform)
    };

    *entry_count = 4;
    return true;
}


ECS_CTOR(FlecsSSAO, ptr, {
    ptr->radius = 0.5f;
    ptr->bias = 0.025f;
    ptr->intensity = 1.0f;
})

static void FlecsSSAO_on_set(
    ecs_iter_t *it)
{
    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        ecs_set(it->world, e, FlecsRenderEffectKind, {
            .shader = flecsEngine_ssao_shader(it->world),
            .setup_callback = flecsEngine_ssao_setup,
            .bind_callback = flecsEngine_ssao_bind
        });
    }
}

void flecsEngine_ssao_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsSSAO);
    ECS_COMPONENT_DEFINE(world, FlecsSSAOImpl);

    ecs_set_hooks(world, FlecsSSAO, {
        .ctor = ecs_ctor(FlecsSSAO)
    });

    ecs_set_hooks(world, FlecsSSAOImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsSSAOImpl),
        .dtor = ecs_dtor(FlecsSSAOImpl)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsSSAO),
        .members = {
            { .name = "radius", .type = ecs_id(ecs_f32_t) },
            { .name = "bias", .type = ecs_id(ecs_f32_t) },
            { .name = "intensity", .type = ecs_id(ecs_f32_t) }
        }
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(FlecsSSAO) }},
        .events = { EcsOnSet },
        .callback = FlecsSSAO_on_set
    });
}
