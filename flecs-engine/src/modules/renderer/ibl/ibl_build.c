#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../renderer.h"
#include "hdri_loader.h"
#include "ibl_helpers_wgsl.h"
#include "ibl_internal.h"
#include "flecs_engine.h"

#define FLECS_ENGINE_IBL_ENV_SIZE (512u)
#define FLECS_ENGINE_IBL_IRRADIANCE_SIZE (32u)
#define FLECS_ENGINE_IBL_FALLBACK_IMAGE_SIZE (1u)

typedef struct FlecsIblFaceUniform {
    float face_index;
    float roughness;
    float face_size;
    float sample_count;
} FlecsIblFaceUniform;

typedef struct FlecsIblBrdfUniform {
    uint32_t sample_count;
    uint32_t _padding0;
    uint32_t _padding1;
    uint32_t _padding2;
} FlecsIblBrdfUniform;

static const char *kPrefilterShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "struct FaceUniform {\n"
    "  data : vec4<f32>\n"
    "};\n"
    "@group(0) @binding(0) var equirect_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var env_sampler : sampler;\n"
    "@group(0) @binding(2) var<uniform> face_uniform : FaceUniform;\n"
    FLECS_ENGINE_IBL_HELPERS_WGSL
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let face = u32(face_uniform.data.x + 0.5);\n"
    "  let roughness = clamp(face_uniform.data.y, 0.0, 1.0);\n"
    "  let n = cubeFaceUvToDir(face, in.uv, face_uniform.data.z);\n"
    "  let sample_count = max(u32(face_uniform.data.w + 0.5), 1u);\n"
    "  let v = n;\n"
    "  var prefiltered = vec3<f32>(0.0);\n"
    "  var total_weight = 0.0;\n"
    "  for (var i = 0u; i < sample_count; i++) {\n"
    "    let xi = hammersley(i, sample_count);\n"
    "    let h = importanceSampleGGX(xi, n, roughness);\n"
    "    let l = normalize(2.0 * dot(v, h) * h - v);\n"
    "    let ndotl = max(dot(n, l), 0.0);\n"
    "    if (ndotl > 0.0) {\n"
    "      let env_uv = directionToEquirectUv(l);\n"
    "      prefiltered += textureSampleLevel(equirect_texture, env_sampler, env_uv, 0.0).rgb * ndotl;\n"
    "      total_weight += ndotl;\n"
    "    }\n"
    "  }\n"
    "  if (total_weight > 0.0) {\n"
    "    return vec4<f32>(prefiltered / total_weight, 1.0);\n"
    "  }\n"
    "  return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
    "}\n";

