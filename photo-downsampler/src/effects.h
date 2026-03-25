#pragma once
#include "image.h"

/*
 * Vignette: darkens edges with a smooth radial falloff.
 * strength: 0.0 (none) … 1.0 (heavy)
 */
void effect_vignette(Image *img, float strength);

/*
 * Film grain: adds pseudo-random luminance noise.
 * strength: 0.0 (none) … 1.0 (heavy)
 * seed:     0 picks a default seed
 */
void effect_grain(Image *img, float strength, unsigned int seed);

/*
 * Ordered (Bayer 8×8) dithering: retro halftone look.
 * Quantizes each channel to 8 levels.
 */
void effect_dither_ordered(Image *img);

/*
 * Floyd-Steinberg error-diffusion dithering.
 * Quantizes each channel to 4 levels for a bold graphic look.
 */
void effect_dither_fs(Image *img);
