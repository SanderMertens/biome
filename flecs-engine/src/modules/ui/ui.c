#define FLECS_ENGINE_UI_IMPL
#include "ui.h"

#include <math.h>
#include <stdio.h>

#define FLECS_UI_DEFAULT_TEXT_SIZE 16.0f

ECS_COMPONENT_DECLARE(FlecsUiEventListener);

typedef struct FlecsUiImpl {
    ecs_query_t *ui_query;
    ecs_query_t *hotkey_query;
    ecs_vec_t hits;    /* ecs_entity_t, hit-test candidates in draw order */
    ecs_vec_t roots;   /* ecs_entity_t scratch */
    ecs_entity_t hovered;
    ecs_entity_t active;
    ecs_entity_t rmb_active;
    bool mouse_captured;
} FlecsUiImpl;

ECS_COMPONENT_DECLARE(FlecsUiImpl);

ECS_CTOR(FlecsUiImpl, ptr, {
    ecs_os_zeromem(ptr);
    ecs_vec_init_t(NULL, &ptr->hits, ecs_entity_t, 0);
    ecs_vec_init_t(NULL, &ptr->roots, ecs_entity_t, 0);
})

ECS_DTOR(FlecsUiImpl, ptr, {
    ecs_vec_fini_t(NULL, &ptr->hits, ecs_entity_t);
    ecs_vec_fini_t(NULL, &ptr->roots, ecs_entity_t);
})

ECS_MOVE(FlecsUiImpl, dst, src, {
    ecs_vec_fini_t(NULL, &dst->hits, ecs_entity_t);
    ecs_vec_fini_t(NULL, &dst->roots, ecs_entity_t);
    *dst = *src;
    ecs_os_zeromem(src);
})

typedef struct {
    float w, h;
} flecs_ui_size_t;

static float flecs_ui_max(
    float a,
    float b)
{
    return a > b ? a : b;
}

static FlecsUiEventListener flecs_ui_listener(
    ecs_world_t *world,
    ecs_entity_t e)
{
    const FlecsUiEventListener *l = ecs_get(
        world, e, FlecsUiEventListener);
    return l ? *l : (FlecsUiEventListener){0};
}

static bool flecs_ui_isWidget(
    ecs_world_t *world,
    ecs_entity_t e)
{
    return ecs_has(world, e, FlecsUiWidgetState) ||
        ecs_has(world, e, FlecsUiEventListener);
}

static bool flecs_ui_isUiEntity(
    ecs_world_t *world,
    ecs_entity_t e)
{
    return ecs_has(world, e, FlecsUiRect) ||
        ecs_has(world, e, FlecsUiBorder) ||
        ecs_has(world, e, FlecsUiLine) ||
        ecs_has(world, e, FlecsUiText) ||
        ecs_has(world, e, FlecsUiPosition) ||
        ecs_has(world, e, FlecsUiLayout) ||
        ecs_has(world, e, FlecsUiAnchor) ||
        ecs_has(world, e, FlecsUiWidgetState) ||
        ecs_has(world, e, FlecsUiEventListener);
}

static void flecs_ui_collectChildren(
    ecs_world_t *world,
    ecs_entity_t e,
    ecs_vec_t *out)
{
    ecs_vec_clear(out);

    ecs_iter_t it = ecs_children(world, e);
    while (ecs_children_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            ecs_entity_t child = it.entities[i];
            if (ecs_has_id(world, child, EcsDisabled) ||
                ecs_has_id(world, child, EcsPrefab))
            {
                continue;
            }
            if (!flecs_ui_isUiEntity(world, child)) {
                continue;
            }
            *ecs_vec_append_t(NULL, out, ecs_entity_t) = child;
        }
    }
}

static flecs_ui_size_t flecs_ui_measure(
    ecs_world_t *world,
    ecs_entity_t e);

