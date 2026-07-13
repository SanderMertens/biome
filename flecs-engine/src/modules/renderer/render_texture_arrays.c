#include "renderer.h"
#include "flecs_engine.h"

static const uint32_t flecs_engine_bucket_dim[FLECS_ENGINE_TEXTURE_BUCKET_COUNT] = {
    128, 256, 512, 1024, 2048, 4096
};

/* Per-channel BC7 format. Albedo/emissive use sRGB for correct
 * gamma-decode on sampling; normal/MR use linear. */
static const WGPUTextureFormat flecs_bc7_channel_format[4] = {
    WGPUTextureFormat_BC7RGBAUnormSrgb,  /* albedo   */
    WGPUTextureFormat_BC7RGBAUnormSrgb,  /* emissive */
    WGPUTextureFormat_BC7RGBAUnorm,      /* mr       */
    WGPUTextureFormat_BC7RGBAUnorm,      /* normal   */
};

static uint32_t flecsEngine_textureArray_mipCount(uint32_t dim)
{
    uint32_t count = 1;
    while (dim > 1) { dim >>= 1; count++; }
    return count;
}

static uint8_t flecsEngine_textureArray_resolveMaxBucket(
    const FlecsSurface *surface)
{
    if (surface) {
        switch (surface->texture_quality) {
        case FlecsTextureQualityLow:      return 2; /* 512  */
        case FlecsTextureQualityMedium:   return 3; /* 1024 */
        case FlecsTextureQualityHigh:     return 4; /* 2048 */
        case FlecsTextureQualityVeryHigh: return 5; /* 4096 */
        default: break;
        }
    }
    return FLECS_ENGINE_TEXTURE_BUCKET_COUNT - 1;
}

static uint8_t flecsEngine_textureArray_pickBucket(
    uint32_t w, uint32_t h, uint8_t max_bucket)
{
    uint32_t max_dim = w > h ? w : h;
    for (uint8_t b = 0; b <= max_bucket; b++) {
        if (max_dim <= flecs_engine_bucket_dim[b]) {
            return b;
        }
    }
    return max_bucket;
}

static bool flecsEngine_textureArray_isBC7(WGPUTextureFormat fmt)
{
    return fmt == WGPUTextureFormat_BC7RGBAUnorm
        || fmt == WGPUTextureFormat_BC7RGBAUnormSrgb;
}

void flecsEngine_textureArray_release(
    FlecsEngineImpl *impl)
{
    for (int b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        flecsEngine_texture_bucket_t *bk = &impl->textures.buckets[b];
        for (int ch = 0; ch < 4; ch++) {
            if (bk->texture_array_views[ch]) {
                wgpuTextureViewRelease(bk->texture_array_views[ch]);
                bk->texture_array_views[ch] = NULL;
            }
            if (bk->texture_arrays[ch]) {
                wgpuTextureRelease(bk->texture_arrays[ch]);
                bk->texture_arrays[ch] = NULL;
            }
            bk->layer_counts[ch] = 0;
        }
        bk->mip_count = 0;
        bk->width = 0;
        bk->height = 0;
        bk->is_bc7 = false;
        FLECS_WGPU_RELEASE(impl->textures.bucket_bind_groups[b],
            wgpuBindGroupRelease);
    }
    FLECS_WGPU_RELEASE(impl->textures.fallback_bind_group, wgpuBindGroupRelease);
}

typedef struct {
    uint32_t bc7_count;
    uint32_t other_count;
} flecs_format_census_t;

