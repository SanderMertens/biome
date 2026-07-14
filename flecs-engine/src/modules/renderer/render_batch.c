#include <math.h>
#include <string.h>

#include "renderer.h"
#include "batches/common/common.h"
#include "../../tracy_hooks.h"
#include "flecs_engine.h"

ECS_COMPONENT_DECLARE(FlecsRenderBatch);
ECS_COMPONENT_DECLARE(FlecsRenderBatchImpl);
ECS_COMPONENT_DECLARE(FlecsBufferSlot);

ECS_DTOR(FlecsRenderBatch, ptr, {
    if (ptr->ctx && ptr->free_ctx) {
        ptr->free_ctx(ptr->ctx);
    }
})

ECS_MOVE(FlecsRenderBatch, dst, src, {
    if (dst->ctx && dst->free_ctx) {
        dst->free_ctx(dst->ctx);
    }
    *dst = *src;
    ecs_os_zeromem(src);
})

static WGPUBindGroupLayout flecsEngine_renderBatch_ensureNoTextureBindLayout(
    FlecsEngineImpl *engine)
{
    if (engine->no_texture_bind_layout) {
        return engine->no_texture_bind_layout;
    }
    engine->no_texture_bind_layout = wgpuDeviceCreateBindGroupLayout(
        engine->device,
        &(WGPUBindGroupLayoutDescriptor){
            .entryCount = 0,
            .entries = NULL
        });
    return engine->no_texture_bind_layout;
}

static WGPUBindGroup flecsEngine_renderBatch_ensureNoTextureBindGroup(
    FlecsEngineImpl *engine)
{
    if (engine->no_texture_bind_group) {
        return engine->no_texture_bind_group;
    }
    WGPUBindGroupLayout layout =
        flecsEngine_renderBatch_ensureNoTextureBindLayout(engine);
    if (!layout) {
        return NULL;
    }
    engine->no_texture_bind_group = wgpuDeviceCreateBindGroup(
        engine->device,
        &(WGPUBindGroupDescriptor){
            .layout = layout,
            .entryCount = 0,
            .entries = NULL
        });
    return engine->no_texture_bind_group;
}

static void flecsEngine_renderBatch_releaseImpl(
    FlecsRenderBatchImpl *ptr)
{
    FLECS_WGPU_RELEASE(ptr->pipeline_hdr, wgpuRenderPipelineRelease);
    FLECS_WGPU_RELEASE(ptr->pipeline_shadow, wgpuRenderPipelineRelease);
}

ECS_DTOR(FlecsRenderBatchImpl, ptr, {
    flecsEngine_renderBatch_releaseImpl(ptr);
})

