#include "hdri_loader.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stb_image.h>
#include <tinyexr.h>

static bool flecsHdriPathHasExt(
    const char *path,
    const char *ext)
{
    size_t path_len = strlen(path);
    size_t ext_len = strlen(ext);
    if (path_len < ext_len) {
        return false;
    }

    const char *suffix = path + (path_len - ext_len);
    for (size_t i = 0; i < ext_len; i ++) {
        unsigned char a = (unsigned char)suffix[i];
        unsigned char b = (unsigned char)ext[i];
        if (tolower(a) != tolower(b)) {
            return false;
        }
    }

    return true;
}

static bool flecsHdriLoadHdr(
    const char *path,
    FlecsHdriImage *out_image)
{
    int width, height, channels;
    float *pixels = stbi_loadf(path, &width, &height, &channels, 4);
    if (!pixels) {
        fprintf(stderr, "stb_image: failed to load '%s': %s\n",
            path, stbi_failure_reason());
        return false;
    }

    out_image->width = width;
    out_image->height = height;
    out_image->pixels_rgba32f = pixels;
    return true;
}

static bool flecsHdriLoadExr(
    const char *path,
    FlecsHdriImage *out_image)
{
    float *pixels = NULL;
    int width, height;
    const char *err = NULL;

    int ret = LoadEXR(&pixels, &width, &height, path, &err);
    if (ret != TINYEXR_SUCCESS) {
        if (err) {
            fprintf(stderr, "tinyexr: failed to load '%s': %s\n", path, err);
            FreeEXRErrorMessage(err);
        }
        return false;
    }

    out_image->width = width;
    out_image->height = height;
    out_image->pixels_rgba32f = pixels;
    return true;
}

bool flecsHdriLoad(
    const char *path,
    FlecsHdriImage *out_image)
{
    if (!path || !out_image) {
        return false;
    }

    out_image->width = 0;
    out_image->height = 0;
    out_image->pixels_rgba32f = NULL;

    if (flecsHdriPathHasExt(path, ".hdr")) {
        return flecsHdriLoadHdr(path, out_image);
    } else if (flecsHdriPathHasExt(path, ".exr")) {
        return flecsHdriLoadExr(path, out_image);
    }

    return false;
}

void flecsHdriImageFree(
    FlecsHdriImage *image)
{
    if (!image) {
        return;
    }

    free(image->pixels_rgba32f);
    image->pixels_rgba32f = NULL;
    image->width = 0;
    image->height = 0;
}
