#include "renderer.h"
#include "flecs_engine.h"
#include <cglm/clipspace/ortho_rh_zo.h>
#include <math.h>

#define FLECS_ENGINE_SHADOW_MAP_FORMAT WGPUTextureFormat_Depth32Float

static const char *kShadowDepthShaderSource =
    "struct ShadowUniforms {\n"
    "  light_vp : mat4x4<f32>\n"
    "}\n"
    "@group(0) @binding(0) var<uniform> shadow_uniforms : ShadowUniforms;\n"
    "struct GpuTransform {\n"
    "  c0 : vec4<f32>,\n"
    "  c1 : vec4<f32>,\n"
    "  c2 : vec4<f32>,\n"
    "  c3 : vec4<f32>\n"
    "}\n"
    "@group(1) @binding(0) var<storage, read> instance_transforms : array<GpuTransform>;\n"
    "@group(1) @binding(1) var<storage, read> instance_material_ids : array<u32>;\n"
    "struct VertexInput {\n"
    "  @location(0) pos : vec3<f32>,\n"
    "  @location(1) slot : u32\n"
    "}\n"
    "@vertex fn vs_main(input : VertexInput) -> @builtin(position) vec4<f32> {\n"
    "  let t = instance_transforms[input.slot];\n"
    "  let model = mat4x4<f32>(\n"
    "    vec4<f32>(t.c0.xyz, 0.0),\n"
    "    vec4<f32>(t.c1.xyz, 0.0),\n"
    "    vec4<f32>(t.c2.xyz, 0.0),\n"
    "    vec4<f32>(t.c3.xyz, 1.0)\n"
    "  );\n"
    "  let world_pos = model * vec4<f32>(input.pos, 1.0);\n"
    "  return shadow_uniforms.light_vp * world_pos;\n"
    "}\n";

int flecsEngine_shadow_initShared(
    ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    impl->shadow.shader_module = flecsEngine_shader_ensureModule(
        world, "ShadowDepthShader", kShadowDepthShaderSource);
    if (!impl->shadow.shader_module) {
        ecs_err("failed to compile shadow depth shader");
        return -1;
    }

    WGPUSamplerDescriptor sampler_desc = {
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Nearest,
        .compare = WGPUCompareFunction_Less,
        .maxAnisotropy = 1
    };

    impl->shadow.sampler = wgpuDeviceCreateSampler(impl->device, &sampler_desc);
    if (!impl->shadow.sampler) {
        ecs_err("failed to create shadow sampler");
        return -1;
    }

    WGPUBindGroupLayoutEntry pass_layout_entries[1] = {{
        .binding = 0,
        .visibility = WGPUShaderStage_Vertex,
        .buffer = (WGPUBufferBindingLayout){
            .type = WGPUBufferBindingType_Uniform,
            .minBindingSize = sizeof(mat4)
        }
    }};

    WGPUBindGroupLayoutDescriptor pass_layout_desc = {
        .entryCount = 1,
        .entries = pass_layout_entries
    };

    impl->shadow.pass_bind_layout = wgpuDeviceCreateBindGroupLayout(
        impl->device, &pass_layout_desc);
    if (!impl->shadow.pass_bind_layout) {
        ecs_err("failed to create shadow pass bind group layout");
        return -1;
    }

    return 0;
}

void flecsEngine_shadow_cleanupShared(
    FlecsEngineImpl *impl)
{
    FLECS_WGPU_RELEASE(impl->shadow.pass_bind_layout, wgpuBindGroupLayoutRelease);
    FLECS_WGPU_RELEASE(impl->shadow.sampler, wgpuSamplerRelease);
}

