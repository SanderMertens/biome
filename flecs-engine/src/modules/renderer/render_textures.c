#include "renderer.h"
#include "flecs_engine.h"
#include "dds.h"

#include <stb_image.h>
#include <stdio.h>
#include <string.h>

static uint32_t flecsEngine_computeMipCount(
    uint32_t width,
    uint32_t height)
{
    uint32_t count = 1;
    uint32_t dim = width > height ? width : height;
    while (dim > 1) {
        dim >>= 1;
        count++;
    }
    return count;
}

static WGPUTexture flecsEngine_texture_createFromPixels(
    WGPUDevice device,
    WGPUQueue queue,
    const uint8_t *pixels,
    uint32_t width,
    uint32_t height,
    WGPUTextureFormat format)
{
    uint32_t mip_count = flecsEngine_computeMipCount(width, height);
    uint32_t bytes_per_pixel = 4;

    /* Allow sRGB view creation for RGBA8Unorm textures so the blit can
     * sample albedo/emissive through an sRGB view (auto sRGB→linear). */
    WGPUTextureFormat srgb_fmt = WGPUTextureFormat_RGBA8UnormSrgb;
    bool has_srgb_view = (format == WGPUTextureFormat_RGBA8Unorm);

    WGPUTextureDescriptor tex_desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst
               | WGPUTextureUsage_CopySrc,
        .dimension = WGPUTextureDimension_2D,
        .size = { .width = width, .height = height, .depthOrArrayLayers = 1 },
        .format = format,
        .mipLevelCount = mip_count,
        .sampleCount = 1,
        .viewFormatCount = has_srgb_view ? 1 : 0,
        .viewFormats = has_srgb_view ? &srgb_fmt : NULL
    };

    WGPUTexture texture = wgpuDeviceCreateTexture(device, &tex_desc);
    if (!texture) {
        ecs_err("failed to create texture (%ux%u)", width, height);
        return NULL;
    }

    /* Upload mip level 0 */
    WGPUTexelCopyTextureInfo dst = {
        .texture = texture,
        .mipLevel = 0,
        .origin = { 0, 0, 0 },
        .aspect = WGPUTextureAspect_All
    };

    WGPUTexelCopyBufferLayout src_layout = {
        .offset = 0,
        .bytesPerRow = width * bytes_per_pixel,
        .rowsPerImage = height
    };

    WGPUExtent3D write_size = { width, height, 1 };
    wgpuQueueWriteTexture(
        queue, &dst, pixels,
        (size_t)(width * height * bytes_per_pixel),
        &src_layout, &write_size);

    /* Generate and upload remaining mip levels via box filter */
    if (mip_count > 1) {
        uint32_t prev_w = width, prev_h = height;
        uint8_t *prev_pixels = ecs_os_malloc(
            (ecs_size_t)(width * height * bytes_per_pixel));
        memcpy(prev_pixels, pixels, width * height * bytes_per_pixel);

        for (uint32_t mip = 1; mip < mip_count; mip++) {
            uint32_t mip_w = prev_w > 1 ? prev_w / 2 : 1;
            uint32_t mip_h = prev_h > 1 ? prev_h / 2 : 1;
            uint8_t *mip_pixels = ecs_os_malloc(
                (ecs_size_t)(mip_w * mip_h * bytes_per_pixel));

            for (uint32_t y = 0; y < mip_h; y++) {
                for (uint32_t x = 0; x < mip_w; x++) {
                    uint32_t sx = x * 2, sy = y * 2;
                    uint32_t sx1 = sx + 1 < prev_w ? sx + 1 : sx;
                    uint32_t sy1 = sy + 1 < prev_h ? sy + 1 : sy;

                    const uint8_t *p00 = &prev_pixels[(sy  * prev_w + sx ) * 4];
                    const uint8_t *p10 = &prev_pixels[(sy  * prev_w + sx1) * 4];
                    const uint8_t *p01 = &prev_pixels[(sy1 * prev_w + sx ) * 4];
                    const uint8_t *p11 = &prev_pixels[(sy1 * prev_w + sx1) * 4];

                    uint8_t *out = &mip_pixels[(y * mip_w + x) * 4];
                    for (int c = 0; c < 4; c++) {
                        out[c] = (uint8_t)(
                            (p00[c] + p10[c] + p01[c] + p11[c] + 2) / 4);
                    }
                }
            }

            dst.mipLevel = mip;
            src_layout.bytesPerRow = mip_w * bytes_per_pixel;
            src_layout.rowsPerImage = mip_h;
            write_size = (WGPUExtent3D){ mip_w, mip_h, 1 };
            wgpuQueueWriteTexture(
                queue, &dst, mip_pixels,
                (size_t)(mip_w * mip_h * bytes_per_pixel),
                &src_layout, &write_size);

            ecs_os_free(prev_pixels);
            prev_pixels = mip_pixels;
            prev_w = mip_w;
            prev_h = mip_h;
        }

        ecs_os_free(prev_pixels);
    }

    return texture;
}