ECS_MOVE(FlecsRenderBatchImpl, dst, src, {
    flecsEngine_renderBatch_releaseImpl(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

static int32_t flecsEngine_renderBatch_setupInstanceBindings(
    int32_t location_offset,
    WGPUVertexBufferLayout *vertex_buffers,
    int32_t vertex_buffer_count,
    WGPUVertexAttribute *slot_attr)
{
    *slot_attr = (WGPUVertexAttribute){
        .format = WGPUVertexFormat_Uint32,
        .shaderLocation = (uint32_t)location_offset,
        .offset = 0
    };

    vertex_buffers[vertex_buffer_count ++] = (WGPUVertexBufferLayout){
        .arrayStride = sizeof(uint32_t),
        .stepMode = WGPUVertexStepMode_Instance,
        .attributeCount = 1,
        .attributes = slot_attr
    };

    return vertex_buffer_count;
}

static WGPURenderPipeline flecsEngine_renderBatch_createShadowPipeline(
    const FlecsEngineImpl *engine,
    const WGPUVertexBufferLayout *vertex_buffers,
    uint32_t vertex_buffer_count)
{
    if (!engine->shadow.shader_module || !engine->shadow.pass_bind_layout) {
        return NULL;
    }

    WGPUBindGroupLayout inst_layout =
        flecsEngine_instanceBind_ensureLayout((FlecsEngineImpl*)engine);

    WGPUBindGroupLayout layouts[2] = {
        engine->shadow.pass_bind_layout,
        inst_layout
    };

    WGPUPipelineLayoutDescriptor layout_desc = {
        .bindGroupLayoutCount = 2,
        .bindGroupLayouts = layouts
    };

    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
        engine->device, &layout_desc);
    if (!pipeline_layout) {
        return NULL;
    }

    WGPUDepthStencilState depth_state = {
        .format = WGPUTextureFormat_Depth32Float,
        .depthWriteEnabled = WGPUOptionalBool_True,
        .depthCompare = WGPUCompareFunction_Less,
        .depthBias = 2,
        .depthBiasSlopeScale = 2.0f,
        .depthBiasClamp = 0.01f,
        .stencilReadMask = 0xFFFFFFFF,
        .stencilWriteMask = 0xFFFFFFFF
    };

    WGPUVertexState vertex_state = {
        .module = engine->shadow.shader_module,
        .entryPoint = WGPU_STR("vs_main"),
        .bufferCount = vertex_buffer_count,
        .buffers = vertex_buffers
    };

    WGPURenderPipelineDescriptor pipeline_desc = {
        .layout = pipeline_layout,
        .vertex = vertex_state,
        .fragment = NULL,
        .depthStencil = &depth_state,
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .cullMode = WGPUCullMode_None,
            .frontFace = WGPUFrontFace_CCW
        },
        .multisample = WGPU_MULTISAMPLE_DEFAULT
    };

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(
        engine->device, &pipeline_desc);
    wgpuPipelineLayoutRelease(pipeline_layout);

    return pipeline;
}

static WGPURenderPipeline flecsEngine_renderBatch_createPipeline(
    const FlecsEngineImpl *engine,
    const FlecsShader *shader,
    const FlecsShaderImpl *shader_impl,
    bool use_textures,
    bool use_material_buffer,
    bool use_instance_buffer,
    const WGPUBlendState *blend,
    WGPUCullMode cull_mode,
    WGPUCompareFunction depth_test,
    bool depth_write,
    const WGPUVertexBufferLayout *vertex_buffers,
    uint32_t vertex_buffer_count,
    WGPUTextureFormat color_format,
    uint32_t sample_count)
{
    if (!engine->scene_bind_layout) {
        return NULL;
    }

    WGPUBindGroupLayout no_texture_layout =
        flecsEngine_renderBatch_ensureNoTextureBindLayout((FlecsEngineImpl*)engine);

    WGPUBindGroupLayout bind_layouts[4] = {
        engine->scene_bind_layout,
        no_texture_layout,
        NULL,
        NULL
    };
    uint32_t bind_layout_count = 2u;

    if (use_textures) {
        WGPUBindGroupLayout tex_layout =
            flecsEngine_textures_ensureBindLayout((FlecsEngineImpl*)engine);
        if (tex_layout) {
            bind_layouts[1] = tex_layout;
        }
    }

    if (use_material_buffer) {
        WGPUBindGroupLayout mat_layout =
            flecsEngine_materialBind_ensureLayout((FlecsEngineImpl*)engine);
        if (mat_layout) {
            bind_layouts[2] = mat_layout;
            bind_layout_count = 3u;
        }
    }

    if (use_instance_buffer) {
        WGPUBindGroupLayout inst_layout =
            flecsEngine_instanceBind_ensureLayout((FlecsEngineImpl*)engine);
        if (inst_layout) {
            if (!bind_layouts[2]) {
                bind_layouts[2] =
                    flecsEngine_materialBind_ensureLayout((FlecsEngineImpl*)engine);
            }
            bind_layouts[3] = inst_layout;
            bind_layout_count = 4u;
        }
    }

    WGPUPipelineLayoutDescriptor pipeline_layout_desc = {
        .bindGroupLayoutCount = bind_layout_count,
        .bindGroupLayouts = bind_layouts
    };

    WGPUPipelineLayout pipeline_layout = wgpuDeviceCreatePipelineLayout(
        engine->device, &pipeline_layout_desc);
    if (!pipeline_layout) {
        return NULL;
    }

    WGPUColorTargetState color_target = {
        .format = color_format,
        .writeMask = WGPUColorWriteMask_All,
        .blend = blend
    };

    WGPUDepthStencilState depth_state = {
        .format = WGPUTextureFormat_Depth32Float,
        .depthWriteEnabled = depth_write
            ? WGPUOptionalBool_True : WGPUOptionalBool_False,
        .depthCompare = depth_test,
        .stencilReadMask = 0xFFFFFFFF,
        .stencilWriteMask = 0xFFFFFFFF
    };


    WGPUVertexState vertex_state = {
        .module = shader_impl->shader_module,
        .entryPoint = WGPU_STR(shader->vertex_entry ? shader->vertex_entry : "vs_main"),
        .bufferCount = vertex_buffer_count,
        .buffers = vertex_buffers
    };

    WGPUFragmentState fragment_state = {
        .module = shader_impl->shader_module,
        .entryPoint = WGPU_STR(shader->fragment_entry ? shader->fragment_entry : "fs_main"),
        .targetCount = 1,
        .targets = &color_target
    };

    WGPURenderPipelineDescriptor pipeline_desc = {
        .layout = pipeline_layout,
        .vertex = vertex_state,
        .fragment = &fragment_state,
        .depthStencil = &depth_state,
        .primitive = {
            .topology = WGPUPrimitiveTopology_TriangleList,
            .cullMode = cull_mode,
            .frontFace = WGPUFrontFace_CCW
        },
        .multisample = WGPU_MULTISAMPLE(sample_count)
    };

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(
        engine->device, &pipeline_desc);
    wgpuPipelineLayoutRelease(pipeline_layout);

    return pipeline;
}

static void flecsEngine_renderBatch_logErr(
    const ecs_world_t *world,
    ecs_entity_t entity,
    const char *msg)
{
    char *name = ecs_get_path(world, entity);
    ecs_err(msg, name);
    ecs_os_free(name);
}

static bool flecsEngine_renderBatch_validate(
    const ecs_world_t *world,
    ecs_entity_t e,
    const FlecsRenderBatch *rb)
{
    if (!rb->vertex_type) {
        flecsEngine_renderBatch_logErr(world, e,
            "missing vertex type for render batch %s");
        return false;
    }
    if (!rb->shader) {
        flecsEngine_renderBatch_logErr(world, e,
            "missing shader asset for render batch %s");
        return false;
    }

    return true;
}

static void flecsEngine_renderBatch_setupShadowPipeline(
    ecs_world_t *world,
    const FlecsEngineImpl *engine,
    FlecsRenderBatchImpl *impl)
{
    if (!impl->uses_shadow || !engine->shadow.pass_bind_layout) {
        return;
    }

    WGPUVertexAttribute shadow_vert_attrs[16];
    WGPUVertexAttribute shadow_slot_attr = {0};
    WGPUVertexBufferLayout shadow_vbufs[2] = {0};

    int32_t sv_count = flecsEngine_vertexAttrFromType(
        world, ecs_id(FlecsGpuVertex), shadow_vert_attrs, 16, 0);

    shadow_vbufs[0] = (WGPUVertexBufferLayout){
        .arrayStride = flecsEngine_type_sizeof(world, ecs_id(FlecsGpuVertex)),
        .stepMode = WGPUVertexStepMode_Vertex,
        .attributeCount = sv_count,
        .attributes = shadow_vert_attrs
    };

    int32_t shadow_vbuf_count = flecsEngine_renderBatch_setupInstanceBindings(
        sv_count, shadow_vbufs, 1, &shadow_slot_attr);

    impl->pipeline_shadow = flecsEngine_renderBatch_createShadowPipeline(
        engine,
        shadow_vbufs,
        (uint32_t)shadow_vbuf_count);
}

static void FlecsRenderBatch_on_set(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsRenderBatch *rb = ecs_field(it, FlecsRenderBatch, 0);
    WGPUVertexAttribute slot_attr = {0};
    WGPUVertexAttribute color_attr = {0};
    WGPUVertexBufferLayout vertex_buffers[3] = {0};
    FlecsEngineImpl *engine = ecs_singleton_get_mut(world, FlecsEngineImpl);
    ecs_assert(engine != NULL, ECS_INTERNAL_ERROR, NULL);

    for (int i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];
        int32_t vertex_buffer_count = 0;
        FlecsRenderBatchImpl impl = {};
        
        if (!flecsEngine_renderBatch_validate(world, e, &rb[i])) {
            continue;
        }

        const FlecsShader *shader = ecs_get(world, rb[i].shader, FlecsShader);
        if (!shader) {
            flecsEngine_renderBatch_logErr(world, e,
                "invalid shader asset for render batch %s");
            continue;
        }

        const FlecsShaderImpl *shader_impl = flecsEngine_shader_ensureImpl(
            world, rb[i].shader);
        if (!shader_impl || !shader_impl->shader_module) {
            continue;
        }

        // Setup vertex attributes
        WGPUVertexAttribute vertex_attrs[16];
        int32_t vertex_attr_count = flecsEngine_vertexAttrFromType(
            world, rb[i].vertex_type, vertex_attrs, 16, 0);
        if (vertex_attr_count == -1) {
            continue;
        }

        vertex_buffers[0] = (WGPUVertexBufferLayout){
            .arrayStride = flecsEngine_type_sizeof(world, rb[i].vertex_type),
            .stepMode = WGPUVertexStepMode_Vertex,
            .attributeCount = vertex_attr_count,
            .attributes = vertex_attrs
        };
        vertex_buffer_count ++;

        if (shader_impl->uses_instance_buffer) {
            vertex_buffer_count = flecsEngine_renderBatch_setupInstanceBindings(
                vertex_attr_count,
                vertex_buffers, vertex_buffer_count, &slot_attr);
        }

        /* Vertex-color batches bind the mesh's per-vertex color buffer as an
         * extra vertex buffer, at the location after the instance slot. */
        flecsEngine_batch_t *cull_buf = rb[i].get_cull_buf
            ? rb[i].get_cull_buf(&rb[i]) : NULL;
        if (cull_buf && (cull_buf->flags & FLECS_BATCH_VERTEX_COLORS)) {
            color_attr = (WGPUVertexAttribute){
                .format = WGPUVertexFormat_Unorm8x4,
                .shaderLocation = (uint32_t)(vertex_attr_count +
                    (shader_impl->uses_instance_buffer ? 1 : 0)),
                .offset = 0
            };
            vertex_buffers[vertex_buffer_count ++] = (WGPUVertexBufferLayout){
                .arrayStride = sizeof(flecs_rgba_t),
                .stepMode = WGPUVertexStepMode_Vertex,
                .attributeCount = 1,
                .attributes = &color_attr
            };
        }

        impl.uses_ibl = shader_impl->uses_ibl;
        impl.uses_shadow = shader_impl->uses_shadow;
        impl.uses_cluster = shader_impl->uses_cluster;
        impl.uses_textures = shader_impl->uses_textures;
        impl.uses_material_buffer = shader_impl->uses_material_buffer;
        impl.uses_instance_buffer = shader_impl->uses_instance_buffer;

        if (rb[i].get_cull_buf) {
            flecsEngine_batch_t *cb = rb[i].get_cull_buf(&rb[i]);
            if (cb) {
                cb->uses_textures = shader_impl->uses_textures;
            }
        }
        bool has_blend = rb[i].blend.color.operation != 0;
        const WGPUBlendState *blend = has_blend ? &rb[i].blend : NULL;
        WGPUCullMode cull_mode = rb[i].cull_mode;
        if (!cull_mode) {
            cull_mode = WGPUCullMode_Back;
        }

        WGPUCompareFunction depth_test = rb[i].depth_test;
        bool depth_write = rb[i].depth_write;
        if (!depth_test) {
            depth_test = WGPUCompareFunction_LessEqual;
            depth_write = true;
        }

        if (!flecsEngine_globals_ensureBindLayout(engine)) {
            flecsEngine_renderBatch_logErr(world, e,
                "failed to create render batch '%s': "
                "scene bind layout is not available");
            flecsEngine_renderBatch_releaseImpl(&impl);
            continue;
        }

        WGPUTextureFormat hdr_format = flecsEngine_getHdrFormat(engine);

        const FlecsSurface *surface = ecs_get(
            world, engine->surface, FlecsSurface);
        uint32_t sample_count = (uint32_t)flecsEngine_surface_sampleCount(surface);

        impl.pipeline_hdr = flecsEngine_renderBatch_createPipeline(
            engine,
            shader,
            shader_impl,
            impl.uses_textures,
            impl.uses_material_buffer,
            impl.uses_instance_buffer,
            blend,
            cull_mode,
            depth_test,
            depth_write,
            vertex_buffers,
            (uint32_t)vertex_buffer_count,
            hdr_format,
            sample_count);
        if (!impl.pipeline_hdr) {
            flecsEngine_renderBatch_releaseImpl(&impl);
            continue;
        }

        if (!has_blend) {
            flecsEngine_renderBatch_setupShadowPipeline(world, engine, &impl);
        }

        ecs_set_ptr(world, e, FlecsRenderBatchImpl, &impl);
    }
}

