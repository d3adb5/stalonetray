/* -------------------------------
 * vim:tabstop=4:shiftwidth=4
 * image.c
 * Fri, 22 Jun 2007 23:32:27 +0700
 * -------------------------------
 * Simple XImage manipulation
 * interface
 * -------------------------------*/

#include "image.h"
#include "debug.h"

/***** Forward declarations  *****/

/* Depth-specialized versions of tinting routine */
int image_tint_32(CARD8 *data, size_t len, CARD32 pixel, CARD8 alpha);
int image_tint_24(CARD8 *data, size_t len, CARD32 pixel, CARD8 alpha);
int image_tint_16(CARD16 *data, size_t len, CARD32 pixel, CARD8 alpha);
int image_tint_15(CARD16 *data, size_t len, CARD32 pixel, CARD8 alpha);

/* Depth-specialized versions of compose routine */
int image_compose_32(CARD8 *data, CARD8 *bg, CARD8 *mask, size_t len);
int image_compose_24(CARD8 *data, CARD8 *bg, CARD8 *mask, size_t len);
int image_compose_16(CARD16 *data, CARD16 *bg, CARD8 *mask, size_t len);
int image_compose_15(CARD16 *data, CARD16 *bg, CARD8 *mask, size_t len);

/***** Interface level *****/

int image_tint(XImage *image, XColor *color, CARD8 alpha)
{
    switch (image->bits_per_pixel) {
    case 32:
        return image_tint_32((CARD8 *)image->data,
            image->width * image->height, color->pixel, alpha);
    case 24:
        return image_tint_24((CARD8 *)image->data,
            image->width * image->height, color->pixel, alpha);
    case 16:
        return image_tint_16((CARD16 *)image->data,
            image->width * image->height, color->pixel, alpha);
    case 15:
        return image_tint_15((CARD16 *)image->data,
            image->width * image->height, color->pixel, alpha);
    default:
        DIE_IE(("image_tint() called with unsupported depth %d\n",
            image->bits_per_pixel));
        return FAILURE;
    }
}

int image_compose(XImage *image, XImage *bg, CARD8 *mask)
{
    switch (image->bits_per_pixel) {
    case 32:
        return image_compose_32((CARD8 *)image->data, (CARD8 *)bg->data, mask,
            (image->width * image->height));
    case 24:
        return image_compose_24((CARD8 *)image->data, (CARD8 *)bg->data, mask,
            (image->width * image->height));
    case 16:
        return image_compose_16((CARD16 *)image->data, (CARD16 *)bg->data,
            mask, (image->width * image->height));
    case 15:
        return image_compose_15((CARD16 *)image->data, (CARD16 *)bg->data,
            mask, (image->width * image->height));
    default:
        DIE_IE(("image_compose() called with unsupported depth %d\n",
            image->bits_per_pixel));
        return FAILURE;
    }
}

CARD8 *image_create_alpha_mask(int ord, int w, int h)
{
    unsigned char *m, *ll, *ul;
    int x, y, bord;

    bord = (1 << ord) - 1;

    m = malloc(w * h);
    if (m == NULL) return NULL;
    memset(m, 255, w * h);

    /* Shade top and bootom of the rectangle */
    ul = m; /* top */
    ll = m + (w * (h - 1)); /* bottom */
    for (y = 0; y < bord; y++) {
        for (x = 0; x < w; x++) {
            ul[x] = ((unsigned int)ul[x] * (y + 1)) >> ord;
            ll[x] = ((unsigned int)ll[x] * (y + 1)) >> ord;
        }
        ul += w;
        ll -= w;
    }

    /* Shade left and right of the rectangle */
    for (x = 0; x < bord; x++) {
        ul = m + x; /* left side */
        ll = m + w - x - 1; /* right side */
        for (y = 0; y < h; y++) {
            *ul = ((unsigned int)*ul * (x + 1)) >> ord;
            *ll = ((unsigned int)*ll * (x + 1)) >> ord;
            ll += w;
            ul += w;
        }
    }

    return m;
}

/***** Implementation level *****/