static void flecsEngine_textureArray_censusFormats(
    const ecs_world_t *world,
    FlecsEngineImpl *impl,
    uint8_t max_bucket,
    flecs_format_census_t census[FLECS_ENGINE_TEXTURE_BUCKET_COUNT])
{
    for (int b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        census[b].bc7_count = 0;
        census[b].other_count = 0;
    }

    ecs_iter_t it = ecs_query_iter(world, impl->textures.query);
    while (ecs_query_next(&it)) {
        const FlecsPbrTextures *textures =
            ecs_field(&it, FlecsPbrTextures, 0);
        const FlecsMaterialId *mat_ids =
            ecs_field(&it, FlecsMaterialId, 1);

        for (int32_t i = 0; i < it.count; i++) {
            uint32_t mat_id = mat_ids[i].value;
            if (mat_id >= impl->materials.count) continue;

            ecs_entity_t tex_entities[4] = {
                textures[i].albedo,
                textures[i].emissive,
                textures[i].roughness,
                textures[i].normal
            };

            uint32_t max_w = 0, max_h = 0;
            bool any_bc7 = false, any_other = false;
            for (int ch = 0; ch < 4; ch++) {
                if (!tex_entities[ch]) continue;
                const FlecsTextureImpl *ti =
                    ecs_get(world, tex_entities[ch], FlecsTextureImpl);
                if (!ti || !ti->texture) continue;

                uint32_t tw = wgpuTextureGetWidth(ti->texture);
                uint32_t th = wgpuTextureGetHeight(ti->texture);
                if (tw > max_w) max_w = tw;
                if (th > max_h) max_h = th;

                if (flecsEngine_textureArray_isBC7(
                        wgpuTextureGetFormat(ti->texture)) && tw == th)
                {
                    any_bc7 = true;
                } else {
                    any_other = true;
                }
            }
            if (!max_w || !max_h) continue;

            uint8_t bucket =
                flecsEngine_textureArray_pickBucket(max_w, max_h, max_bucket);
            if (any_bc7) census[bucket].bc7_count++;
            if (any_other) census[bucket].other_count++;
        }
    }
}

static void flecsEngine_textureArray_decideBucketFormats(
    FlecsEngineImpl *impl,
    uint8_t max_bucket,
    const flecs_format_census_t census[FLECS_ENGINE_TEXTURE_BUCKET_COUNT])
{
    uint8_t top_bucket = max_bucket;
    for (int b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        bool all_bc7 = census[b].bc7_count > 0 && census[b].other_count == 0;
        bool all_other = census[b].other_count > 0 && census[b].bc7_count == 0;
        bool mixed = census[b].bc7_count > 0 && census[b].other_count > 0;

        if (all_bc7) {
            impl->textures.buckets[b].is_bc7 = true;
        } else if (all_other) {
            impl->textures.buckets[b].is_bc7 = false;
        } else if (mixed && (uint8_t)b == top_bucket) {
            /* Top bucket mixed: prefer BC7, non-BC7 sources will be
             * redirected to the next-smaller bucket. */
            impl->textures.buckets[b].is_bc7 = true;
        } else {
            /* Smaller bucket mixed: prefer RGBA8, BC7 sources will be
             * decoded via the blit sampler (already works). */
            impl->textures.buckets[b].is_bc7 = false;
        }
    }
}

/* ---- Survey (format-aware) ---- */