int flecsEngine_shadow_initView(
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    uint32_t shadow_map_size)
{
    flecs_view_shadow_t *s = &view_impl->shadow;
    s->map_size = shadow_map_size;

    if (!engine->shadow.pass_bind_layout) {
        ecs_err("shadow shared resources not initialized");
        return -1;
    }

    WGPUTextureDescriptor tex_desc = {
        .usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
        .dimension = WGPUTextureDimension_2D,
        .size = (WGPUExtent3D){
            .width = s->map_size,
            .height = s->map_size,
            .depthOrArrayLayers = FLECS_ENGINE_SHADOW_CASCADE_COUNT
        },
        .format = FLECS_ENGINE_SHADOW_MAP_FORMAT,
        .mipLevelCount = 1,
        .sampleCount = 1
    };

    s->texture = wgpuDeviceCreateTexture(engine->device, &tex_desc);
    if (!s->texture) {
        ecs_err("failed to create shadow map texture");
        return -1;
    }

    WGPUTextureViewDescriptor array_view_desc = {
        .format = FLECS_ENGINE_SHADOW_MAP_FORMAT,
        .dimension = WGPUTextureViewDimension_2DArray,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = FLECS_ENGINE_SHADOW_CASCADE_COUNT,
        .aspect = WGPUTextureAspect_DepthOnly,
    };

    s->texture_view = wgpuTextureCreateView(s->texture, &array_view_desc);
    if (!s->texture_view) {
        ecs_err("failed to create shadow map array view");
        return -1;
    }

    for (int i = 0; i < FLECS_ENGINE_SHADOW_CASCADE_COUNT; i++) {
        WGPUTextureViewDescriptor layer_desc = {
            .format = FLECS_ENGINE_SHADOW_MAP_FORMAT,
            .dimension = WGPUTextureViewDimension_2D,
            .baseMipLevel = 0,
            .mipLevelCount = 1,
            .baseArrayLayer = (uint32_t)i,
            .arrayLayerCount = 1,
            .aspect = WGPUTextureAspect_DepthOnly,
        };

        s->layer_views[i] = wgpuTextureCreateView(s->texture, &layer_desc);
        if (!s->layer_views[i]) {
            ecs_err("failed to create shadow map layer view %d", i);
            return -1;
        }
    }

    for (int i = 0; i < FLECS_ENGINE_SHADOW_CASCADE_COUNT; i++) {
        s->vp_buffers[i] = flecsEngine_createUniformBuffer(
            engine->device, sizeof(mat4));
        if (!s->vp_buffers[i]) {
            ecs_err("failed to create shadow VP buffer %d", i);
            return -1;
        }

        WGPUBindGroupEntry pass_entries[1] = {{
            .binding = 0,
            .buffer = s->vp_buffers[i],
            .size = sizeof(mat4)
        }};

        WGPUBindGroupDescriptor pass_group_desc = {
            .layout = engine->shadow.pass_bind_layout,
            .entryCount = 1,
            .entries = pass_entries
        };

        s->pass_bind_groups[i] = wgpuDeviceCreateBindGroup(
            engine->device, &pass_group_desc);
        if (!s->pass_bind_groups[i]) {
            ecs_err("failed to create shadow pass bind group %d", i);
            return -1;
        }
    }

    /* Bump scene bind version so the view's group 0 is rebuilt with the
     * new shadow texture view. */
    view_impl->scene_bind_version = 0;

    return 0;
}

void flecsEngine_shadow_cleanupView(
    FlecsRenderViewImpl *view_impl)
{
    flecs_view_shadow_t *s = &view_impl->shadow;
    for (int i = 0; i < FLECS_ENGINE_SHADOW_CASCADE_COUNT; i++) {
        if (s->pass_bind_groups[i]) {
            wgpuBindGroupRelease(s->pass_bind_groups[i]);
            s->pass_bind_groups[i] = NULL;
        }
        if (s->vp_buffers[i]) {
            wgpuBufferRelease(s->vp_buffers[i]);
            s->vp_buffers[i] = NULL;
        }
        if (s->layer_views[i]) {
            wgpuTextureViewRelease(s->layer_views[i]);
            s->layer_views[i] = NULL;
        }
    }
    FLECS_WGPU_RELEASE(s->texture_view, wgpuTextureViewRelease);
    FLECS_WGPU_RELEASE(s->texture, wgpuTextureRelease);
    s->map_size = 0;
}