void flecsEngine_renderBatch_render(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    const FlecsRenderView *view,
    ecs_entity_t batch_entity)
{
    FLECS_TRACY_ZONE_BEGIN("BatchRender");
    const FlecsRenderBatch *batch = ecs_get(
        world, batch_entity, FlecsRenderBatch);
    FlecsRenderBatchImpl *impl = ecs_get_mut(
        world, batch_entity, FlecsRenderBatchImpl);
    if (!batch || !impl) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    WGPURenderPipeline pipeline = impl->pipeline_hdr;
    ecs_assert(pipeline != NULL, ECS_INTERNAL_ERROR, NULL);

    if (pipeline != view_impl->last_pipeline) {
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        view_impl->last_pipeline = pipeline;
    }

    if (impl->uses_textures) {
        if (!engine->textures.fallback_bind_group) {
            FLECS_TRACY_ZONE_END;
            return;
        }
        wgpuRenderPassEncoderSetBindGroup(pass, 1,
            engine->textures.fallback_bind_group, 0, NULL);
    } else {
        WGPUBindGroup no_texture = flecsEngine_renderBatch_ensureNoTextureBindGroup(
            (FlecsEngineImpl*)engine);
        if (no_texture) {
            wgpuRenderPassEncoderSetBindGroup(pass, 1, no_texture, 0, NULL);
        }
    }

    flecsEngine_renderView_ensureSceneBindGroup(world, engine, view_impl, view);
    wgpuRenderPassEncoderSetBindGroup(
        pass, 0, view_impl->scene_bind_group, 0, NULL);

    batch->callback(world, engine, view_impl, pass, batch);
    FLECS_TRACY_ZONE_END;
}

