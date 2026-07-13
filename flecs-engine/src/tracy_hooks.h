#ifndef FLECS_ENGINE_TRACY_HOOKS_H
#define FLECS_ENGINE_TRACY_HOOKS_H

#ifdef FLECS_ENGINE_TRACY
  #include <tracy/TracyC.h>
  #include <string.h>
  #define FLECS_TRACY_ZONE_BEGIN(name) \
      TracyCZoneN(__tracy_zone_ctx, name, 1)
  #define FLECS_TRACY_ZONE_END \
      TracyCZoneEnd(__tracy_zone_ctx)
  #define FLECS_TRACY_ZONE_BEGIN_N(id, name) \
      TracyCZoneN(id, name, 1)
  #define FLECS_TRACY_ZONE_END_N(id) \
      TracyCZoneEnd(id)
  /* Begin a zone with a runtime-computed name. `static_name` is the base
   * category (compile-time) and `dynamic_name` is a runtime string used as
   * the display name of the zone. */
  #define FLECS_TRACY_ZONE_BEGIN_DYN(id, static_name, dynamic_name) \
      TracyCZoneN(id, static_name, 1); \
      do { \
          const char *__tr_n = (dynamic_name); \
          if (__tr_n) TracyCZoneName(id, __tr_n, strlen(__tr_n)); \
      } while (0)
  #define FLECS_TRACY_FRAME_MARK TracyCFrameMark
#else
  #define FLECS_TRACY_ZONE_BEGIN(name) (void)0
  #define FLECS_TRACY_ZONE_END (void)0
  #define FLECS_TRACY_ZONE_BEGIN_N(id, name) (void)0
  #define FLECS_TRACY_ZONE_END_N(id) (void)0
  #define FLECS_TRACY_ZONE_BEGIN_DYN(id, static_name, dynamic_name) \
      ((void)(dynamic_name))
  #define FLECS_TRACY_FRAME_MARK (void)0
#endif

#endif