WGPUTexture flecsEngine_texture_create1x1(
    WGPUDevice device,
    WGPUQueue queue,
    uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    uint8_t pixels[4] = { r, g, b, a };
    return flecsEngine_texture_createFromPixels(
        device, queue, pixels, 1, 1, WGPUTextureFormat_RGBA8Unorm);
}

static WGPUTexture flecsEngine_texture_loadDds(
    WGPUDevice device,
    WGPUQueue queue,
    const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ecs_err("failed to open DDS file: %s", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0) {
        ecs_err("invalid DDS file size: %s", path);
        fclose(f);
        return NULL;
    }

    uint8_t *file_data = ecs_os_malloc((ecs_size_t)file_size);
    if (fread(file_data, 1, (size_t)file_size, f) != (size_t)file_size) {
        ecs_err("failed to read DDS file: %s", path);
        ecs_os_free(file_data);
        fclose(f);
        return NULL;
    }
    fclose(f);

    FlecsDdsInfo info;
    if (!flecs_dds_parse(file_data, (uint32_t)file_size, &info)) {
        ecs_err("failed to parse DDS: %s", path);
        ecs_os_free(file_data);
        return NULL;
    }

    int wgpu_format = flecs_dds_wgpu_format(info.dxgi_format);
    if (!wgpu_format) {
        ecs_err("unsupported DDS format %u: %s", info.dxgi_format, path);
        ecs_os_free(file_data);
        return NULL;
    }

    uint32_t block_size = flecs_dds_block_size(info.dxgi_format);

    WGPUTextureDescriptor tex_desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst
               | WGPUTextureUsage_CopySrc,
        .dimension = WGPUTextureDimension_2D,
        .size = { .width = info.width, .height = info.height,
                  .depthOrArrayLayers = 1 },
        .format = (WGPUTextureFormat)wgpu_format,
        .mipLevelCount = info.mip_count,
        .sampleCount = 1
    };

    WGPUTexture texture = wgpuDeviceCreateTexture(device, &tex_desc);
    if (!texture) {
        ecs_err("failed to create DDS texture: %s", path);
        ecs_os_free(file_data);
        return NULL;
    }

    /* Upload all mip levels from the DDS file */
    const uint8_t *data_ptr = info.pixel_data;
    uint32_t mip_w = info.width, mip_h = info.height;

    for (uint32_t mip = 0; mip < info.mip_count; mip++) {
        uint32_t bw = (mip_w + 3) / 4;
        uint32_t bh = (mip_h + 3) / 4;
        uint32_t mip_data_size = bw * bh * block_size;

        if (data_ptr + mip_data_size > info.pixel_data + info.pixel_data_size) {
            break;
        }

        WGPUTexelCopyTextureInfo dst = {
            .texture = texture,
            .mipLevel = mip,
            .origin = { 0, 0, 0 },
            .aspect = WGPUTextureAspect_All
        };

        WGPUTexelCopyBufferLayout src_layout = {
            .offset = 0,
            .bytesPerRow = bw * block_size,
            .rowsPerImage = bh
        };

        /* Use physical (block-aligned) size for compressed texture copies */
        WGPUExtent3D write_size = { bw * 4, bh * 4, 1 };
        wgpuQueueWriteTexture(
            queue, &dst, data_ptr, mip_data_size,
            &src_layout, &write_size);

        data_ptr += mip_data_size;
        mip_w = mip_w > 1 ? mip_w / 2 : 1;
        mip_h = mip_h > 1 ? mip_h / 2 : 1;
    }

    ecs_os_free(file_data);
    return texture;
}