static const char *kIrradianceShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "struct FaceUniform {\n"
    "  data : vec4<f32>\n"
    "};\n"
    "@group(0) @binding(0) var equirect_texture : texture_2d<f32>;\n"
    "@group(0) @binding(1) var env_sampler : sampler;\n"
    "@group(0) @binding(2) var<uniform> face_uniform : FaceUniform;\n"
    FLECS_ENGINE_IBL_HELPERS_WGSL
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec4<f32> {\n"
    "  let face = u32(face_uniform.data.x + 0.5);\n"
    "  let n = cubeFaceUvToDir(face, in.uv, face_uniform.data.z);\n"
    "  let up_guess = select(vec3<f32>(0.0, 1.0, 0.0), vec3<f32>(1.0, 0.0, 0.0), abs(n.y) > 0.999);\n"
    "  let tangent = normalize(cross(up_guess, n));\n"
    "  let bitangent = cross(n, tangent);\n"
    "  var irradiance = vec3<f32>(0.0);\n"
    "  let sample_delta = 0.025;\n"
    "  var num_samples = 0.0;\n"
    "  for (var phi = 0.0; phi < 6.2831853; phi += sample_delta) {\n"
    "    for (var theta = 0.0; theta < 1.5707963; theta += sample_delta) {\n"
    "      let s = sin(theta);\n"
    "      let c = cos(theta);\n"
    "      let tangent_sample = vec3<f32>(s * cos(phi), s * sin(phi), c);\n"
    "      let world_sample = tangent * tangent_sample.x + bitangent * tangent_sample.y + n * tangent_sample.z;\n"
    "      let env_uv = directionToEquirectUv(world_sample);\n"
    "      irradiance += textureSampleLevel(equirect_texture, env_sampler, env_uv, 0.0).rgb * c * s;\n"
    "    }\n"
    "    num_samples += 1.0;\n"
    "  }\n"
    "  irradiance = irradiance * PI / (num_samples * f32(u32(1.5707963 / sample_delta) + 1u));\n"
    "  return vec4<f32>(irradiance, 1.0);\n"
    "}\n";

static const char *kBrdfLutShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "struct BrdfUniform {\n"
    "  sample_count : u32,\n"
    "  _padding0 : u32,\n"
    "  _padding1 : u32,\n"
    "  _padding2 : u32\n"
    "};\n"
    "@group(0) @binding(0) var<uniform> brdf_uniform : BrdfUniform;\n"
    FLECS_ENGINE_IBL_HELPERS_WGSL
    "fn geometrySchlickGGX_IBL(ndotv : f32, roughness : f32) -> f32 {\n"
    "  let k = (roughness * roughness) / 2.0;\n"
    "  return ndotv / max(ndotv * (1.0 - k) + k, 1e-4);\n"
    "}\n"
    "fn geometrySmith(n : vec3<f32>, v : vec3<f32>, l : vec3<f32>, roughness : f32) -> f32 {\n"
    "  let ndotv = max(dot(n, v), 0.0);\n"
    "  let ndotl = max(dot(n, l), 0.0);\n"
    "  let ggx_v = geometrySchlickGGX_IBL(ndotv, roughness);\n"
    "  let ggx_l = geometrySchlickGGX_IBL(ndotl, roughness);\n"
    "  return ggx_v * ggx_l;\n"
    "}\n"
    "@fragment fn fs_main(in : VertexOutput) -> @location(0) vec2<f32> {\n"
    "  let ndotv = clamp(in.uv.x, 0.0, 1.0);\n"
    "  let roughness = clamp(in.uv.y, 0.0, 1.0);\n"
    "  let n = vec3<f32>(0.0, 0.0, 1.0);\n"
    "  let sample_count = max(brdf_uniform.sample_count, 1u);\n"
    "  let v = vec3<f32>(sqrt(max(1.0 - ndotv * ndotv, 0.0)), 0.0, ndotv);\n"
    "  var a = 0.0;\n"
    "  var b = 0.0;\n"
    "  for (var i = 0u; i < sample_count; i++) {\n"
    "    let xi = hammersley(i, sample_count);\n"
    "    let h = importanceSampleGGX(xi, n, roughness);\n"
    "    let l = normalize(2.0 * dot(v, h) * h - v);\n"
    "    let ndotl = max(l.z, 0.0);\n"
    "    let ndoth = max(h.z, 0.0);\n"
    "    let vdoth = max(dot(v, h), 0.0);\n"
    "    if (ndotl > 0.0) {\n"
    "      let g = geometrySmith(n, v, l, roughness);\n"
    "      let g_vis = (g * vdoth) / max(ndoth * ndotv, 1e-4);\n"
    "      let fc = pow(1.0 - vdoth, 5.0);\n"
    "      a += (1.0 - fc) * g_vis;\n"
    "      b += fc * g_vis;\n"
    "    }\n"
    "  }\n"
    "  return vec2<f32>(a, b) / f32(sample_count);\n"
    "}\n";

static void flecsIblLogImageStats(
    const FlecsHdriImage *image,
    const char *path)
{
    if (!image || !image->pixels_rgba32f || image->width <= 0 || image->height <= 0) {
        return;
    }

    size_t pixel_count = (size_t)image->width * (size_t)image->height;
    float min_l = INFINITY;
    float max_l = 0.0f;
    for (size_t i = 0; i < pixel_count; i ++) {
        const float *p = &image->pixels_rgba32f[i * 4u];
        float l = p[0] * 0.2126f + p[1] * 0.7152f + p[2] * 0.0722f;
        if (l < min_l) {
            min_l = l;
        }
        if (l > max_l) {
            max_l = l;
        }
    }

    ecs_dbg(
        "[ibl] loaded HDRI '%s' (%dx%d), luminance range [%g, %g]",
        path ? path : "<unknown>",
        image->width,
        image->height,
        (double)min_l,
        (double)max_l);

    if (max_l <= 1.05f) {
        ecs_warn(
            "[ibl] HDRI '%s' appears low-range (max luminance=%g); reflections may look flat",
            path ? path : "<unknown>",
            (double)max_l);
    }
}

uint32_t flecsIblComputeMipCount(
    uint32_t size)
{
    uint32_t mip_count = 1u;
    while (size > 1u) {
        size >>= 1u;
        mip_count ++;
    }
    return mip_count;
}

static uint16_t flecsIblFloatToHalf(
    float value)
{
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));

    uint32_t sign = (bits >> 16u) & 0x8000u;
    uint32_t mantissa = bits & 0x007FFFFFu;
    int32_t exp = ((int32_t)((bits >> 23u) & 0xFFu)) - 127 + 15;

    if (exp <= 0) {
        if (exp < -10) {
            return (uint16_t)sign;
        }

        mantissa = (mantissa | 0x00800000u) >> (1 - exp);
        if (mantissa & 0x00001000u) {
            mantissa += 0x00002000u;
        }

        return (uint16_t)(sign | (mantissa >> 13u));
    }

    if (exp >= 31) {
        return (uint16_t)(sign | 0x7C00u);
    }

    if (mantissa & 0x00001000u) {
        mantissa += 0x00002000u;
        if (mantissa & 0x00800000u) {
            mantissa = 0;
            exp ++;
            if (exp >= 31) {
                return (uint16_t)(sign | 0x7C00u);
            }
        }
    }

    return (uint16_t)(sign | ((uint32_t)exp << 10u) | (mantissa >> 13u));
}

