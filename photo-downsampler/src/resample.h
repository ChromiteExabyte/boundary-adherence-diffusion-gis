#pragma once
#include "image.h"

typedef enum {
    ALGO_NEAREST,    /* pixel art / lo-fi */
    ALGO_BOX,        /* area average, fast */
    ALGO_BILINEAR,   /* smooth, fast */
    ALGO_BICUBIC,    /* sharp, good for slight downscale */
    ALGO_LANCZOS,    /* highest quality, default */
} ResampleAlgo;

/*
 * Convert src to a size×size square image.
 *
 * pad=0  →  center-crop the shorter dimension (no distortion, no borders)
 * pad=1  →  letterbox/pillarbox with black bars
 */
Image *image_to_square(const Image *src, int size, ResampleAlgo algo, int pad);