static bool flecsEngine_pathEndsWith(
    const char *path,
    const char *ext)
{
    size_t plen = strlen(path);
    size_t elen = strlen(ext);
    if (plen < elen) return false;
    return strcmp(path + plen - elen, ext) == 0;
}

WGPUTexture flecsEngine_texture_loadFile(
    WGPUDevice device,
    WGPUQueue queue,
    const char *path)
{
    if (flecsEngine_pathEndsWith(path, ".dds")) {
        return flecsEngine_texture_loadDds(device, queue, path);
    }

    int w, h, channels;
    uint8_t *pixels = stbi_load(path, &w, &h, &channels, 4);
    if (!pixels) {
        /* Try .dds fallback for .png paths */
        if (flecsEngine_pathEndsWith(path, ".png")) {
            size_t len = strlen(path);
            char *dds_path = ecs_os_malloc((ecs_size_t)(len + 1));
            memcpy(dds_path, path, len + 1);
            memcpy(dds_path + len - 4, ".dds", 4);
            WGPUTexture tex = flecsEngine_texture_loadDds(
                device, queue, dds_path);
            ecs_os_free(dds_path);
            if (tex) {
                return tex;
            }
        }
        ecs_err("failed to load texture: %s (%s)", path, stbi_failure_reason());
        return NULL;
    }

    WGPUTexture tex = flecsEngine_texture_createFromPixels(
        device, queue, pixels, (uint32_t)w, (uint32_t)h,
        WGPUTextureFormat_RGBA8Unorm);

    stbi_image_free(pixels);
    return tex;
}

static WGPUTextureView flecsEngine_texture_createArrayView(
    WGPUTexture texture)
{
    WGPUTextureViewDescriptor vd = {
        .format = wgpuTextureGetFormat(texture),
        .dimension = WGPUTextureViewDimension_2DArray,
        .baseMipLevel = 0,
        .mipLevelCount = wgpuTextureGetMipLevelCount(texture),
        .baseArrayLayer = 0,
        .arrayLayerCount = wgpuTextureGetDepthOrArrayLayers(texture)
    };
    return wgpuTextureCreateView(texture, &vd);
}

void flecsEngine_pbr_texture_ensureFallbacks(
    FlecsEngineImpl *engine)
{
    if (engine->textures.fallback_white_tex) {
        return;
    }

    WGPUDevice device = engine->device;
    WGPUQueue queue = engine->queue;

    engine->textures.fallback_white_tex = flecsEngine_texture_create1x1(
        device, queue, 255, 255, 255, 255);
    engine->textures.fallback_white_array_view =
        flecsEngine_texture_createArrayView(
            engine->textures.fallback_white_tex);
    engine->textures.fallback_white_view =
        wgpuTextureCreateView(
            engine->textures.fallback_white_tex, NULL);

    /* Default normal: (0.5, 0.5, 1.0) = flat surface pointing up */
    engine->textures.fallback_normal_tex = flecsEngine_texture_create1x1(
        device, queue, 128, 128, 255, 255);
    engine->textures.fallback_normal_array_view =
        flecsEngine_texture_createArrayView(
            engine->textures.fallback_normal_tex);
}

