#include "../../renderer.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsGammaCorrect);

static const char *kShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "@group(0) @binding(0) var input_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var input_sampler : sampler;\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let c = textureSample(input_texture, input_sampler, in.uv);\n"
    "  let gamma = vec3<f32>(1.0 / 2.2);\n"
    "  return vec4<f32>(pow(c.rgb, gamma), c.a);\n"
    "}\n";

static ecs_entity_t flecsEngine_gammaCorrect_shader(
    ecs_world_t *world)
{
    return flecsEngine_shader_ensure(world, "GammaCorrectPostShader",
        &(FlecsShader){
            .source = kShaderSource,
            .vertex_entry = "vs_main",
            .fragment_entry = "fs_main"
        });
}

static void FlecsGammaCorrect_on_add(
    ecs_iter_t *it)
{
    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        ecs_set(it->world, e, FlecsRenderEffectKind, {
            .shader = flecsEngine_gammaCorrect_shader(it->world)
        });
    }
}

void flecsEngine_gammaCorrect_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsGammaCorrect);

    ecs_observer(world, {
        .query.terms = {{ .id = ecs_id(FlecsGammaCorrect) }},
        .events = { EcsOnAdd },
        .callback = FlecsGammaCorrect_on_add
    });
}