static flecs_ui_size_t flecs_ui_measureContent(
    ecs_world_t *world,
    ecs_entity_t e,
    const FlecsUiLayout *layout)
{
    ecs_vec_t children;
    ecs_vec_init_t(NULL, &children, ecs_entity_t, 0);
    flecs_ui_collectChildren(world, e, &children);

    int32_t count = ecs_vec_count(&children);
    ecs_entity_t *ids = ecs_vec_first_t(&children, ecs_entity_t);

    float w = 0, h = 0;
    for (int32_t i = 0; i < count; i ++) {
        flecs_ui_size_t cs = flecs_ui_measure(world, ids[i]);
        if (layout->direction == FlecsUiRow) {
            w += cs.w + (i ? layout->spacing : 0);
            h = flecs_ui_max(h, cs.h);
        } else if (layout->direction == FlecsUiColumn) {
            w = flecs_ui_max(w, cs.w);
            h += cs.h + (i ? layout->spacing : 0);
        } else {
            const FlecsUiPosition *pos = ecs_get(
                world, ids[i], FlecsUiPosition);
            float px = pos ? pos->x : 0;
            float py = pos ? pos->y : 0;
            w = flecs_ui_max(w, px + cs.w);
            h = flecs_ui_max(h, py + cs.h);
        }
    }

    ecs_vec_fini_t(NULL, &children, ecs_entity_t);

    return (flecs_ui_size_t){
        w + layout->padding * 2.0f,
        h + layout->padding * 2.0f
    };
}

static flecs_ui_size_t flecs_ui_measure(
    ecs_world_t *world,
    ecs_entity_t e)
{
    flecs_ui_size_t s = { 0, 0 };

    const FlecsUiRect *rect = ecs_get(world, e, FlecsUiRect);
    if (rect) {
        s.w = flecs_ui_max(s.w, rect->width);
        s.h = flecs_ui_max(s.h, rect->height);
    }

    const FlecsUiLine *line = ecs_get(world, e, FlecsUiLine);
    if (line) {
        s.w = flecs_ui_max(s.w, flecs_ui_max(line->x1, line->x2));
        s.h = flecs_ui_max(s.h, flecs_ui_max(line->y1, line->y2));
    }

    const FlecsUiText *text = ecs_get(world, e, FlecsUiText);
    if (text && text->text) {
        float size = text->size > 0 ? text->size : FLECS_UI_DEFAULT_TEXT_SIZE;
        s.w = flecs_ui_max(s.w,
            flecsEngine_ui2dTextWidth(world, size, text->text));
        s.h = flecs_ui_max(s.h, size);
    }

    const FlecsUiLayout *layout = ecs_get(world, e, FlecsUiLayout);
    if (layout) {
        if (layout->width > 0) {
            s.w = flecs_ui_max(s.w, layout->width);
        }
        if (layout->height > 0) {
            s.h = flecs_ui_max(s.h, layout->height);
        }
        if (layout->width <= 0 || layout->height <= 0) {
            flecs_ui_size_t content = flecs_ui_measureContent(
                world, e, layout);
            if (layout->width <= 0) {
                s.w = flecs_ui_max(s.w, content.w);
            }
            if (layout->height <= 0) {
                s.h = flecs_ui_max(s.h, content.h);
            }
        }
    }

    return s;
}

typedef struct {
    ecs_world_t *world;
    FlecsUiImpl *impl;
    bool draw;
} flecs_ui_walk_t;

