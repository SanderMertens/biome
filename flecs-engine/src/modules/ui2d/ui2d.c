#include "ui2d.h"
#include "font_default.h"

#include <stb_truetype.h>

#include <math.h>
#include <string.h>

#define FLECS_UI2D_ATLAS_SIZE 1024
#define FLECS_UI2D_BAKE_PX 64.0f
#define FLECS_UI2D_FIRST_CHAR 32
#define FLECS_UI2D_CHAR_COUNT 95

typedef struct {
    float x, y;      /* NDC */
    float u, v;
    uint32_t color;  /* RGBA8, possibly linearized for sRGB targets */
    float shape_x, shape_y;
    float shape_w, shape_h;
    float radius_top_left, radius_top_right;
    float radius_bottom_right, radius_bottom_left;
    float top_width, right_width, bottom_width, left_width;
    float shape;
} flecs_ui2d_vertex_t;

typedef struct FlecsUi2dImpl {
    /* CPU submission queue, reset every frame */
    flecs_ui2d_vertex_t *verts;
    int32_t vert_count;
    int32_t vert_cap;
    int64_t frame;

    /* Font */
    unsigned char *ttf;             /* NULL: use embedded default */
    int32_t ttf_len;
    stbtt_bakedchar baked[FLECS_UI2D_CHAR_COUNT];
    unsigned char *atlas_bitmap;    /* kept until uploaded */
    float ascent_px;
    bool font_baked;

    /* GPU */
    WGPURenderPipeline pipeline;
    WGPUBindGroupLayout bind_layout;
    WGPUBindGroup bind_group;
    WGPUSampler sampler;
    WGPUTexture atlas_tex;
    WGPUTextureView atlas_view;
    WGPUBuffer vbuf;
    uint64_t vbuf_size;
    WGPUTextureFormat pipeline_format;
} FlecsUi2dImpl;

ECS_COMPONENT_DECLARE(FlecsUi2dImpl);

