#include "renderer.h"
#include "../../tracy_hooks.h"
#include "flecs_engine.h"

static WGPURenderPipeline flecsEngine_renderEffect_createPipeline(
    const FlecsEngineImpl *engine,
    const FlecsShader *shader,
    const FlecsShaderImpl *shader_impl,
    WGPUBindGroupLayout bind_layout,
    WGPUTextureFormat color_format);

ECS_COMPONENT_DECLARE(FlecsRenderEffectKind);
ECS_COMPONENT_DECLARE(FlecsRenderEffectImpl);

ECS_DTOR(FlecsRenderEffectKind, ptr, {
    if (ptr->ctx && ptr->free_ctx) {
        ptr->free_ctx(ptr->ctx);
    }
})

ECS_MOVE(FlecsRenderEffectKind, dst, src, {
    if (dst->ctx && dst->free_ctx) {
        dst->free_ctx(dst->ctx);
    }
    *dst = *src;
    ecs_os_zeromem(src);
})

static void flecsEngine_renderEffect_release(
    FlecsRenderEffectImpl *ptr)
{
    flecsEngine_bindGroup_release(&ptr->bind_group);
    FLECS_WGPU_RELEASE(ptr->input_sampler, wgpuSamplerRelease);
    FLECS_WGPU_RELEASE(ptr->pipeline_surface, wgpuRenderPipelineRelease);
    FLECS_WGPU_RELEASE(ptr->pipeline_hdr, wgpuRenderPipelineRelease);
    FLECS_WGPU_RELEASE(ptr->bind_layout, wgpuBindGroupLayoutRelease);
}

ECS_DTOR(FlecsRenderEffectImpl, ptr, {
    flecsEngine_renderEffect_release(ptr);
})

ECS_MOVE(FlecsRenderEffectImpl, dst, src, {
    flecsEngine_renderEffect_release(dst);
    *dst = *src;
    ecs_os_zeromem(src);
})

static bool flecsEngine_renderEffect_createInputSampler(
    const FlecsEngineImpl *engine,
    FlecsRenderEffectImpl *impl)
{
    impl->input_sampler = flecsEngine_createLinearClampSampler(engine->device);
    return impl->input_sampler != NULL;
}

