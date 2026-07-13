#include "mip_pyramid.h"
#include "flecs_engine.h"

uint32_t flecsEngine_mipPyramid_maxMips(
    uint32_t width,
    uint32_t height)
{
    uint32_t max_dim = width > height ? width : height;
    uint32_t mips = 1u;
    while ((max_dim >> mips) > 0u) {
        mips ++;
    }
    return mips;
}

bool flecsEngine_mipPyramid_create(
    WGPUDevice device,
    uint32_t width,
    uint32_t height,
    uint32_t mip_count,
    WGPUTextureFormat format,
    WGPUTextureUsage usage,
    WGPUTexture *out_texture,
    WGPUTextureView **out_mip_views)
{
    *out_texture = NULL;
    *out_mip_views = NULL;

    WGPUTexture texture = wgpuDeviceCreateTexture(
        device, &(WGPUTextureDescriptor){
            .usage = usage,
            .dimension = WGPUTextureDimension_2D,
            .size = { width, height, 1 },
            .format = format,
            .mipLevelCount = mip_count,
            .sampleCount = 1
        });
    if (!texture) {
        return false;
    }

    WGPUTextureView *views = ecs_os_calloc_n(WGPUTextureView, mip_count);
    if (!views) {
        wgpuTextureRelease(texture);
        return false;
    }

    for (uint32_t i = 0; i < mip_count; i ++) {
        views[i] = wgpuTextureCreateView(
            texture, &(WGPUTextureViewDescriptor){
                .format = format,
                .dimension = WGPUTextureViewDimension_2D,
                .baseMipLevel = i,
                .mipLevelCount = 1,
                .baseArrayLayer = 0,
                .arrayLayerCount = 1
            });
        if (!views[i]) {
            for (uint32_t j = 0; j < i; j ++) {
                wgpuTextureViewRelease(views[j]);
            }
            ecs_os_free(views);
            wgpuTextureRelease(texture);
            return false;
        }
    }

    *out_texture = texture;
    *out_mip_views = views;
    return true;
}

void flecsEngine_mipPyramid_release(
    WGPUTexture *texture,
    WGPUTextureView **mip_views,
    uint32_t mip_count)
{
    if (mip_views && *mip_views) {
        for (uint32_t i = 0; i < mip_count; i ++) {
            if ((*mip_views)[i]) {
                wgpuTextureViewRelease((*mip_views)[i]);
            }
        }
        ecs_os_free(*mip_views);
        *mip_views = NULL;
    }
    if (texture && *texture) {
        wgpuTextureRelease(*texture);
        *texture = NULL;
    }
}

