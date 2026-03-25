#include "resample.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define CLAMP(x, lo, hi) ((x) < (lo) ? (lo) : (x) > (hi) ? (hi) : (x))

/* =========================================================================
   Separable filter infrastructure (used by Lanczos and bicubic)
   ========================================================================= */

typedef struct {
    int    *idx;   /* source pixel indices */
    double *wt;    /* corresponding normalized weights */
    int     n;     /* number of contributions */
} Contrib;

static Contrib *make_contribs(int src_size, int dst_size,
                               double (*kernel)(double x, void *ctx),
                               void *ctx, double support)
{
    Contrib *c = calloc((size_t)dst_size, sizeof(*c));
    if (!c) return NULL;

    double scale  = (double)dst_size / src_size;
    /* When downsampling, widen support and scale kernel to low-pass filter */
    double fscale = scale < 1.0 ? scale : 1.0;
    double supp   = support / fscale;

    for (int i = 0; i < dst_size; i++) {
        double center = (i + 0.5) / scale - 0.5;
        int left  = (int)ceil(center - supp);
        int right = (int)floor(center + supp);
        int n     = right - left + 1;

        c[i].idx = malloc((size_t)n * sizeof(int));
        c[i].wt  = malloc((size_t)n * sizeof(double));
        c[i].n   = n;
        if (!c[i].idx || !c[i].wt) {
            /* cleanup on OOM */
            free(c[i].idx); free(c[i].wt); c[i].n = 0;
            continue;
        }

        double sum = 0.0;
        for (int j = 0; j < n; j++) {
            int src_j = CLAMP(left + j, 0, src_size - 1);
            double w  = kernel((center - (left + j)) * fscale, ctx);
            c[i].idx[j] = src_j;
            c[i].wt[j]  = w;
            sum += w;
        }
        /* Normalize so weights sum to 1 */
        if (sum != 0.0)
            for (int j = 0; j < n; j++)
                c[i].wt[j] /= sum;
    }
    return c;
}

static void free_contribs(Contrib *c, int n)
{
    if (!c) return;
    for (int i = 0; i < n; i++) {
        free(c[i].idx);
        free(c[i].wt);
    }
    free(c);
}

/* Apply pre-computed horizontal + vertical contributions.
   Uses a float intermediate buffer for the horizontal pass. */
static Image *apply_contribs(const Image *src,
                              Contrib *hc, int dst_w,
                              Contrib *vc, int dst_h)
{
    int ch = src->channels;

    /* Horizontal pass → float buffer [src_h × dst_w × ch] */
    float *tmp = malloc((size_t)src->height * dst_w * ch * sizeof(float));
    if (!tmp) return NULL;

    for (int y = 0; y < src->height; y++) {
        for (int x = 0; x < dst_w; x++) {
            float *out = tmp + ((size_t)y * dst_w + x) * ch;
            for (int c = 0; c < ch; c++) out[c] = 0.0f;
            for (int k = 0; k < hc[x].n; k++) {
                const uint8_t *p = src->data +
                    ((size_t)y * src->width + hc[x].idx[k]) * ch;
                double w = hc[x].wt[k];
                for (int c = 0; c < ch; c++)
                    out[c] += (float)(p[c] * w);
            }
        }
    }

    /* Vertical pass → output image [dst_h × dst_w × ch] */
    Image *dst = image_new(dst_w, dst_h, ch);
    if (!dst) { free(tmp); return NULL; }

    for (int y = 0; y < dst_h; y++) {
        for (int x = 0; x < dst_w; x++) {
            double acc[4] = {0, 0, 0, 0};
            for (int k = 0; k < vc[y].n; k++) {
                const float *p = tmp + ((size_t)vc[y].idx[k] * dst_w + x) * ch;
                double w = vc[y].wt[k];
                for (int c = 0; c < ch; c++)
                    acc[c] += p[c] * w;
            }
            uint8_t *out = dst->data + ((size_t)y * dst_w + x) * ch;
            for (int c = 0; c < ch; c++)
                out[c] = (uint8_t)CLAMP((int)(acc[c] + 0.5), 0, 255);
        }
    }

    free(tmp);
    return dst;
}

/* =========================================================================
   Lanczos kernel (a=3)
   ========================================================================= */

