#include "../../renderer.h"
#include "../../../../tracy_hooks.h"
#include "flecs_engine.h"

static const char *kShaderSource =
    FLECS_ENGINE_FULLSCREEN_VS_WGSL
    "@group(0) @binding(0) var ms_depth : texture_depth_multisampled_2d;\n"
    "struct FragOut {\n"
    "  @builtin(frag_depth) depth : f32\n"
    "};\n"
    "@fragment fn fs_main(in : VertexOutput) -> FragOut {\n"
    "  let dims = textureDimensions(ms_depth);\n"
    "  let texel = vec2<i32>(in.uv * vec2<f32>(f32(dims.x), f32(dims.y)));\n"
    "  var out : FragOut;\n"
    "  out.depth = textureLoad(ms_depth, texel, 0);\n"
    "  return out;\n"
    "}\n";

int flecsEngine_initDepthResolve(
    ecs_world_t *world,
    FlecsEngineImpl *impl)
{
    WGPUShaderModule module = flecsEngine_shader_ensureModule(
        world, "DepthResolveShader", kShaderSource);
    if (!module) {
        return -1;
    }

    WGPUBindGroupLayoutEntry layout_entry = {
        .binding = 0,
        .visibility = WGPUShaderStage_Fragment,
        .texture = {
            .sampleType = WGPUTextureSampleType_Depth,
            .viewDimension = WGPUTextureViewDimension_2D,
            .multisampled = true
        }
    };

    impl->pipelines.depth_resolve_bind_layout = wgpuDeviceCreateBindGroupLayout(
        impl->device, &(WGPUBindGroupLayoutDescriptor){
            .entries = &layout_entry,
            .entryCount = 1
        });
    if (!impl->pipelines.depth_resolve_bind_layout) {
        return -1;
    }

    WGPUDepthStencilState depth_stencil = {
        .format = WGPUTextureFormat_Depth32Float,
        .depthWriteEnabled = true,
        .depthCompare = WGPUCompareFunction_Always
    };

    impl->pipelines.depth_resolve_pipeline = flecsEngine_createFullscreenPipeline(
        impl, module, impl->pipelines.depth_resolve_bind_layout,
        NULL, NULL, NULL, &depth_stencil);

    return impl->pipelines.depth_resolve_pipeline ? 0 : -1;
}

void flecsEngine_depthResolve(
    const FlecsEngineImpl *impl,
    FlecsRenderViewImpl *view_impl,
    WGPUCommandEncoder encoder)
{
    FLECS_TRACY_ZONE_BEGIN("DepthResolve");
    if (!impl->pipelines.depth_resolve_pipeline ||
        !view_impl->msaa_depth_texture_view ||
        !view_impl->depth_texture_view)
    {
        FLECS_TRACY_ZONE_END;
        return;
    }

    if (!view_impl->depth_resolve_bind_group ||
        view_impl->depth_resolve_bind_view != view_impl->msaa_depth_texture_view)
    {
        FLECS_WGPU_RELEASE(view_impl->depth_resolve_bind_group,
            wgpuBindGroupRelease);
        WGPUBindGroupEntry entry = {
            .binding = 0,
            .textureView = view_impl->msaa_depth_texture_view
        };
        view_impl->depth_resolve_bind_group = wgpuDeviceCreateBindGroup(
            impl->device, &(WGPUBindGroupDescriptor){
                .layout = impl->pipelines.depth_resolve_bind_layout,
                .entryCount = 1,
                .entries = &entry
            });
        if (!view_impl->depth_resolve_bind_group) {
            FLECS_TRACY_ZONE_END;
            return;
        }
        view_impl->depth_resolve_bind_view = view_impl->msaa_depth_texture_view;
    }
    WGPUBindGroup bind_group = view_impl->depth_resolve_bind_group;

    WGPURenderPassDepthStencilAttachment depth_attachment = {
        .view = view_impl->depth_texture_view,
        .depthLoadOp = WGPULoadOp_Clear,
        .depthStoreOp = WGPUStoreOp_Store,
        .depthClearValue = 1.0f,
        .depthReadOnly = false,
        .stencilLoadOp = WGPULoadOp_Undefined,
        .stencilStoreOp = WGPUStoreOp_Undefined,
        .stencilClearValue = 0,
        .stencilReadOnly = true
    };

    WGPURenderPassDescriptor pass_desc = {
        .colorAttachmentCount = 0,
        .colorAttachments = NULL,
        .depthStencilAttachment = &depth_attachment
    };

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
        encoder, &pass_desc);
    if (pass) {
        wgpuRenderPassEncoderSetPipeline(pass, impl->pipelines.depth_resolve_pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bind_group, 0, NULL);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    FLECS_TRACY_ZONE_END;
}
