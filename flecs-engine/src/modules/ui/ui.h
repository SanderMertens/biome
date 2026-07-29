#ifndef FLECS_ENGINE_UI_INTERNAL_H
#define FLECS_ENGINE_UI_INTERNAL_H

#include "../../private.h"

float flecsEngine_uiAlignOffset(
    FlecsUiAlign halign,
    float avail,
    float content);

float flecsEngine_uiValignOffset(
    FlecsUiVAlign valign,
    float avail,
    float content);

#endif
