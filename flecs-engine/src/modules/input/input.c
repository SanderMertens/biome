#define FLECS_ENGINE_INPUT_IMPL
#include "input.h"
#include "../../tracy_hooks.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/html5.h>
#endif

ECS_COMPONENT_DECLARE(FlecsInput);
extern ECS_COMPONENT_DECLARE(FlecsEngineImpl);
extern ECS_COMPONENT_DECLARE(FlecsSurfaceImpl);

#ifndef __EMSCRIPTEN__
static int flecsEngine_input_keyCode(
    int glfw_key)
{
    switch (glfw_key) {
    case GLFW_KEY_SPACE: return FLECS_KEY_SPACE;
    case GLFW_KEY_APOSTROPHE: return FLECS_KEY_APOSTROPHE;
    case GLFW_KEY_COMMA: return FLECS_KEY_COMMA;
    case GLFW_KEY_MINUS: return FLECS_KEY_MINUS;
    case GLFW_KEY_PERIOD: return FLECS_KEY_PERIOD;
    case GLFW_KEY_SLASH: return FLECS_KEY_SLASH;
    case GLFW_KEY_0: return FLECS_KEY_0;
    case GLFW_KEY_1: return FLECS_KEY_1;
    case GLFW_KEY_2: return FLECS_KEY_2;
    case GLFW_KEY_3: return FLECS_KEY_3;
    case GLFW_KEY_4: return FLECS_KEY_4;
    case GLFW_KEY_5: return FLECS_KEY_5;
    case GLFW_KEY_6: return FLECS_KEY_6;
    case GLFW_KEY_7: return FLECS_KEY_7;
    case GLFW_KEY_8: return FLECS_KEY_8;
    case GLFW_KEY_9: return FLECS_KEY_9;
    case GLFW_KEY_SEMICOLON: return FLECS_KEY_SEMICOLON;
    case GLFW_KEY_EQUAL: return FLECS_KEY_EQUAL;
    case GLFW_KEY_A: return FLECS_KEY_A;
    case GLFW_KEY_B: return FLECS_KEY_B;
    case GLFW_KEY_C: return FLECS_KEY_C;
    case GLFW_KEY_D: return FLECS_KEY_D;
    case GLFW_KEY_E: return FLECS_KEY_E;
    case GLFW_KEY_F: return FLECS_KEY_F;
    case GLFW_KEY_G: return FLECS_KEY_G;
    case GLFW_KEY_H: return FLECS_KEY_H;
    case GLFW_KEY_I: return FLECS_KEY_I;
    case GLFW_KEY_J: return FLECS_KEY_J;
    case GLFW_KEY_K: return FLECS_KEY_K;
    case GLFW_KEY_L: return FLECS_KEY_L;
    case GLFW_KEY_M: return FLECS_KEY_M;
    case GLFW_KEY_N: return FLECS_KEY_N;
    case GLFW_KEY_O: return FLECS_KEY_O;
    case GLFW_KEY_P: return FLECS_KEY_P;
    case GLFW_KEY_Q: return FLECS_KEY_Q;
    case GLFW_KEY_R: return FLECS_KEY_R;
    case GLFW_KEY_S: return FLECS_KEY_S;
    case GLFW_KEY_T: return FLECS_KEY_T;
    case GLFW_KEY_U: return FLECS_KEY_U;
    case GLFW_KEY_V: return FLECS_KEY_V;
    case GLFW_KEY_W: return FLECS_KEY_W;
    case GLFW_KEY_X: return FLECS_KEY_X;
    case GLFW_KEY_Y: return FLECS_KEY_Y;
    case GLFW_KEY_Z: return FLECS_KEY_Z;
    case GLFW_KEY_LEFT_BRACKET: return FLECS_KEY_LEFT_BRACKET;
    case GLFW_KEY_BACKSLASH: return FLECS_KEY_BACKSLASH;
    case GLFW_KEY_RIGHT_BRACKET: return FLECS_KEY_RIGHT_BRACKET;
    case GLFW_KEY_GRAVE_ACCENT: return FLECS_KEY_GRAVE_ACCENT;
    case GLFW_KEY_ESCAPE: return FLECS_KEY_ESCAPE;
    case GLFW_KEY_ENTER: return FLECS_KEY_RETURN;
    case GLFW_KEY_TAB: return FLECS_KEY_TAB;
    case GLFW_KEY_BACKSPACE: return FLECS_KEY_BACKSPACE;
    case GLFW_KEY_INSERT: return FLECS_KEY_INSERT;
    case GLFW_KEY_DELETE: return FLECS_KEY_DELETE;
    case GLFW_KEY_RIGHT: return FLECS_KEY_RIGHT;
    case GLFW_KEY_LEFT: return FLECS_KEY_LEFT;
    case GLFW_KEY_DOWN: return FLECS_KEY_DOWN;
    case GLFW_KEY_UP: return FLECS_KEY_UP;
    case GLFW_KEY_PAGE_UP: return FLECS_KEY_PAGE_UP;
    case GLFW_KEY_PAGE_DOWN: return FLECS_KEY_PAGE_DOWN;
    case GLFW_KEY_HOME: return FLECS_KEY_HOME;
    case GLFW_KEY_END: return FLECS_KEY_END;
    case GLFW_KEY_LEFT_SHIFT: return FLECS_KEY_LEFT_SHIFT;
    case GLFW_KEY_LEFT_CONTROL: return FLECS_KEY_LEFT_CTRL;
    case GLFW_KEY_LEFT_ALT: return FLECS_KEY_LEFT_ALT;
    case GLFW_KEY_RIGHT_SHIFT: return FLECS_KEY_RIGHT_SHIFT;
    case GLFW_KEY_RIGHT_CONTROL: return FLECS_KEY_RIGHT_CTRL;
    case GLFW_KEY_RIGHT_ALT: return FLECS_KEY_RIGHT_ALT;
    default:
        return FLECS_KEY_UNKNOWN;
    }
}
#endif /* !__EMSCRIPTEN__ */

