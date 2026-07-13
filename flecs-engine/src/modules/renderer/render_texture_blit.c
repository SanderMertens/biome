#include <string.h>

#include "renderer.h"
#include "flecs_engine.h"

static const char *kBlitShaderSource =
    "struct VertexOut {\n"
    "  @builtin(position) pos : vec4<f32>,\n"
    "  @location(0) uv : vec2<f32>,\n"
    "};\n"
    "@vertex fn vs_main(@builtin(vertex_index) id : u32) -> VertexOut {\n"
    "  var out : VertexOut;\n"
    "  let x = f32((id << 1u) & 2u);\n"
    "  let y = f32(id & 2u);\n"
    "  out.uv = vec2<f32>(x, y);\n"
    "  out.pos = vec4<f32>(x * 2.0 - 1.0, 1.0 - y * 2.0, 0.0, 1.0);\n"
    "  return out;\n"
    "}\n"
    "@group(0) @binding(0) var src_tex  : texture_2d<f32>;\n"
    "@group(0) @binding(1) var src_samp : sampler;\n"
    "@fragment fn fs_main(in : VertexOut) -> @location(0) vec4<f32> {\n"
    "  return textureSample(src_tex, src_samp, in.uv);\n"
    "}\n";

static const char *kMipGenShaderSource =
    "@group(0) @binding(0) var src : texture_2d_array<f32>;\n"
    "@group(0) @binding(1) var dst : texture_storage_2d_array<rgba8unorm, write>;\n"
    "@compute @workgroup_size(8, 8, 1)\n"
    "fn cs_main(@builtin(global_invocation_id) id : vec3<u32>) {\n"
    "  let dst_dim = textureDimensions(dst);\n"
    "  if (id.x >= dst_dim.x || id.y >= dst_dim.y) { return; }\n"
    "  let layer = i32(id.z);\n"
    "  let src_xy = vec2<i32>(id.xy * 2u);\n"
    "  let s00 = textureLoad(src, src_xy + vec2<i32>(0, 0), layer, 0);\n"
    "  let s10 = textureLoad(src, src_xy + vec2<i32>(1, 0), layer, 0);\n"
    "  let s01 = textureLoad(src, src_xy + vec2<i32>(0, 1), layer, 0);\n"
    "  let s11 = textureLoad(src, src_xy + vec2<i32>(1, 1), layer, 0);\n"
    "  let avg = (s00 + s10 + s01 + s11) * 0.25;\n"
    "  textureStore(dst, vec2<i32>(id.xy), layer, avg);\n"
    "}\n";

static void flecsEngine_textureArray_initBlitPipeline(
    ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    WGPUBindGroupLayoutEntry blit_entries[2] = {0};
    blit_entries[0].binding = 0;
    blit_entries[0].visibility = WGPUShaderStage_Fragment;
    blit_entries[0].texture.sampleType = WGPUTextureSampleType_Float;
    blit_entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
    blit_entries[1].binding = 1;
    blit_entries[1].visibility = WGPUShaderStage_Fragment;
    blit_entries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    impl->textures.blit_bind_layout = wgpuDeviceCreateBindGroupLayout(
        impl->device, &(WGPUBindGroupLayoutDescriptor){
            .entries = blit_entries,
            .entryCount = 2
        });

    WGPUShaderModule module = flecsEngine_shader_ensureModule(
        world, "TextureArrayBlitShader", kBlitShaderSource);

    WGPUColorTargetState color_target = {
        .format = FLECS_ENGINE_BUCKET_FORMAT,
        .writeMask = WGPUColorWriteMask_All
    };

    impl->textures.blit_pipeline = flecsEngine_createFullscreenPipeline(
        impl, module, impl->textures.blit_bind_layout,
        NULL, NULL, &color_target, NULL);

    /* Linear-filter, clamp-to-edge sampler used for the blit. */
    impl->textures.blit_sampler =
        flecsEngine_createLinearClampSampler(impl->device);
}

