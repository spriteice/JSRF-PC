/*
 * The game's five graffiti sizes, and a picture fitted to one -- see
 * graffiti_fit.c. JSRF-PC Tools' graffiti page (b3 row 35).
 *
 * Pictures are 8-bit RGBA, four bytes a pixel, rows top to bottom, alpha
 * not premultiplied.
 */
#ifndef GRAFFITI_FIT_H
#define GRAFFITI_FIT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The size classes, as the game numbers them (a graffiti save's first byte). */
enum {
    GRAFFITI_SS,
    GRAFFITI_S,
    GRAFFITI_M,
    GRAFFITI_L,
    GRAFFITI_XL,
    GRAFFITI_SIZES
};

/* A size's picture, in pixels: SS and S 128 x 128, M 384 x 256, L 512 x 256,
 * XL 1024 x 256. 0 for a size that isn't one. */
int graffiti_width(int size);
int graffiti_height(int size);
/* "SS", "S", "M", "L" or "XL"; "?" for a size that isn't one. */
const char *graffiti_size_name(int size);

/* Auto's size for a picture of w x h: the one whose shape is nearest, by
 * the ratio's logarithm, of S (1:1), M (3:2), L (2:1) and XL (4:1). A
 * picture as tall as wide, or taller, is S; SS is only ever chosen by hand.
 * A ratio exactly between two shapes takes the squarer. */
int graffiti_auto_size(int w, int h);

/* How a picture is fitted to its size. */
enum {
    GRAFFITI_FIT_MARGINS,   /* the whole picture, unsquashed, centred, the gaps clear */
    GRAFFITI_FIT_CROP,      /* a box of the size's shape, filling the size */
    GRAFFITI_FIT_STRETCH    /* the whole picture, squashed to fill the size */
};

/* A box in the picture, in its pixels: from (x, y), w across and h down. */
typedef struct graffiti_box {
    double x, y, w, h;
} graffiti_box;

/* The crop box of size's shape that crop starts with: the largest that
 * fits in a w x h picture, centred. */
graffiti_box graffiti_crop_default(int w, int h, int size);

/* A crop box made to fit: size's shape kept, the box no bigger than the
 * picture and moved inside it, at least a pixel each way. */
graffiti_box graffiti_crop_clamp(graffiti_box box, int w, int h, int size);

/* The picture (rgba, w x h) fitted to size's exact pixels, into out
 * (graffiti_width(size) x graffiti_height(size) x 4 bytes). crop is the box
 * for GRAFFITI_FIT_CROP (NULL for graffiti_crop_default's); the other modes
 * take the whole picture. Shrinking weighs each source pixel by how much of
 * it a target pixel covers, and enlarging is bilinear, both with
 * premultiplied alpha, so clear pixels don't darken an edge. Then the game's
 * all-or-nothing transparency: a pixel half clear or more is clear (alpha
 * 0), any other solid (alpha 255). A clear pixel's colour is 0. 0, or -1
 * when a size or picture isn't one, or memory runs out. */
int graffiti_fit(const uint8_t *rgba, int w, int h, int size, int mode,
                 const graffiti_box *crop, uint8_t *out);

/* A picture (rgba, w x h) scaled to ow x oh, into out (ow x oh x 4 bytes),
 * as graffiti_fit scales, but with its alpha kept: for the page's working
 * copy of a big picture, and its previews. 0, or -1 as graffiti_fit. */
int graffiti_scale(const uint8_t *rgba, int w, int h, uint8_t *out, int ow, int oh);

/* White made clear, in place, in a w x h picture: each solid pixel whose red,
 * green and blue are all at least 255 - tolerance, when it can be reached
 * from the picture's edge through such pixels or clear ones (alpha 0), so
 * white inside a design stays. everywhere set makes every such pixel clear,
 * reached or not. The pixels made clear, or -1 when memory runs out. */
long graffiti_white_to_clear(uint8_t *rgba, int w, int h, int tolerance, int everywhere);

/* The tolerance the page starts with. */
#define GRAFFITI_WHITE_TOLERANCE 24

#ifdef __cplusplus
}
#endif

#endif