static flecs_engine_key_state_t* flecsEngine_input_keyGet(
    FlecsInput *input,
    int key_code)
{
    int32_t key_count = (int32_t)(sizeof(input->keys) / sizeof(input->keys[0]));
    if (key_code < 0 || key_code >= key_count) {
        return NULL;
    }
    return &input->keys[key_code];
}

static void flecsEngine_input_keyDown(
    flecs_engine_key_state_t *key)
{
    if (!key) {
        return;
    }

    if (key->state) {
        key->pressed = false;
    } else {
        key->pressed = true;
    }

    key->state = true;
    key->current = true;
}

static void flecsEngine_input_keyUp(
    flecs_engine_key_state_t *key)
{
    if (!key) {
        return;
    }

    key->current = false;
}

static void flecsEngine_input_keyReset(
    flecs_engine_key_state_t *state)
{
    if (!state->current) {
        state->state = false;
        state->pressed = false;
    } else if (state->state) {
        state->pressed = false;
    }
}

static void flecsEngine_input_keysReset(
    FlecsInput *input)
{
    int32_t key_count = (int32_t)(sizeof(input->keys) / sizeof(input->keys[0]));
    for (int32_t k = 0; k < key_count; k ++) {
        flecsEngine_input_keyReset(&input->keys[k]);
    }
}

static void flecsEngine_input_mouseButtonReset(
    flecs_engine_key_state_t *mouse)
{
    flecsEngine_input_keyReset(mouse);
}

static void flecsEngine_input_mouseReset(
    FlecsInput *input)
{
    flecsEngine_input_mouseButtonReset(&input->mouse.left);
    flecsEngine_input_mouseButtonReset(&input->mouse.right);
    input->mouse.rel = (flecs_engine_mouse_coord_t){0};
    input->mouse.scroll = (flecs_engine_mouse_coord_t){0};
}