#define sr16(p, a) (((CARD32)((p)&0xf800) * (a)))
#define sg16(p, a) (((CARD32)((p)&0x7e0) * (a)))
#define sb16(p, a) (((CARD32)((p)&0x1f) * (a)))

#define sr15(p, a) (((CARD32)((p)&0x7c00) * (a)))
#define sg15(p, a) (((CARD32)((p)&0x3e0) * (a)))
#define sb15(p, a) (((CARD32)((p)&0x1f) * (a)))

int image_tint_32(CARD8 *data, size_t len, CARD32 pixel, CARD8 alpha)
{
    CARD8 *p, tr, tg, tb, ralpha;

    ralpha = 255 - alpha;

#ifndef BIGENDIAN
    tr = ((pixel & 0x00ff0000) * alpha) >> 24;
    tg = ((pixel & 0x0000ff00) * alpha) >> 16;
    tb = ((pixel & 0x000000ff) * alpha) >> 8;
#else
    tr = ((pixel & 0x000000ff) * alpha) >> 8;
    tg = ((pixel & 0x0000ff00) * alpha) >> 16;
    tb = ((pixel & 0x00ff0000) * alpha) >> 24;
#endif

    /* traverse data by 4 bytes starting from the end */
    for (p = data + (len - 1) * 4; p >= data; p -= 4) {
#ifndef BIGENDIAN
        p[0] = (((CARD16)p[0] * ralpha) >> 8) + tb;
        p[1] = (((CARD16)p[1] * ralpha) >> 8) + tg;
        p[2] = (((CARD16)p[2] * ralpha) >> 8) + tr;
#else
        p[0] = (((CARD16)p[0] * ralpha) >> 8) + tr;
        p[1] = (((CARD16)p[1] * ralpha) >> 8) + tg;
        p[2] = (((CARD16)p[2] * ralpha) >> 8) + tb;
#endif
    }

    return SUCCESS;
}

int image_tint_24(CARD8 *data, size_t len, CARD32 pixel, CARD8 alpha)
{
    CARD8 *p, tr, tg, tb, ralpha;

    ralpha = 255 - alpha;

#ifndef BIGENDIAN
    tr = ((pixel & 0x00ff0000) * alpha) >> 24;
    tg = ((pixel & 0x0000ff00) * alpha) >> 16;
    tb = ((pixel & 0x000000ff) * alpha) >> 8;
#else
    tr = ((pixel & 0x000000ff) * alpha) >> 8;
    tg = ((pixel & 0x0000ff00) * alpha) >> 16;
    tb = ((pixel & 0x00ff0000) * alpha) >> 24;
#endif

    /* traverse data by 3 bytes starting from the end */
    for (p = data + (len - 1) * 3; p >= data; p -= 3) {
#ifndef BIGENDIAN
        p[0] = (((CARD16)p[0] * ralpha) >> 8) + tb;
        p[1] = (((CARD16)p[1] * ralpha) >> 8) + tg;
        p[2] = (((CARD16)p[2] * ralpha) >> 8) + tr;
#else
        p[0] = (((CARD16)p[0] * ralpha) >> 8) + tr;
        p[1] = (((CARD16)p[1] * ralpha) >> 8) + tg;
        p[2] = (((CARD16)p[2] * ralpha) >> 8) + tb;
#endif
    }

    return SUCCESS;
}

int image_tint_16(CARD16 *data, size_t len, CARD32 pixel, CARD8 alpha)
{
    CARD32 tr, tg, tb;
    CARD32 r, g, b;
    CARD16 *p;
    CARD8 ralpha;

    ralpha = 255 - alpha;
    tr = sr16(pixel, alpha);
    tg = sg16(pixel, alpha);
    tb = sb16(pixel, alpha);

    /* traverse data by 2 bytes starting from the end */
    for (p = data + len - 1; p >= data; p--) {
        r = sr16(*p, ralpha) + tr;
        g = sg16(*p, ralpha) + tg;
        b = sb16(*p, ralpha) + tb;
        *p = ((r >> 8) & 0xf800) | ((g >> 8) & 0x7e0) | ((b >> 8) & 0x1f);
    }

    return SUCCESS;
}