static void flecsEngine_shadow_computeSingleCascade(
    const vec3 ray_dir,
    const vec3 up,
    mat4 inv_view,
    float tan_half_fov,
    float aspect,
    float cascade_near,
    float cascade_far,
    float max_range,
    uint32_t cascade_size,
    mat4 out_vp)
{
    /* Compute 8 frustum corners in view space (right-handed, -Z forward) */
    float nh = cascade_near * tan_half_fov;
    float nw = nh * aspect;
    float fh = cascade_far * tan_half_fov;
    float fw = fh * aspect;

    vec4 corners[8] = {
        { -nw, -nh, -cascade_near, 1.0f },
        {  nw, -nh, -cascade_near, 1.0f },
        {  nw,  nh, -cascade_near, 1.0f },
        { -nw,  nh, -cascade_near, 1.0f },
        { -fw, -fh, -cascade_far, 1.0f },
        {  fw, -fh, -cascade_far, 1.0f },
        {  fw,  fh, -cascade_far, 1.0f },
        { -fw,  fh, -cascade_far, 1.0f },
    };

    /* Transform corners to world space and compute center */
    vec3 center = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 8; i++) {
        glm_mat4_mulv(inv_view, corners[i], corners[i]);
        center[0] += corners[i][0];
        center[1] += corners[i][1];
        center[2] += corners[i][2];
    }

    glm_vec3_scale(center, 1.0f / 8.0f, center);

    /* Build light view matrix centered on the cascade frustum */
    float shadow_distance = 200.0f;
    vec3 light_pos;
    glm_vec3_scale((float*)ray_dir, -shadow_distance, light_pos);
    glm_vec3_add(light_pos, center, light_pos);

    mat4 light_view;
    glm_lookat(light_pos, center, (float*)up, light_view);

    /* Transform corners to light space and compute tight AABB */
    float min_x, max_x, min_y, max_y, min_z, max_z;
    {
        vec4 lc;
        glm_mat4_mulv(light_view, corners[0], lc);
        min_x = max_x = lc[0];
        min_y = max_y = lc[1];
        min_z = max_z = lc[2];
    }

    for (int i = 1; i < 8; i++) {
        vec4 lc;
        glm_mat4_mulv(light_view, corners[i], lc);
        if (lc[0] < min_x) min_x = lc[0];
        if (lc[0] > max_x) max_x = lc[0];
        if (lc[1] < min_y) min_y = lc[1];
        if (lc[1] > max_y) max_y = lc[1];
        if (lc[2] < min_z) min_z = lc[2];
        if (lc[2] > max_z) max_z = lc[2];
    }

    /* Push the light back so tall shadow casters remain in front. */
    float z_push = max_z + shadow_distance;
    if (z_push > 0.0f) {
        light_view[3][2] -= z_push;
        min_z -= z_push;
        max_z -= z_push;
    }

    float half_x = (max_x - min_x) * 0.5f;
    float half_y = (max_y - min_y) * 0.5f;
    half_x += (2.0f * half_x) / (float)cascade_size;
    half_y += (2.0f * half_y) / (float)cascade_size;

    /* Re-center light view on the AABB center */
    float aabb_cx = (min_x + max_x) * 0.5f;
    float aabb_cy = (min_y + max_y) * 0.5f;
    light_view[3][0] -= aabb_cx;
    light_view[3][1] -= aabb_cy;

    /* Extend far Z range to catch shadow casters behind the camera */
    vec3 cam_fwd = {
        -inv_view[2][0], -inv_view[2][1], -inv_view[2][2]
    };
    float ext_scale = glm_vec3_dot(cam_fwd, (float*)ray_dir);
    if (ext_scale < 0.0f) ext_scale = 0.0f;
    float z_range = max_z - min_z;
    float back_ext = z_range * ext_scale;
    if (max_range > 0.0f && back_ext > max_range) back_ext = max_range;
    min_z -= back_ext;

    /* Snap light-space origin to texel boundaries to prevent shimmer */
    float texel_x = (2.0f * half_x) / (float)cascade_size;
    float texel_y = (2.0f * half_y) / (float)cascade_size;
    vec4 origin = {0.0f, 0.0f, 0.0f, 1.0f};
    glm_mat4_mulv(light_view, origin, origin);
    origin[0] = floorf(origin[0] / texel_x) * texel_x;
    origin[1] = floorf(origin[1] / texel_y) * texel_y;

    vec4 snapped;
    glm_mat4_mulv(light_view, (vec4){0.0f, 0.0f, 0.0f, 1.0f}, snapped);
    light_view[3][0] += origin[0] - snapped[0];
    light_view[3][1] += origin[1] - snapped[1];

    /* Build orthographic projection */
    float near_plane = 0.01f;
    float far_plane = -min_z;
    if (far_plane <= near_plane) {
        far_plane = near_plane + 1.0f;
    }

    mat4 light_proj;
    glm_ortho_rh_zo(
        -half_x, half_x,
        -half_y, half_y,
        near_plane, far_plane,
        light_proj);

    glm_mat4_mul(light_proj, light_view, out_vp);
}

