#ifndef FLECS_ENGINE_RENDER_TEXTURES_H
#define FLECS_ENGINE_RENDER_TEXTURES_H

WGPUBindGroupLayout flecsEngine_textures_ensureBindLayout(
    FlecsEngineImpl *impl);

WGPUTexture flecsEngine_texture_loadFile(
    WGPUDevice device,
    WGPUQueue queue,
    const char *path);

WGPUTexture flecsEngine_texture_create1x1(
    WGPUDevice device,
    WGPUQueue queue,
    uint8_t r, uint8_t g, uint8_t b, uint8_t a);

void flecsEngine_pbr_texture_ensureFallbacks(
    FlecsEngineImpl *engine);

void flecsEngine_pbr_texture_ensureSamplers(
    FlecsEngineImpl *engine,
    uint16_t max_aniso);

void flecsEngine_material_buildTextureArrays(
    ecs_world_t *world,
    FlecsEngineImpl *impl);

void flecsEngine_textureArray_rebuildBindGroups(
    FlecsEngineImpl *impl);

#define FLECS_ENGINE_BUCKET_FORMAT WGPUTextureFormat_RGBA8Unorm

void flecsEngine_textureArray_release(
    FlecsEngineImpl *impl);

void flecsEngine_textureArray_blitTextures(
    const ecs_world_t *world,
    FlecsEngineImpl *impl);

void flecsEngine_textureArray_copyTextures_bc7(
    const ecs_world_t *world,
    FlecsEngineImpl *impl);

void flecsEngine_bc7_encodeSolidBlock(
    uint8_t block[16],
    uint8_t r, uint8_t g, uint8_t b, uint8_t a);

void flecsEngine_textureBlit_init(
    ecs_world_t *world,
    FlecsEngineImpl *impl);

void flecsEngine_textureBlit_release(
    FlecsEngineImpl *impl);

void flecsEngine_texture_onSet(
    ecs_iter_t *it);

const char* flecsEngine_texture_formatName(
    WGPUTextureFormat format);

void flecsEngine_transmission_updateSnapshot(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    WGPUCommandEncoder encoder,
    WGPUTexture src_texture,
    uint32_t width,
    uint32_t height);

void flecsEngine_transmission_releaseView(
    FlecsRenderViewImpl *view_impl);

void flecsEngine_transmission_releaseShared(
    FlecsEngineImpl *engine);

#endif
