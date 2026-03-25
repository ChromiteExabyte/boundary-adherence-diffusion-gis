#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "image.h"

#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))
#include "resample.h"
#include "effects.h"

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [options] <input> <output>\n"
        "\n"
        "Aesthetic photo downsampler — always outputs a 1:1 (square) image.\n"
        "Supported formats: JPEG (.jpg / .jpeg), PNG (.png)\n"
        "\n"
        "Resize options:\n"
        "  -s <pixels>       Output size (default: 512)\n"
        "  -a <algo>         Resample algorithm (default: lanczos)\n"
        "                      lanczos   — highest quality, minimal aliasing\n"
        "                      bicubic   — sharp, good for mild downscale\n"
        "                      bilinear  — smooth and fast\n"
        "                      box       — area average, great for extreme downscale\n"
        "                      nearest   — pixel-art / lo-fi look\n"
        "  --pad             Pad shorter side with black instead of center-cropping\n"
        "\n"
        "Aesthetic effects (applied in order listed):\n"
        "  --vignette <f>    Radial edge darkening, strength 0.0–1.0 (try 0.5)\n"
        "  --grain <f>       Film grain strength 0.0–1.0 (try 0.15)\n"
        "  --dither-ordered  Bayer 8×8 ordered dithering (retro halftone)\n"
        "  --dither-fs       Floyd-Steinberg error diffusion (bold graphic look)\n"
        "\n"
        "Output options:\n"
        "  -q <1-100>        JPEG quality (default: 85)\n"
        "  -h, --help        Show this help\n"
        "\n"
        "Examples:\n"
        "  %s photo.jpg out.jpg\n"
        "  %s -s 256 -a nearest --dither-ordered photo.jpg retro.png\n"
        "  %s -s 1024 --vignette 0.6 --grain 0.12 photo.jpg aesthetic.jpg\n",
        prog, prog, prog, prog);
}

int main(int argc, char **argv)
{
    /* ---- defaults ---- */
    int          size         = 512;
    int          quality      = 85;
    int          pad          = 0;
    int          dither_ord   = 0;
    int          dither_fs    = 0;
    float        vignette     = 0.0f;
    float        grain        = 0.0f;
    ResampleAlgo algo         = ALGO_LANCZOS;
    const char  *input        = NULL;
    const char  *output       = NULL;

    /* ---- argument parsing ---- */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;

        } else if (!strcmp(argv[i], "-s")) {
            if (++i >= argc) { fprintf(stderr, "-s needs a value\n"); return 1; }
            size = atoi(argv[i]);
            if (size <= 0) { fprintf(stderr, "size must be > 0\n"); return 1; }

        } else if (!strcmp(argv[i], "-q")) {
            if (++i >= argc) { fprintf(stderr, "-q needs a value\n"); return 1; }
            quality = atoi(argv[i]);
            quality = CLAMP(quality, 1, 100);

        } else if (!strcmp(argv[i], "-a")) {
            if (++i >= argc) { fprintf(stderr, "-a needs a value\n"); return 1; }
            if      (!strcmp(argv[i], "nearest"))  algo = ALGO_NEAREST;
            else if (!strcmp(argv[i], "box"))       algo = ALGO_BOX;
            else if (!strcmp(argv[i], "bilinear"))  algo = ALGO_BILINEAR;
            else if (!strcmp(argv[i], "bicubic"))   algo = ALGO_BICUBIC;
            else if (!strcmp(argv[i], "lanczos"))   algo = ALGO_LANCZOS;
            else {
                fprintf(stderr, "Unknown algorithm '%s'. "
                        "Choose: nearest, box, bilinear, bicubic, lanczos\n", argv[i]);
                return 1;
            }

        } else if (!strcmp(argv[i], "--pad")) {
            pad = 1;

        } else if (!strcmp(argv[i], "--dither-ordered")) {
            dither_ord = 1;

        } else if (!strcmp(argv[i], "--dither-fs")) {
            dither_fs = 1;

        } else if (!strcmp(argv[i], "--vignette")) {
            if (++i >= argc) { fprintf(stderr, "--vignette needs a value\n"); return 1; }
            vignette = strtof(argv[i], NULL);

        } else if (!strcmp(argv[i], "--grain")) {
            if (++i >= argc) { fprintf(stderr, "--grain needs a value\n"); return 1; }
            grain = strtof(argv[i], NULL);

        } else if (!input) {
            input = argv[i];

        } else if (!output) {
            output = argv[i];

        } else {
            fprintf(stderr, "Unexpected argument: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (!input || !output) {
        usage(argv[0]);
        return 1;
    }

    /* ---- load ---- */
    Image *src = image_load(input);
    if (!src) {
        fprintf(stderr, "Failed to load: %s\n", input);
        return 1;
    }
    fprintf(stderr, "Loaded  %s  (%d×%d)\n", input, src->width, src->height);

    /* ---- square crop/pad + resize ---- */
    Image *out = image_to_square(src, size, algo, pad);
    image_free(src);
    if (!out) {
        fprintf(stderr, "Resize failed (out of memory?)\n");
        return 1;
    }

    /* ---- aesthetic effects ---- */
    if (vignette > 0.0f)  effect_vignette(out, vignette);
    if (grain    > 0.0f)  effect_grain(out, grain, 0);
    if (dither_ord)       effect_dither_ordered(out);
    if (dither_fs)        effect_dither_fs(out);

    /* ---- save ---- */
    if (image_save(out, output, quality) != 0) {
        fprintf(stderr, "Failed to save: %s\n", output);
        image_free(out);
        return 1;
    }
    fprintf(stderr, "Saved   %s  (%d×%d)\n", output, out->width, out->height);

    image_free(out);
    return 0;
}