#ifndef __EMSCRIPTEN__
static void flecsEngine_input_onKey(
    GLFWwindow *window,
    int key,
    int scancode,
    int action,
    int mods)
{
    (void)scancode;
    (void)mods;

    ecs_world_t *world = glfwGetWindowUserPointer(window);
    if (!world) {
        return;
    }

    FlecsInput *input = ecs_singleton_ensure(world, FlecsInput);
    if (!input) {
        return;
    }

    flecs_engine_key_state_t *state = flecsEngine_input_keyGet(
        input, flecsEngine_input_keyCode(key));

    if (action == GLFW_RELEASE) {
        flecsEngine_input_keyUp(state);
    } else if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        flecsEngine_input_keyDown(state);
    }
}

static void flecsEngine_input_onMouseButton(
    GLFWwindow *window,
    int button,
    int action,
    int mods)
{
    (void)mods;

    ecs_world_t *world = glfwGetWindowUserPointer(window);
    if (!world) {
        return;
    }

    FlecsInput *input = ecs_singleton_ensure(world, FlecsInput);
    if (!input) {
        return;
    }

    flecs_engine_key_state_t *state = NULL;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        state = &input->mouse.left;
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        state = &input->mouse.right;
    }

    if (!state) {
        return;
    }

    if (action == GLFW_RELEASE) {
        flecsEngine_input_keyUp(state);
    } else if (action == GLFW_PRESS) {
        flecsEngine_input_keyDown(state);
    }
}

static void flecsEngine_input_onScroll(
    GLFWwindow *window,
    double xoffset,
    double yoffset)
{
    ecs_world_t *world = glfwGetWindowUserPointer(window);
    if (!world) {
        return;
    }

    FlecsInput *input = ecs_singleton_ensure(world, FlecsInput);
    if (!input) {
        return;
    }

    input->mouse.scroll.x += (float)xoffset;
    input->mouse.scroll.y += (float)yoffset;
}

static void flecsEngine_input_bindWindow(
    ecs_world_t *world,
    GLFWwindow *window)
{
    if (!window) {
        return;
    }

    glfwSetWindowUserPointer(window, world);
    glfwSetKeyCallback(window, flecsEngine_input_onKey);
    glfwSetMouseButtonCallback(window, flecsEngine_input_onMouseButton);
    glfwSetScrollCallback(window, flecsEngine_input_onScroll);
}
#else /* __EMSCRIPTEN__ */

/* On the web, input is delivered through emscripten's HTML5 event callbacks
 * attached to the "#canvas" element. Keyboard events only reach the canvas
 * while it has focus, which the frontend toggles when the canvas is selected,
 * matching the previous (sokol) renderer's behavior. */

static ecs_world_t *flecs_em_input_world = NULL;

static int flecsEngine_input_em_keyCode(
    int dom_key)
{
    if (dom_key >= 'A' && dom_key <= 'Z') {
        return FLECS_KEY_A + (dom_key - 'A'); /* DOM is uppercase, FLECS lower */
    }
    if (dom_key >= '0' && dom_key <= '9') {
        return dom_key; /* FLECS_KEY_0..9 are the ASCII digits */
    }
    switch (dom_key) {
    case 32:  return FLECS_KEY_SPACE;
    case 13:  return FLECS_KEY_RETURN;
    case 9:   return FLECS_KEY_TAB;
    case 8:   return FLECS_KEY_BACKSPACE;
    case 27:  return FLECS_KEY_ESCAPE;
    case 16:  return FLECS_KEY_LEFT_SHIFT;
    case 17:  return FLECS_KEY_LEFT_CTRL;
    case 18:  return FLECS_KEY_LEFT_ALT;
    case 37:  return FLECS_KEY_LEFT;
    case 38:  return FLECS_KEY_UP;
    case 39:  return FLECS_KEY_RIGHT;
    case 40:  return FLECS_KEY_DOWN;
    case 33:  return FLECS_KEY_PAGE_UP;
    case 34:  return FLECS_KEY_PAGE_DOWN;
    case 36:  return FLECS_KEY_HOME;
    case 35:  return FLECS_KEY_END;
    case 45:  return FLECS_KEY_INSERT;
    case 46:  return FLECS_KEY_DELETE;
    default:  return FLECS_KEY_UNKNOWN;
    }
}

