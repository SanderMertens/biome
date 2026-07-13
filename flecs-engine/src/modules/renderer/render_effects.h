#ifndef FLECS_ENGINE_RENDER_EFFECTS_INTERNAL_H
#define FLECS_ENGINE_RENDER_EFFECTS_INTERNAL_H

struct FlecsRenderEffectKind;

typedef bool (*flecs_render_effect_setup_callback)(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    ecs_entity_t effect_entity,
    const struct FlecsRenderEffectKind *kind,
    FlecsRenderEffectImpl *effect_impl,
    WGPUBindGroupLayoutEntry *layout_entries,
    uint32_t *entry_count);

typedef bool (*flecs_render_effect_bind_callback)(
    const ecs_world_t *world,
    const FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    ecs_entity_t effect_entity,
    const struct FlecsRenderEffectKind *kind,
    const FlecsRenderEffectImpl *effect_impl,
    WGPUBindGroupEntry *entries,
    uint32_t *entry_count);

typedef bool (*flecs_render_effect_render_callback)(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    WGPUCommandEncoder encoder,
    ecs_entity_t effect_entity,
    const struct FlecsRenderEffectKind *kind,
    FlecsRenderEffectImpl *effect_impl,
    WGPUTextureView input_view,
    WGPUTextureFormat input_format,
    WGPUTextureView output_view,
    WGPUTextureFormat output_format,
    WGPULoadOp output_load_op);

/* Per-effect-kind dispatch: shader + callbacks. Set by an observer on the
 * effect's settings (or tag) component, not by the user. */
typedef struct FlecsRenderEffectKind {
    ecs_entity_t shader;
    flecs_render_effect_setup_callback setup_callback;
    flecs_render_effect_bind_callback bind_callback;
    flecs_render_effect_render_callback render_callback;
    void *ctx;
    void (*free_ctx)(void *ctx);
} FlecsRenderEffectKind;

extern ECS_COMPONENT_DECLARE(FlecsRenderEffectKind);

int flecsEngine_initPassthrough(
    ecs_world_t *world,
    FlecsEngineImpl *impl);

int flecsEngine_initDepthResolve(
    ecs_world_t *world,
    FlecsEngineImpl *impl);

void flecsEngine_depthResolve(
    const FlecsEngineImpl *impl,
    FlecsRenderViewImpl *view_impl,
    WGPUCommandEncoder encoder);

void flecsEngine_renderEffect_register(
    ecs_world_t *world);

void flecsEngine_tonyMcMapFace_register(
    ecs_world_t *world);

void flecsEngine_bloom_register(
    ecs_world_t *world);

void flecsEngine_heightFog_register(
    ecs_world_t *world);

void flecsEngine_ssao_register(
    ecs_world_t *world);

void flecsEngine_sunShafts_register(
    ecs_world_t *world);

void flecsEngine_autoExposure_register(
    ecs_world_t *world);

void flecsEngine_invert_register(
    ecs_world_t *world);

void flecsEngine_gammaCorrect_register(
    ecs_world_t *world);

bool flecsEngine_renderEffect_render(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    WGPUCommandEncoder encoder,
    WGPUTextureView output_view,
    WGPULoadOp load_op,
    WGPUColor clear_value,
    ecs_entity_t effect_entity,
    const FlecsRenderEffectKind *kind,
    FlecsRenderEffectImpl *effect_impl,
    WGPUTextureView input_view,
    WGPUTextureFormat output_format,
    const char *ts_name,
    const WGPURenderPassTimestampWrites *ts_writes);

void flecsEngine_renderView_renderEffects(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view,
    FlecsRenderViewImpl *viewImpl,
    WGPUTextureView view_texture,
    WGPUCommandEncoder encoder);

/* Shared fullscreen-triangle vertex shader used by all post-process effects.
 * Produces a single triangle covering clip space with correct UVs. */
#define FLECS_ENGINE_FULLSCREEN_VS_WGSL \
    "struct VertexOutput {\n" \
    "  @builtin(position) pos : vec4<f32>,\n" \
    "  @location(0) uv : vec2<f32>\n" \
    "};\n" \
    "@vertex fn vs_main(@builtin(vertex_index) vid : u32) -> VertexOutput {\n" \
    "  var out : VertexOutput;\n" \
    "  var pos = array<vec2<f32>, 3>(\n" \
    "      vec2<f32>(-1.0, -1.0),\n" \
    "      vec2<f32>(3.0, -1.0),\n" \
    "      vec2<f32>(-1.0, 3.0));\n" \
    "  let p = pos[vid];\n" \
    "  out.pos = vec4<f32>(p, 0.0, 1.0);\n" \
    "  out.uv = vec2<f32>((p.x + 1.0) * 0.5, (1.0 - p.y) * 0.5);\n" \
    "  return out;\n" \
    "}\n"

#endif
