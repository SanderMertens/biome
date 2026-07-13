#ifndef FLECS_ENGINE_UI2D_H
#define FLECS_ENGINE_UI2D_H

/* Screen-space 2D overlay for HUDs and simple game UI.
 *
 * Immediate mode: game systems submit rects and text every frame (during
 * OnUpdate); the renderer draws the overlay on top of the finished 3D frame
 * and clears the submission list. Nothing is retained between frames.
 *
 * Coordinates are logical window pixels with the origin at the top-left,
 * matching FlecsInput mouse.wnd, so hit testing is a simple rect compare.
 *
 * Text uses a TTF font rendered through stb_truetype into a glyph atlas.
 * A default font (B612 Regular) is embedded; flecsEngine_ui2dSetFont can
 * replace it before the first frame is drawn. */

#ifdef __cplusplus
extern "C" {
#endif

/* Solid rectangle. */
void flecsEngine_ui2dRect(
    ecs_world_t *world,
    float x,
    float y,
    float w,
    float h,
    FlecsRgba color);

/* Solid rectangle with independently rounded corners. */
void flecsEngine_ui2dRoundedRect(
    ecs_world_t *world,
    float x,
    float y,
    float w,
    float h,
    float radius_top_left,
    float radius_top_right,
    float radius_bottom_left,
    float radius_bottom_right,
    FlecsRgba color);

/* Inset border with independently rounded corners and edge widths. */
void flecsEngine_ui2dBorder(
    ecs_world_t *world,
    float x,
    float y,
    float w,
    float h,
    float radius_top_left,
    float radius_top_right,
    float radius_bottom_left,
    float radius_bottom_right,
    float top_width,
    float right_width,
    float bottom_width,
    float left_width,
    FlecsRgba color);

/* Solid convex quadrilateral given four corner points in draw order.
 * Enables rotated primitives (e.g. arbitrary-angle lines) that the
 * axis-aligned rect calls can't express. */
void flecsEngine_ui2dQuad(
    ecs_world_t *world,
    float x0,
    float y0,
    float x1,
    float y1,
    float x2,
    float y2,
    float x3,
    float y3,
    FlecsRgba color);

/* Draw text with its top-left corner at (x, y). size is the line height in
 * logical pixels. */
void flecsEngine_ui2dText(
    ecs_world_t *world,
    float x,
    float y,
    float size,
    FlecsRgba color,
    const char *text);

/* Width in logical pixels the text would occupy at the given size. */
float flecsEngine_ui2dTextWidth(
    ecs_world_t *world,
    float size,
    const char *text);

/* Logical size of the surface the overlay is drawn on. Returns false when
 * no surface exists (headless). */
bool flecsEngine_ui2dScreenSize(
    ecs_world_t *world,
    float *w,
    float *h);

/* True while the mouse is inside the rect. Same logical-pixel space as
 * the draw calls, so widgets can pair a draw with a hover test for
 * immediate-mode hover/click behavior. */
bool flecsEngine_ui2dHovered(
    ecs_world_t *world,
    float x,
    float y,
    float w,
    float h);

/* Replace the font. ttf_data is copied. Call before the first text draw;
 * afterwards it rebuilds the atlas on the next frame. */
void flecsEngine_ui2dSetFont(
    ecs_world_t *world,
    const void *ttf_data,
    int32_t length);

void FlecsEngineUi2dImport(
    ecs_world_t *world);

#ifdef __cplusplus
}
#endif

#endif