static double sinc(double x)
{
    if (x == 0.0) return 1.0;
    x *= M_PI;
    return sin(x) / x;
}

static double lanczos_kernel(double x, void *ctx)
{
    int a = *(int *)ctx;
    if (x < 0.0) x = -x;
    if (x >= a)  return 0.0;
    return sinc(x) * sinc(x / a);
}

static Image *resize_lanczos(const Image *src, int dst_w, int dst_h)
{
    int a = 3;  /* Lanczos-3 */
    Contrib *hc = make_contribs(src->width,  dst_w, lanczos_kernel, &a, a);
    Contrib *vc = make_contribs(src->height, dst_h, lanczos_kernel, &a, a);

    Image *dst = NULL;
    if (hc && vc)
        dst = apply_contribs(src, hc, dst_w, vc, dst_h);

    free_contribs(hc, dst_w);
    free_contribs(vc, dst_h);
    return dst;
}

/* =========================================================================
   Bicubic (Mitchell-Netravali B=1/3, C=1/3)
   ========================================================================= */

static double mitchell_kernel(double x, void *ctx)
{
    (void)ctx;
    const double B = 1.0/3, C = 1.0/3;
    x = fabs(x);
    if (x < 1.0)
        return ((12 - 9*B - 6*C)*x*x*x + (-18 + 12*B + 6*C)*x*x + (6 - 2*B)) / 6.0;
    if (x < 2.0)
        return ((-B - 6*C)*x*x*x + (6*B + 30*C)*x*x +
                (-12*B - 48*C)*x + (8*B + 24*C)) / 6.0;
    return 0.0;
}

static Image *resize_bicubic(const Image *src, int dst_w, int dst_h)
{
    Contrib *hc = make_contribs(src->width,  dst_w, mitchell_kernel, NULL, 2.0);
    Contrib *vc = make_contribs(src->height, dst_h, mitchell_kernel, NULL, 2.0);

    Image *dst = NULL;
    if (hc && vc)
        dst = apply_contribs(src, hc, dst_w, vc, dst_h);

    free_contribs(hc, dst_w);
    free_contribs(vc, dst_h);
    return dst;
}

/* =========================================================================
   Bilinear
   ========================================================================= */

static Image *resize_bilinear(const Image *src, int dst_w, int dst_h)
{
    Image *dst = image_new(dst_w, dst_h, src->channels);
    if (!dst) return NULL;
    int ch = src->channels;

    for (int y = 0; y < dst_h; y++) {
        double fy = (y + 0.5) * src->height / (double)dst_h - 0.5;
        int y0 = CLAMP((int)fy,     0, src->height - 1);
        int y1 = CLAMP(y0 + 1,      0, src->height - 1);
        double dy = fy - floor(fy);

        for (int x = 0; x < dst_w; x++) {
            double fx = (x + 0.5) * src->width / (double)dst_w - 0.5;
            int x0 = CLAMP((int)fx, 0, src->width - 1);
            int x1 = CLAMP(x0 + 1, 0, src->width - 1);
            double dx = fx - floor(fx);

            const uint8_t *p00 = src->data + ((size_t)y0 * src->width + x0) * ch;
            const uint8_t *p01 = src->data + ((size_t)y0 * src->width + x1) * ch;
            const uint8_t *p10 = src->data + ((size_t)y1 * src->width + x0) * ch;
            const uint8_t *p11 = src->data + ((size_t)y1 * src->width + x1) * ch;
            uint8_t *out = dst->data + ((size_t)y * dst_w + x) * ch;

            for (int c = 0; c < ch; c++) {
                double v = p00[c]*(1-dx)*(1-dy) + p01[c]*dx*(1-dy)
                         + p10[c]*(1-dx)*dy     + p11[c]*dx*dy;
                out[c] = (uint8_t)CLAMP((int)(v + 0.5), 0, 255);
            }
        }
    }
    return dst;
}

/* =========================================================================
   Nearest neighbor  (pixel-art / lo-fi aesthetic)
   ========================================================================= */