static WGPURenderPipeline flecsIblCreatePipeline(
    const FlecsEngineImpl *engine,
    WGPUShaderModule shader_module,
    WGPUBindGroupLayout bind_layout,
    const char *fragment_entry,
    WGPUTextureFormat format)
{
    WGPUColorTargetState color_target = {
        .format = format,
        .writeMask = WGPUColorWriteMask_All
    };
    return flecsEngine_createFullscreenPipeline(
        engine, shader_module, bind_layout,
        NULL, fragment_entry, &color_target, NULL);
}

static WGPUTextureView flecsIblCreateCubeView(
    WGPUTexture texture,
    WGPUTextureFormat format,
    uint32_t mip_count)
{
    WGPUTextureViewDescriptor view_desc = {
        .format = format,
        .dimension = WGPUTextureViewDimension_Cube,
        .baseMipLevel = 0,
        .mipLevelCount = mip_count,
        .baseArrayLayer = 0,
        .arrayLayerCount = 6,
        .aspect = WGPUTextureAspect_All
    };

    return wgpuTextureCreateView(texture, &view_desc);
}

static WGPUTextureView flecsIblCreateCubeFaceView(
    WGPUTexture texture,
    WGPUTextureFormat format,
    uint32_t mip_level,
    uint32_t array_layer)
{
    WGPUTextureViewDescriptor view_desc = {
        .format = format,
        .dimension = WGPUTextureViewDimension_2D,
        .baseMipLevel = mip_level,
        .mipLevelCount = 1,
        .baseArrayLayer = array_layer,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    };

    return wgpuTextureCreateView(texture, &view_desc);
}

static WGPUTextureView flecsIblCreateTexture2DView(
    WGPUTexture texture,
    WGPUTextureFormat format)
{
    WGPUTextureViewDescriptor view_desc = {
        .format = format,
        .dimension = WGPUTextureViewDimension_2D,
        .baseMipLevel = 0,
        .mipLevelCount = 1,
        .baseArrayLayer = 0,
        .arrayLayerCount = 1,
        .aspect = WGPUTextureAspect_All
    };

    return wgpuTextureCreateView(texture, &view_desc);
}