void flecsEngine_renderView_ensureSceneBindGroup(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    const FlecsRenderView *view)
{
    ecs_entity_t atmosphere = flecsEngine_renderView_atmosphere(world, view);
    ecs_entity_t hdri = atmosphere
        ? atmosphere
        : (view->hdri ? view->hdri : engine->fallback_hdri);

    FlecsHdriImpl *ibl = ecs_get_mut(world, hdri, FlecsHdriImpl);
    ecs_assert(ibl != NULL, ECS_INTERNAL_ERROR, NULL);

    if (!view_impl->scene_bind_group ||
        view_impl->scene_bind_hdri != hdri ||
        view_impl->scene_bind_version != engine->scene_bind_version)
    {
        flecsEngine_globals_createBindGroup(engine, view_impl, hdri, ibl);
    }
}

void flecsEngine_renderBatch_extract(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    ecs_entity_t batch_entity)
{
    FLECS_TRACY_ZONE_BEGIN("BatchExtract");
    const FlecsRenderBatch *batch = ecs_get(
        world, batch_entity, FlecsRenderBatch);
    if (!batch || !batch->extract_callback) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    batch->extract_callback(world, engine, view_impl, batch);
    FLECS_TRACY_ZONE_END;
}

void flecsEngine_renderBatch_upload(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    ecs_entity_t batch_entity)
{
    FLECS_TRACY_ZONE_BEGIN("BatchUploadCallback");
    const FlecsRenderBatch *batch = ecs_get(
        world, batch_entity, FlecsRenderBatch);
    if (!batch || !batch->upload_callback) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    batch->upload_callback(world, engine, view_impl, batch);
    FLECS_TRACY_ZONE_END;
}