static bool flecsEngine_textureArray_survey(
    ecs_world_t *world,
    FlecsEngineImpl *impl,
    uint8_t max_bucket,
    uint32_t bucket_channel_layers[FLECS_ENGINE_TEXTURE_BUCKET_COUNT][4])
{
    for (int b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        for (int ch = 0; ch < 4; ch++) {
            bucket_channel_layers[b][ch] = 1;
        }
    }

    bool any_material = false;

    ecs_iter_t it = ecs_query_iter(world, impl->textures.query);
    while (ecs_query_next(&it)) {
        const FlecsPbrTextures *textures =
            ecs_field(&it, FlecsPbrTextures, 0);
        const FlecsMaterialId *mat_ids =
            ecs_field(&it, FlecsMaterialId, 1);

        for (int32_t i = 0; i < it.count; i++) {
            uint32_t mat_id = mat_ids[i].value;
            if (mat_id >= impl->materials.count) continue;

            /* Reset layer fields; survey is the sole source of truth for
             * (bucket, layer) and stale values from a prior build can
             * point at slots that no longer hold this material's data. */
            FlecsGpuMaterial *gm = &impl->materials.cpu_materials[mat_id];
            gm->layer_albedo   = 0;
            gm->layer_emissive = 0;
            gm->layer_mr       = 0;
            gm->layer_normal   = 0;

            ecs_entity_t tex_entities[4] = {
                textures[i].albedo,
                textures[i].emissive,
                textures[i].roughness,
                textures[i].normal
            };

            bool has_channel[4] = { false, false, false, false };
            bool any_non_bc7 = false;
            uint32_t max_w = 0, max_h = 0;
            for (int ch = 0; ch < 4; ch++) {
                if (!tex_entities[ch]) continue;
                const FlecsTextureImpl *ti =
                    ecs_get(world, tex_entities[ch], FlecsTextureImpl);
                if (!ti || !ti->texture) continue;

                has_channel[ch] = true;
                uint32_t tw = wgpuTextureGetWidth(ti->texture);
                uint32_t th = wgpuTextureGetHeight(ti->texture);
                if (tw > max_w) max_w = tw;
                if (th > max_h) max_h = th;

                /* Non-square BC7 sources can't be copied into square
                 * bucket arrays, so treat them as non-BC7 for routing. */
                if (!flecsEngine_textureArray_isBC7(
                        wgpuTextureGetFormat(ti->texture)) || tw != th)
                {
                    any_non_bc7 = true;
                }
            }

            if (!max_w || !max_h) continue;

            uint8_t bucket =
                flecsEngine_textureArray_pickBucket(max_w, max_h, max_bucket);

            if (any_non_bc7 && impl->textures.buckets[bucket].is_bc7
                && bucket > 0)
            {
                bucket = bucket - 1;
            }

            impl->materials.cpu_buckets[mat_id] = (int8_t)bucket;

            if (has_channel[0]) {
                gm->layer_albedo = bucket_channel_layers[bucket][0]++;
            }
            if (has_channel[1]) {
                gm->layer_emissive = bucket_channel_layers[bucket][1]++;
            }
            if (has_channel[2]) {
                gm->layer_mr = bucket_channel_layers[bucket][2]++;
            }
            if (has_channel[3]) {
                gm->layer_normal = bucket_channel_layers[bucket][3]++;
            }

            uint32_t bk_dim = flecs_engine_bucket_dim[bucket];
            bool bk_bc7 = impl->textures.buckets[bucket].is_bc7;
            uint32_t bk_mips =
                flecsEngine_textureArray_mipCount(bk_dim);
            for (int ch = 0; ch < 4; ch++) {
                if (!tex_entities[ch]) continue;
                FlecsTextureInfo *info = ecs_ensure(
                    world, tex_entities[ch], FlecsTextureInfo);
                info->actual.width = bk_dim;
                info->actual.height = bk_dim;
                info->actual.mip_count = bk_mips;
                ecs_os_free((char*)info->actual.format);
                if (bk_bc7) {
                    info->actual.format = ecs_os_strdup(
                        flecsEngine_texture_formatName(
                            flecs_bc7_channel_format[ch]));
                } else {
                    info->actual.format = ecs_os_strdup(
                        flecsEngine_texture_formatName(
                            FLECS_ENGINE_BUCKET_FORMAT));
                }
            }

            any_material = true;
        }
    }

    for (int b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        for (int ch = 0; ch < 4; ch++) {
            if (bucket_channel_layers[b][ch] == 1) {
                bucket_channel_layers[b][ch] = 0;
            }
        }
    }

    return any_material;
}

/* ---- Bucket creation ---- */

static bool flecsEngine_textureArray_createBucket(
    FlecsEngineImpl *impl,
    uint8_t bucket_idx,
    const uint32_t channel_layer_counts[4])
{
    flecsEngine_texture_bucket_t *bk = &impl->textures.buckets[bucket_idx];
    uint32_t dim = flecs_engine_bucket_dim[bucket_idx];
    uint32_t mip_count = flecsEngine_textureArray_mipCount(dim);

    for (int ch = 0; ch < 4; ch++) {
        uint32_t lc = channel_layer_counts[ch];
        if (!lc) {
            bk->layer_counts[ch] = 0;
            continue;
        }

        WGPUTextureFormat fmt = bk->is_bc7
            ? flecs_bc7_channel_format[ch]
            : FLECS_ENGINE_BUCKET_FORMAT;

        WGPUTextureUsage usage = WGPUTextureUsage_TextureBinding
                               | WGPUTextureUsage_CopyDst;
        if (!bk->is_bc7) {
            usage |= WGPUTextureUsage_RenderAttachment
                   | WGPUTextureUsage_StorageBinding;
        }

        WGPUTextureDescriptor desc = {
            .usage = usage,
            .dimension = WGPUTextureDimension_2D,
            .size = {
                .width = dim,
                .height = dim,
                .depthOrArrayLayers = lc
            },
            .format = fmt,
            .mipLevelCount = mip_count,
            .sampleCount = 1
        };
        bk->texture_arrays[ch] = wgpuDeviceCreateTexture(impl->device, &desc);
        if (!bk->texture_arrays[ch]) {
            ecs_err("failed to create texture array (bucket %u, channel %d, "
                "%ux%u, %u layers, %s)", bucket_idx, ch, dim, dim, lc,
                bk->is_bc7 ? "BC7" : "RGBA8");
            return false;
        }
        bk->layer_counts[ch] = lc;
    }

    bk->mip_count = mip_count;
    bk->width = dim;
    bk->height = dim;
    return true;
}