bool flecsEngine_renderEffect_render(
    const ecs_world_t *world,
    FlecsEngineImpl *engine,
    const FlecsRenderViewImpl *view_impl,
    WGPUCommandEncoder encoder,
    WGPUTextureView output_view,
    WGPULoadOp load_op,
    WGPUColor clear_value,
    ecs_entity_t effect_entity,
    const FlecsRenderEffectKind *kind,
    FlecsRenderEffectImpl *impl,
    WGPUTextureView input_view,
    WGPUTextureFormat output_format,
    const char *ts_name,
    const WGPURenderPassTimestampWrites *ts_writes)
{
    ecs_assert(kind != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(impl != NULL, ECS_INVALID_PARAMETER, NULL);
    ecs_assert(input_view != NULL, ECS_INVALID_PARAMETER, NULL);

    WGPUBindGroupEntry entries[8] = {
        { .binding = 0, .textureView = input_view },
        { .binding = 1, .sampler = impl->input_sampler }
    };

    uint32_t entry_count = 2;
    if (kind->bind_callback) {
        bool bind_ok = kind->bind_callback(
            world,
            engine,
            view_impl,
            effect_entity,
            kind,
            impl,
            entries,
            &entry_count);
        ecs_assert(bind_ok, ECS_INTERNAL_ERROR, NULL);
    }
    ecs_assert(entry_count > 0, ECS_INTERNAL_ERROR, NULL);
    ecs_assert(entry_count <= 8, ECS_INTERNAL_ERROR, NULL);

    WGPUBindGroup bind_group = flecsEngine_bindGroup_ensure(
        &impl->bind_group, engine->device, impl->bind_layout,
        entries, entry_count);
    ecs_assert(bind_group != NULL, ECS_INTERNAL_ERROR, NULL);

    WGPURenderPipeline pipeline =
        output_format == flecsEngine_getViewTargetFormat(engine)
            ? impl->pipeline_surface
            : impl->pipeline_hdr;
    ecs_assert(pipeline != NULL, ECS_INTERNAL_ERROR, NULL);

    return flecsEngine_fullscreenPass(
        encoder, output_view, load_op, clear_value,
        pipeline, bind_group, engine, ts_name, ts_writes);
}

static int32_t flecsEngine_resolveEffectInput(
    const flecs_render_view_effect_t *effects,
    int32_t input)
{
    while (input > 0) {
        if (effects[input - 1].enabled) {
            return input;
        }
        input--;
    }
    return 0;
}

void flecsEngine_renderView_renderEffects(
    ecs_world_t *world,
    ecs_entity_t view_entity,
    FlecsEngineImpl *engine,
    const FlecsRenderView *view,
    FlecsRenderViewImpl *viewImpl,
    WGPUTextureView view_texture,
    WGPUCommandEncoder encoder)
{
    FLECS_TRACY_ZONE_BEGIN("RenderEffects");
    int32_t effect_count = ecs_vec_count(&view->effects);
    const flecs_render_view_effect_t *effects = NULL;

    /* Sync enabled flag to EcsDisabled tag on each effect entity, and find
     * the last enabled effect. */
    int32_t last_enabled = -1;
    if (effect_count > 0) {
        effects = ecs_vec_first(&view->effects);
        ecs_defer_suspend(world);
        for (int32_t i = 0; i < effect_count; i ++) {
            ecs_entity_t e = effects[i].effect;
            if (!e) continue;
            bool has_disabled = ecs_has_id(world, e, EcsDisabled);
            if (effects[i].enabled && has_disabled) {
                ecs_remove_id(world, e, EcsDisabled);
            } else if (!effects[i].enabled && !has_disabled) {
                ecs_add_id(world, e, EcsDisabled);
            }
        }
        ecs_defer_resume(world);
        for (int32_t i = effect_count - 1; i >= 0; i --) {
            if (effects[i].enabled) {
                last_enabled = i;
                break;
            }
        }
    }

    /* No effects enabled — blit batch output to screen via passthrough. */
    if (last_enabled < 0) {
        if (!viewImpl->passthrough_bind_group) {
            WGPUBindGroupEntry entries[2] = {
                { .binding = 0, .textureView = viewImpl->effect_target_views[0] },
                { .binding = 1, .sampler = engine->pipelines.passthrough_sampler }
            };
            viewImpl->passthrough_bind_group = wgpuDeviceCreateBindGroup(
                engine->device,
                &(WGPUBindGroupDescriptor){
                    .layout = engine->pipelines.passthrough_bind_layout,
                    .entryCount = 2,
                    .entries = entries
                });
        }

        flecsEngine_fullscreenPass(
            encoder, view_texture, WGPULoadOp_Clear, (WGPUColor){0, 0, 0, 1},
            engine->pipelines.passthrough_pipeline,
            viewImpl->passthrough_bind_group,
            engine, "Passthrough", NULL);
        FLECS_TRACY_ZONE_END;
        return;
    }

    const FlecsSurface *surface = ecs_get(world, engine->surface, FlecsSurface);
    bool needs_upscale = surface->resolution_scale > 1;

    for (int32_t i = 0; i < effect_count; i ++) {
        if (!effects[i].enabled) {
            continue;
        }

        ecs_entity_t entity = effects[i].effect;
        const FlecsRenderEffectKind *kind = ecs_get(
            world, entity, FlecsRenderEffectKind);
        FlecsRenderEffectImpl *effect_impl = ecs_get_mut(
            world, entity, FlecsRenderEffectImpl);

        ecs_assert(kind != NULL, ECS_INVALID_PARAMETER, NULL);
        ecs_assert(effect_impl != NULL, ECS_INVALID_PARAMETER, NULL);

        ecs_assert(effects[i].input >= 0, ECS_INVALID_PARAMETER, NULL);
        ecs_assert(effects[i].input <= i, ECS_INVALID_PARAMETER, NULL);

        const char *effect_name = ecs_get_name(world, entity);
        FLECS_TRACY_ZONE_BEGIN_DYN(effect_zone, "Effect", effect_name);

        bool is_last = (i == last_enabled);
        bool writes_to_final = is_last && !needs_upscale;
        WGPUTextureView output_view = writes_to_final
            ? view_texture
            : viewImpl->effect_target_views[i + 1];
        WGPUTextureFormat output_format = writes_to_final
            ? flecsEngine_getViewTargetFormat(engine)
            : viewImpl->effect_target_format;

        /* input == 0 means "previous effect's output" (or the batches
         * framebuffer for the first effect). Any explicit non-zero input
         * picks that specific point in the chain. */
        int32_t requested_input = effects[i].input ? effects[i].input : i;
        int32_t resolved_input = flecsEngine_resolveEffectInput(
            effects, requested_input);
        WGPUTextureView input_view =
            viewImpl->effect_target_views[resolved_input];
        WGPULoadOp load_op = writes_to_final ? WGPULoadOp_Load : WGPULoadOp_Clear;

        if (kind->render_callback) {
            bool render_ok = kind->render_callback(
                world,
                engine,
                viewImpl,
                encoder,
                entity,
                kind,
                effect_impl,
                input_view,
                viewImpl->effect_target_format,
                output_view,
                output_format,
                load_op);
            FLECS_TRACY_ZONE_END_N(effect_zone);
            if (!render_ok) {
                ecs_err("failed to render effect");
                FLECS_TRACY_ZONE_END;
                return;
            }
            continue;
        }

        flecsEngine_renderEffect_render(
            world, engine, viewImpl, encoder,
            output_view, load_op, (WGPUColor){0, 0, 0, 1},
            entity, kind, effect_impl,
            input_view, output_format,
            effect_name ? effect_name : "Effect", NULL);

        FLECS_TRACY_ZONE_END_N(effect_zone);
    }

    /* When upscaling is needed, passthrough blits from the last effect's
     * intermediate target to the final view texture at window resolution. */
    if (needs_upscale) {
        WGPUTextureView upscale_input =
            viewImpl->effect_target_views[last_enabled + 1];

        if (!viewImpl->upscale_bind_group ||
            viewImpl->upscale_bind_input_view != upscale_input)
        {
            FLECS_WGPU_RELEASE(viewImpl->upscale_bind_group, wgpuBindGroupRelease);
            WGPUBindGroupEntry entries[2] = {
                { .binding = 0, .textureView = upscale_input },
                { .binding = 1, .sampler = engine->pipelines.passthrough_sampler }
            };
            viewImpl->upscale_bind_group = wgpuDeviceCreateBindGroup(engine->device,
                &(WGPUBindGroupDescriptor){
                    .layout = engine->pipelines.passthrough_bind_layout,
                    .entryCount = 2,
                    .entries = entries
                });
            viewImpl->upscale_bind_input_view = upscale_input;
        }

        flecsEngine_fullscreenPass(
            encoder, view_texture, WGPULoadOp_Clear, (WGPUColor){0, 0, 0, 1},
            engine->pipelines.passthrough_pipeline,
            viewImpl->upscale_bind_group,
            engine, "Upscale", NULL);
    }
    FLECS_TRACY_ZONE_END;
}

static WGPURenderPipeline flecsEngine_renderEffect_createPipeline(
    const FlecsEngineImpl *engine,
    const FlecsShader *shader,
    const FlecsShaderImpl *shader_impl,
    WGPUBindGroupLayout bind_layout,
    WGPUTextureFormat color_format)
{
    WGPUColorTargetState color_target = {
        .format = color_format,
        .writeMask = WGPUColorWriteMask_All
    };

    return flecsEngine_createFullscreenPipeline(
        engine, shader_impl->shader_module, bind_layout,
        shader->vertex_entry, shader->fragment_entry,
        &color_target, NULL);
}

static void FlecsRenderEffectKind_on_set(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsRenderEffectKind *kinds = ecs_field(it, FlecsRenderEffectKind, 0);
    const FlecsEngineImpl *engine = ecs_singleton_get(world, FlecsEngineImpl);
    if (!engine) {
        ecs_err("cannot build render effects: engine is not initialized");
        return;
    }

    for (int32_t i = 0; i < it->count; i ++) {
        ecs_entity_t e = it->entities[i];

        const FlecsRenderEffectImpl *existing = ecs_get(
            world, e, FlecsRenderEffectImpl);
        if (existing &&
            existing->built_shader == kinds[i].shader &&
            existing->built_setup == (void*)kinds[i].setup_callback &&
            existing->built_bind == (void*)kinds[i].bind_callback)
        {
            continue;
        }

        FlecsRenderEffectImpl impl = {};

        if (!kinds[i].shader) {
            char *effect_name = ecs_get_path(world, e);
            ecs_err("missing shader asset for render effect %s", effect_name);
            ecs_os_free(effect_name);
            continue;
        }

        const FlecsShader *shader = ecs_get(world, kinds[i].shader, FlecsShader);
        if (!shader) {
            char *effect_name = ecs_get_path(world, e);
            char *shader_name = ecs_get_path(world, kinds[i].shader);
            ecs_err("invalid shader asset '%s' for render effect %s",
                shader_name, effect_name);
            ecs_os_free(shader_name);
            ecs_os_free(effect_name);
            continue;
        }

        const FlecsShaderImpl *shader_impl = flecsEngine_shader_ensureImpl(
            (ecs_world_t*)world, kinds[i].shader);
        if (!shader_impl || !shader_impl->shader_module) {
            char *effect_name = ecs_get_path(world, e);
            ecs_err("missing compiled shader for render effect %s", effect_name);
            ecs_os_free(effect_name);
            continue;
        }

        if (!flecsEngine_renderEffect_createInputSampler(engine, &impl)) {
            flecsEngine_renderEffect_release(&impl);
            continue;
        }

        WGPUBindGroupLayoutEntry layout_entries[8] = {0};
        uint32_t layout_entry_count = 2;

        layout_entries[0] = (WGPUBindGroupLayoutEntry){
            .binding = 0,
            .visibility = WGPUShaderStage_Fragment,
            .texture = {
                .sampleType = WGPUTextureSampleType_Float,
                .viewDimension = WGPUTextureViewDimension_2D,
                .multisampled = false
            }
        };

        layout_entries[1] = (WGPUBindGroupLayoutEntry){
            .binding = 1,
            .visibility = WGPUShaderStage_Fragment,
            .sampler = {
                .type = WGPUSamplerBindingType_Filtering
            }
        };

        if (kinds[i].setup_callback) {
            if (!kinds[i].setup_callback(
                world,
                engine,
                e,
                &kinds[i],
                &impl,
                layout_entries,
                &layout_entry_count))
            {
                flecsEngine_renderEffect_release(&impl);
                continue;
            }
        }

        if (layout_entry_count > 2 && !kinds[i].bind_callback) {
            char *effect_name = ecs_get_path(world, e);
            ecs_err(
                "render effect %s has custom setup bindings but no bind callback",
                effect_name);
            ecs_os_free(effect_name);
            flecsEngine_renderEffect_release(&impl);
            continue;
        }

        ecs_assert(layout_entry_count > 0, ECS_INTERNAL_ERROR, NULL);
        ecs_assert(layout_entry_count <= 8, ECS_INTERNAL_ERROR, NULL);

        WGPUBindGroupLayoutDescriptor bind_layout_desc = {
            .entries = layout_entries,
            .entryCount = layout_entry_count
        };

        impl.bind_layout = wgpuDeviceCreateBindGroupLayout(
            engine->device, &bind_layout_desc);
        if (!impl.bind_layout) {
            flecsEngine_renderEffect_release(&impl);
            continue;
        }

        impl.pipeline_surface = flecsEngine_renderEffect_createPipeline(
            engine,
            shader,
            shader_impl,
            impl.bind_layout,
            flecsEngine_getViewTargetFormat(engine));
        if (!impl.pipeline_surface) {
            flecsEngine_renderEffect_release(&impl);
            continue;
        }

        WGPUTextureFormat hdr_format = flecsEngine_getHdrFormat(engine);

        impl.pipeline_hdr = flecsEngine_renderEffect_createPipeline(
            engine,
            shader,
            shader_impl,
            impl.bind_layout,
            hdr_format);
        if (!impl.pipeline_hdr) {
            flecsEngine_renderEffect_release(&impl);
            continue;
        }

        impl.built_shader = kinds[i].shader;
        impl.built_setup = (void*)kinds[i].setup_callback;
        impl.built_bind = (void*)kinds[i].bind_callback;

        /* ecs_set_ptr on an existing impl memcpys over the old value
         * without invoking hooks; release it first or its GPU resources
         * leak. */
        FlecsRenderEffectImpl *old = ecs_get_mut(
            world, e, FlecsRenderEffectImpl);
        if (old) {
            flecsEngine_renderEffect_release(old);
            ecs_os_zeromem(old);
        }
        ecs_set_ptr(world, e, FlecsRenderEffectImpl, &impl);
    }
}

void flecsEngine_renderEffect_register(
    ecs_world_t *world)
{
    ECS_COMPONENT_DEFINE(world, FlecsRenderEffectKind);
    ECS_COMPONENT_DEFINE(world, FlecsRenderEffectImpl);

    ecs_set_hooks(world, FlecsRenderEffectKind, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsRenderEffectKind),
        .dtor = ecs_dtor(FlecsRenderEffectKind),
        .on_set = FlecsRenderEffectKind_on_set
    });

    ecs_set_hooks(world, FlecsRenderEffectImpl, {
        .ctor = flecs_default_ctor,
        .move = ecs_move(FlecsRenderEffectImpl),
        .dtor = ecs_dtor(FlecsRenderEffectImpl)
    });
}