static const char *kUi2dShaderSource =
    "struct VertexIn {\n"
    "  @location(0) pos : vec2<f32>,\n"
    "  @location(1) uv : vec2<f32>,\n"
    "  @location(2) color : vec4<f32>,\n"
    "  @location(3) shape_pos : vec2<f32>,\n"
    "  @location(4) shape_size : vec2<f32>,\n"
    "  @location(5) radii : vec4<f32>,\n"
    "  @location(6) widths : vec4<f32>,\n"
    "  @location(7) shape : f32,\n"
    "};\n"
    "struct VertexOut {\n"
    "  @builtin(position) pos : vec4<f32>,\n"
    "  @location(0) uv : vec2<f32>,\n"
    "  @location(1) color : vec4<f32>,\n"
    "  @location(2) shape_pos : vec2<f32>,\n"
    "  @location(3) @interpolate(flat) shape_size : vec2<f32>,\n"
    "  @location(4) @interpolate(flat) radii : vec4<f32>,\n"
    "  @location(5) @interpolate(flat) widths : vec4<f32>,\n"
    "  @location(6) @interpolate(flat) shape : f32,\n"
    "};\n"
    "@vertex fn vs_main(in : VertexIn) -> VertexOut {\n"
    "  var out : VertexOut;\n"
    "  out.pos = vec4<f32>(in.pos, 0.0, 1.0);\n"
    "  out.uv = in.uv;\n"
    "  out.color = in.color;\n"
    "  out.shape_pos = in.shape_pos;\n"
    "  out.shape_size = in.shape_size;\n"
    "  out.radii = in.radii;\n"
    "  out.widths = in.widths;\n"
    "  out.shape = in.shape;\n"
    "  return out;\n"
    "}\n"
    "fn rounded_box_sdf(p : vec2<f32>, size : vec2<f32>, "
        "radii : vec4<f32>) -> f32 {\n"
    "  let centered = p - size * 0.5;\n"
    "  let bottom = centered.y > 0.0;\n"
    "  let left_r = select(radii.x, radii.w, bottom);\n"
    "  let right_r = select(radii.y, radii.z, bottom);\n"
    "  let radius = select(left_r, right_r, centered.x > 0.0);\n"
    "  let q = abs(centered) - (size * 0.5 - vec2<f32>(radius));\n"
    "  return min(max(q.x, q.y), 0.0) + "
        "length(max(q, vec2<f32>(0.0))) - radius;\n"
    "}\n"
    "@group(0) @binding(0) var atlas : texture_2d<f32>;\n"
    "@group(0) @binding(1) var samp : sampler;\n"
    "@fragment fn fs_main(in : VertexOut) -> @location(0) vec4<f32> {\n"
    "  if (in.shape > 0.5) {\n"
    "    let outer_d = rounded_box_sdf("
        "in.shape_pos, in.shape_size, in.radii);\n"
    "    let aa = max(fwidth(outer_d), 0.75);\n"
    "    let outer = 1.0 - smoothstep(-aa, aa, outer_d);\n"
    "    var coverage = outer;\n"
    "    if (in.shape > 1.5) {\n"
    "      let inner_origin = vec2<f32>(in.widths.w, in.widths.x);\n"
    "      let inner_size = max(in.shape_size - vec2<f32>("
        "in.widths.w + in.widths.y, in.widths.x + in.widths.z), "
        "vec2<f32>(0.0));\n"
    "      let inner_radii = max(in.radii - vec4<f32>("
        "max(in.widths.w, in.widths.x), "
        "max(in.widths.y, in.widths.x), "
        "max(in.widths.y, in.widths.z), "
        "max(in.widths.w, in.widths.z)), vec4<f32>(0.0));\n"
    "      let inner_d = rounded_box_sdf("
        "in.shape_pos - inner_origin, inner_size, inner_radii);\n"
    "      let inner_aa = max(fwidth(inner_d), 0.75);\n"
    "      let inner = 1.0 - smoothstep(-inner_aa, inner_aa, inner_d);\n"
    "      coverage = outer * (1.0 - inner);\n"
    "    }\n"
    "    return vec4<f32>(in.color.rgb, in.color.a * coverage);\n"
    "  }\n"
    "  let a = textureSample(atlas, samp, in.uv).r;\n"
    "  return vec4<f32>(in.color.rgb, in.color.a * a);\n"
    "}\n";

static void flecsEngine_ui2d_releaseGpu(
    FlecsUi2dImpl *impl)
{
    FLECS_WGPU_RELEASE(impl->pipeline, wgpuRenderPipelineRelease);
    FLECS_WGPU_RELEASE(impl->bind_layout, wgpuBindGroupLayoutRelease);
    FLECS_WGPU_RELEASE(impl->bind_group, wgpuBindGroupRelease);
    FLECS_WGPU_RELEASE(impl->sampler, wgpuSamplerRelease);
    FLECS_WGPU_RELEASE(impl->atlas_view, wgpuTextureViewRelease);
    FLECS_WGPU_RELEASE(impl->atlas_tex, wgpuTextureRelease);
    FLECS_WGPU_RELEASE(impl->vbuf, wgpuBufferRelease);
    impl->vbuf_size = 0;
}

static void flecsEngine_ui2d_releaseImpl(
    FlecsUi2dImpl *impl)
{
    flecsEngine_ui2d_releaseGpu(impl);
    ecs_os_free(impl->verts);
    ecs_os_free(impl->ttf);
    ecs_os_free(impl->atlas_bitmap);
    impl->verts = NULL;
    impl->ttf = NULL;
    impl->atlas_bitmap = NULL;
}

ECS_DTOR(FlecsUi2dImpl, ptr, {
    flecsEngine_ui2d_releaseImpl(ptr);
})