static Image *resize_nearest(const Image *src, int dst_w, int dst_h)
{
    Image *dst = image_new(dst_w, dst_h, src->channels);
    if (!dst) return NULL;
    int ch = src->channels;

    for (int y = 0; y < dst_h; y++) {
        int sy = CLAMP((int)((y + 0.5) * src->height / dst_h), 0, src->height-1);
        for (int x = 0; x < dst_w; x++) {
            int sx = CLAMP((int)((x + 0.5) * src->width / dst_w), 0, src->width-1);
            memcpy(dst->data + ((size_t)y * dst_w + x) * ch,
                   src->data + ((size_t)sy * src->width + sx) * ch, (size_t)ch);
        }
    }
    return dst;
}

/* =========================================================================
   Box filter  (area average — best for large downscale ratios)
   ========================================================================= */

static Image *resize_box(const Image *src, int dst_w, int dst_h)
{
    /* Upsample path: fall back to bilinear */
    if (dst_w > src->width || dst_h > src->height)
        return resize_bilinear(src, dst_w, dst_h);

    Image *dst = image_new(dst_w, dst_h, src->channels);
    if (!dst) return NULL;
    int ch = src->channels;

    double sx = (double)src->width  / dst_w;
    double sy = (double)src->height / dst_h;

    for (int y = 0; y < dst_h; y++) {
        int iy0 = (int)(y * sy);
        int iy1 = CLAMP((int)ceil((y + 1) * sy) - 1, 0, src->height - 1);

        for (int x = 0; x < dst_w; x++) {
            int ix0 = (int)(x * sx);
            int ix1 = CLAMP((int)ceil((x + 1) * sx) - 1, 0, src->width - 1);

            double acc[4] = {0, 0, 0, 0};
            int count = 0;
            for (int iy = iy0; iy <= iy1; iy++) {
                for (int ix = ix0; ix <= ix1; ix++) {
                    const uint8_t *p = src->data +
                        ((size_t)iy * src->width + ix) * ch;
                    for (int c = 0; c < ch; c++) acc[c] += p[c];
                    count++;
                }
            }
            uint8_t *out = dst->data + ((size_t)y * dst_w + x) * ch;
            for (int c = 0; c < ch; c++)
                out[c] = (uint8_t)(acc[c] / count + 0.5);
        }
    }
    return dst;
}

/* =========================================================================
   Crop / pad helpers
   ========================================================================= */

static Image *crop_square(const Image *src)
{
    int side = src->width < src->height ? src->width : src->height;
    int ox   = (src->width  - side) / 2;
    int oy   = (src->height - side) / 2;
    int ch   = src->channels;

    Image *out = image_new(side, side, ch);
    if (!out) return NULL;

    for (int y = 0; y < side; y++) {
        memcpy(out->data + (size_t)y * side * ch,
               src->data + ((size_t)(y + oy) * src->width + ox) * ch,
               (size_t)side * ch);
    }
    return out;
}

static Image *pad_square(const Image *src)
{
    int side = src->width > src->height ? src->width : src->height;
    int ox   = (side - src->width)  / 2;
    int oy   = (side - src->height) / 2;
    int ch   = src->channels;

    Image *out = image_new(side, side, ch);
    if (!out) return NULL;
    memset(out->data, 0, (size_t)side * side * ch);  /* black fill */

    for (int y = 0; y < src->height; y++) {
        memcpy(out->data + ((size_t)(y + oy) * side + ox) * ch,
               src->data + (size_t)y * src->width * ch,
               (size_t)src->width * ch);
    }
    return out;
}

/* =========================================================================
   Public API
   ========================================================================= */

static Image *do_resize(const Image *src, int w, int h, ResampleAlgo algo)
{
    switch (algo) {
    case ALGO_NEAREST:  return resize_nearest(src, w, h);
    case ALGO_BOX:      return resize_box(src, w, h);
    case ALGO_BILINEAR: return resize_bilinear(src, w, h);
    case ALGO_BICUBIC:  return resize_bicubic(src, w, h);
    case ALGO_LANCZOS:
    default:            return resize_lanczos(src, w, h);
    }
}

Image *image_to_square(const Image *src, int size, ResampleAlgo algo, int pad)
{
    Image *sq = pad ? pad_square(src) : crop_square(src);
    if (!sq) return NULL;
    if (sq->width == size) return sq;   /* already the right size */

    Image *out = do_resize(sq, size, size, algo);
    image_free(sq);
    return out;
}