static bool flecsIblCreateEquirectTexture(
    const FlecsEngineImpl *engine,
    FlecsHdriImpl *ibl,
    const FlecsHdriImage *image)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        .dimension = WGPUTextureDimension_2D,
        .size = (WGPUExtent3D){
            .width = (uint32_t)image->width,
            .height = (uint32_t)image->height,
            .depthOrArrayLayers = 1
        },
        .format = WGPUTextureFormat_RGBA16Float,
        .mipLevelCount = 1,
        .sampleCount = 1
    };

    ibl->ibl_equirect_texture = wgpuDeviceCreateTexture(engine->device, &desc);
    if (!ibl->ibl_equirect_texture) {
        return false;
    }

    ibl->ibl_equirect_texture_view = flecsIblCreateTexture2DView(
        ibl->ibl_equirect_texture,
        WGPUTextureFormat_RGBA16Float);
    if (!ibl->ibl_equirect_texture_view) {
        return false;
    }

    size_t pixel_count = (size_t)image->width * (size_t)image->height;
    size_t texel_count = pixel_count * 4u;
    uint16_t *pixels_rgba16 = malloc(texel_count * sizeof(uint16_t));
    if (!pixels_rgba16) {
        return false;
    }

    for (size_t i = 0; i < texel_count; i ++) {
        pixels_rgba16[i] = flecsIblFloatToHalf(image->pixels_rgba32f[i]);
    }

    WGPUTexelCopyTextureInfo destination = {
        .texture = ibl->ibl_equirect_texture,
        .mipLevel = 0,
        .origin = {0, 0, 0},
        .aspect = WGPUTextureAspect_All
    };

    WGPUTexelCopyBufferLayout layout = {
        .offset = 0,
        .bytesPerRow = (uint32_t)image->width * sizeof(uint16_t) * 4u,
        .rowsPerImage = (uint32_t)image->height
    };

    WGPUExtent3D size = {
        .width = (uint32_t)image->width,
        .height = (uint32_t)image->height,
        .depthOrArrayLayers = 1
    };

    wgpuQueueWriteTexture(
        engine->queue,
        &destination,
        pixels_rgba16,
        texel_count * sizeof(uint16_t),
        &layout,
        &size);

    free(pixels_rgba16);
    return true;
}

bool flecsIblCreateCubeTexture(
    const FlecsEngineImpl *engine,
    uint32_t size,
    uint32_t mip_count,
    WGPUTextureFormat format,
    WGPUTexture *out_texture,
    WGPUTextureView *out_cube_view)
{
    WGPUTextureDescriptor desc = {
        .usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment,
        .dimension = WGPUTextureDimension_2D,
        .size = (WGPUExtent3D){
            .width = size,
            .height = size,
            .depthOrArrayLayers = 6
        },
        .format = format,
        .mipLevelCount = mip_count,
        .sampleCount = 1
    };

    *out_texture = wgpuDeviceCreateTexture(engine->device, &desc);
    if (!*out_texture) {
        return false;
    }

    *out_cube_view = flecsIblCreateCubeView(*out_texture, format, mip_count);
    if (!*out_cube_view) {
        return false;
    }

    return true;
}

bool flecsIblCreateSampler(
    const FlecsEngineImpl *engine,
    FlecsHdriImpl *ibl)
{
    WGPUSamplerDescriptor sampler_desc = {
        .addressModeU = WGPUAddressMode_ClampToEdge,
        .addressModeV = WGPUAddressMode_ClampToEdge,
        .addressModeW = WGPUAddressMode_ClampToEdge,
        .magFilter = WGPUFilterMode_Linear,
        .minFilter = WGPUFilterMode_Linear,
        .mipmapFilter = WGPUMipmapFilterMode_Linear,
        .lodMinClamp = 0.0f,
        .lodMaxClamp = (float)ibl->ibl_prefilter_mip_count,
        .maxAnisotropy = 1
    };

    ibl->ibl_sampler = wgpuDeviceCreateSampler(engine->device, &sampler_desc);
    return ibl->ibl_sampler != NULL;
}

static bool flecsIblDrawFullscreenPass(
    WGPUCommandEncoder encoder,
    WGPUTextureView target_view,
    WGPURenderPipeline pipeline,
    WGPUBindGroup bind_group)
{
    return flecsEngine_fullscreenPass(
        encoder, target_view, WGPULoadOp_Clear, (WGPUColor){0},
        pipeline, bind_group, NULL, NULL, NULL);
}