static FlecsInput* flecsEngine_input_em_get(void) {
    if (!flecs_em_input_world) {
        return NULL;
    }
    return ecs_singleton_ensure(flecs_em_input_world, FlecsInput);
}

static EM_BOOL flecsEngine_input_em_onKey(
    int event_type,
    const EmscriptenKeyboardEvent *e,
    void *user_data)
{
    (void)user_data;
    FlecsInput *input = flecsEngine_input_em_get();
    if (!input) {
        return EM_FALSE;
    }

    int key = flecsEngine_input_em_keyCode((int)e->keyCode);
    if (key == FLECS_KEY_UNKNOWN) {
        return EM_FALSE; /* let the browser handle keys the engine ignores */
    }

    flecs_engine_key_state_t *state = flecsEngine_input_keyGet(input, key);
    if (event_type == EMSCRIPTEN_EVENT_KEYUP) {
        flecsEngine_input_keyUp(state);
    } else {
        flecsEngine_input_keyDown(state);
    }
    return EM_TRUE; /* consume so the page doesn't scroll/shortcut on game keys */
}

static EM_BOOL flecsEngine_input_em_onMouse(
    int event_type,
    const EmscriptenMouseEvent *e,
    void *user_data)
{
    (void)user_data;
    FlecsInput *input = flecsEngine_input_em_get();
    if (!input) {
        return EM_FALSE;
    }

    if (event_type == EMSCRIPTEN_EVENT_MOUSEMOVE) {
        input->mouse.wnd.x = (float)e->targetX;
        input->mouse.wnd.y = (float)e->targetY;
        return EM_FALSE;
    }

    flecs_engine_key_state_t *btn = NULL;
    if (e->button == 0) {
        btn = &input->mouse.left;
    } else if (e->button == 2) {
        btn = &input->mouse.right;
    }
    if (!btn) {
        return EM_FALSE;
    }

    if (event_type == EMSCRIPTEN_EVENT_MOUSEUP) {
        flecsEngine_input_keyUp(btn);
    } else {
        flecsEngine_input_keyDown(btn);
    }
    /* Don't consume: let the click focus the canvas so keyboard events route
     * here (the keyboard callbacks only fire while the canvas is focused). */
    return EM_FALSE;
}

static EM_BOOL flecsEngine_input_em_onWheel(
    int event_type,
    const EmscriptenWheelEvent *e,
    void *user_data)
{
    (void)event_type;
    (void)user_data;
    FlecsInput *input = flecsEngine_input_em_get();
    if (!input) {
        return EM_FALSE;
    }
    /* Normalize browser pixel deltas to GLFW-like notch units (up is +1). */
    input->mouse.scroll.x += -(float)e->deltaX / 100.0f;
    input->mouse.scroll.y += -(float)e->deltaY / 100.0f;
    return EM_TRUE;
}

static void flecsEngine_input_em_register(
    ecs_world_t *world)
{
    flecs_em_input_world = world;

    const char *canvas = "#canvas";
    emscripten_set_keydown_callback(
        canvas, NULL, EM_TRUE, flecsEngine_input_em_onKey);
    emscripten_set_keyup_callback(
        canvas, NULL, EM_TRUE, flecsEngine_input_em_onKey);
    emscripten_set_mousedown_callback(
        canvas, NULL, EM_TRUE, flecsEngine_input_em_onMouse);
    emscripten_set_mouseup_callback(
        canvas, NULL, EM_TRUE, flecsEngine_input_em_onMouse);
    emscripten_set_mousemove_callback(
        canvas, NULL, EM_TRUE, flecsEngine_input_em_onMouse);
    emscripten_set_wheel_callback(
        canvas, NULL, EM_TRUE, flecsEngine_input_em_onWheel);
}
#endif /* __EMSCRIPTEN__ */