/* ---- Fallback fill ---- */

static void flecsEngine_textureArray_fillBucketFallback(
    FlecsEngineImpl *impl,
    uint8_t bucket_idx)
{
    flecsEngine_texture_bucket_t *bk = &impl->textures.buckets[bucket_idx];

    uint32_t dim = bk->width;
    if (!dim) {
        return;
    }

    if (bk->is_bc7) {
        /* BC7 fallback: encode a solid-color BC7 block per channel and
         * tile it across every mip of every layer. */
        static const uint8_t fallback_colors[4][4] = {
            { 255, 255, 255, 255 }, /* albedo    */
            { 255, 255, 255, 255 }, /* emissive  */
            { 255, 255, 255, 255 }, /* roughness */
            { 128, 128, 255, 255 }  /* normal    */
        };

        for (int ch = 0; ch < 4; ch++) {
            uint32_t lc = bk->layer_counts[ch];
            if (!lc || !bk->texture_arrays[ch]) continue;

            uint8_t block[16];
            flecsEngine_bc7_encodeSolidBlock(block,
                fallback_colors[ch][0], fallback_colors[ch][1],
                fallback_colors[ch][2], fallback_colors[ch][3]);

            for (uint32_t mip = 0; mip < bk->mip_count; mip++) {
                uint32_t mw = dim >> mip; if (!mw) mw = 1;
                uint32_t mh = dim >> mip; if (!mh) mh = 1;
                uint32_t bw = (mw + 3) / 4; if (!bw) bw = 1;
                uint32_t bh = (mh + 3) / 4; if (!bh) bh = 1;
                uint32_t mip_bytes = bw * bh * 16;
                uint8_t *fill = ecs_os_malloc((ecs_size_t)mip_bytes);
                for (uint32_t bi = 0; bi < bw * bh; bi++) {
                    memcpy(fill + bi * 16, block, 16);
                }

                for (uint32_t layer = 0; layer < lc; layer++) {
                    WGPUTexelCopyTextureInfo dst = {
                        .texture = bk->texture_arrays[ch],
                        .mipLevel = mip,
                        .origin = { 0, 0, layer }
                    };
                    WGPUTexelCopyBufferLayout src_layout = {
                        .bytesPerRow = bw * 16,
                        .rowsPerImage = bh
                    };
                    WGPUExtent3D size = { bw * 4, bh * 4, 1 };
                    wgpuQueueWriteTexture(
                        impl->queue, &dst, fill,
                        mip_bytes, &src_layout, &size);
                }

                ecs_os_free(fill);
            }
        }
    } else {
        /* RGBA8 fallback: fill mip 0 with per-channel neutral colour. */
        static const uint8_t fallback_rgba[4][4] = {
            { 255, 255, 255, 255 }, /* albedo    */
            { 255, 255, 255, 255 }, /* emissive  */
            { 255, 255, 255, 255 }, /* roughness */
            { 128, 128, 255, 255 }  /* normal    */
        };

        uint32_t row_bytes = dim * 4;
        uint32_t layer_bytes = row_bytes * dim;
        uint8_t *fill = ecs_os_malloc((ecs_size_t)layer_bytes);

        for (int ch = 0; ch < 4; ch++) {
            uint32_t lc = bk->layer_counts[ch];
            if (!lc || !bk->texture_arrays[ch]) continue;

            for (uint32_t p = 0; p < dim * dim; p++) {
                memcpy(fill + p * 4, fallback_rgba[ch], 4);
            }

            for (uint32_t layer = 0; layer < lc; layer++) {
                WGPUTexelCopyTextureInfo dst = {
                    .texture = bk->texture_arrays[ch],
                    .mipLevel = 0,
                    .origin = { 0, 0, layer }
                };
                WGPUTexelCopyBufferLayout src_layout = {
                    .bytesPerRow = row_bytes,
                    .rowsPerImage = dim
                };
                WGPUExtent3D size = { dim, dim, 1 };
                wgpuQueueWriteTexture(
                    impl->queue, &dst, fill,
                    layer_bytes, &src_layout, &size);
            }
        }

        ecs_os_free(fill);
    }
}

