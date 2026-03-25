#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct Image {
    uint8_t *data;
    int      width;
    int      height;
    int      channels;   /* always 3 (RGB) after load */
} Image;

Image *image_new(int width, int height, int channels);
Image *image_load(const char *path);
int    image_save(const Image *img, const char *path, int jpeg_quality);
void   image_free(Image *img);

static inline uint8_t *image_px(Image *img, int x, int y) {
    return img->data + ((size_t)y * img->width + x) * img->channels;
}