ECS_MOVE(FlecsUi2dImpl, dst, src, {
    flecsEngine_ui2d_releaseImpl(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

static bool flecsEngine_ui2d_ensureFont(
    FlecsUi2dImpl *impl)
{
    if (impl->font_baked) {
        return true;
    }

    const unsigned char *ttf = impl->ttf
        ? impl->ttf : flecsEngine_ui2d_font_default;

    if (!impl->atlas_bitmap) {
        impl->atlas_bitmap = ecs_os_malloc(
            FLECS_UI2D_ATLAS_SIZE * FLECS_UI2D_ATLAS_SIZE);
    }

    int rows = stbtt_BakeFontBitmap(ttf, 0, FLECS_UI2D_BAKE_PX,
        impl->atlas_bitmap, FLECS_UI2D_ATLAS_SIZE, FLECS_UI2D_ATLAS_SIZE,
        FLECS_UI2D_FIRST_CHAR, FLECS_UI2D_CHAR_COUNT, impl->baked);
    if (rows <= 0) {
        ecs_err("ui2d: failed to bake font atlas");
        return false;
    }

    /* Solid white block in the bottom-right corner for untextured quads */
    for (int y = FLECS_UI2D_ATLAS_SIZE - 8; y < FLECS_UI2D_ATLAS_SIZE; y ++) {
        memset(&impl->atlas_bitmap[y * FLECS_UI2D_ATLAS_SIZE +
            FLECS_UI2D_ATLAS_SIZE - 8], 255, 8);
    }

    stbtt_fontinfo info;
    int ascent = 0, descent = 0, line_gap = 0;
    if (stbtt_InitFont(&info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
        stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);
        float scale = stbtt_ScaleForPixelHeight(&info, FLECS_UI2D_BAKE_PX);
        impl->ascent_px = (float)ascent * scale;
    } else {
        impl->ascent_px = FLECS_UI2D_BAKE_PX * 0.8f;
    }

    impl->font_baked = true;
    return true;
}

static FlecsUi2dImpl* flecsEngine_ui2d_get(
    ecs_world_t *world)
{
    if (!ecs_id(FlecsUi2dImpl)) {
        return NULL;
    }
    return ecs_singleton_ensure(world, FlecsUi2dImpl);
}

static bool flecsEngine_ui2d_surfaceSize(
    ecs_world_t *world,
    float *w,
    float *h)
{
    const FlecsEngineImpl *engine = ecs_singleton_get(world, FlecsEngineImpl);
    if (!engine || !engine->surface) {
        return false;
    }
    const FlecsSurface *surface = ecs_get(
        world, engine->surface, FlecsSurface);
    if (!surface || surface->width <= 0 || surface->height <= 0) {
        return false;
    }
    if (surface->point_width > 0 && surface->point_height > 0) {
        *w = (float)surface->point_width;
        *h = (float)surface->point_height;
    } else {
        *w = (float)surface->width;
        *h = (float)surface->height;
    }
    return true;
}

bool flecsEngine_ui2dScreenSize(
    ecs_world_t *world,
    float *w,
    float *h)
{
    return flecsEngine_ui2d_surfaceSize(world, w, h);
}

bool flecsEngine_ui2dHovered(
    ecs_world_t *world,
    float x,
    float y,
    float w,
    float h)
{
    const FlecsInput *input = ecs_singleton_get(world, FlecsInput);
    if (!input) {
        return false;
    }
    float mx = input->mouse.wnd.x;
    float my = input->mouse.wnd.y;
    return mx >= x && mx <= x + w && my >= y && my <= y + h;
}

static uint32_t flecsEngine_ui2d_packColor(
    ecs_world_t *world,
    FlecsRgba color)
{
    const FlecsEngineImpl *engine = ecs_singleton_get(world, FlecsEngineImpl);
    uint8_t r = color.r, g = color.g, b = color.b;
    if (engine) {
        switch (engine->target_format) {
        case WGPUTextureFormat_BGRA8UnormSrgb:
        case WGPUTextureFormat_RGBA8UnormSrgb: {
            /* Linearize so sRGB encoding on write restores the color */
            float fr = powf((float)r / 255.0f, 2.2f);
            float fg = powf((float)g / 255.0f, 2.2f);
            float fb = powf((float)b / 255.0f, 2.2f);
            r = (uint8_t)(fr * 255.0f + 0.5f);
            g = (uint8_t)(fg * 255.0f + 0.5f);
            b = (uint8_t)(fb * 255.0f + 0.5f);
            break;
        }
        default:
            break;
        }
    }
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) |
        ((uint32_t)color.a << 24);
}

static flecs_ui2d_vertex_t* flecsEngine_ui2d_alloc(
    ecs_world_t *world,
    FlecsUi2dImpl *impl,
    int32_t count)
{
    const ecs_world_info_t *info = ecs_get_world_info(world);
    if (impl->frame != info->frame_count_total) {
        impl->frame = info->frame_count_total;
        impl->vert_count = 0;
    }

    if (impl->vert_count + count > impl->vert_cap) {
        int32_t cap = impl->vert_cap ? impl->vert_cap * 2 : 1024;
        while (cap < impl->vert_count + count) cap *= 2;
        impl->verts = ecs_os_realloc(
            impl->verts, cap * (int32_t)sizeof(flecs_ui2d_vertex_t));
        impl->vert_cap = cap;
    }

    flecs_ui2d_vertex_t *result = &impl->verts[impl->vert_count];
    impl->vert_count += count;
    return result;
}

static void flecsEngine_ui2d_emitQuad(
    flecs_ui2d_vertex_t *v,
    float x0, float y0, float x1, float y1,
    float u0, float v0, float u1, float v1,
    float sw, float sh,
    uint32_t color)
{
    float nx0 = x0 / sw * 2.0f - 1.0f;
    float nx1 = x1 / sw * 2.0f - 1.0f;
    float ny0 = 1.0f - y0 / sh * 2.0f;
    float ny1 = 1.0f - y1 / sh * 2.0f;

    v[0] = (flecs_ui2d_vertex_t){ nx0, ny0, u0, v0, color };
    v[1] = (flecs_ui2d_vertex_t){ nx1, ny0, u1, v0, color };
    v[2] = (flecs_ui2d_vertex_t){ nx1, ny1, u1, v1, color };
    v[3] = (flecs_ui2d_vertex_t){ nx0, ny0, u0, v0, color };
    v[4] = (flecs_ui2d_vertex_t){ nx1, ny1, u1, v1, color };
    v[5] = (flecs_ui2d_vertex_t){ nx0, ny1, u0, v1, color };
}

#define FLECS_UI2D_WHITE_UV \
    ((FLECS_UI2D_ATLAS_SIZE - 4.0f) / FLECS_UI2D_ATLAS_SIZE)

void flecsEngine_ui2dRect(
    ecs_world_t *world,
    float x,
    float y,
    float w,
    float h,
    FlecsRgba color)
{
    FlecsUi2dImpl *impl = flecsEngine_ui2d_get(world);
    float sw, sh;
    if (!impl || !flecsEngine_ui2d_surfaceSize(world, &sw, &sh)) {
        return;
    }

    uint32_t c = flecsEngine_ui2d_packColor(world, color);
    flecs_ui2d_vertex_t *v = flecsEngine_ui2d_alloc(world, impl, 6);
    float white = FLECS_UI2D_WHITE_UV;
    flecsEngine_ui2d_emitQuad(v, x, y, x + w, y + h,
        white, white, white, white, sw, sh, c);
}

static float flecsEngine_ui2d_clampRadius(
    float radius,
    float w,
    float h)
{
    float max_radius = fminf(w, h) * 0.5f;
    return fminf(fmaxf(radius, 0.0f), max_radius);
}

static void flecsEngine_ui2d_shape(
    ecs_world_t *world,
    float x,
    float y,
    float w,
    float h,
    float radius_top_left,
    float radius_top_right,
    float radius_bottom_left,
    float radius_bottom_right,
    float top_width,
    float right_width,
    float bottom_width,
    float left_width,
    float shape,
    FlecsRgba color)
{
    FlecsUi2dImpl *impl = flecsEngine_ui2d_get(world);
    float sw, sh;
    if (!impl || w <= 0.0f || h <= 0.0f ||
        !flecsEngine_ui2d_surfaceSize(world, &sw, &sh))
    {
        return;
    }

    uint32_t c = flecsEngine_ui2d_packColor(world, color);
    flecs_ui2d_vertex_t *v = flecsEngine_ui2d_alloc(world, impl, 6);
    float white = FLECS_UI2D_WHITE_UV;
    flecsEngine_ui2d_emitQuad(v, x, y, x + w, y + h,
        white, white, white, white, sw, sh, c);

    const float local_x[6] = { 0, w, w, 0, w, 0 };
    const float local_y[6] = { 0, 0, h, 0, h, h };
    float tl = flecsEngine_ui2d_clampRadius(radius_top_left, w, h);
    float tr = flecsEngine_ui2d_clampRadius(radius_top_right, w, h);
    float bl = flecsEngine_ui2d_clampRadius(radius_bottom_left, w, h);
    float br = flecsEngine_ui2d_clampRadius(radius_bottom_right, w, h);
    for (int32_t i = 0; i < 6; i ++) {
        v[i].shape_x = local_x[i];
        v[i].shape_y = local_y[i];
        v[i].shape_w = w;
        v[i].shape_h = h;
        v[i].radius_top_left = tl;
        v[i].radius_top_right = tr;
        v[i].radius_bottom_right = br;
        v[i].radius_bottom_left = bl;
        v[i].top_width = fmaxf(top_width, 0.0f);
        v[i].right_width = fmaxf(right_width, 0.0f);
        v[i].bottom_width = fmaxf(bottom_width, 0.0f);
        v[i].left_width = fmaxf(left_width, 0.0f);
        v[i].shape = shape;
    }
}

void flecsEngine_ui2dRoundedRect(
    ecs_world_t *world,
    float x,
    float y,
    float w,
    float h,
    float radius_top_left,
    float radius_top_right,
    float radius_bottom_left,
    float radius_bottom_right,
    FlecsRgba color)
{
    flecsEngine_ui2d_shape(world, x, y, w, h,
        radius_top_left, radius_top_right,
        radius_bottom_left, radius_bottom_right,
        0, 0, 0, 0, 1.0f, color);
}

void flecsEngine_ui2dQuad(
    ecs_world_t *world,
    float x0,
    float y0,
    float x1,
    float y1,
    float x2,
    float y2,
    float x3,
    float y3,
    FlecsRgba color)
{
    FlecsUi2dImpl *impl = flecsEngine_ui2d_get(world);
    float sw, sh;
    if (!impl || !flecsEngine_ui2d_surfaceSize(world, &sw, &sh)) {
        return;
    }

    uint32_t c = flecsEngine_ui2d_packColor(world, color);
    flecs_ui2d_vertex_t *v = flecsEngine_ui2d_alloc(world, impl, 6);
    float white = FLECS_UI2D_WHITE_UV;

    float px[4] = { x0, x1, x2, x3 };
    float py[4] = { y0, y1, y2, y3 };
    float nx[4], ny[4];
    for (int32_t i = 0; i < 4; i ++) {
        nx[i] = px[i] / sw * 2.0f - 1.0f;
        ny[i] = 1.0f - py[i] / sh * 2.0f;
    }

    v[0] = (flecs_ui2d_vertex_t){ nx[0], ny[0], white, white, c };
    v[1] = (flecs_ui2d_vertex_t){ nx[1], ny[1], white, white, c };
    v[2] = (flecs_ui2d_vertex_t){ nx[2], ny[2], white, white, c };
    v[3] = (flecs_ui2d_vertex_t){ nx[0], ny[0], white, white, c };
    v[4] = (flecs_ui2d_vertex_t){ nx[2], ny[2], white, white, c };
    v[5] = (flecs_ui2d_vertex_t){ nx[3], ny[3], white, white, c };
}

void flecsEngine_ui2dBorder(
    ecs_world_t *world,
    float x,
    float y,
    float w,
    float h,
    float radius_top_left,
    float radius_top_right,
    float radius_bottom_left,
    float radius_bottom_right,
    float top_width,
    float right_width,
    float bottom_width,
    float left_width,
    FlecsRgba color)
{
    flecsEngine_ui2d_shape(world, x, y, w, h,
        radius_top_left, radius_top_right,
        radius_bottom_left, radius_bottom_right,
        top_width, right_width, bottom_width, left_width, 2.0f, color);
}

void flecsEngine_ui2dText(
    ecs_world_t *world,
    float x,
    float y,
    float size,
    FlecsRgba color,
    const char *text)
{
    FlecsUi2dImpl *impl = flecsEngine_ui2d_get(world);
    float sw, sh;
    if (!impl || !text || !flecsEngine_ui2d_surfaceSize(world, &sw, &sh)) {
        return;
    }
    if (!flecsEngine_ui2d_ensureFont(impl)) {
        return;
    }

    float scale = size / FLECS_UI2D_BAKE_PX;
    float pen_x = x / scale;
    float pen_y = (y + impl->ascent_px * scale) / scale;
    uint32_t c = flecsEngine_ui2d_packColor(world, color);

    for (const char *ch = text; *ch; ch ++) {
        unsigned char uc = (unsigned char)*ch;
        if (uc < FLECS_UI2D_FIRST_CHAR ||
            uc >= FLECS_UI2D_FIRST_CHAR + FLECS_UI2D_CHAR_COUNT)
        {
            continue;
        }

        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(impl->baked,
            FLECS_UI2D_ATLAS_SIZE, FLECS_UI2D_ATLAS_SIZE,
            uc - FLECS_UI2D_FIRST_CHAR, &pen_x, &pen_y, &q, 1);

        flecs_ui2d_vertex_t *v = flecsEngine_ui2d_alloc(world, impl, 6);
        flecsEngine_ui2d_emitQuad(v,
            q.x0 * scale, q.y0 * scale, q.x1 * scale, q.y1 * scale,
            q.s0, q.t0, q.s1, q.t1, sw, sh, c);
    }
}

float flecsEngine_ui2dTextWidth(
    ecs_world_t *world,
    float size,
    const char *text)
{
    FlecsUi2dImpl *impl = flecsEngine_ui2d_get(world);
    if (!impl || !text || !flecsEngine_ui2d_ensureFont(impl)) {
        return 0;
    }

    float scale = size / FLECS_UI2D_BAKE_PX;
    float width = 0;
    for (const char *ch = text; *ch; ch ++) {
        unsigned char uc = (unsigned char)*ch;
        if (uc < FLECS_UI2D_FIRST_CHAR ||
            uc >= FLECS_UI2D_FIRST_CHAR + FLECS_UI2D_CHAR_COUNT)
        {
            continue;
        }
        width += impl->baked[uc - FLECS_UI2D_FIRST_CHAR].xadvance;
    }
    return width * scale;
}

void flecsEngine_ui2dSetFont(
    ecs_world_t *world,
    const void *ttf_data,
    int32_t length)
{
    FlecsUi2dImpl *impl = flecsEngine_ui2d_get(world);
    if (!impl || !ttf_data || length <= 0) {
        return;
    }

    ecs_os_free(impl->ttf);
    impl->ttf = ecs_os_malloc(length);
    memcpy(impl->ttf, ttf_data, length);
    impl->ttf_len = length;
    impl->font_baked = false;

    /* Force atlas re-upload */
    FLECS_WGPU_RELEASE(impl->bind_group, wgpuBindGroupRelease);
    FLECS_WGPU_RELEASE(impl->atlas_view, wgpuTextureViewRelease);
    FLECS_WGPU_RELEASE(impl->atlas_tex, wgpuTextureRelease);
}

static bool flecsEngine_ui2d_ensurePipeline(
    ecs_world_t *world,
    FlecsUi2dImpl *impl,
    FlecsEngineImpl *engine)
{
    if (impl->pipeline && impl->pipeline_format == engine->target_format) {
        return true;
    }

    FLECS_WGPU_RELEASE(impl->pipeline, wgpuRenderPipelineRelease);

    if (!impl->bind_layout) {
        WGPUBindGroupLayoutEntry entries[2] = {0};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Fragment;
        entries[0].texture.sampleType = WGPUTextureSampleType_Float;
        entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

        impl->bind_layout = wgpuDeviceCreateBindGroupLayout(
            engine->device, &(WGPUBindGroupLayoutDescriptor){
                .entries = entries,
                .entryCount = 2
            });
        if (!impl->bind_layout) {
            return false;
        }
    }

    (void)world;
    WGPUShaderModule module = flecsEngine_createShaderModule(
        engine->device, kUi2dShaderSource);
    if (!module) {
        return false;
    }

    WGPUVertexAttribute attrs[8] = {
        { .format = WGPUVertexFormat_Float32x2, .offset = 0,
          .shaderLocation = 0 },
        { .format = WGPUVertexFormat_Float32x2, .offset = 8,
          .shaderLocation = 1 },
        { .format = WGPUVertexFormat_Unorm8x4, .offset = 16,
          .shaderLocation = 2 },
        { .format = WGPUVertexFormat_Float32x2, .offset = 20,
          .shaderLocation = 3 },
        { .format = WGPUVertexFormat_Float32x2, .offset = 28,
          .shaderLocation = 4 },
        { .format = WGPUVertexFormat_Float32x4, .offset = 36,
          .shaderLocation = 5 },
        { .format = WGPUVertexFormat_Float32x4, .offset = 52,
          .shaderLocation = 6 },
        { .format = WGPUVertexFormat_Float32, .offset = 68,
          .shaderLocation = 7 }
    };

    WGPUVertexBufferLayout vbuf_layout = {
        .arrayStride = sizeof(flecs_ui2d_vertex_t),
        .stepMode = WGPUVertexStepMode_Vertex,
        .attributeCount = 8,
        .attributes = attrs
    };

    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(
        engine->device, &(WGPUPipelineLayoutDescriptor){
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &impl->bind_layout
        });

    WGPUBlendState blend = {
        .color = {
            .operation = WGPUBlendOperation_Add,
            .srcFactor = WGPUBlendFactor_SrcAlpha,
            .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha
        },
        .alpha = {
            .operation = WGPUBlendOperation_Add,
            .srcFactor = WGPUBlendFactor_One,
            .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha
        }
    };

    WGPUColorTargetState color_target = {
        .format = engine->target_format,
        .blend = &blend,
        .writeMask = WGPUColorWriteMask_All
    };

    WGPURenderPipelineDescriptor desc = {
        .layout = layout,
        .vertex = {
            .module = module,
            .entryPoint = WGPU_STR("vs_main"),
            .bufferCount = 1,
            .buffers = &vbuf_layout
        },
        .fragment = &(WGPUFragmentState){
            .module = module,
            .entryPoint = WGPU_STR("fs_main"),
            .targetCount = 1,
            .targets = &color_target
        },
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .cullMode = WGPUCullMode_None
        },
        .multisample = {
            .count = 1,
            .mask = 0xFFFFFFFF
        }
    };

    impl->pipeline = wgpuDeviceCreateRenderPipeline(engine->device, &desc);
    wgpuPipelineLayoutRelease(layout);
    wgpuShaderModuleRelease(module);
    if (!impl->pipeline) {
        return false;
    }

    impl->pipeline_format = engine->target_format;
    return true;
}

static bool flecsEngine_ui2d_ensureAtlas(
    FlecsUi2dImpl *impl,
    FlecsEngineImpl *engine)
{
    if (impl->atlas_view) {
        return true;
    }
    if (!impl->atlas_bitmap) {
        return false;
    }

    impl->atlas_tex = wgpuDeviceCreateTexture(engine->device,
        &(WGPUTextureDescriptor){
            .usage = WGPUTextureUsage_TextureBinding |
                WGPUTextureUsage_CopyDst,
            .dimension = WGPUTextureDimension_2D,
            .size = { FLECS_UI2D_ATLAS_SIZE, FLECS_UI2D_ATLAS_SIZE, 1 },
            .format = WGPUTextureFormat_R8Unorm,
            .mipLevelCount = 1,
            .sampleCount = 1
        });
    if (!impl->atlas_tex) {
        return false;
    }

    wgpuQueueWriteTexture(engine->queue,
        &(WGPUTexelCopyTextureInfo){
            .texture = impl->atlas_tex,
            .mipLevel = 0,
            .origin = { 0, 0, 0 },
            .aspect = WGPUTextureAspect_All
        },
        impl->atlas_bitmap,
        FLECS_UI2D_ATLAS_SIZE * FLECS_UI2D_ATLAS_SIZE,
        &(WGPUTexelCopyBufferLayout){
            .offset = 0,
            .bytesPerRow = FLECS_UI2D_ATLAS_SIZE,
            .rowsPerImage = FLECS_UI2D_ATLAS_SIZE
        },
        &(WGPUExtent3D){ FLECS_UI2D_ATLAS_SIZE, FLECS_UI2D_ATLAS_SIZE, 1 });

    impl->atlas_view = wgpuTextureCreateView(impl->atlas_tex, NULL);

    ecs_os_free(impl->atlas_bitmap);
    impl->atlas_bitmap = NULL;

    if (!impl->sampler) {
        impl->sampler = flecsEngine_createLinearClampSampler(engine->device);
    }

    FLECS_WGPU_RELEASE(impl->bind_group, wgpuBindGroupRelease);
    WGPUBindGroupEntry entries[2] = {
        { .binding = 0, .textureView = impl->atlas_view },
        { .binding = 1, .sampler = impl->sampler }
    };
    impl->bind_group = wgpuDeviceCreateBindGroup(engine->device,
        &(WGPUBindGroupDescriptor){
            .layout = impl->bind_layout,
            .entries = entries,
            .entryCount = 2
        });

    return impl->bind_group != NULL;
}

void flecsEngine_ui2d_render(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    WGPUTextureView target,
    WGPUCommandEncoder encoder)
{
    if (!ecs_id(FlecsUi2dImpl)) {
        return;
    }
    FlecsUi2dImpl *impl = ecs_singleton_ensure(world, FlecsUi2dImpl);

    const ecs_world_info_t *info = ecs_get_world_info(world);
    if (impl->frame != info->frame_count_total || !impl->vert_count) {
        impl->vert_count = 0;
        return;
    }

    if (!flecsEngine_ui2d_ensureFont(impl)) {
        impl->vert_count = 0;
        return;
    }
    if (!flecsEngine_ui2d_ensurePipeline(world, impl, engine) ||
        !flecsEngine_ui2d_ensureAtlas(impl, engine))
    {
        impl->vert_count = 0;
        return;
    }

    uint64_t bytes = (uint64_t)impl->vert_count *
        sizeof(flecs_ui2d_vertex_t);
    if (!impl->vbuf || impl->vbuf_size < bytes) {
        FLECS_WGPU_RELEASE(impl->vbuf, wgpuBufferRelease);
        uint64_t size = 65536;
        while (size < bytes) size *= 2;
        impl->vbuf = wgpuDeviceCreateBuffer(engine->device,
            &(WGPUBufferDescriptor){
                .usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
                .size = size
            });
        if (!impl->vbuf) {
            impl->vert_count = 0;
            return;
        }
        impl->vbuf_size = size;
    }

    wgpuQueueWriteBuffer(engine->queue, impl->vbuf, 0, impl->verts, bytes);

    WGPURenderPassColorAttachment color_attachment = {
        .view = target,
        WGPU_DEPTH_SLICE
        .loadOp = WGPULoadOp_Load,
        .storeOp = WGPUStoreOp_Store
    };

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder,
        &(WGPURenderPassDescriptor){
            .colorAttachmentCount = 1,
            .colorAttachments = &color_attachment
        });

    wgpuRenderPassEncoderSetPipeline(pass, impl->pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, impl->bind_group, 0, NULL);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, impl->vbuf, 0, bytes);
    wgpuRenderPassEncoderDraw(pass, (uint32_t)impl->vert_count, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    impl->vert_count = 0;
}

void FlecsEngineUi2dImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineUi2d);

    ecs_set_name_prefix(world, "Flecs");

    ECS_COMPONENT_DEFINE(world, FlecsUi2dImpl);

    ecs_set_hooks(world, FlecsUi2dImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsUi2dImpl),
        .dtor = ecs_dtor(FlecsUi2dImpl)
    });

    ecs_singleton_set(world, FlecsUi2dImpl, {0});
}
