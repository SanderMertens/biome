#include "../../renderer.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsSunShafts);
ECS_COMPONENT_DECLARE(FlecsSunShaftsImpl);

typedef struct FlecsSunShaftsUniform {
    float sun[4];         /* xy = sun uv, z = valid flag, w = intensity */
    float params[4];      /* density, weight, decay, exposure */
    float color[4];
} FlecsSunShaftsUniform;

static const char *kShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "struct SunShaftsUniforms {\n"
    "  sun : vec4<f32>,\n"
    "  params : vec4<f32>,\n"
    "  color : vec4<f32>,\n"
    "};\n"
    "@group(0) @binding(0) var input_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var input_sampler : sampler;\n"
    "@group(0) @binding(2) var depth_texture : texture_depth_2d;\n"
    "@group(0) @binding(3) var<uniform> uniforms : SunShaftsUniforms;\n"
    "const NUM_SAMPLES : u32 = 64u;\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let src = textureSample(input_texture, input_sampler, in.uv);\n"
    "  let sun_uv = uniforms.sun.xy;\n"
    "  let valid = uniforms.sun.z;\n"
    "  let intensity = uniforms.sun.w;\n"
    "  if (valid < 0.5 || intensity <= 0.0) { return src; }\n"
    "  let density = uniforms.params.x;\n"
    "  let weight = uniforms.params.y;\n"
    "  let decay = uniforms.params.z;\n"
    "  let exposure = uniforms.params.w;\n"
    "  let dims = textureDimensions(depth_texture);\n"
    "  let dims_f = vec2<f32>(f32(dims.x), f32(dims.y));\n"
    "  let delta = (in.uv - sun_uv) * (density / f32(NUM_SAMPLES));\n"
    "  var tex_coord = in.uv;\n"
    "  var illum = 0.0;\n"
    "  var illum_decay = 1.0;\n"
    "  for (var i : u32 = 0u; i < NUM_SAMPLES; i = i + 1u) {\n"
    "    tex_coord = tex_coord - delta;\n"
    "    let uv_clamped = clamp(tex_coord, vec2<f32>(0.0), vec2<f32>(0.9999));\n"
    "    let texel = vec2<i32>(uv_clamped * dims_f);\n"
    "    let depth = textureLoad(depth_texture, texel, 0);\n"
    "    let sky = select(0.0, illum_decay * weight, depth >= 0.9999);\n"
    "    illum = illum + sky;\n"
    "    illum_decay = illum_decay * decay;\n"
    "  }\n"
    /* Radial fade so shafts vanish as the sun leaves the screen (sun_uv
     * outside [0,1] still produces a march direction, but the effect should
     * soften with angular distance). */
    "  let d = distance(in.uv, sun_uv);\n"
    "  let radial = clamp(1.0 - d, 0.0, 1.0);\n"
    "  let shafts = uniforms.color.rgb * illum * exposure * intensity * radial;\n"
    "  return vec4<f32>(src.rgb + max(shafts, vec3<f32>(0.0)), src.a);\n"
    "}\n";