void flecsEngine_pbr_texture_ensureSamplers(
    FlecsEngineImpl *engine,
    uint16_t max_aniso)
{
    if (max_aniso < 1) max_aniso = 1;

    if (engine->textures.pbr_sampler &&
        engine->textures.applied_max_aniso != max_aniso)
    {
        FLECS_WGPU_RELEASE(engine->textures.pbr_sampler, wgpuSamplerRelease);
    }

    if (!engine->textures.pbr_sampler) {
        WGPUSamplerDescriptor sd = {
            .addressModeU = WGPUAddressMode_Repeat,
            .addressModeV = WGPUAddressMode_Repeat,
            .addressModeW = WGPUAddressMode_Repeat,
            .magFilter = WGPUFilterMode_Linear,
            .minFilter = WGPUFilterMode_Linear,
            .mipmapFilter = WGPUMipmapFilterMode_Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 32.0f,
            .maxAnisotropy = max_aniso
        };
        engine->textures.pbr_sampler = wgpuDeviceCreateSampler(
            engine->device, &sd);
    }

    if (!engine->textures.pbr_low_sampler) {
        WGPUSamplerDescriptor sd = {
            .addressModeU = WGPUAddressMode_Repeat,
            .addressModeV = WGPUAddressMode_Repeat,
            .addressModeW = WGPUAddressMode_Repeat,
            .magFilter = WGPUFilterMode_Linear,
            .minFilter = WGPUFilterMode_Linear,
            .mipmapFilter = WGPUMipmapFilterMode_Linear,
            .lodMinClamp = 0.0f,
            .lodMaxClamp = 32.0f,
            .maxAnisotropy = 1
        };
        engine->textures.pbr_low_sampler = wgpuDeviceCreateSampler(
            engine->device, &sd);
    }

    engine->textures.applied_max_aniso = max_aniso;
}