static void flecs_ui_place(
    flecs_ui_walk_t *ctx,
    ecs_entity_t e,
    float x,
    float y,
    float slot_w)
{
    ecs_world_t *world = ctx->world;

    const FlecsUiPosition *pos = ecs_get(world, e, FlecsUiPosition);
    if (pos) {
        x += pos->x;
        y += pos->y;
    }

    flecs_ui_size_t size = flecs_ui_measure(world, e);

    const FlecsUiRect *rect = ecs_get(world, e, FlecsUiRect);
    const FlecsUiBorder *border = ecs_get(world, e, FlecsUiBorder);
    const FlecsUiLine *line = ecs_get(world, e, FlecsUiLine);
    const FlecsUiText *text = ecs_get(world, e, FlecsUiText);
    const FlecsUiLayout *layout = ecs_get(world, e, FlecsUiLayout);
    bool interactive = flecs_ui_isWidget(world, e);

    float rect_w = 0, rect_h = 0;
    if (rect) {
        rect_w = rect->width > 0 ? rect->width : size.w;
        rect_h = rect->height > 0 ? rect->height : size.h;
    }
    float box_w = rect_w;
    float box_h = rect_h;
    if (layout || interactive) {
        box_w = flecs_ui_max(box_w, size.w);
        box_h = flecs_ui_max(box_h, size.h);
    }

    FlecsUiBounds *bounds = ecs_ensure(world, e, FlecsUiBounds);
    bounds->x = x;
    bounds->y = y;
    bounds->width = flecs_ui_max(box_w, size.w);
    bounds->height = flecs_ui_max(box_h, size.h);

    /* Text and lines don't capture the mouse unless the element listens
     * for events or has a background box. */
    if (box_w > 0 && box_h > 0) {
        *ecs_vec_append_t(NULL, &ctx->impl->hits, ecs_entity_t) = e;
    }

    if (ctx->draw) {
        FlecsRgba color = { 255, 255, 255, 255 };
        const FlecsRgba *cptr = ecs_get(world, e, FlecsRgba);
        if (cptr) {
            color = *cptr;
        }

        if (rect) {
            if (border) {
                float radius = border->radius > 0 ? border->radius : 0;
                float tl = border->radius_top_left > 0
                    ? border->radius_top_left : radius;
                float tr = border->radius_top_right > 0
                    ? border->radius_top_right : radius;
                float bl = border->radius_bottom_left > 0
                    ? border->radius_bottom_left : radius;
                float br = border->radius_bottom_right > 0
                    ? border->radius_bottom_right : radius;
                flecsEngine_ui2dRoundedRect(world, x, y, rect_w, rect_h,
                    tl, tr, bl, br, color);
            } else {
                flecsEngine_ui2dRect(world, x, y, rect_w, rect_h, color);
            }
        }

        if (line) {
            float dx = line->x2 - line->x1;
            float dy = line->y2 - line->y1;
            float len = sqrtf(dx * dx + dy * dy);
            if (len > 0) {
                float t = line->thickness > 0 ? line->thickness : 1.0f;
                float nx = -dy / len * t * 0.5f;
                float ny = dx / len * t * 0.5f;
                flecsEngine_ui2dQuad(world,
                    x + line->x1 + nx, y + line->y1 + ny,
                    x + line->x2 + nx, y + line->y2 + ny,
                    x + line->x2 - nx, y + line->y2 - ny,
                    x + line->x1 - nx, y + line->y1 - ny,
                    color);
            }
        }

        if (rect && border) {
            FlecsRgba border_color = border->color.a
                ? border->color : color;
            float radius = border->radius > 0 ? border->radius : 0;
            float tl = border->radius_top_left > 0
                ? border->radius_top_left : radius;
            float tr = border->radius_top_right > 0
                ? border->radius_top_right : radius;
            float bl = border->radius_bottom_left > 0
                ? border->radius_bottom_left : radius;
            float br = border->radius_bottom_right > 0
                ? border->radius_bottom_right : radius;
            float width = border->width > 0 ? border->width : 1.0f;
            float top = border->top_width > 0
                ? border->top_width : width;
            float right = border->right_width > 0
                ? border->right_width : width;
            float bottom = border->bottom_width > 0
                ? border->bottom_width : width;
            float left = border->left_width > 0
                ? border->left_width : width;
            flecsEngine_ui2dBorder(world, x, y, rect_w, rect_h,
                tl, tr, bl, br, top, right, bottom, left, border_color);
        }

        if (text && text->text) {
            float text_size = text->size > 0
                ? text->size : FLECS_UI_DEFAULT_TEXT_SIZE;
            float align_w = (rect || layout)
                ? flecs_ui_max(box_w, size.w) : slot_w;
            float tx = x;
            if (text->align != FlecsUiLeft) {
                float tw = flecsEngine_ui2dTextWidth(
                    world, text_size, text->text);
                if (text->align == FlecsUiCenter) {
                    tx = x + (align_w - tw) * 0.5f;
                } else {
                    tx = x + align_w - tw;
                }
            }
            flecsEngine_ui2dText(world, tx, y, text_size, color, text->text);
        }
    }

    float padding = layout ? layout->padding : 0;
    float spacing = layout ? layout->spacing : 0;
    FlecsUiDirection direction = layout ? layout->direction : FlecsUiNone;
    float inner_w = flecs_ui_max(size.w - padding * 2.0f, 0);

    ecs_vec_t children;
    ecs_vec_init_t(NULL, &children, ecs_entity_t, 0);
    flecs_ui_collectChildren(world, e, &children);

    int32_t count = ecs_vec_count(&children);
    ecs_entity_t *ids = ecs_vec_first_t(&children, ecs_entity_t);
    float cx = x + padding;
    float cy = y + padding;

    for (int32_t i = 0; i < count; i ++) {
        if (direction == FlecsUiRow) {
            flecs_ui_size_t cs = flecs_ui_measure(world, ids[i]);
            flecs_ui_place(ctx, ids[i], cx, cy, cs.w);
            cx += cs.w + spacing;
        } else if (direction == FlecsUiColumn) {
            flecs_ui_size_t cs = flecs_ui_measure(world, ids[i]);
            flecs_ui_place(ctx, ids[i], cx, cy, inner_w);
            cy += cs.h + spacing;
        } else {
            flecs_ui_place(ctx, ids[i], cx, cy, inner_w);
        }
    }

    ecs_vec_fini_t(NULL, &children, ecs_entity_t);
}