int image_tint_15(CARD16 *data, size_t len, CARD32 pixel, CARD8 alpha)
{
    CARD32 tr, tg, tb;
    CARD32 r, g, b;
    CARD16 *p;
    CARD8 ralpha;

    ralpha = 255 - alpha;
    tr = sr15(pixel, alpha);
    tg = sg15(pixel, alpha);
    tb = sb15(pixel, alpha);

    /* traverse data by 2 bytes starting from the end */
    for (p = data + len - 1; p >= data; p--) {
        r = sr15(*p, ralpha) + tr;
        g = sg15(*p, ralpha) + tg;
        b = sb15(*p, ralpha) + tb;
        *p = ((r >> 8) & 0x7c00) | ((g >> 8) & 0x3e0) | ((b >> 8) & 0x1f);
    }

    return SUCCESS;
}

int image_compose_32(CARD8 *data, CARD8 *bg, CARD8 *mask, size_t len)
{
    CARD8 *p, *b, *m;
    CARD16 a, ra;

    /* traverse data, bg by 4 bytes and mask by 1 byte starting from the end */
    for (p = data + (len - 1) * 4, b = bg + (len - 1) * 4, m = mask + len - 1;
         p != data - 4; p -= 4, b -= 4, m--) {
        a = *m;
        ra = 255 - a;
        p[0] = (p[0] * a + b[0] * ra) >> 8;
        p[1] = (p[1] * a + b[1] * ra) >> 8;
        p[2] = (p[2] * a + b[2] * ra) >> 8;
    }

    return SUCCESS;
}

int image_compose_24(CARD8 *data, CARD8 *bg, CARD8 *mask, size_t len)
{
    CARD8 *p, *b, *m;
    CARD16 a, ra;

    /* traverse data, bg by 3 bytes and mask by 1 byte starting from the end */
    for (p = data + (len - 1) * 3, b = bg + (len - 1) * 3, m = mask + len - 1;
         p != data - 3; p -= 3, b -= 3, m--) {
        a = *m;
        ra = 255 - a;
        p[0] = (p[0] * a + b[0] * ra) >> 8;
        p[1] = (p[1] * a + b[1] * ra) >> 8;
        p[2] = (p[2] * a + b[2] * ra) >> 8;
    }

    return SUCCESS;
}

int image_compose_16(CARD16 *data, CARD16 *bg, CARD8 *mask, size_t len)
{
    CARD32 r, g, b;
    CARD16 *p, *pb;
    CARD8 a, ra, *m;

    /* traverse data by 2 bytes starting from the end */
    for (p = data + len - 1, pb = bg + len - 1, m = mask + len - 1; p != data;
         p--, pb--, m--) {
        a = *m;
        ra = 255 - a;
        r = sr16(*p, a) + sr16(*pb, ra);
        g = sg16(*p, a) + sg16(*pb, ra);
        b = sb16(*p, a) + sb16(*pb, ra);
        *p = ((r >> 8) & 0xf800) | ((g >> 8) & 0x7e0) | ((b >> 8) & 0x1f);
    }

    return SUCCESS;
}

int image_compose_15(CARD16 *data, CARD16 *bg, CARD8 *mask, size_t len)
{
    CARD32 r, g, b;
    CARD16 *p, *pb;
    CARD8 a, ra, *m;

    /* traverse data by 2 bytes starting from the end */
    for (p = data + len - 1, pb = bg + len - 1, m = mask + len - 1; p != data;
         p--, pb--, m--) {
        a = *mask;
        ra = 255 - a;
        r = sr15(*p, a) + sr15(*pb, ra);
        g = sg15(*p, a) + sg15(*pb, ra);
        b = sb15(*p, a) + sb15(*pb, ra);
        *p = ((r >> 8) & 0x7c00) | ((g >> 8) & 0x3e0) | ((b >> 8) & 0x1f);
    }

    return SUCCESS;
}