const char* flecsEngine_texture_formatName(
    WGPUTextureFormat format)
{
    switch (format) {
    case WGPUTextureFormat_Undefined: return "Undefined";
    case WGPUTextureFormat_R8Unorm: return "R8Unorm";
    case WGPUTextureFormat_R8Snorm: return "R8Snorm";
    case WGPUTextureFormat_R8Uint: return "R8Uint";
    case WGPUTextureFormat_R8Sint: return "R8Sint";
    case WGPUTextureFormat_R16Uint: return "R16Uint";
    case WGPUTextureFormat_R16Sint: return "R16Sint";
    case WGPUTextureFormat_R16Float: return "R16Float";
    case WGPUTextureFormat_RG8Unorm: return "RG8Unorm";
    case WGPUTextureFormat_RG8Snorm: return "RG8Snorm";
    case WGPUTextureFormat_RG8Uint: return "RG8Uint";
    case WGPUTextureFormat_RG8Sint: return "RG8Sint";
    case WGPUTextureFormat_R32Float: return "R32Float";
    case WGPUTextureFormat_R32Uint: return "R32Uint";
    case WGPUTextureFormat_R32Sint: return "R32Sint";
    case WGPUTextureFormat_RG16Uint: return "RG16Uint";
    case WGPUTextureFormat_RG16Sint: return "RG16Sint";
    case WGPUTextureFormat_RG16Float: return "RG16Float";
    case WGPUTextureFormat_RGBA8Unorm: return "RGBA8Unorm";
    case WGPUTextureFormat_RGBA8UnormSrgb: return "RGBA8UnormSrgb";
    case WGPUTextureFormat_RGBA8Snorm: return "RGBA8Snorm";
    case WGPUTextureFormat_RGBA8Uint: return "RGBA8Uint";
    case WGPUTextureFormat_RGBA8Sint: return "RGBA8Sint";
    case WGPUTextureFormat_BGRA8Unorm: return "BGRA8Unorm";
    case WGPUTextureFormat_BGRA8UnormSrgb: return "BGRA8UnormSrgb";
#ifndef __EMSCRIPTEN__
    case WGPUTextureFormat_RGB10A2Uint: return "RGB10A2Uint";
#endif
    case WGPUTextureFormat_RGB10A2Unorm: return "RGB10A2Unorm";
    case WGPUTextureFormat_RG11B10Ufloat: return "RG11B10Ufloat";
    case WGPUTextureFormat_RGB9E5Ufloat: return "RGB9E5Ufloat";
    case WGPUTextureFormat_RG32Float: return "RG32Float";
    case WGPUTextureFormat_RG32Uint: return "RG32Uint";
    case WGPUTextureFormat_RG32Sint: return "RG32Sint";
    case WGPUTextureFormat_RGBA16Uint: return "RGBA16Uint";
    case WGPUTextureFormat_RGBA16Sint: return "RGBA16Sint";
    case WGPUTextureFormat_RGBA16Float: return "RGBA16Float";
    case WGPUTextureFormat_RGBA32Float: return "RGBA32Float";
    case WGPUTextureFormat_RGBA32Uint: return "RGBA32Uint";
    case WGPUTextureFormat_RGBA32Sint: return "RGBA32Sint";
    case WGPUTextureFormat_Stencil8: return "Stencil8";
    case WGPUTextureFormat_Depth16Unorm: return "Depth16Unorm";
    case WGPUTextureFormat_Depth24Plus: return "Depth24Plus";
    case WGPUTextureFormat_Depth24PlusStencil8: return "Depth24PlusStencil8";
    case WGPUTextureFormat_Depth32Float: return "Depth32Float";
    case WGPUTextureFormat_Depth32FloatStencil8: return "Depth32FloatStencil8";
    case WGPUTextureFormat_BC1RGBAUnorm: return "BC1RGBAUnorm";
    case WGPUTextureFormat_BC1RGBAUnormSrgb: return "BC1RGBAUnormSrgb";
    case WGPUTextureFormat_BC2RGBAUnorm: return "BC2RGBAUnorm";
    case WGPUTextureFormat_BC2RGBAUnormSrgb: return "BC2RGBAUnormSrgb";
    case WGPUTextureFormat_BC3RGBAUnorm: return "BC3RGBAUnorm";
    case WGPUTextureFormat_BC3RGBAUnormSrgb: return "BC3RGBAUnormSrgb";
    case WGPUTextureFormat_BC4RUnorm: return "BC4RUnorm";
    case WGPUTextureFormat_BC4RSnorm: return "BC4RSnorm";
    case WGPUTextureFormat_BC5RGUnorm: return "BC5RGUnorm";
    case WGPUTextureFormat_BC5RGSnorm: return "BC5RGSnorm";
    case WGPUTextureFormat_BC6HRGBUfloat: return "BC6HRGBUfloat";
    case WGPUTextureFormat_BC6HRGBFloat: return "BC6HRGBFloat";
    case WGPUTextureFormat_BC7RGBAUnorm: return "BC7RGBAUnorm";
    case WGPUTextureFormat_BC7RGBAUnormSrgb: return "BC7RGBAUnormSrgb";
    case WGPUTextureFormat_ETC2RGB8Unorm: return "ETC2RGB8Unorm";
    case WGPUTextureFormat_ETC2RGB8UnormSrgb: return "ETC2RGB8UnormSrgb";
    case WGPUTextureFormat_ETC2RGB8A1Unorm: return "ETC2RGB8A1Unorm";
    case WGPUTextureFormat_ETC2RGB8A1UnormSrgb: return "ETC2RGB8A1UnormSrgb";
    case WGPUTextureFormat_ETC2RGBA8Unorm: return "ETC2RGBA8Unorm";
    case WGPUTextureFormat_ETC2RGBA8UnormSrgb: return "ETC2RGBA8UnormSrgb";
    case WGPUTextureFormat_EACR11Unorm: return "EACR11Unorm";
    case WGPUTextureFormat_EACR11Snorm: return "EACR11Snorm";
    case WGPUTextureFormat_EACRG11Unorm: return "EACRG11Unorm";
    case WGPUTextureFormat_EACRG11Snorm: return "EACRG11Snorm";
    case WGPUTextureFormat_ASTC4x4Unorm: return "ASTC4x4Unorm";
    case WGPUTextureFormat_ASTC4x4UnormSrgb: return "ASTC4x4UnormSrgb";
    case WGPUTextureFormat_ASTC5x4Unorm: return "ASTC5x4Unorm";
    case WGPUTextureFormat_ASTC5x4UnormSrgb: return "ASTC5x4UnormSrgb";
    case WGPUTextureFormat_ASTC5x5Unorm: return "ASTC5x5Unorm";
    case WGPUTextureFormat_ASTC5x5UnormSrgb: return "ASTC5x5UnormSrgb";
    case WGPUTextureFormat_ASTC6x5Unorm: return "ASTC6x5Unorm";
    case WGPUTextureFormat_ASTC6x5UnormSrgb: return "ASTC6x5UnormSrgb";
    case WGPUTextureFormat_ASTC6x6Unorm: return "ASTC6x6Unorm";
    case WGPUTextureFormat_ASTC6x6UnormSrgb: return "ASTC6x6UnormSrgb";
    case WGPUTextureFormat_ASTC8x5Unorm: return "ASTC8x5Unorm";
    case WGPUTextureFormat_ASTC8x5UnormSrgb: return "ASTC8x5UnormSrgb";
    case WGPUTextureFormat_ASTC8x6Unorm: return "ASTC8x6Unorm";
    case WGPUTextureFormat_ASTC8x6UnormSrgb: return "ASTC8x6UnormSrgb";
    case WGPUTextureFormat_ASTC8x8Unorm: return "ASTC8x8Unorm";
    case WGPUTextureFormat_ASTC8x8UnormSrgb: return "ASTC8x8UnormSrgb";
    case WGPUTextureFormat_ASTC10x5Unorm: return "ASTC10x5Unorm";
    case WGPUTextureFormat_ASTC10x5UnormSrgb: return "ASTC10x5UnormSrgb";
    case WGPUTextureFormat_ASTC10x6Unorm: return "ASTC10x6Unorm";
    case WGPUTextureFormat_ASTC10x6UnormSrgb: return "ASTC10x6UnormSrgb";
    case WGPUTextureFormat_ASTC10x8Unorm: return "ASTC10x8Unorm";
    case WGPUTextureFormat_ASTC10x8UnormSrgb: return "ASTC10x8UnormSrgb";
    case WGPUTextureFormat_ASTC10x10Unorm: return "ASTC10x10Unorm";
    case WGPUTextureFormat_ASTC10x10UnormSrgb: return "ASTC10x10UnormSrgb";
    case WGPUTextureFormat_ASTC12x10Unorm: return "ASTC12x10Unorm";
    case WGPUTextureFormat_ASTC12x10UnormSrgb: return "ASTC12x10UnormSrgb";
    case WGPUTextureFormat_ASTC12x12Unorm: return "ASTC12x12Unorm";
    case WGPUTextureFormat_ASTC12x12UnormSrgb: return "ASTC12x12UnormSrgb";
    default: return "Unknown";
    }
}