void flecsEngine_renderBatch_renderShadow(
    ecs_world_t *world,
    FlecsEngineImpl *engine,
    FlecsRenderViewImpl *view_impl,
    const WGPURenderPassEncoder pass,
    ecs_entity_t batch_entity)
{
    FLECS_TRACY_ZONE_BEGIN("BatchShadow");
    const FlecsRenderBatch *batch = ecs_get(
        world, batch_entity, FlecsRenderBatch);
    const FlecsRenderBatchImpl *impl = ecs_get(
        world, batch_entity, FlecsRenderBatchImpl);
    if (!batch || !impl || !impl->pipeline_shadow || !impl->uses_shadow) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    WGPURenderPipeline pipeline = impl->pipeline_shadow;

    if (pipeline != view_impl->last_pipeline) {
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        view_impl->last_pipeline = pipeline;
    }

    wgpuRenderPassEncoderSetBindGroup(
        pass, 0,
        view_impl->shadow.pass_bind_groups[view_impl->shadow.current_cascade],
        0, NULL);

    if (batch->shadow_callback) {
        batch->shadow_callback(world, engine, view_impl, pass, batch);
    }
    FLECS_TRACY_ZONE_END;
}

void flecsEngine_bufferSlot_markChanged(
    ecs_world_t *world,
    ecs_entity_t entity)
{
    const FlecsBufferSlot *bs = ecs_get(world, entity, FlecsBufferSlot);
    if (!bs || !bs->group) {
        return;
    }
    flecsEngine_batch_group_t *group = bs->group;

    ecs_map_val_t *existing =
        ecs_map_get(&group->changed_set, (ecs_map_key_t)entity);
    if (existing) {
        return;
    }
    ecs_map_ensure(&group->changed_set, (ecs_map_key_t)entity);
    *ecs_vec_append_t(NULL, &group->changed, ecs_entity_t) = entity;
    *ecs_vec_append_t(NULL, &group->changed_slots, int32_t) = bs->slot;
}