static void flecsEngine_textureArray_initMipGenPipeline(
    ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    WGPUBindGroupLayoutEntry mip_entries[2] = {0};
    mip_entries[0].binding = 0;
    mip_entries[0].visibility = WGPUShaderStage_Compute;
    mip_entries[0].texture.sampleType = WGPUTextureSampleType_Float;
    mip_entries[0].texture.viewDimension = WGPUTextureViewDimension_2DArray;
    mip_entries[1].binding = 1;
    mip_entries[1].visibility = WGPUShaderStage_Compute;
    mip_entries[1].storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    mip_entries[1].storageTexture.format = FLECS_ENGINE_BUCKET_FORMAT;
    mip_entries[1].storageTexture.viewDimension =
        WGPUTextureViewDimension_2DArray;

    impl->textures.mipgen_bind_layout = wgpuDeviceCreateBindGroupLayout(
        impl->device, &(WGPUBindGroupLayoutDescriptor){
            .entries = mip_entries,
            .entryCount = 2
        });

    WGPUShaderModule module = flecsEngine_shader_ensureModule(
        world, "TextureArrayMipGenShader", kMipGenShaderSource);

    impl->textures.mipgen_pipeline = flecsEngine_createComputePipeline(
        impl, module, impl->textures.mipgen_bind_layout, NULL);
}

void flecsEngine_textureBlit_init(
    ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    flecsEngine_textureArray_initBlitPipeline(world, impl);
    flecsEngine_textureArray_initMipGenPipeline(world, impl);
}

/* Run a single blit: sample from src_2d_view and write to dst_slice_view
 * (which must be a 2D view of one slice/mip 0 of a bucket array). */
static void flecsEngine_textureArray_doBlit(
    FlecsEngineImpl *impl,
    WGPUCommandEncoder encoder,
    WGPUTextureView src_2d_view,
    WGPUTextureView dst_slice_view)
{
    WGPUBindGroupEntry bg_entries[2] = {
        { .binding = 0, .textureView = src_2d_view },
        { .binding = 1, .sampler = impl->textures.blit_sampler }
    };
    WGPUBindGroup bg = wgpuDeviceCreateBindGroup(
        impl->device, &(WGPUBindGroupDescriptor){
            .layout = impl->textures.blit_bind_layout,
            .entries = bg_entries,
            .entryCount = 2
        });

    flecsEngine_fullscreenPass(
        encoder, dst_slice_view, WGPULoadOp_Clear, (WGPUColor){0, 0, 0, 0},
        impl->textures.blit_pipeline, bg, NULL, NULL, NULL);
    wgpuBindGroupRelease(bg);
}

/* Generate mips 1..N for one channel of a bucket via the compute shader. */
static void flecsEngine_textureArray_genMipsForChannel(
    FlecsEngineImpl *impl,
    WGPUCommandEncoder encoder,
    WGPUTexture array_tex,
    uint32_t arr_w,
    uint32_t arr_h,
    uint32_t layer_count,
    uint32_t mip_count)
{
    for (uint32_t mip = 1; mip < mip_count; mip++) {
        uint32_t dst_w = arr_w >> mip; if (!dst_w) dst_w = 1;
        uint32_t dst_h = arr_h >> mip; if (!dst_h) dst_h = 1;

        WGPUTextureView src_view = wgpuTextureCreateView(
            array_tex, &(WGPUTextureViewDescriptor){
                .format = FLECS_ENGINE_BUCKET_FORMAT,
                .dimension = WGPUTextureViewDimension_2DArray,
                .baseMipLevel = mip - 1,
                .mipLevelCount = 1,
                .baseArrayLayer = 0,
                .arrayLayerCount = layer_count
            });
        WGPUTextureView dst_view = wgpuTextureCreateView(
            array_tex, &(WGPUTextureViewDescriptor){
                .format = FLECS_ENGINE_BUCKET_FORMAT,
                .dimension = WGPUTextureViewDimension_2DArray,
                .baseMipLevel = mip,
                .mipLevelCount = 1,
                .baseArrayLayer = 0,
                .arrayLayerCount = layer_count
            });

        WGPUBindGroupEntry bg_entries[2] = {
            { .binding = 0, .textureView = src_view },
            { .binding = 1, .textureView = dst_view }
        };
        WGPUBindGroup bg = wgpuDeviceCreateBindGroup(
            impl->device, &(WGPUBindGroupDescriptor){
                .layout = impl->textures.mipgen_bind_layout,
                .entries = bg_entries,
                .entryCount = 2
            });

        WGPUComputePassEncoder cpass = wgpuCommandEncoderBeginComputePass(
            encoder, &(WGPUComputePassDescriptor){0});
        wgpuComputePassEncoderSetPipeline(cpass, impl->textures.mipgen_pipeline);
        wgpuComputePassEncoderSetBindGroup(cpass, 0, bg, 0, NULL);
        wgpuComputePassEncoderDispatchWorkgroups(
            cpass,
            (dst_w + 7) / 8,
            (dst_h + 7) / 8,
            layer_count);
        wgpuComputePassEncoderEnd(cpass);
        wgpuComputePassEncoderRelease(cpass);

        wgpuBindGroupRelease(bg);
        wgpuTextureViewRelease(src_view);
        wgpuTextureViewRelease(dst_view);
    }
}