void flecsEngine_shadow_computeCascades(
    const ecs_world_t *world,
    const FlecsRenderView *view,
    ecs_entity_t light,
    uint32_t shadow_map_size,
    float max_range,
    mat4 out_light_vp[FLECS_ENGINE_SHADOW_CASCADE_COUNT],
    float out_splits[FLECS_ENGINE_SHADOW_CASCADE_COUNT])
{
    for (int i = 0; i < FLECS_ENGINE_SHADOW_CASCADE_COUNT; i++) {
        glm_mat4_identity(out_light_vp[i]);
        out_splits[i] = 0.0f;
    }

    if (!light || !view->camera) {
        return;
    }

    /* Get light direction */
    const FlecsRotation3 *rotation = ecs_get(
        world, light, FlecsRotation3);
    if (!rotation) {
        return;
    }

    vec3 ray_dir;
    if (!flecsEngine_lightDirFromRotation(rotation, ray_dir)) {
        return;
    }

    /* Choose an up vector that isn't parallel to the light direction */
    vec3 up = {0.0f, 1.0f, 0.0f};
    if (fabsf(ray_dir[1]) > 0.99f) {
        up[0] = 0.0f; up[1] = 0.0f; up[2] = 1.0f;
    }

    /* Get camera properties for frustum computation */
    const FlecsCamera *cam = ecs_get(world, view->camera, FlecsCamera);
    const FlecsCameraImpl *cam_impl = ecs_get(
        world, view->camera, FlecsCameraImpl);
    if (!cam || !cam_impl) {
        return;
    }

    float near = cam->near_;
    float far = cam->far_;
    if (max_range > 0 && max_range < far) {
        far = max_range;
    }
    float fov = cam->fov;
    float aspect = cam->aspect_ratio;
    if (aspect <= 0.0f) {
        aspect = 1.0f;
    }

    /* Compute inverse view matrix (view space -> world space) */
    mat4 inv_view;
    glm_mat4_inv((vec4*)cam_impl->view, inv_view);

    /* Compute cascade split distances (practical split scheme) */
    float lambda = 0.75f;
    float splits[FLECS_ENGINE_SHADOW_CASCADE_COUNT];
    for (int i = 0; i < FLECS_ENGINE_SHADOW_CASCADE_COUNT; i++) {
        float p = (float)(i + 1) / (float)FLECS_ENGINE_SHADOW_CASCADE_COUNT;
        float log_split = near * powf(far / near, p);
        float lin_split = near + (far - near) * p;
        splits[i] = lambda * log_split + (1.0f - lambda) * lin_split;
        out_splits[i] = splits[i];
    }

    float tan_half_fov = tanf(fov * 0.5f);

    for (int c = 0; c < FLECS_ENGINE_SHADOW_CASCADE_COUNT; c++) {
        float cascade_near = (c == 0) ? near : splits[c - 1];
        float cascade_far = splits[c];

        flecsEngine_shadow_computeSingleCascade(
            ray_dir, up, inv_view, tan_half_fov, aspect,
            cascade_near, cascade_far, max_range, shadow_map_size,
            out_light_vp[c]);
    }
}

int flecsEngine_shadow_ensureViewSize(
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    uint32_t shadow_map_size)
{
    if (shadow_map_size == 0) {
        shadow_map_size = FLECS_ENGINE_SHADOW_MAP_SIZE_DEFAULT;
    }

    if (view_impl->shadow.map_size == shadow_map_size &&
        view_impl->shadow.texture_view)
    {
        return 0;
    }

    flecsEngine_shadow_cleanupView(view_impl);
    return flecsEngine_shadow_initView(engine, view_impl, shadow_map_size);
}
