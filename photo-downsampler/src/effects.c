#include "effects.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))

/* =========================================================================
   Vignette
   ========================================================================= */

void effect_vignette(Image *img, float strength)
{
    float cx = img->width  * 0.5f;
    float cy = img->height * 0.5f;
    /* Normalize distance to 1.0 at the corner */
    float r  = sqrtf(cx * cx + cy * cy);
    int ch   = img->channels;

    for (int y = 0; y < img->height; y++) {
        float dy = (y - cy) / r;
        for (int x = 0; x < img->width; x++) {
            float dx = (x - cx) / r;
            float d2 = dx*dx + dy*dy;   /* 0 at center, ~1 at corner */
            float v  = 1.0f - strength * d2;
            if (v < 0.0f) v = 0.0f;

            uint8_t *p = img->data + ((size_t)y * img->width + x) * ch;
            for (int c = 0; c < ch; c++)
                p[c] = (uint8_t)(p[c] * v + 0.5f);
        }
    }
}

/* =========================================================================
   Film grain  (xorshift PRNG for speed and reproducibility)
   ========================================================================= */

static uint32_t xorshift(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}

void effect_grain(Image *img, float strength, unsigned int seed)
{
    uint32_t state = seed ? (uint32_t)seed : 0xdeadbeef;
    int ch  = img->channels;
    int n   = img->width * img->height * ch;
    int amp = (int)(strength * 64.0f + 0.5f);
    if (amp == 0) return;

    for (int i = 0; i < n; i++) {
        /* Sum of two uniforms → triangle distribution, zero-mean */
        int32_t r0 = (int32_t)(xorshift(&state) & 0xFF) - 128;
        int32_t r1 = (int32_t)(xorshift(&state) & 0xFF) - 128;
        int32_t noise = (r0 + r1) * amp / 256;
        img->data[i] = (uint8_t)CLAMP((int32_t)img->data[i] + noise, 0, 255);
    }
}

/* =========================================================================
   Ordered dithering  (8×8 Bayer matrix)
   ========================================================================= */

/* Standard 8×8 Bayer threshold map, values 0-63 */
static const uint8_t BAYER8[8][8] = {
    {  0, 32,  8, 40,  2, 34, 10, 42 },
    { 48, 16, 56, 24, 50, 18, 58, 26 },
    { 12, 44,  4, 36, 14, 46,  6, 38 },
    { 60, 28, 52, 20, 62, 30, 54, 22 },
    {  3, 35, 11, 43,  1, 33,  9, 41 },
    { 51, 19, 59, 27, 49, 17, 57, 25 },
    { 15, 47,  7, 39, 13, 45,  5, 37 },
    { 63, 31, 55, 23, 61, 29, 53, 21 },
};

void effect_dither_ordered(Image *img)
{
    int ch = img->channels;
    for (int y = 0; y < img->height; y++) {
        for (int x = 0; x < img->width; x++) {
            /* Map Bayer value (0-63) to a bias in (-128, +124) */
            int bias = (int)BAYER8[y & 7][x & 7] * 4 - 128;
            uint8_t *p = img->data + ((size_t)y * img->width + x) * ch;
            for (int c = 0; c < ch; c++) {
                int v = (int)p[c] + bias;
                /* Quantize to 8 levels (0, 32, 64, …, 224) */
                v = (v / 32) * 32;
                p[c] = (uint8_t)CLAMP(v, 0, 224);
            }
        }
    }
}

/* =========================================================================
   Floyd-Steinberg error-diffusion dithering
   ========================================================================= */

void effect_dither_fs(Image *img)
{
    int w  = img->width;
    int h  = img->height;
    int ch = img->channels;

    /* Accumulate errors in float */
    float *buf = malloc((size_t)w * h * ch * sizeof(float));
    if (!buf) return;
    for (int i = 0; i < w * h * ch; i++)
        buf[i] = (float)img->data[i];

    /* Quantize to 4 levels per channel: 0, 85, 170, 255 */
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            for (int c = 0; c < ch; c++) {
                float old_val = buf[((size_t)y * w + x) * ch + c];
                float new_val = roundf(old_val / 85.0f) * 85.0f;
                new_val = CLAMP(new_val, 0.0f, 255.0f);
                buf[((size_t)y * w + x) * ch + c] = new_val;

                float err = old_val - new_val;
                /* Distribute error to right, bottom-left, bottom, bottom-right */
                if (x + 1 < w)
                    buf[((size_t)y * w + x + 1)     * ch + c] += err * (7.0f / 16);
                if (y + 1 < h) {
                    if (x > 0)
                        buf[((size_t)(y+1) * w + x - 1) * ch + c] += err * (3.0f / 16);
                    buf[((size_t)(y+1) * w + x    ) * ch + c] += err * (5.0f / 16);
                    if (x + 1 < w)
                        buf[((size_t)(y+1) * w + x + 1) * ch + c] += err * (1.0f / 16);
                }
            }
        }
    }

    for (int i = 0; i < w * h * ch; i++)
        img->data[i] = (uint8_t)CLAMP((int)(buf[i] + 0.5f), 0, 255);

    free(buf);
}
