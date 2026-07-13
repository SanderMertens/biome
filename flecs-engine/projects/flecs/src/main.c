#include "flecs_engine.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

typedef struct {
  const char *frame_output_path;
  const char *scene_path;
  int32_t width;
  int32_t height;
} FlecsAppOptions;

static void flecsPrintUsage(
  const char *argv0)
{
  printf(
    "Usage: %s [--scene <file.flecs>] [--frame-out <file.ppm>] [--width <px>] [--height <px>] [--size <WxH>]\n"
    "\n"
    "  --scene <path>      Load scene from a Flecs script file (overrides default).\n"
    "  --frame-out <path>  Render one frame to a PPM image, then quit.\n"
    "  --width <px>        Output width (default: 1280).\n"
    "  --height <px>       Output height (default: 800).\n"
    "  --size <WxH>        Set width and height together.\n"
    "  -h, --help          Show this help.\n",
    argv0);
}

static bool flecsParsePositiveI32(
  const char *arg,
  int32_t *out)
{
  char *end = NULL;
  long value = strtol(arg, &end, 10);
  if (end == arg || *end != '\0' || value <= 0 || value > INT_MAX) {
    return false;
  }

  *out = (int32_t)value;
  return true;
}

static bool flecsParseSize(
  const char *arg,
  int32_t *width,
  int32_t *height)
{
  int32_t w = 0;
  int32_t h = 0;
  char tail = '\0';

  if (sscanf(arg, "%dx%d%c", &w, &h, &tail) != 2 || w <= 0 || h <= 0) {
    return false;
  }

  *width = w;
  *height = h;
  return true;
}

static int flecsParseArgs(
  int argc,
  char *argv[],
  FlecsAppOptions *options)
{
  for (int i = 1; i < argc; i ++) {
    const char *arg = argv[i];

    if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) {
      flecsPrintUsage(argv[0]);
      return 1;
    }

    if (!strcmp(arg, "--frame-out")) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Missing value for --frame-out\n");
        return -1;
      }
      options->frame_output_path = argv[++ i];
      continue;
    }

    if (!strcmp(arg, "--scene")) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Missing value for --scene\n");
        return -1;
      }
      options->scene_path = argv[++ i];
      continue;
    }

    if (!strcmp(arg, "--width")) {
      if (i + 1 >= argc || !flecsParsePositiveI32(argv[i + 1], &options->width)) {
        fprintf(stderr, "Invalid value for --width\n");
        return -1;
      }
      i ++;
      continue;
    }

    if (!strcmp(arg, "--height")) {
      if (i + 1 >= argc || !flecsParsePositiveI32(argv[i + 1], &options->height)) {
        fprintf(stderr, "Invalid value for --height\n");
        return -1;
      }
      i ++;
      continue;
    }

    if (!strcmp(arg, "--size")) {
      if (i + 1 >= argc || !flecsParseSize(argv[i + 1], &options->width, &options->height)) {
        fprintf(stderr, "Invalid value for --size, expected WxH\n");
        return -1;
      }
      i ++;
      continue;
    }

    fprintf(stderr, "Unknown argument: %s\n", arg);
    return -1;
  }

  return 0;
}

#ifdef __EMSCRIPTEN__
static void flecsWasmFrame(void *arg) {
  ecs_world_t *world = arg;
  if (!ecs_progress(world, 0)) {
    emscripten_cancel_main_loop();
  }
}
#endif

int main(
  int argc,
  char *argv[])
{
  FlecsAppOptions options = {
    .width = 1280,
    .height = 800
  };

  int parse_result = flecsParseArgs(argc, argv, &options);
  if (parse_result != 0) {
    return parse_result > 0 ? 0 : 1;
  }

  ecs_world_t *world = ecs_init();
#ifndef __EMSCRIPTEN__
  ECS_IMPORT(world, FlecsStats);
#endif
  ECS_IMPORT(world, FlecsScriptMath);
  ECS_IMPORT(world, FlecsEngine);

  if (!options.frame_output_path) {
    ecs_log_set_level(0);
  }

  ecs_entity_t surface = ecs_entity(world, { .name = "surface" });
  ecs_set(world, surface, FlecsSurface, {
    .title = "Hello World",
    .width = options.width,
    .height = options.height,
    .resolution_scale = 1,
    .vsync = true,
    .msaa = FlecsMsaa4x,
    .write_to_file = options.frame_output_path,
  });

  if (options.scene_path) {
    ecs_entity_t s = ecs_script(world, {
      .filename = options.scene_path
    });
    if (!s) {
      ecs_err("failed to load script\n");
    }
  }

#ifdef __EMSCRIPTEN__
  emscripten_set_main_loop_arg(
      (em_arg_callback_func)flecsWasmFrame, world, 0, 1);
#else
  if (!options.frame_output_path) {
    ecs_singleton_set(world, EcsRest, {0});
  }
  while (ecs_progress(world, 0)) {}
#endif

  ecs_log_set_level(-1);

  return ecs_fini(world);
}