/* Static entities are extracted into their GPU slot once; when their
 * material inputs change afterwards, mark the slot so the next extract
 * re-packs and re-uploads it. */
static void FlecsBufferSlot_material_changed(ecs_iter_t *it)
{
    for (int32_t i = 0; i < it->count; i ++) {
        flecsEngine_bufferSlot_markChanged(it->world, it->entities[i]);
    }
}

/* Removing a material override must also re-pack the slot so the
 * instance falls back to its shared material instead of keeping the
 * stale override on the GPU. */
static void FlecsBufferSlot_material_removed(ecs_iter_t *it)
{
    const ecs_world_t *world = ecs_get_world(it->world);
    if (ecs_should_quit(world) || ecs_is_fini(world)) {
        return;
    }
}

void flecsEngine_bufferSlot_release(
    ecs_world_t *world,
    ecs_entity_t entity,
    const FlecsBufferSlot *slot)
{
    const ecs_world_t *real_world = ecs_get_world(world);
    if (ecs_should_quit(real_world) || ecs_is_fini(real_world)) {
        return;
    }

    flecsEngine_batch_group_t *group = slot->group;
    if (!group || !group->batch) {
        return;
    }
    int32_t slot_index = slot->slot;
    flecsEngine_batch_t *batch = group->batch;
    *ecs_vec_append_t(NULL, &batch->free_slots, int32_t) = slot_index;

    if (batch->static_buffers.cpu_slot_to_group &&
        slot_index < batch->static_buffers.capacity)
    {
        batch->static_buffers.cpu_slot_to_group[slot_index] =
            FLECS_ENGINE_BATCH_SLOT_FREE;
    }

    if (group->slot_count > 0) {
        group->slot_count --;
    }

    ecs_map_remove(&group->changed_set, (ecs_map_key_t)entity);
}