void flecsEngine_textureArray_blitTextures(
    const ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        impl->device, &(WGPUCommandEncoderDescriptor){0});

    ecs_iter_t it = ecs_query_iter(world, impl->textures.query);
    while (ecs_query_next(&it)) {
        const FlecsPbrTextures *textures =
            ecs_field(&it, FlecsPbrTextures, 0);
        const FlecsMaterialId *mat_ids =
            ecs_field(&it, FlecsMaterialId, 1);

        for (int32_t i = 0; i < it.count; i++) {
            uint32_t mat_id = mat_ids[i].value;
            if (mat_id >= impl->materials.count) continue;

            const FlecsGpuMaterial *gm =
                &impl->materials.cpu_materials[mat_id];
            int8_t b = impl->materials.cpu_buckets[mat_id];
            if (b < 0 || b >= FLECS_ENGINE_TEXTURE_BUCKET_COUNT) continue;
            uint8_t bucket = (uint8_t)b;

            flecsEngine_texture_bucket_t *bk = &impl->textures.buckets[bucket];
            if (bk->is_bc7) continue;  /* handled by BC7 copy path */

            ecs_entity_t tex_entities[4] = {
                textures[i].albedo,
                textures[i].emissive,
                textures[i].roughness,
                textures[i].normal
            };
            uint32_t per_channel_slot[4] = {
                gm->layer_albedo,
                gm->layer_emissive,
                gm->layer_mr,
                gm->layer_normal
            };

            for (int ch = 0; ch < 4; ch++) {
                if (!tex_entities[ch]) continue;
                if (!bk->texture_arrays[ch]) continue;
                uint32_t slot = per_channel_slot[ch];
                if (slot == 0) continue;  /* neutral, never blitted */
                if (slot >= bk->layer_counts[ch]) continue;

                const FlecsTextureImpl *ti =
                    ecs_get(world, tex_entities[ch], FlecsTextureImpl);
                if (!ti || !ti->texture) continue;

                WGPUTextureFormat src_fmt = wgpuTextureGetFormat(ti->texture);
                if ((ch == 0 || ch == 1)
                    && src_fmt == WGPUTextureFormat_RGBA8Unorm)
                {
                    src_fmt = WGPUTextureFormat_RGBA8UnormSrgb;
                }

                WGPUTextureView src_view = wgpuTextureCreateView(
                    ti->texture, &(WGPUTextureViewDescriptor){
                        .format = src_fmt,
                        .dimension = WGPUTextureViewDimension_2D,
                        .baseMipLevel = 0,
                        .mipLevelCount = wgpuTextureGetMipLevelCount(
                            ti->texture),
                        .baseArrayLayer = 0,
                        .arrayLayerCount = 1
                    });

                WGPUTextureView dst_view = wgpuTextureCreateView(
                    bk->texture_arrays[ch], &(WGPUTextureViewDescriptor){
                        .format = FLECS_ENGINE_BUCKET_FORMAT,
                        .dimension = WGPUTextureViewDimension_2D,
                        .baseMipLevel = 0,
                        .mipLevelCount = 1,
                        .baseArrayLayer = slot,
                        .arrayLayerCount = 1
                    });

                flecsEngine_textureArray_doBlit(
                    impl, encoder, src_view, dst_view);

                wgpuTextureViewRelease(src_view);
                wgpuTextureViewRelease(dst_view);
            }
        }
    }

    /* Generate mips 1..N for every populated RGBA8 (bucket, channel).
     * BC7 buckets use source mips directly — no generation needed. */
    for (int b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        flecsEngine_texture_bucket_t *bk = &impl->textures.buckets[b];
        if (bk->is_bc7 || bk->mip_count <= 1) continue;
        for (int ch = 0; ch < 4; ch++) {
            if (!bk->layer_counts[ch] || !bk->texture_arrays[ch]) continue;
            flecsEngine_textureArray_genMipsForChannel(
                impl, encoder,
                bk->texture_arrays[ch],
                bk->width, bk->height,
                bk->layer_counts[ch], bk->mip_count);
        }
    }

    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(
        encoder, &(WGPUCommandBufferDescriptor){0});
    wgpuQueueSubmit(impl->queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);
}

