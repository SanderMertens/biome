#ifndef FLECS_ENGINE_HDRI_LOADER_H
#define FLECS_ENGINE_HDRI_LOADER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlecsHdriImage {
    int32_t width;
    int32_t height;
    float *pixels_rgba32f;
} FlecsHdriImage;

bool flecsHdriLoad(
    const char *path,
    FlecsHdriImage *out_image);

void flecsHdriImageFree(
    FlecsHdriImage *image);

#ifdef __cplusplus
}
#endif

#endif