static void FlecsBufferSlot_on_remove(ecs_iter_t *it)
{
    FlecsBufferSlot *bs = ecs_field(it, FlecsBufferSlot, 0);
    for (int32_t i = 0; i < it->count; i ++) {
        flecsEngine_bufferSlot_release(
            it->world, it->entities[i], &bs[i]);
    }
}

void flecsEngine_renderBatch_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsRenderBatch);
    ECS_COMPONENT_DEFINE(world, FlecsRenderBatchImpl);
    ECS_COMPONENT_DEFINE(world, FlecsBufferSlot);

    ecs_set_hooks(world, FlecsRenderBatch, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsRenderBatch),
        .dtor = ecs_dtor(FlecsRenderBatch),
        .on_set = FlecsRenderBatch_on_set
    });

    ecs_set_hooks(world, FlecsRenderBatchImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsRenderBatchImpl),
        .dtor = ecs_dtor(FlecsRenderBatchImpl)
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "FlecsBufferSlotOnRemove" }),
        .query.terms = {{ ecs_id(FlecsBufferSlot) }},
        .events = { EcsOnRemove },
        .callback = FlecsBufferSlot_on_remove
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, { .name = "FlecsBufferSlotRgbaChanged" }),
        .query.terms = {
            { ecs_id(FlecsRgba) },
            { ecs_id(FlecsBufferSlot), .inout = EcsInOutNone }
        },
        .events = { EcsOnSet },
        .callback = FlecsBufferSlot_material_changed
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, {
            .name = "FlecsBufferSlotEmissiveChanged" }),
        .query.terms = {
            { ecs_id(FlecsEmissive) },
            { ecs_id(FlecsBufferSlot), .inout = EcsInOutNone }
        },
        .events = { EcsOnSet },
        .callback = FlecsBufferSlot_material_changed
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, {
            .name = "FlecsBufferSlotPbrChanged" }),
        .query.terms = {
            { ecs_id(FlecsPbrMaterial) },
            { ecs_id(FlecsBufferSlot), .inout = EcsInOutNone }
        },
        .events = { EcsOnSet },
        .callback = FlecsBufferSlot_material_changed
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, {
            .name = "FlecsBufferSlotTintChanged" }),
        .query.terms = {
            { ecs_id(FlecsActualTint) },
            { ecs_id(FlecsBufferSlot), .inout = EcsInOutNone }
        },
        .events = { EcsOnSet },
        .callback = FlecsBufferSlot_material_changed
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, {
            .name = "FlecsBufferSlotTransitionChanged" }),
        .query.terms = {
            { ecs_pair(ecs_id(FlecsTransitionValue), EcsWildcard) },
            { ecs_id(FlecsBufferSlot), .inout = EcsInOutNone }
        },
        .events = { EcsOnSet },
        .callback = FlecsBufferSlot_material_changed
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, {
            .name = "FlecsBufferSlotEmissiveRemoved" }),
        .query.terms = {
            { ecs_id(FlecsEmissive) },
            { ecs_id(FlecsBufferSlot), .inout = EcsInOutNone }
        },
        .events = { EcsOnRemove },
        .callback = FlecsBufferSlot_material_removed
    });

    ecs_observer(world, {
        .entity = ecs_entity(world, {
            .name = "FlecsBufferSlotPbrRemoved" }),
        .query.terms = {
            { ecs_id(FlecsPbrMaterial) },
            { ecs_id(FlecsBufferSlot), .inout = EcsInOutNone }
        },
        .events = { EcsOnRemove },
        .callback = FlecsBufferSlot_material_removed
    });
}