static void flecs_ui_walk(
    ecs_world_t *world,
    FlecsUiImpl *impl,
    bool draw)
{
    ecs_vec_clear(&impl->hits);

    float sw, sh;
    if (!impl->ui_query || !flecsEngine_ui2dScreenSize(world, &sw, &sh)) {
        return;
    }

    ecs_vec_clear(&impl->roots);
    ecs_iter_t it = ecs_query_iter(world, impl->ui_query);
    while (ecs_query_next(&it)) {
        for (int32_t i = 0; i < it.count; i ++) {
            ecs_entity_t e = it.entities[i];
            ecs_entity_t parent = ecs_get_parent(world, e);
            if (parent && flecs_ui_isUiEntity(world, parent)) {
                continue;
            }
            *ecs_vec_append_t(NULL, &impl->roots, ecs_entity_t) = e;
        }
    }

    flecs_ui_walk_t ctx = { world, impl, draw };
    int32_t count = ecs_vec_count(&impl->roots);
    ecs_entity_t *roots = ecs_vec_first_t(&impl->roots, ecs_entity_t);

    for (int32_t i = 0; i < count; i ++) {
        flecs_ui_size_t size = flecs_ui_measure(world, roots[i]);
        float x = 0, y = 0;
        const FlecsUiAnchor *anchor = ecs_get(
            world, roots[i], FlecsUiAnchor);
        if (anchor) {
            if (anchor->h == FlecsUiCenter) {
                x = (sw - size.w) * 0.5f;
            } else if (anchor->h == FlecsUiRight) {
                x = sw - size.w;
            }
            if (anchor->v == FlecsUiMiddle) {
                y = (sh - size.h) * 0.5f;
            } else if (anchor->v == FlecsUiBottom) {
                y = sh - size.h;
            }
            x += anchor->x;
            y += anchor->y;
        }
        flecs_ui_place(&ctx, roots[i], x, y, size.w);
    }
}

static void flecs_ui_invoke(
    ecs_world_t *world,
    ecs_entity_t widget,
    flecs_ui_event_callback_t callback,
    void *callback_ctx)
{
    if (callback) {
        callback(world, widget, callback_ctx);
    }
}