/* ---- Bind group ---- */

static WGPUBindGroup flecsEngine_textureArray_createBucketBindGroup(
    FlecsEngineImpl *impl,
    WGPUBindGroupLayout layout,
    WGPUSampler aniso_sampler,
    WGPUSampler low_sampler,
    WGPUTextureView channel_views[4])
{
    WGPUTextureView fallback_white  = impl->textures.fallback_white_array_view;
    WGPUTextureView fallback_normal = impl->textures.fallback_normal_array_view;

    WGPUBindGroupEntry entries[6] = {0};
    for (uint32_t ch = 0; ch < 4; ch++) {
        WGPUTextureView fb = (ch == 3) ? fallback_normal : fallback_white;
        entries[ch] = (WGPUBindGroupEntry){
            .binding = ch,
            .textureView = channel_views[ch] ? channel_views[ch] : fb
        };
    }
    entries[4] = (WGPUBindGroupEntry){
        .binding = 4,
        .sampler = aniso_sampler
    };
    entries[5] = (WGPUBindGroupEntry){
        .binding = 5,
        .sampler = low_sampler
    };

    return wgpuDeviceCreateBindGroup(impl->device,
        &(WGPUBindGroupDescriptor){
            .layout = layout,
            .entries = entries,
            .entryCount = 6
        });
}

static void flecsEngine_textureArray_createBindGroup(
    FlecsEngineImpl *impl,
    uint16_t max_aniso)
{
    for (int b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        flecsEngine_texture_bucket_t *bk = &impl->textures.buckets[b];

        for (int ch = 0; ch < 4; ch++) {
            if (!bk->layer_counts[ch] || !bk->texture_arrays[ch]) continue;
            WGPUTextureFormat vf = bk->is_bc7
                ? flecs_bc7_channel_format[ch]
                : FLECS_ENGINE_BUCKET_FORMAT;
            WGPUTextureViewDescriptor vd = {
                .format = vf,
                .dimension = WGPUTextureViewDimension_2DArray,
                .baseMipLevel = 0,
                .mipLevelCount = bk->mip_count,
                .baseArrayLayer = 0,
                .arrayLayerCount = bk->layer_counts[ch]
            };
            bk->texture_array_views[ch] = wgpuTextureCreateView(
                bk->texture_arrays[ch], &vd);
        }
    }

    flecsEngine_pbr_texture_ensureSamplers(impl, max_aniso);
    WGPUSampler aniso_sampler = impl->textures.pbr_sampler;
    WGPUSampler low_sampler   = impl->textures.pbr_low_sampler;
    WGPUBindGroupLayout layout =
        flecsEngine_textures_ensureBindLayout(impl);

    for (int b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        flecsEngine_texture_bucket_t *bk = &impl->textures.buckets[b];
        WGPUTextureView views[4] = {
            bk->texture_array_views[0],
            bk->texture_array_views[1],
            bk->texture_array_views[2],
            bk->texture_array_views[3]
        };
        impl->textures.bucket_bind_groups[b] =
            flecsEngine_textureArray_createBucketBindGroup(
                impl, layout, aniso_sampler, low_sampler, views);
    }

    WGPUTextureView fallback_views[4] = { NULL, NULL, NULL, NULL };
    impl->textures.fallback_bind_group =
        flecsEngine_textureArray_createBucketBindGroup(
            impl, layout, aniso_sampler, low_sampler, fallback_views);
}