void flecsEngine_texture_onSet(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsTexture *tex = ecs_field(it, FlecsTexture, 0);

    FlecsEngineImpl *engine = ecs_singleton_get_mut(world, FlecsEngineImpl);
    if (!engine) {
        return;
    }

    for (int i = 0; i < it->count; i++) {
        if (!tex[i].path) {
            continue;
        }

        WGPUTexture texture = flecsEngine_texture_loadFile(
            engine->device, engine->queue, tex[i].path);
        if (texture) {
            FlecsTextureImpl *tex_impl = ecs_ensure(
                world, it->entities[i], FlecsTextureImpl);
            tex_impl->texture = texture;
            tex_impl->view =
                flecsEngine_texture_createArrayView(texture);

            uint32_t tw = wgpuTextureGetWidth(texture);
            uint32_t th = wgpuTextureGetHeight(texture);
            WGPUTextureFormat fmt = wgpuTextureGetFormat(texture);

            FlecsTextureInfo *info = ecs_ensure(
                world, it->entities[i], FlecsTextureInfo);
            info->source.width = tw;
            info->source.height = th;
            info->source.mip_count = wgpuTextureGetMipLevelCount(texture);
            ecs_os_free((char*)info->source.format);
            info->source.format = ecs_os_strdup(
                flecsEngine_texture_formatName(fmt));

            ecs_trace("loaded texture: %ux%u %s %s\n",
                tw, th,
                flecsEngine_texture_formatName(fmt),
                tex[i].path);
        }
    }
}