static void flecs_ui_hotkeys(
    ecs_world_t *world,
    FlecsUiImpl *impl,
    const FlecsInput *input)
{
    if (!impl->hotkey_query) {
        return;
    }
    ecs_iter_t it = ecs_query_iter(world, impl->hotkey_query);
    while (ecs_query_next(&it)) {
        FlecsUiHotkey *hk = ecs_field(&it, FlecsUiHotkey, 0);
        for (int32_t i = 0; i < it.count; i ++) {
            const char *key = hk[i].key;
            if (!key) {
                continue;
            }
            int32_t kc = (unsigned char)key[0];
            if (kc <= 0 || kc >= 128 || !input->keys[kc].pressed) {
                continue;
            }
            ecs_entity_t e = it.entities[i];
            if (ecs_has_id(world, e, EcsDisabled)) {
                continue;
            }
            FlecsUiEventListener l = flecs_ui_listener(world, e);
            flecs_ui_invoke(world, e, l.on_lmb_up, l.ctx);
        }
    }
}

static void FlecsUiUpdate(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsUiImpl *impl = ecs_singleton_ensure(world, FlecsUiImpl);

    flecs_ui_walk(world, impl, false);

    const FlecsInput *input = ecs_singleton_get(world, FlecsInput);
    if (!input) {
        return;
    }

    flecs_ui_hotkeys(world, impl, input);

    float mx = input->mouse.wnd.x;
    float my = input->mouse.wnd.y;
    bool moved = input->mouse.rel.x != 0 || input->mouse.rel.y != 0;
    bool l_pressed = input->mouse.left.pressed;
    bool l_state = input->mouse.left.state;
    bool r_pressed = input->mouse.right.pressed;
    bool r_state = input->mouse.right.state;

    /* Hit candidates are in draw order; the last bounds containing the
     * mouse is the topmost one. Hover resolves to its nearest widget
     * ancestor so decorations (borders, fills) don't shadow their
     * widget, while plain panels drawn on top of a widget do occlude
     * it. */
    bool captured = false;
    ecs_entity_t hovered = 0;
    int32_t count = ecs_vec_count(&impl->hits);
    ecs_entity_t *hits = ecs_vec_first_t(&impl->hits, ecs_entity_t);
    for (int32_t i = count - 1; i >= 0; i --) {
        const FlecsUiBounds *b = ecs_get(world, hits[i], FlecsUiBounds);
        if (!b || mx < b->x || mx > b->x + b->width ||
            my < b->y || my > b->y + b->height)
        {
            continue;
        }
        captured = true;
        ecs_entity_t e = hits[i];
        while (e) {
            if (flecs_ui_isWidget(world, e)) {
                hovered = e;
                break;
            }
            ecs_entity_t parent = ecs_get_parent(world, e);
            if (!parent || !flecs_ui_isUiEntity(world, parent)) {
                break;
            }
            e = parent;
        }
        break;
    }

    ecs_entity_t prev = impl->hovered;
    if (prev && prev != hovered && ecs_is_alive(world, prev)) {
        const FlecsUiWidgetState *cur = ecs_get(
            world, prev, FlecsUiWidgetState);
        FlecsUiWidgetState s = cur ? *cur : (FlecsUiWidgetState){0};
        s.hover = false;
        if (prev != impl->active) {
            s.lmb_down = false;
            s.drag = false;
        }
        if (prev != impl->rmb_active) {
            s.rmb_down = false;
        }
        ecs_set_ptr(world, prev, FlecsUiWidgetState, &s);
        FlecsUiEventListener l = flecs_ui_listener(world, prev);
        flecs_ui_invoke(world, prev, l.on_leave, l.ctx);
    }
    impl->hovered = hovered;

    if (hovered) {
        const FlecsUiWidgetState *cur = ecs_get(
            world, hovered, FlecsUiWidgetState);
        FlecsUiWidgetState s = cur ? *cur : (FlecsUiWidgetState){0};
        FlecsUiEventListener l = flecs_ui_listener(world, hovered);
        bool entered = !s.hover || prev != hovered;
        bool write = entered;

        s.mouse_x = mx;
        s.mouse_y = my;
        s.hover = true;

        if (l_pressed) {
            s.lmb_down = true;
            s.drag = false;
            s.drag_anchor_x = mx;
            s.drag_anchor_y = my;
            impl->active = hovered;
            write = true;
        }
        if (r_pressed) {
            s.rmb_down = true;
            impl->rmb_active = hovered;
            write = true;
        }

        bool dragging = impl->active == hovered && l_state && !l_pressed;
        if (moved && dragging && !s.drag) {
            s.drag = true;
        }

        bool l_released = impl->active == hovered && !l_state && s.lmb_down;
        if (l_released) {
            s.lmb_down = false;
            s.drag = false;
            write = true;
        }
        bool r_released =
            impl->rmb_active == hovered && !r_state && s.rmb_down;
        if (r_released) {
            s.rmb_down = false;
            write = true;
        }

        if (moved && (s.drag || l.on_move || l.on_drag)) {
            write = true;
        }

        if (write) {
            ecs_set_ptr(world, hovered, FlecsUiWidgetState, &s);
        }

        if (entered) {
            flecs_ui_invoke(world, hovered, l.on_enter, l.ctx);
        }
        if (l_pressed) {
            flecs_ui_invoke(world, hovered, l.on_lmb_down, l.ctx);
        }
        if (r_pressed) {
            flecs_ui_invoke(world, hovered, l.on_rmb_down, l.ctx);
        }
        if (moved) {
            if (s.drag) {
                flecs_ui_invoke(world, hovered, l.on_drag, l.ctx);
            } else {
                flecs_ui_invoke(world, hovered, l.on_move, l.ctx);
            }
        }
        if (l_released) {
            flecs_ui_invoke(world, hovered, l.on_lmb_up, l.ctx);
            impl->active = 0;
        }
        if (r_released) {
            flecs_ui_invoke(world, hovered, l.on_rmb_up, l.ctx);
            impl->rmb_active = 0;
        }
    }

    if (impl->active && impl->active != hovered) {
        if (!ecs_is_alive(world, impl->active)) {
            impl->active = 0;
        } else {
            ecs_entity_t active = impl->active;
            const FlecsUiWidgetState *cur = ecs_get(
                world, active, FlecsUiWidgetState);
            FlecsUiWidgetState s = cur ? *cur : (FlecsUiWidgetState){0};
            FlecsUiEventListener l = flecs_ui_listener(world, active);

            s.mouse_x = mx;
            s.mouse_y = my;

            if (l_state) {
                if (moved) {
                    s.drag = true;
                    ecs_set_ptr(world, active, FlecsUiWidgetState, &s);
                    flecs_ui_invoke(world, active, l.on_drag, l.ctx);
                }
            } else {
                s.lmb_down = false;
                s.drag = false;
                ecs_set_ptr(world, active, FlecsUiWidgetState, &s);
                flecs_ui_invoke(world, active, l.on_lmb_up, l.ctx);
                impl->active = 0;
            }
        }
    }

    if (impl->rmb_active && impl->rmb_active != hovered) {
        if (!ecs_is_alive(world, impl->rmb_active)) {
            impl->rmb_active = 0;
        } else if (!r_state) {
            ecs_entity_t rmb_active = impl->rmb_active;
            const FlecsUiWidgetState *cur = ecs_get(
                world, rmb_active, FlecsUiWidgetState);
            FlecsUiWidgetState s = cur ? *cur : (FlecsUiWidgetState){0};
            FlecsUiEventListener l = flecs_ui_listener(world, rmb_active);
            s.rmb_down = false;
            ecs_set_ptr(world, rmb_active, FlecsUiWidgetState, &s);
            flecs_ui_invoke(world, rmb_active, l.on_rmb_up, l.ctx);
            impl->rmb_active = 0;
        }
    }

    impl->mouse_captured = captured;
}

