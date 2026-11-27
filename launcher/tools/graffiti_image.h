/*
 * Pictures read for JSRF-PC Tools' graffiti page -- see graffiti_image.c
 * (b3 row 35): JPEG, PNG, WebP, AVIF, SVG and TIFF, each through its own
 * library in third_party, into 8-bit RGBA as graffiti_fit.h takes it.
 */
#ifndef GRAFFITI_IMAGE_H
#define GRAFFITI_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    GRAFFITI_IMAGE_NONE,    /* not a picture the page reads */
    GRAFFITI_IMAGE_JPEG,
    GRAFFITI_IMAGE_PNG,
    GRAFFITI_IMAGE_WEBP,
    GRAFFITI_IMAGE_AVIF,
    GRAFFITI_IMAGE_SVG,
    GRAFFITI_IMAGE_TIFF,
    GRAFFITI_IMAGE_KINDS
};

/* The longest side a picture may have, and the most pixels. Bigger ones
 * are refused, as are ones the libraries can't read. */
#define GRAFFITI_IMAGE_SIDE     16384
#define GRAFFITI_IMAGE_PIXELS   (64L * 1000 * 1000)
/* An SVG is drawn with its longer side this many pixels, its shape kept:
 * twice XL's width, so any size is fitted by shrinking. */
#define GRAFFITI_SVG_SIDE       2048

typedef struct graffiti_image {
    int width, height;
    uint8_t *rgba;          /* malloc'ed, width x height x 4, alpha not premultiplied */
    int kind;               /* GRAFFITI_IMAGE_* */
    int opaque;             /* 1 when every pixel is solid: white to clear is offered */
} graffiti_image;

/* What a file is, from its first bytes (at least 12; 512 for an SVG): the
 * format's own signature for JPEG, PNG, WebP, AVIF and TIFF, and an <svg
 * element near the start, after any XML declaration, comments and doctype,
 * for SVG. GRAFFITI_IMAGE_NONE for anything else, whatever its name. */
int graffiti_image_kind(const uint8_t *data, size_t n);

/* 1 when a file's name ends in one of the formats' extensions, without
 * regard to case: what a dropped folder is walked for before the content is
 * looked at. */
int graffiti_image_name_ok(const char *name);

/* "JPEG", "PNG", "WebP", "AVIF", "SVG", "TIFF", or "" for none. */
const char *graffiti_image_kind_name(int kind);

/* A file's picture: its first frame or page, turned upright where the file
 * says how it was taken (a JPEG's EXIF orientation, an AVIF's rotation and
 * mirror, a TIFF's orientation), in 8 bits a channel. 0, or -1 with a
 * sentence in why. */
int graffiti_image_read(const uint8_t *data, size_t n, graffiti_image *out, char *why, size_t wn);

void graffiti_image_free(graffiti_image *img);

/* A JPEG's EXIF orientation, 1 to 8, or 1 when it has none (exposed for the
 * tests). */
int graffiti_jpeg_orientation(const uint8_t *data, size_t n);
/* rgba (w x h) turned by an EXIF orientation, 1 to 8, into a new buffer;
 * *w and *h swapped for 5 to 8. NULL when memory runs out. */
uint8_t *graffiti_orient(const uint8_t *rgba, int *w, int *h, int orientation);

#ifdef __cplusplus
}
#endif

#endif
