/*
 * image.c — load/save using stb_image (public domain, bundled in vendor/).
 *
 * Supports: JPEG, PNG, BMP, TGA, GIF (load); JPEG, PNG (save).
 * All images are normalized to 8-bit RGB on load.
 */
#define _POSIX_C_SOURCE 200112L
#include "image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Pull in stb implementations exactly once */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_GIF
#include "../vendor/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../vendor/stb_image_write.h"

/* =========================================================================
   Image lifecycle
   ========================================================================= */

Image *image_new(int width, int height, int channels)
{
    Image *img = calloc(1, sizeof(*img));
    if (!img) return NULL;
    img->width    = width;
    img->height   = height;
    img->channels = channels;
    img->data     = malloc((size_t)width * height * channels);
    if (!img->data) { free(img); return NULL; }
    return img;
}

void image_free(Image *img)
{
    if (!img) return;
    free(img->data);
    free(img);
}

/* =========================================================================
   Load
   ========================================================================= */

Image *image_load(const char *path)
{
    int w, h, orig_channels;
    /* Force 3-channel (RGB) output */
    uint8_t *data = stbi_load(path, &w, &h, &orig_channels, 3);
    if (!data) {
        fprintf(stderr, "Failed to load '%s': %s\n", path, stbi_failure_reason());
        return NULL;
    }

    Image *img   = calloc(1, sizeof(*img));
    if (!img) { stbi_image_free(data); return NULL; }
    img->width    = w;
    img->height   = h;
    img->channels = 3;
    img->data     = data;   /* stb malloc — freed via free() which is compatible */
    return img;
}

/* =========================================================================
   Save
   ========================================================================= */

static const char *ext_of(const char *path)
{
    const char *dot = strrchr(path, '.');
    return dot ? dot + 1 : "";
}

int image_save(const Image *img, const char *path, int jpeg_quality)
{
    const char *ext = ext_of(path);
    int ok = 0;

    if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) {
        ok = stbi_write_jpg(path, img->width, img->height,
                            img->channels, img->data, jpeg_quality);
    } else if (strcasecmp(ext, "png") == 0) {
        int stride = img->width * img->channels;
        ok = stbi_write_png(path, img->width, img->height,
                            img->channels, img->data, stride);
    } else {
        fprintf(stderr, "Unsupported output format: .%s  (use .jpg or .png)\n", ext);
        return -1;
    }

    if (!ok) {
        fprintf(stderr, "Failed to write '%s'\n", path);
        return -1;
    }
    return 0;
}