static ecs_entity_t flecsEngine_sunShafts_shader(
    ecs_world_t *world)
{
    return flecsEngine_shader_ensure(world, "SunShaftsShader",
        &(FlecsShader){
            .source = kShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}

static void flecsEngine_sunShafts_releaseResources(
    FlecsSunShaftsImpl *impl)
{
    FLECS_WGPU_RELEASE(impl->uniform_buffer, wgpuBufferRelease);
}

ECS_DTOR(FlecsSunShaftsImpl, ptr, {
    flecsEngine_sunShafts_releaseResources(ptr);
})

ECS_MOVE(FlecsSunShaftsImpl, dst, src, {
    flecsEngine_sunShafts_releaseResources(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

static void flecsEngine_sunShafts_fillUniform(
    const ecs_world_t *world,
    const FlecsRenderViewImpl *view_impl,
    const FlecsSunShafts *shafts,
    FlecsSunShaftsUniform *uniform)
{
    uniform->sun[0] = 0.5f;
    uniform->sun[1] = 0.5f;
    uniform->sun[2] = 0.0f;
    uniform->sun[3] = shafts->intensity;

    uniform->params[0] = shafts->density;
    uniform->params[1] = shafts->weight;
    uniform->params[2] = shafts->decay;
    uniform->params[3] = shafts->exposure;

    const FlecsRgba *shaft_rgb = &shafts->color;
    FlecsRgba light_rgb;
    if (shafts->light) {
        const FlecsRgba *light_color = ecs_get(world, shafts->light, FlecsRgba);
        if (light_color) {
            light_rgb = *light_color;
            shaft_rgb = &light_rgb;
        }
    }

    uniform->color[0] = flecsEngine_colorChannelToFloat(shaft_rgb->r);
    uniform->color[1] = flecsEngine_colorChannelToFloat(shaft_rgb->g);
    uniform->color[2] = flecsEngine_colorChannelToFloat(shaft_rgb->b);
    uniform->color[3] = flecsEngine_colorChannelToFloat(shaft_rgb->a);

    if (!view_impl || !view_impl->main_light ||
        !view_impl->camera_view_proj_valid)
    {
        return;
    }

    const FlecsRotation3 *rot = ecs_get(
        world, view_impl->main_light, FlecsRotation3);
    if (!rot) return;

    vec3 ray_dir;
    if (!flecsEngine_lightDirFromRotation(rot, ray_dir)) return;

    /* The sun is a directional light infinitely far in the -ray_dir
     * direction. Project a distant point so the clip-space w is positive
     * and stable. */
    const float kSunDistance = 1.0e4f;
    vec4 sun_world = {
        view_impl->camera_pos[0] - ray_dir[0] * kSunDistance,
        view_impl->camera_pos[1] - ray_dir[1] * kSunDistance,
        view_impl->camera_pos[2] - ray_dir[2] * kSunDistance,
        1.0f
    };

    mat4 mvp;
    glm_mat4_copy((vec4*)view_impl->camera_view_proj, mvp);

    vec4 clip;
    glm_mat4_mulv(mvp, sun_world, clip);

    if (clip[3] <= 1e-4f) {
        /* Sun behind camera */
        return;
    }

    float ndc_x = clip[0] / clip[3];
    float ndc_y = clip[1] / clip[3];

    uniform->sun[0] = ndc_x * 0.5f + 0.5f;
    uniform->sun[1] = (1.0f - ndc_y) * 0.5f;
    uniform->sun[2] = 1.0f;
}

static bool flecsEngine_sunShafts_setup(
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

    FlecsSunShaftsImpl shafts_impl = {0};

    shafts_impl.uniform_buffer = flecsEngine_createUniformBuffer(
        engine->device, sizeof(FlecsSunShaftsUniform));
    if (!shafts_impl.uniform_buffer) {
        return false;
    }

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
            .minBindingSize = sizeof(FlecsSunShaftsUniform)
        }
    };

    ecs_set_ptr((ecs_world_t*)world, effect_entity, FlecsSunShaftsImpl, &shafts_impl);

    *entry_count = 4;
    return true;
}

static bool flecsEngine_sunShafts_bind(
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

    const FlecsSunShaftsImpl *shafts_impl = ecs_get(
        world, effect_entity, FlecsSunShaftsImpl);
    if (!shafts_impl || !shafts_impl->uniform_buffer) {
        return false;
    }

    const FlecsSunShafts *shafts = ecs_get(world, effect_entity, FlecsSunShafts);
    if (!shafts) {
        return false;
    }

    FlecsSunShaftsUniform uniform = {0};
    flecsEngine_sunShafts_fillUniform(world, view_impl, shafts, &uniform);
    wgpuQueueWriteBuffer(
        engine->queue,
        shafts_impl->uniform_buffer,
        0,
        &uniform,
        sizeof(uniform));

    entries[2] = (WGPUBindGroupEntry){
        .binding = 2,
        .textureView = view_impl->depth_texture_view
    };

    entries[3] = (WGPUBindGroupEntry){
        .binding = 3,
        .buffer = shafts_impl->uniform_buffer,
        .offset = 0,
        .size = sizeof(FlecsSunShaftsUniform)
    };

    *entry_count = 4;
    return true;
}

ECS_CTOR(FlecsSunShafts, ptr, {
    ptr->intensity = 1.0f;
    ptr->density = 0.9f;
    ptr->weight = 0.04f;
    ptr->decay = 0.97f;
    ptr->exposure = 0.25f;
    ptr->color = (flecs_rgba_t){255, 240, 210, 255};
    ptr->light = 0;
})

static void FlecsSunShafts_on_set(
    ecs_iter_t *it)
{
    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        ecs_set(it->world, e, FlecsRenderEffectKind, {
            .shader = flecsEngine_sunShafts_shader(it->world),
            .setup_callback = flecsEngine_sunShafts_setup,
            .bind_callback = flecsEngine_sunShafts_bind
        });
    }
}

void flecsEngine_sunShafts_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsSunShafts);
    ECS_COMPONENT_DEFINE(world, FlecsSunShaftsImpl);

    ecs_set_hooks(world, FlecsSunShafts, {
        .ctor = ecs_ctor(FlecsSunShafts)
    });

    ecs_set_hooks(world, FlecsSunShaftsImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsSunShaftsImpl),
        .dtor = ecs_dtor(FlecsSunShaftsImpl)
    });

    ecs_struct(world, {
        .entity = ecs_id(FlecsSunShafts),
        .members = {
            { .name = "intensity", .type = ecs_id(ecs_f32_t) },
            { .name = "density", .type = ecs_id(ecs_f32_t) },
            { .name = "weight", .type = ecs_id(ecs_f32_t) },
            { .name = "decay", .type = ecs_id(ecs_f32_t) },
            { .name = "exposure", .type = ecs_id(ecs_f32_t) },
            { .name = "color", .type = ecs_id(flecs_rgba_t) },
            { .name = "light", .type = ecs_id(ecs_entity_t) }
        }
    });

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(FlecsSunShafts) }},
        .events = { EcsOnSet },
        .callback = FlecsSunShafts_on_set
    });
}