static void FlecsReadInputs(
    ecs_iter_t *it)
{
    FLECS_TRACY_ZONE_BEGIN("ReadInputs");
    const FlecsEngineImpl *engine = ecs_singleton_get(it->world, FlecsEngineImpl);
    if (!engine || !engine->surface) {
        FLECS_TRACY_ZONE_END;
        return;
    }

#ifdef __EMSCRIPTEN__
    /* No GLFW window on the web. Input arrives asynchronously via the HTML5
     * callbacks (registered once); here we just advance the per-frame state
     * and derive the mouse delta from the latest cursor position. */
    static bool em_registered = false;
    if (!em_registered) {
        flecsEngine_input_em_register(it->world);
        em_registered = true;
    }

    FlecsInput *input = ecs_singleton_ensure(it->world, FlecsInput);

    float prev_x = input->mouse.wnd.x;
    float prev_y = input->mouse.wnd.y;

    flecsEngine_input_keysReset(input);
    flecsEngine_input_mouseReset(input);

    input->mouse.rel.x = input->mouse.wnd.x - prev_x;
    input->mouse.rel.y = input->mouse.wnd.y - prev_y;

    double css_w = 0.0;
    double css_h = 0.0;
    emscripten_get_element_css_size("#canvas", &css_w, &css_h);
    input->mouse.view.x = input->mouse.wnd.x - ((float)css_w * 0.5f);
    input->mouse.view.y = input->mouse.wnd.y - ((float)css_h * 0.5f);
    if (css_w > 0 && css_h > 0) {
        input->mouse.view_norm.x = input->mouse.view.x / ((float)css_w * 0.5f);
        input->mouse.view_norm.y = input->mouse.view.y / ((float)css_h * 0.5f);
    }
    FLECS_TRACY_ZONE_END;
#else
    const FlecsSurfaceImpl *wnd = ecs_get(
        it->world, engine->surface, FlecsSurfaceImpl);
    if (!wnd || !wnd->window) {
        FLECS_TRACY_ZONE_END;
        return;
    }

    flecsEngine_input_bindWindow(it->world, wnd->window);

    FlecsInput *input = ecs_singleton_ensure(it->world, FlecsInput);

    float prev_x = input->mouse.wnd.x;
    float prev_y = input->mouse.wnd.y;

    flecsEngine_input_keysReset(input);
    flecsEngine_input_mouseReset(input);

    glfwPollEvents();

    double wnd_x = 0.0;
    double wnd_y = 0.0;
    glfwGetCursorPos(wnd->window, &wnd_x, &wnd_y);

    input->mouse.wnd.x = (float)wnd_x;
    input->mouse.wnd.y = (float)wnd_y;
    input->mouse.rel.x = input->mouse.wnd.x - prev_x;
    input->mouse.rel.y = input->mouse.wnd.y - prev_y;

    int32_t wnd_w = 0;
    int32_t wnd_h = 0;
    glfwGetWindowSize(wnd->window, &wnd_w, &wnd_h);

    input->mouse.view.x = input->mouse.wnd.x - ((float)wnd_w * 0.5f);
    input->mouse.view.y = input->mouse.wnd.y - ((float)wnd_h * 0.5f);
    if (wnd_w > 0 && wnd_h > 0) {
        input->mouse.view_norm.x = input->mouse.view.x / ((float)wnd_w * 0.5f);
        input->mouse.view_norm.y = input->mouse.view.y / ((float)wnd_h * 0.5f);
    }
    FLECS_TRACY_ZONE_END;
#endif
}

void FlecsEngineInputImport(
    ecs_world_t *world)
{
    ECS_MODULE(world, FlecsEngineInput);

    ecs_set_name_prefix(world, "Flecs");

    ECS_META_COMPONENT(world, flecs_engine_key_state_t);
    ECS_META_COMPONENT(world, flecs_engine_mouse_coord_t);
    ECS_META_COMPONENT(world, flecs_engine_mouse_state_t);
    ECS_META_COMPONENT(world, FlecsInput);

    ecs_add_id(world, ecs_id(FlecsInput), EcsSingleton);
    ecs_singleton_set(world, FlecsInput, {0});

    ECS_SYSTEM(world, FlecsReadInputs, EcsOnLoad);
}