void flecsEngine_textureArray_rebuildBindGroups(
    FlecsEngineImpl *impl)
{
    WGPUSampler aniso_sampler = impl->textures.pbr_sampler;
    WGPUSampler low_sampler   = impl->textures.pbr_low_sampler;
    WGPUBindGroupLayout layout =
        flecsEngine_textures_ensureBindLayout(impl);

    for (int b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        FLECS_WGPU_RELEASE(impl->textures.bucket_bind_groups[b],
            wgpuBindGroupRelease);
        flecsEngine_texture_bucket_t *bk = &impl->textures.buckets[b];
        WGPUTextureView views[4] = {
            bk->texture_array_views[0],
            bk->texture_array_views[1],
            bk->texture_array_views[2],
            bk->texture_array_views[3]
        };
        impl->textures.bucket_bind_groups[b] =
            flecsEngine_textureArray_createBucketBindGroup(
                impl, layout, aniso_sampler, low_sampler, views);
    }

    FLECS_WGPU_RELEASE(impl->textures.fallback_bind_group,
        wgpuBindGroupRelease);
    WGPUTextureView fallback_views[4] = { NULL, NULL, NULL, NULL };
    impl->textures.fallback_bind_group =
        flecsEngine_textureArray_createBucketBindGroup(
            impl, layout, aniso_sampler, low_sampler, fallback_views);
}

/* ---- Build orchestrator ---- */

void flecsEngine_material_buildTextureArrays(
    ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    if (!impl->textures.query || !impl->device) {
        return;
    }

    flecsEngine_textureArray_release(impl);
    flecsEngine_pbr_texture_ensureFallbacks(impl);

    impl->textures.bucket_version ++;

    const FlecsSurface *surface = ecs_get(world, impl->surface, FlecsSurface);
    uint16_t max_aniso = (surface && surface->anisotropy != FlecsAnisotropyDefault)
        ? (uint16_t)surface->anisotropy
        : (uint16_t)FlecsAnisotropyHigh;

    uint8_t max_bucket = flecsEngine_textureArray_resolveMaxBucket(surface);
    impl->textures.applied_max_bucket = max_bucket;
    impl->textures.applied_texture_quality = surface
        ? (int32_t)surface->texture_quality
        : (int32_t)FlecsTextureQualityDefault;

    /* Reset bucket assignments — survey only overwrites entries it
     * processes, and stale values from a prior build can point at
     * buckets that no longer hold the material's textures. */
    if (impl->materials.cpu_buckets && impl->materials.buffer_capacity) {
        ecs_os_memset_n(
            impl->materials.cpu_buckets, FLECS_ENGINE_BUCKET_UNSET,
            int8_t, (int32_t)impl->materials.buffer_capacity);
    }

    /* Pass 1: format census — decide BC7 vs RGBA8 per bucket. */
    flecs_format_census_t census[FLECS_ENGINE_TEXTURE_BUCKET_COUNT];
    flecsEngine_textureArray_censusFormats(world, impl, max_bucket, census);
    flecsEngine_textureArray_decideBucketFormats(impl, max_bucket, census);

    /* Pass 2: slot assignment (format-aware, with redirection). */
    uint32_t bucket_channel_layers[FLECS_ENGINE_TEXTURE_BUCKET_COUNT][4];
    bool any_material = flecsEngine_textureArray_survey(
        world, impl, max_bucket, bucket_channel_layers);

    if (!any_material) {
        /* No textured materials. Still build a bind group using the
         * fallback views so that pbr batches (which always
         * declare the texture array bindings) can draw. */
        flecsEngine_textureArray_createBindGroup(impl, max_aniso);
        return;
    }

    for (uint8_t b = 0; b < FLECS_ENGINE_TEXTURE_BUCKET_COUNT; b++) {
        uint32_t bucket_total = 0;
        for (int ch = 0; ch < 4; ch++) {
            bucket_total += bucket_channel_layers[b][ch];
        }
        if (!bucket_total) continue;

        if (!flecsEngine_textureArray_createBucket(
                impl, b, bucket_channel_layers[b]))
        {
            flecsEngine_textureArray_release(impl);
            return;
        }
        flecsEngine_textureArray_fillBucketFallback(impl, b);
    }

    /* Blit RGBA8 buckets (existing path). */
    flecsEngine_textureArray_blitTextures(world, impl);

    /* Copy BC7 buckets (direct mip-preserving copy). */
    flecsEngine_textureArray_copyTextures_bc7(world, impl);

    /* Re-upload material buffer with updated (bucket, layer) values. */
    if (impl->materials.count) {
        wgpuQueueWriteBuffer(
            impl->queue,
            impl->materials.buffer,
            0,
            impl->materials.cpu_materials,
            (uint64_t)impl->materials.count * sizeof(FlecsGpuMaterial));
    }

    flecsEngine_textureArray_createBindGroup(impl, max_aniso);
}