/* ---- BC7 solid-color block encoder ---- */

void flecsEngine_bc7_encodeSolidBlock(
    uint8_t block[16],
    uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    memset(block, 0, 16);

    uint8_t r7 = r >> 1, g7 = g >> 1, b7 = b >> 1;

    uint64_t lo = 0, hi = 0;
    int bit = 0;

    #define WB(val, cnt) do { \
        uint64_t _v = (uint64_t)(val); \
        for (int _i = 0; _i < (cnt); _i++, bit++) { \
            if (bit < 64) lo |= ((_v >> _i) & 1) << bit; \
            else          hi |= ((_v >> _i) & 1) << (bit - 64); \
        } \
    } while(0)

    WB(1 << 5, 6);   /* mode 5 */
    WB(0, 2);        /* rotation */
    WB(r7, 7);       /* ep0.R */
    WB(g7, 7);       /* ep0.G */
    WB(b7, 7);       /* ep0.B */
    WB(r7, 7);       /* ep1.R */
    WB(g7, 7);       /* ep1.G */
    WB(b7, 7);       /* ep1.B */
    WB(a, 8);        /* ep0.A */
    WB(a, 8);        /* ep1.A */
    /* indices: all 0 — already zeroed by memset */

    #undef WB

    memcpy(block, &lo, 8);
    memcpy(block + 8, &hi, 8);
}

/* ---- BC7 direct-copy path ---- */

static bool flecsEngine_textureArray_isBC7(WGPUTextureFormat fmt)
{
    return fmt == WGPUTextureFormat_BC7RGBAUnorm
        || fmt == WGPUTextureFormat_BC7RGBAUnormSrgb;
}