static void FlecsUiRender(
    ecs_iter_t *it)
{
    ecs_world_t *world = it->world;
    FlecsUiImpl *impl = ecs_singleton_ensure(world, FlecsUiImpl);
    flecs_ui_walk(world, impl, true);
}

bool flecsEngine_uiMouseCaptured(
    ecs_world_t *world)
{
    if (!ecs_id(FlecsUiImpl)) {
        return false;
    }
    const FlecsUiImpl *impl = ecs_singleton_get(world, FlecsUiImpl);
    return impl && impl->mouse_captured;
}

void FlecsEngineUiImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineUi);

    ecs_set_name_prefix(world, "FlecsUi");

    ECS_META_COMPONENT(world, FlecsUiAlign);
    ECS_META_COMPONENT(world, FlecsUiVAlign);
    ECS_META_COMPONENT(world, FlecsUiDirection);
    ECS_META_COMPONENT(world, FlecsUiRect);
    ECS_META_COMPONENT(world, FlecsUiBorder);
    ECS_META_COMPONENT(world, FlecsUiLine);
    ECS_META_COMPONENT(world, FlecsUiText);
    ECS_META_COMPONENT(world, FlecsUiPosition);
    ECS_META_COMPONENT(world, FlecsUiLayout);
    ECS_META_COMPONENT(world, FlecsUiAnchor);
    ECS_META_COMPONENT(world, FlecsUiBounds);
    ECS_META_COMPONENT(world, FlecsUiHotkey);
    ECS_META_COMPONENT(world, FlecsUiWidgetState);

    /* Function pointers aren't scriptable; the listener is registered
     * without reflection on purpose. */
    ECS_COMPONENT_DEFINE(world, FlecsUiEventListener);

    ecs_add_pair(world, ecs_id(FlecsUiEventListener), EcsWith,
        ecs_id(FlecsUiWidgetState));

    ECS_COMPONENT_DEFINE(world, FlecsUiImpl);

    ecs_set_hooks(world, FlecsUiImpl, {
        .ctor = ecs_ctor(FlecsUiImpl),
        .move = ecs_move(FlecsUiImpl),
        .dtor = ecs_dtor(FlecsUiImpl)
    });

    ecs_add_pair(world, ecs_id(FlecsUiLayout), EcsWith, EcsOrderedChildren);
    ecs_add_pair(world, ecs_id(FlecsUiBorder), EcsWith,
        ecs_id(FlecsUiRect));

    FlecsUiImpl *impl = ecs_singleton_ensure(world, FlecsUiImpl);
    impl->ui_query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "UiElementQuery" }),
        .terms = {
            { .id = ecs_id(FlecsUiRect), .oper = EcsOr,
              .inout = EcsInOutNone },
            { .id = ecs_id(FlecsUiBorder), .oper = EcsOr,
              .inout = EcsInOutNone },
            { .id = ecs_id(FlecsUiLine), .oper = EcsOr,
              .inout = EcsInOutNone },
            { .id = ecs_id(FlecsUiText), .oper = EcsOr,
              .inout = EcsInOutNone },
            { .id = ecs_id(FlecsUiPosition), .oper = EcsOr,
              .inout = EcsInOutNone },
            { .id = ecs_id(FlecsUiLayout), .oper = EcsOr,
              .inout = EcsInOutNone },
            { .id = ecs_id(FlecsUiAnchor), .oper = EcsOr,
              .inout = EcsInOutNone },
            { .id = ecs_id(FlecsUiWidgetState), .oper = EcsOr,
              .inout = EcsInOutNone },
            { .id = ecs_id(FlecsUiEventListener), .inout = EcsInOutNone }
        }
    });

    impl->hotkey_query = ecs_query(world, {
        .entity = ecs_entity(world, { .name = "UiHotkeyQuery" }),
        .terms = {
            { .id = ecs_id(FlecsUiHotkey), .inout = EcsIn },
            { .id = ecs_id(FlecsUiEventListener), .inout = EcsInOutNone }
        }
    });

    ECS_SYSTEM(world, FlecsUiUpdate, EcsPostLoad);
    ECS_SYSTEM(world, FlecsUiRender, EcsPreStore);
}