static bool flecsIblSubmitFullscreenPass(
    const FlecsEngineImpl *engine,
    WGPUTextureView target_view,
    WGPURenderPipeline pipeline,
    WGPUBindGroup bind_group)
{
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
        engine->device, &(WGPUCommandEncoderDescriptor){0});
    if (!encoder) {
        return false;
    }

    if (!flecsIblDrawFullscreenPass(encoder, target_view, pipeline, bind_group)) {
        wgpuCommandEncoderRelease(encoder);
        return false;
    }

    WGPUCommandBuffer command_buffer = wgpuCommandEncoderFinish(
        encoder, &(WGPUCommandBufferDescriptor){0});
    wgpuCommandEncoderRelease(encoder);
    if (!command_buffer) {
        return false;
    }

    wgpuQueueSubmit(engine->queue, 1, &command_buffer);
    wgpuCommandBufferRelease(command_buffer);
    return true;
}

bool flecsIblRunPreprocessPasses(
    ecs_world_t *world,
    const FlecsEngineImpl *engine,
    FlecsHdriImpl *ibl,
    WGPUCommandEncoder encoder,
    uint32_t filter_sample_count,
    uint32_t lut_sample_count)
{
    bool result = false;
    WGPUShaderModule prefilter_shader = NULL;
    WGPUShaderModule irradiance_shader = NULL;
    WGPUShaderModule brdf_shader = NULL;
    WGPUBindGroupLayout prefilter_bind_layout = NULL;
    WGPUBindGroupLayout brdf_bind_layout = NULL;
    WGPUBindGroup prefilter_bind_group = NULL;
    WGPUBindGroup brdf_bind_group = NULL;
    WGPUBuffer prefilter_uniform_buffer = NULL;
    WGPUBuffer brdf_uniform_buffer = NULL;
    WGPURenderPipeline prefilter_pipeline = NULL;
    WGPURenderPipeline irradiance_pipeline = NULL;
    WGPURenderPipeline brdf_pipeline = NULL;

    prefilter_shader = flecsEngine_shader_ensureModule(
        world, "IblPrefilterShader", kPrefilterShaderSource);
    irradiance_shader = flecsEngine_shader_ensureModule(
        world, "IblIrradianceShader", kIrradianceShaderSource);
    brdf_shader = flecsEngine_shader_ensureModule(
        world, "IblBrdfLutShader", kBrdfLutShaderSource);
    if (!prefilter_shader || !irradiance_shader || !brdf_shader) {
        goto cleanup;
    }

    WGPUBindGroupLayoutEntry prefilter_layout_entries[3] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            }
        },
        {
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = {
                .type = WGPUSamplerBindingType_Filtering
            }
        },
        {
            .binding = 2,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(FlecsIblFaceUniform)
            }
        }
    };

    prefilter_bind_layout = wgpuDeviceCreateBindGroupLayout(
        engine->device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = 3,
            .entries = prefilter_layout_entries
        });
    if (!prefilter_bind_layout) {
        goto cleanup;
    }

    prefilter_uniform_buffer = flecsEngine_createUniformBuffer(
        engine->device, sizeof(FlecsIblFaceUniform));
    if (!prefilter_uniform_buffer) {
        goto cleanup;
    }

    prefilter_bind_group = wgpuDeviceCreateBindGroup(
        engine->device,
        &(WGPUBindGroupDescriptor){
            .layout = prefilter_bind_layout,
            .entryCount = 3,
            .entries = (WGPUBindGroupEntry[3]){
                {
                    .binding = 0,
                    .textureView = ibl->ibl_equirect_texture_view
                },
                {
                    .binding = 1,
                    .sampler = ibl->ibl_sampler
                },
                {
                    .binding = 2,
                    .buffer = prefilter_uniform_buffer,
                    .size = sizeof(FlecsIblFaceUniform)
                }
            }
        });
    if (!prefilter_bind_group) {
        goto cleanup;
    }

    WGPUBindGroupLayoutEntry brdf_layout_entries[1] = {
        {
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .buffer = {
                .type = WGPUBufferBindingType_Uniform,
                .minBindingSize = sizeof(FlecsIblBrdfUniform)
            }
        }
    };

    brdf_bind_layout = wgpuDeviceCreateBindGroupLayout(
        engine->device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = 1,
            .entries = brdf_layout_entries
        });
    if (!brdf_bind_layout) {
        goto cleanup;
    }

    brdf_uniform_buffer = flecsEngine_createUniformBuffer(
        engine->device, sizeof(FlecsIblBrdfUniform));
    if (!brdf_uniform_buffer) {
        goto cleanup;
    }

    FlecsIblBrdfUniform brdf_uniform = {
        .sample_count = lut_sample_count,
        ._padding0 = 0u,
        ._padding1 = 0u,
        ._padding2 = 0u
    };
    wgpuQueueWriteBuffer(
        engine->queue,
        brdf_uniform_buffer,
        0,
        &brdf_uniform,
        sizeof(brdf_uniform));

    brdf_bind_group = wgpuDeviceCreateBindGroup(
        engine->device,
        &(WGPUBindGroupDescriptor){
            .layout = brdf_bind_layout,
            .entryCount = 1,
            .entries = (WGPUBindGroupEntry[1]){
                {
                    .binding = 0,
                    .buffer = brdf_uniform_buffer,
                    .size = sizeof(FlecsIblBrdfUniform)
                }
            }
        });
    if (!brdf_bind_group) {
        goto cleanup;
    }

    prefilter_pipeline = flecsIblCreatePipeline(
        engine, prefilter_shader, prefilter_bind_layout,
        "fs_main", WGPUTextureFormat_RGBA16Float);
    irradiance_pipeline = flecsIblCreatePipeline(
        engine, irradiance_shader, prefilter_bind_layout,
        "fs_main", WGPUTextureFormat_RGBA16Float);
    brdf_pipeline = flecsIblCreatePipeline(
        engine, brdf_shader, brdf_bind_layout,
        "fs_main", WGPUTextureFormat_RG16Float);
    if (!prefilter_pipeline || !irradiance_pipeline || !brdf_pipeline) {
        goto cleanup;
    }

    for (uint32_t mip = 0; mip < ibl->ibl_prefilter_mip_count; mip ++) {
        float roughness = ibl->ibl_prefilter_mip_count > 1u
            ? (float)mip / (float)(ibl->ibl_prefilter_mip_count - 1u)
            : 0.0f;

        for (uint32_t face = 0; face < 6u; face ++) {
            uint32_t face_size_u = FLECS_ENGINE_IBL_ENV_SIZE >> mip;
            if (!face_size_u) {
                face_size_u = 1u;
            }
            FlecsIblFaceUniform uniform = {
                .face_index = (float)face,
                .roughness = roughness,
                .face_size = (float)face_size_u,
                .sample_count = (float)filter_sample_count
            };
            wgpuQueueWriteBuffer(
                engine->queue,
                prefilter_uniform_buffer,
                0,
                &uniform,
                sizeof(uniform));

            WGPUTextureView face_view = flecsIblCreateCubeFaceView(
                ibl->ibl_prefiltered_cubemap,
                WGPUTextureFormat_RGBA16Float,
                mip,
                face);
            if (!face_view) {
                goto cleanup;
            }

            bool draw_ok = flecsIblDrawFullscreenPass(
                encoder,
                face_view,
                prefilter_pipeline,
                prefilter_bind_group);
            wgpuTextureViewRelease(face_view);
            if (!draw_ok) {
                goto cleanup;
            }
        }
    }

    /* Render the Lambertian-irradiance cubemap (single mip). */
    for (uint32_t face = 0; face < 6u; face ++) {
        FlecsIblFaceUniform uniform = {
            .face_index = (float)face,
            .roughness = 0.0f,
            .face_size = (float)FLECS_ENGINE_IBL_IRRADIANCE_SIZE,
            .sample_count = 0.0f
        };
        wgpuQueueWriteBuffer(
            engine->queue,
            prefilter_uniform_buffer,
            0,
            &uniform,
            sizeof(uniform));

        WGPUTextureView face_view = flecsIblCreateCubeFaceView(
            ibl->ibl_irradiance_cubemap,
            WGPUTextureFormat_RGBA16Float,
            0,
            face);
        if (!face_view) {
            goto cleanup;
        }

        bool draw_ok = flecsIblDrawFullscreenPass(
            encoder,
            face_view,
            irradiance_pipeline,
            prefilter_bind_group);
        wgpuTextureViewRelease(face_view);
        if (!draw_ok) {
            goto cleanup;
        }
    }

    result = true;

