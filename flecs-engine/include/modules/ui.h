#ifndef FLECS_ENGINE_UI_H
#define FLECS_ENGINE_UI_H

#undef ECS_META_IMPL
#ifndef FLECS_ENGINE_UI_IMPL
#define ECS_META_IMPL EXTERN
#endif

ECS_ENUM(FlecsUiAlign, {
    FlecsUiLeft,
    FlecsUiCenter,
    FlecsUiRight,
});

ECS_ENUM(FlecsUiVAlign, {
    FlecsUiTop,
    FlecsUiMiddle,
    FlecsUiBottom,
});

ECS_ENUM(FlecsUiDirection, {
    FlecsUiNone,
    FlecsUiRow,
    FlecsUiColumn,
});

ECS_STRUCT(FlecsUiRect, {
    float width;
    float height;
});

ECS_STRUCT(FlecsUiBorder, {
    /* Shorthand values are inherited by zero-valued corner/edge fields. */
    float radius;
    float radius_top_left;
    float radius_top_right;
    float radius_bottom_left;
    float radius_bottom_right;
    float width;
    float top_width;
    float bottom_width;
    float left_width;
    float right_width;
    /* A zero-alpha color inherits the entity's FlecsRgba. */
    FlecsRgba color;
});

ECS_STRUCT(FlecsUiLine, {
    float x1;
    float y1;
    float x2;
    float y2;
    float thickness;
});

ECS_STRUCT(FlecsUiText, {
    char *text;
    float size;
    FlecsUiAlign align;
});

ECS_STRUCT(FlecsUiPosition, {
    float x;
    float y;
});

ECS_STRUCT(FlecsUiLayout, {
    FlecsUiDirection direction;
    float padding;
    float spacing;
    float width;
    float height;
});

ECS_STRUCT(FlecsUiAnchor, {
    FlecsUiAlign h;
    FlecsUiVAlign v;
    float x;
    float y;
});

/* Absolute screen rectangle of an element, written by the layout pass
 * every frame. Read-only for applications. */
ECS_STRUCT(FlecsUiBounds, {
    float x;
    float y;
    float width;
    float height;
});

ECS_STRUCT(FlecsUiHotkey, {
    char *key;
});

ECS_STRUCT(FlecsUiWidgetState, {
    bool hover;
    bool drag;
    bool lmb_down;
    bool rmb_down;
    float mouse_x;
    float mouse_y;
    float drag_anchor_x;
    float drag_anchor_y;
});

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*flecs_ui_event_callback_t)(
    ecs_world_t *world,
    ecs_entity_t widget,
    void *ctx);

/* Mouse callbacks invoked by the input pass; setting this component with
 * at least one callback makes the element hit-testable. */
typedef struct FlecsUiEventListener {
    flecs_ui_event_callback_t on_enter;
    flecs_ui_event_callback_t on_leave;
    flecs_ui_event_callback_t on_move;
    flecs_ui_event_callback_t on_lmb_down;
    flecs_ui_event_callback_t on_lmb_up;
    flecs_ui_event_callback_t on_rmb_down;
    flecs_ui_event_callback_t on_rmb_up;
    flecs_ui_event_callback_t on_drag;

    void *ctx;
} FlecsUiEventListener;

extern ECS_COMPONENT_DECLARE(FlecsUiEventListener);

bool flecsEngine_uiMouseCaptured(
    ecs_world_t *world);

int flecsEngine_uiLoadWidgets(
    ecs_world_t *world,
    const char *filename);

void FlecsEngineUiImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#endif
