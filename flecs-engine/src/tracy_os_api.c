#include <flecs_engine.h>

#ifdef FLECS_ENGINE_TRACY

#include <tracy/TracyC.h>
#include <string.h>

#define FLECS_ENGINE_TRACY_STACK_SIZE 128

static _Thread_local TracyCZoneCtx flecsEngine_tracyZoneStack[
    FLECS_ENGINE_TRACY_STACK_SIZE];
static _Thread_local int flecsEngine_tracyZoneStackTop = 0;

static void flecsEngine_tracyTracePush(
    const char *filename,
    size_t line,
    const char *name)
{
    if (!filename) filename = "";
    if (!name) name = "";

    uint64_t srcloc = ___tracy_alloc_srcloc_name(
        (uint32_t)line,
        filename, strlen(filename),
        "", 0,
        name, strlen(name),
        0);

    TracyCZoneCtx ctx = ___tracy_emit_zone_begin_alloc(srcloc, 1);

    if (flecsEngine_tracyZoneStackTop < FLECS_ENGINE_TRACY_STACK_SIZE) {
        flecsEngine_tracyZoneStack[flecsEngine_tracyZoneStackTop ++] = ctx;
    } else {
        ___tracy_emit_zone_end(ctx);
    }
}

static void flecsEngine_tracyTracePop(
    const char *filename,
    size_t line,
    const char *name)
{
    (void)filename;
    (void)line;
    (void)name;

    if (flecsEngine_tracyZoneStackTop > 0) {
        TracyCZoneCtx ctx = flecsEngine_tracyZoneStack[
            -- flecsEngine_tracyZoneStackTop];
        ___tracy_emit_zone_end(ctx);
    }
}

void flecsEngine_initTracy(void) {
    ecs_os_set_api_defaults();
    ecs_os_api_t os_api = ecs_os_get_api();
    os_api.perf_trace_push_ = flecsEngine_tracyTracePush;
    os_api.perf_trace_pop_ = flecsEngine_tracyTracePop;
    ecs_os_set_api(&os_api);
}

#else

void flecsEngine_initTracy(void) {
}

#endif