cleanup:
    if (brdf_pipeline) {
        wgpuRenderPipelineRelease(brdf_pipeline);
    }
    if (irradiance_pipeline) {
        wgpuRenderPipelineRelease(irradiance_pipeline);
    }
    if (prefilter_pipeline) {
        wgpuRenderPipelineRelease(prefilter_pipeline);
    }
    if (prefilter_bind_group) {
        wgpuBindGroupRelease(prefilter_bind_group);
    }
    if (brdf_bind_group) {
        wgpuBindGroupRelease(brdf_bind_group);
    }
    if (prefilter_uniform_buffer) {
        wgpuBufferRelease(prefilter_uniform_buffer);
    }
    if (brdf_uniform_buffer) {
        wgpuBufferRelease(brdf_uniform_buffer);
    }
    if (prefilter_bind_layout) {
        wgpuBindGroupLayoutRelease(prefilter_bind_layout);
    }
    if (brdf_bind_layout) {
        wgpuBindGroupLayoutRelease(brdf_bind_layout);
    }
    return result;
}

bool flecsEngine_ibl_initResources(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsHdriImpl *ibl,
    const char *hdri_path,
    uint32_t filter_sample_count,
    uint32_t lut_sample_count)
{
    flecsEngine_ibl_releaseRuntimeResources(ibl);

    if (!flecsEngine_globals_ensureBindLayout(engine)) {
        return false;
    }

    ibl->ibl_prefilter_mip_count = flecsIblComputeMipCount(
        FLECS_ENGINE_IBL_ENV_SIZE);

    FlecsHdriImage image = {0};
    if (hdri_path && hdri_path[0]) {
        if (!flecsHdriLoad(hdri_path, &image)) {
            ecs_warn(
                "failed to load HDRI '%s', falling back to black IBL",
                hdri_path);
        } else {
            flecsIblLogImageStats(&image, hdri_path);
        }
    }

    if (!image.pixels_rgba32f) {
        /* No HDRI — build a single black pixel so the IBL pipeline has a
         * valid input. Views should provide either an HDRI or an atmosphere
         * for meaningful lighting. */
        image.width = 1;
        image.height = 1;
        image.pixels_rgba32f = malloc(sizeof(float) * 4);
        if (!image.pixels_rgba32f) {
            return false;
        }
        image.pixels_rgba32f[0] = 0.0f;
        image.pixels_rgba32f[1] = 0.0f;
        image.pixels_rgba32f[2] = 0.0f;
        image.pixels_rgba32f[3] = 1.0f;
    }

    bool ok = false;
    if (!flecsIblCreateEquirectTexture(engine, ibl, &image)) {
        goto done;
    }

    if (!flecsIblCreateCubeTexture(
        engine,
        FLECS_ENGINE_IBL_ENV_SIZE,
        ibl->ibl_prefilter_mip_count,
        WGPUTextureFormat_RGBA16Float,
        &ibl->ibl_prefiltered_cubemap,
        &ibl->ibl_prefiltered_cubemap_view))
    {
        goto done;
    }

    if (!flecsIblCreateCubeTexture(
        engine,
        FLECS_ENGINE_IBL_IRRADIANCE_SIZE,
        1u,
        WGPUTextureFormat_RGBA16Float,
        &ibl->ibl_irradiance_cubemap,
        &ibl->ibl_irradiance_cubemap_view))
    {
        goto done;
    }

    if (!flecsIblCreateSampler(engine, ibl)) {
        goto done;
    }

    {
        WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(
            engine->device, &(WGPUCommandEncoderDescriptor){0});
        if (!enc) goto done;
        bool ok_pp = flecsIblRunPreprocessPasses(
            world, engine, ibl, enc, filter_sample_count, lut_sample_count);
        WGPUCommandBuffer cb = wgpuCommandEncoderFinish(
            enc, &(WGPUCommandBufferDescriptor){0});
        wgpuCommandEncoderRelease(enc);
        if (!ok_pp || !cb) {
            if (cb) wgpuCommandBufferRelease(cb);
            goto done;
        }
        wgpuQueueSubmit(engine->queue, 1, &cb);
        wgpuCommandBufferRelease(cb);
    }

    /* Bump the scene bind version so every view rebuilds its group 0
     * with the new IBL cubemap on next render. */
    engine->scene_bind_version ++;

    ok = true;

done:
    flecsHdriImageFree(&image);
    if (!ok) {
        flecsEngine_ibl_releaseRuntimeResources(ibl);
    }
    return ok;
}