void flecsEngine_textureArray_copyTextures_bc7(
    const ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        impl->device, &(WGPUCommandEncoderDescriptor){0});

    bool any_work = false;

    ecs_iter_t it = ecs_query_iter(world, impl->textures.query);
    while (ecs_query_next(&it)) {
        const FlecsPbrTextures *textures =
            ecs_field(&it, FlecsPbrTextures, 0);
        const FlecsMaterialId *mat_ids =
            ecs_field(&it, FlecsMaterialId, 1);

        for (int32_t i = 0; i < it.count; i++) {
            uint32_t mat_id = mat_ids[i].value;
            if (mat_id >= impl->materials.count) continue;

            const FlecsGpuMaterial *gm =
                &impl->materials.cpu_materials[mat_id];
            int8_t b = impl->materials.cpu_buckets[mat_id];
            if (b < 0 || b >= FLECS_ENGINE_TEXTURE_BUCKET_COUNT) continue;
            uint8_t bucket = (uint8_t)b;

            flecsEngine_texture_bucket_t *bk = &impl->textures.buckets[bucket];
            if (!bk->is_bc7) continue;  /* RGBA8 buckets handled by blit */

            ecs_entity_t tex_entities[4] = {
                textures[i].albedo,
                textures[i].emissive,
                textures[i].roughness,
                textures[i].normal
            };
            uint32_t per_channel_slot[4] = {
                gm->layer_albedo,
                gm->layer_emissive,
                gm->layer_mr,
                gm->layer_normal
            };

            for (int ch = 0; ch < 4; ch++) {
                if (!tex_entities[ch]) continue;
                if (!bk->texture_arrays[ch]) continue;
                uint32_t slot = per_channel_slot[ch];
                if (slot == 0) continue;
                if (slot >= bk->layer_counts[ch]) continue;

                const FlecsTextureImpl *ti =
                    ecs_get(world, tex_entities[ch], FlecsTextureImpl);
                if (!ti || !ti->texture) continue;

                if (!flecsEngine_textureArray_isBC7(
                        wgpuTextureGetFormat(ti->texture)))
                {
                    continue;
                }

                uint32_t src_w = wgpuTextureGetWidth(ti->texture);
                uint32_t src_h = wgpuTextureGetHeight(ti->texture);
                uint32_t src_mips = wgpuTextureGetMipLevelCount(ti->texture);

                uint32_t src_mip_start = 0;
                uint32_t dst_mip_start = 0;
                uint32_t src_max = src_w > src_h ? src_w : src_h;

                if (src_max > bk->width) {
                    while ((src_max >> src_mip_start) > bk->width &&
                           src_mip_start + 1 < src_mips)
                    {
                        src_mip_start++;
                    }
                } else if (src_max < bk->width) {
                    while ((bk->width >> dst_mip_start) > src_max &&
                           dst_mip_start + 1 < bk->mip_count)
                    {
                        dst_mip_start++;
                    }
                }

                /* Verify dimensions actually match at the copy point. */
                uint32_t eff_src_w = src_w >> src_mip_start;
                uint32_t eff_src_h = src_h >> src_mip_start;
                if (!eff_src_w) eff_src_w = 1;
                if (!eff_src_h) eff_src_h = 1;
                uint32_t eff_dst_w = bk->width >> dst_mip_start;
                uint32_t eff_dst_h = bk->height >> dst_mip_start;
                if (eff_src_w != eff_dst_w || eff_src_h != eff_dst_h) {
                    continue;  /* non-square or mismatched — keep fallback */
                }

                uint32_t avail_src = src_mips > src_mip_start
                    ? src_mips - src_mip_start : 0;
                uint32_t avail_dst = bk->mip_count > dst_mip_start
                    ? bk->mip_count - dst_mip_start : 0;
                uint32_t copy_mips = avail_src < avail_dst
                    ? avail_src : avail_dst;

                for (uint32_t mi = 0; mi < copy_mips; mi++) {
                    uint32_t mw = bk->width >> (dst_mip_start + mi);
                    uint32_t mh = bk->height >> (dst_mip_start + mi);
                    if (!mw) mw = 1;
                    if (!mh) mh = 1;

                    if (mw < 4 || mh < 4) break;

                    WGPUTexelCopyTextureInfo src = {
                        .texture = ti->texture,
                        .mipLevel = src_mip_start + mi,
                        .origin = { 0, 0, 0 },
                        .aspect = WGPUTextureAspect_All
                    };
                    WGPUTexelCopyTextureInfo dst = {
                        .texture = bk->texture_arrays[ch],
                        .mipLevel = dst_mip_start + mi,
                        .origin = { 0, 0, slot },
                        .aspect = WGPUTextureAspect_All
                    };
                    WGPUExtent3D size = { mw, mh, 1 };
                    wgpuCommandEncoderCopyTextureToTexture(
                        encoder, &src, &dst, &size);
                    any_work = true;
                }
            }
        }
    }

    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(
        encoder, &(WGPUCommandBufferDescriptor){0});
    if (any_work) {
        wgpuQueueSubmit(impl->queue, 1, &cmd);
    }
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);
}

void flecsEngine_textureBlit_release(
    FlecsEngineImpl *impl)
{
    FLECS_WGPU_RELEASE(impl->textures.blit_pipeline, wgpuRenderPipelineRelease);
    FLECS_WGPU_RELEASE(impl->textures.blit_bind_layout, wgpuBindGroupLayoutRelease);
    FLECS_WGPU_RELEASE(impl->textures.blit_sampler, wgpuSamplerRelease);
    FLECS_WGPU_RELEASE(impl->textures.mipgen_pipeline, wgpuComputePipelineRelease);
    FLECS_WGPU_RELEASE(impl->textures.mipgen_bind_layout, wgpuBindGroupLayoutRelease);
}
