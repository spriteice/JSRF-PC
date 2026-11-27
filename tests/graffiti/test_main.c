/* Run with: python tests/graffiti/run.py
 *
 * JSRF-PC Tools' graffiti page without the window: launcher/tools/
 * graffiti_fit.c and graffiti_save.c as they are, with the save core
 * (jsrf_save.c, the engine's HMAC-SHA1) and the setup's file helpers.
 *   1. The sizes, and Auto's choice either side of each turning point, for
 *      a square and for a tall picture.
 *   2. Fit, crop and stretch: the size's exact pixels, the margins clear,
 *      the crop box's shape and clamping.
 *   3. The resample: a flat colour stays flat, a fine checker shrinks to its
 *      mean, a clear neighbour doesn't darken an edge, and enlarging is
 *      bilinear.
 *   4. The game's all-or-nothing transparency at half clear and either side.
 *   5. White made clear: from the edge, a white middle kept; everywhere.
 *   6. RGB565, and a colour landing on the clear colour moved off it.
 *   7. Each size's save.dat: its length, header, preview, design, canvas
 *      (M's in 512-pixel rows), wall texture (decoded again: flat blocks
 *      exact, a gradient near, the alpha half exact), stickers and last 16
 *      bytes, written to the run's folder for run.py to check every
 *      signature with Python's own HMAC.
 *   8. The re-sign and the move to another slot: only the signatures, and
 *      the slot byte, change; a file of the wrong length, class or tag is
 *      refused untouched.
 *   9. Slots: names, folders (printed for run.py to hash itself), the
 *      garage's places, the taken ones found in a title folder, the first
 *      free, and none when all 280 are taken.
 *  10. A design's whole save written into a title folder, never over one
 *      that's there, and the icon found in a game folder whatever its case.
 *  11. The pictures: what a file is by its content, not its name; the names
 *      a folder is walked for; each format read pixel by pixel from
 *      tests/graffiti/pictures (made by make_pictures.py), the lossy ones
 *      within a tolerance; a JPEG's EXIF turn and the eight orientations;
 *      an animation's first frame; a damaged file, one too big and one
 *      that isn't a picture refused with a sentence; and one picture taken
 *      the whole way, to a save that verifies.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graffiti_fit.h"
#include "graffiti_image.h"
#include "graffiti_save.h"
#include "jsrf_save.h"
#include "setup.h"

static int failures;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (!(cond)) {                                                \
            failures++;                                               \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);               \
            printf(__VA_ARGS__);                                      \
            printf("\n");                                             \
        }                                                             \
    } while (0)

static uint8_t *picture(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    uint8_t *p = malloc((size_t)w * h * 4);
    for (size_t i = 0; i < (size_t)w * h; i++) {
        p[i * 4 + 0] = r;
        p[i * 4 + 1] = g;
        p[i * 4 + 2] = b;
        p[i * 4 + 3] = a;
    }
    return p;
}

static uint8_t *px(uint8_t *p, int w, int x, int y)
{
    return p + ((size_t)y * w + x) * 4;
}

static uint8_t *fitted(int size)
{
    return calloc((size_t)graffiti_width(size) * graffiti_height(size), 4);
}

/* ---- 1. sizes and Auto ------------------------------------------------------ */

static void test_sizes(void)
{
    static const int want[GRAFFITI_SIZES][2] = { { 128, 128 }, { 128, 128 }, { 384, 256 }, { 512, 256 }, { 1024, 256 } };
    for (int s = 0; s < GRAFFITI_SIZES; s++)
        CHECK(graffiti_width(s) == want[s][0] && graffiti_height(s) == want[s][1], "size %d is %dx%d", s,
              graffiti_width(s), graffiti_height(s));
    CHECK(graffiti_width(5) == 0 && graffiti_width(-1) == 0, "a size that isn't one has a width");
    CHECK(!strcmp(graffiti_size_name(GRAFFITI_XL), "XL") && !strcmp(graffiti_size_name(9), "?"), "names");

    CHECK(graffiti_auto_size(500, 500) == GRAFFITI_S, "a square isn't S");
    CHECK(graffiti_auto_size(300, 900) == GRAFFITI_S, "a tall picture isn't S");
    CHECK(graffiti_auto_size(0, 10) == GRAFFITI_S, "an empty picture isn't S");
    /* The turning points: between shapes 1 and 1.5, 1.5 and 2, 2 and 4, by
     * the ratio's logarithm, so at sqrt(1.5), sqrt(3) and sqrt(8). */
    static const struct { double at; int below, above; } turn[] = {
        { 1.224744871, GRAFFITI_S, GRAFFITI_M },
        { 1.732050808, GRAFFITI_M, GRAFFITI_L },
        { 2.828427125, GRAFFITI_L, GRAFFITI_XL },
    };
    for (int i = 0; i < 3; i++) {
        int h = 100000;
        int lo = (int)(turn[i].at * h * 0.999), hi = (int)(turn[i].at * h * 1.001);
        CHECK(graffiti_auto_size(lo, h) == turn[i].below, "just under %.3f isn't %s", turn[i].at,
              graffiti_size_name(turn[i].below));
        CHECK(graffiti_auto_size(hi, h) == turn[i].above, "just over %.3f isn't %s", turn[i].at,
              graffiti_size_name(turn[i].above));
    }
    CHECK(graffiti_auto_size(384, 256) == GRAFFITI_M && graffiti_auto_size(512, 256) == GRAFFITI_L &&
          graffiti_auto_size(1024, 256) == GRAFFITI_XL && graffiti_auto_size(9000, 100) == GRAFFITI_XL,
          "the sizes' own shapes aren't theirs");
}

/* ---- 2. fit, crop, stretch ---------------------------------------------------- */

static void test_modes(void)
{
    /* A 300 x 100 solid blue picture into M (384 x 256): fitted, it is 384 x
     * 128, rows 64 to 191, the rest clear. */
    uint8_t *p = picture(300, 100, 0, 0, 255, 255), *o = fitted(GRAFFITI_M);
    CHECK(graffiti_fit(p, 300, 100, GRAFFITI_M, GRAFFITI_FIT_MARGINS, NULL, o) == 0, "fit failed");
    int solid_rows = 0, first = -1;
    for (int y = 0; y < 256; y++) {
        int solid = 0;
        for (int x = 0; x < 384; x++)
            solid += px(o, 384, x, y)[3] == 255;
        CHECK(solid == 0 || solid == 384, "row %d is part solid (%d)", y, solid);
        if (solid) {
            solid_rows++;
            if (first < 0) first = y;
        }
    }
    CHECK(solid_rows == 128 && first == 64, "fitted rows %d from %d", solid_rows, first);
    CHECK(px(o, 384, 10, 10)[0] == 0 && px(o, 384, 10, 10)[2] == 0, "a margin's colour isn't 0");
    CHECK(px(o, 384, 200, 100)[2] == 255 && px(o, 384, 200, 100)[0] == 0, "the picture's colour changed");

    /* Stretched, it fills the size. */
    CHECK(graffiti_fit(p, 300, 100, GRAFFITI_M, GRAFFITI_FIT_STRETCH, NULL, o) == 0, "stretch failed");
    int clear = 0;
    for (int i = 0; i < 384 * 256; i++)
        clear += o[i * 4 + 3] == 0;
    CHECK(clear == 0, "stretched, %d pixels are clear", clear);
    free(p);

    /* A picture left half red, right half green, 200 x 100, cropped to S:
     * the default box is the middle 100 x 100, half each. A box on the
     * right is green only. */
    p = picture(200, 100, 255, 0, 0, 255);
    for (int y = 0; y < 100; y++)
        for (int x = 100; x < 200; x++) {
            px(p, 200, x, y)[0] = 0;
            px(p, 200, x, y)[1] = 255;
        }
    graffiti_box box = graffiti_crop_default(200, 100, GRAFFITI_S);
    CHECK(box.x == 50 && box.y == 0 && box.w == 100 && box.h == 100, "default box %g,%g %gx%g", box.x, box.y,
          box.w, box.h);
    free(o);
    o = fitted(GRAFFITI_S);
    CHECK(graffiti_fit(p, 200, 100, GRAFFITI_S, GRAFFITI_FIT_CROP, NULL, o) == 0, "crop failed");
    CHECK(px(o, 128, 10, 64)[0] == 255 && px(o, 128, 117, 64)[1] == 255, "the default crop isn't half and half");
    graffiti_box right = { 100, 0, 100, 100 };
    CHECK(graffiti_fit(p, 200, 100, GRAFFITI_S, GRAFFITI_FIT_CROP, &right, o) == 0, "crop failed");
    CHECK(px(o, 128, 0, 0)[1] == 255 && px(o, 128, 0, 0)[0] == 0, "the right box isn't green");

    /* Clamping: too big, off the picture, the wrong shape. */
    graffiti_box big = { -40, 30, 500, 20 };
    box = graffiti_crop_clamp(big, 200, 100, GRAFFITI_S);
    CHECK(box.w == 100 && box.h == 100 && box.x == 0 && box.y == 0, "clamped %g,%g %gx%g", box.x, box.y, box.w,
          box.h);
    graffiti_box late = { 190, 90, 50, 10 };
    box = graffiti_crop_clamp(late, 200, 100, GRAFFITI_L);
    CHECK(box.w == 50 && box.h == 25 && box.x == 150 && box.y == 75, "clamped %g,%g %gx%g", box.x, box.y,
          box.w, box.h);
    CHECK(graffiti_fit(p, 200, 100, 7, GRAFFITI_FIT_CROP, NULL, o) == -1, "a size that isn't one fitted");
    CHECK(graffiti_fit(p, 200, 100, GRAFFITI_S, 9, NULL, o) == -1, "a mode that isn't one fitted");
    free(p);
    free(o);
}

/* ---- 3 and 4. the resample and the threshold ---------------------------------- */

static void test_resample(void)
{
    uint8_t *p = picture(1000, 1000, 37, 140, 222, 255), *o = fitted(GRAFFITI_S);
    graffiti_fit(p, 1000, 1000, GRAFFITI_S, GRAFFITI_FIT_STRETCH, NULL, o);
    int off = 0;
    for (int i = 0; i < 128 * 128; i++)
        off += o[i * 4] != 37 || o[i * 4 + 1] != 140 || o[i * 4 + 2] != 222 || o[i * 4 + 3] != 255;
    CHECK(off == 0, "a flat colour shrunk isn't flat (%d off)", off);
    free(p);

    /* A one-pixel checker of black and white, 512 x 512, to 128: grey. */
    p = picture(512, 512, 0, 0, 0, 255);
    for (int y = 0; y < 512; y++)
        for (int x = (y & 1); x < 512; x += 2)
            memset(px(p, 512, x, y), 255, 3);
    graffiti_fit(p, 512, 512, GRAFFITI_S, GRAFFITI_FIT_STRETCH, NULL, o);
    int worst = 0;
    for (int i = 0; i < 128 * 128; i++)
        for (int c = 0; c < 3; c++) {
            int d = abs(o[i * 4 + c] - 128);
            if (d > worst) worst = d;
        }
    CHECK(worst <= 1, "a checker shrinks to %d off its mean", worst);
    free(p);

    /* Red, then clear black from column 191, 384 across, shrunk to 128: the
     * pixel at 63 covers two red and one clear, so it is solid, and red, not
     * the two-thirds red that averaging without the alpha would give. */
    p = picture(384, 128, 255, 0, 0, 255);
    for (int y = 0; y < 128; y++)
        for (int x = 191; x < 384; x++)
            memset(px(p, 384, x, y), 0, 4);
    graffiti_fit(p, 384, 128, GRAFFITI_S, GRAFFITI_FIT_STRETCH, NULL, o);
    CHECK(px(o, 128, 63, 5)[0] == 255 && px(o, 128, 63, 5)[3] == 255, "the edge's red is %d, alpha %d",
          px(o, 128, 63, 5)[0], px(o, 128, 63, 5)[3]);
    CHECK(px(o, 128, 64, 5)[3] == 0, "the clear part isn't clear");
    free(p);

    /* graffiti_scale keeps alpha: red, then clear, in columns of 2, halved
     * to 1 column each, then the same 4 x 1 shrunk to 1 x 1, half clear,
     * stays red at half alpha; and a size it's asked for is the size given. */
    p = picture(4, 1, 255, 0, 0, 255);
    memset(px(p, 4, 2, 0), 0, 8);
    {
        uint8_t q[4 * 4];
        CHECK(graffiti_scale(p, 4, 1, q, 2, 1) == 0 && q[0] == 255 && q[3] == 255 && q[7] == 0,
              "scaled: %d %d / %d", q[0], q[3], q[7]);
        CHECK(graffiti_scale(p, 4, 1, q, 1, 1) == 0 && q[0] == 255 && q[1] == 0 && q[3] >= 127 && q[3] <= 128,
              "half clear: %d, alpha %d", q[0], q[3]);
        CHECK(graffiti_scale(p, 4, 1, q, 0, 1) == -1 && graffiti_scale(NULL, 4, 1, q, 1, 1) == -1, "a size of 0");
    }
    free(p);
    p = picture(300, 200, 9, 99, 199, 255);
    {
        uint8_t *q = malloc(97 * 61 * 4);
        int off = 0;
        CHECK(graffiti_scale(p, 300, 200, q, 97, 61) == 0, "97 x 61");
        for (int i = 0; i < 97 * 61; i++)
            off += q[i * 4] != 9 || q[i * 4 + 1] != 99 || q[i * 4 + 2] != 199 || q[i * 4 + 3] != 255;
        CHECK(off == 0, "a flat colour scaled to 97 x 61 isn't flat (%d off)", off);
        free(q);
    }
    free(p);

    /* A solid green pixel and a clear one, 2 x 1, enlarged to S: bilinear
     * between them, and every solid pixel the green, never darker. */
    p = picture(2, 1, 0, 0, 0, 0);
    memcpy(px(p, 2, 0, 0), (uint8_t[]){ 0, 200, 0, 255 }, 4);
    graffiti_fit(p, 2, 1, GRAFFITI_S, GRAFFITI_FIT_STRETCH, NULL, o);
    CHECK(px(o, 128, 0, 0)[1] == 200 && px(o, 128, 0, 0)[3] == 255, "enlarged, the solid end isn't green");
    CHECK(px(o, 128, 127, 127)[3] == 0, "enlarged, the clear end isn't clear");
    int dark = 0, solid_across = 0;
    for (int i = 0; i < 128 * 128; i++)
        dark += o[i * 4 + 3] && o[i * 4 + 1] != 200;
    for (int x = 0; x < 128; x++)
        solid_across += px(o, 128, x, 0)[3] == 255;
    CHECK(dark == 0, "enlarged, %d solid pixels aren't the green", dark);
    /* Pixel centres at 0.5 and 1.5 of the picture: half solid at its middle,
     * so the first 64 columns are solid. */
    CHECK(solid_across == 64, "enlarged, %d columns solid, not 64", solid_across);
    free(p);

    /* The threshold. A 2 x 1 of a solid and a clear pixel, shrunk to... the
     * smallest target is 128 across, so shrink a 256 x 1 strip of pairs into
     * 128: each target pixel is the pair's mean. */
    for (int a = 0; a < 3; a++) {
        uint8_t alpha[3][2] = { { 255, 0 }, { 255, 1 }, { 254, 0 } };   /* half; just over; just under */
        p = picture(256, 256, 0, 0, 255, 0);
        for (int y = 0; y < 256; y++)
            for (int x = 0; x < 256; x++)
                px(p, 256, x, y)[3] = alpha[a][(x + y) & 1];
        graffiti_fit(p, 256, 256, GRAFFITI_S, GRAFFITI_FIT_STRETCH, NULL, o);
        int solid = 0;
        for (int i = 0; i < 128 * 128; i++)
            solid += o[i * 4 + 3] == 255;
        int want = a == 1 ? 128 * 128 : 0;
        CHECK(solid == want, "alpha %d and %d: %d solid, not %d", alpha[a][0], alpha[a][1], solid, want);
        free(p);
    }
    /* One source pixel at alpha 128 and at 127, kept at its own size. */
    p = picture(128, 128, 9, 9, 9, 128);
    graffiti_fit(p, 128, 128, GRAFFITI_S, GRAFFITI_FIT_STRETCH, NULL, o);
    CHECK(o[3] == 255 && o[0] == 9, "alpha 128 isn't solid, or its colour moved");
    for (int i = 0; i < 128 * 128; i++) p[i * 4 + 3] = 127;
    graffiti_fit(p, 128, 128, GRAFFITI_S, GRAFFITI_FIT_STRETCH, NULL, o);
    CHECK(o[3] == 0 && o[0] == 0, "alpha 127 isn't clear");
    free(p);
    free(o);
}

/* ---- 5. white made clear ------------------------------------------------------- */

static void test_white(void)
{
    /* 20 x 20 white, a black ring from 5 to 14, white inside it, and one
     * near-white pixel on the edge within the tolerance. */
    uint8_t *p = picture(20, 20, 255, 255, 255, 255);
    for (int y = 5; y < 15; y++)
        for (int x = 5; x < 15; x++)
            if (x == 5 || x == 14 || y == 5 || y == 14)
                memset(px(p, 20, x, y), 0, 3);
    memset(px(p, 20, 0, 0), 240, 3);
    long n = graffiti_white_to_clear(p, 20, 20, GRAFFITI_WHITE_TOLERANCE, 0);
    CHECK(n == 400 - 100, "%ld pixels made clear, not 300", n);
    CHECK(px(p, 20, 0, 0)[3] == 0 && px(p, 20, 19, 19)[3] == 0, "the background stayed");
    CHECK(px(p, 20, 9, 9)[3] == 255, "the white middle went");
    CHECK(px(p, 20, 5, 9)[3] == 255, "the ring went");
    /* A pixel just outside the tolerance stays. */
    free(p);
    p = picture(3, 1, 255, 255, 255, 255);
    memset(px(p, 3, 1, 0), 255 - GRAFFITI_WHITE_TOLERANCE - 1, 3);
    n = graffiti_white_to_clear(p, 3, 1, GRAFFITI_WHITE_TOLERANCE, 0);
    CHECK(n == 2 && px(p, 3, 1, 0)[3] == 255, "outside the tolerance: %ld made clear", n);
    free(p);
    /* Everywhere: the middle too. */
    p = picture(20, 20, 255, 255, 255, 255);
    for (int y = 5; y < 15; y++)
        for (int x = 5; x < 15; x++)
            if (x == 5 || x == 14 || y == 5 || y == 14)
                memset(px(p, 20, x, y), 0, 3);
    n = graffiti_white_to_clear(p, 20, 20, GRAFFITI_WHITE_TOLERANCE, 1);
    CHECK(n == 400 - 36 && px(p, 20, 9, 9)[3] == 0, "everywhere made %ld clear", n);
    /* A clear ring passes the fill through to the white it holds, which
     * touches no edge itself. */
    free(p);
    p = picture(5, 5, 255, 255, 255, 255);
    for (int y = 0; y < 5; y++)
        for (int x = 0; x < 5; x++)
            if (x == 0 || x == 4 || y == 0 || y == 4)
                px(p, 5, x, y)[3] = 0;
    n = graffiti_white_to_clear(p, 5, 5, GRAFFITI_WHITE_TOLERANCE, 0);
    CHECK(n == 9 && px(p, 5, 2, 2)[3] == 0, "through a clear ring: %ld", n);
    free(p);
}

/* ---- 6. RGB565 --------------------------------------------------------------- */

static void test_rgb565(void)
{
    CHECK(graffiti_rgb565(255, 255, 255) == 0xFFFF && graffiti_rgb565(0, 0, 0) == 0, "white or black");
    CHECK(graffiti_rgb565(255, 0, 0) == 0xF800 && graffiti_rgb565(0, 255, 0) == 0x07E0 &&
          graffiti_rgb565(0, 0, 255) == 0x001F, "a primary");
    CHECK(graffiti_rgb565(4, 0, 0) == 0 && graffiti_rgb565(5, 0, 0) == 0x0800, "rounding at the half");
    /* (198, 195, 198) rounds to 0xC618, the clear colour: moved a step of blue. */
    CHECK(graffiti_rgb565(198, 195, 198) == 0xC619, "the clear colour isn't moved: %04X",
          graffiti_rgb565(198, 195, 198));
    int clear = 0;
    for (int r = 0; r < 256; r += 1)
        for (int g = 0; g < 256; g += 3)
            for (int b = 0; b < 256; b += 1)
                clear += graffiti_rgb565((uint8_t)r, (uint8_t)g, (uint8_t)b) == GRAFFITI_CLEAR;
    CHECK(clear == 0, "%d colours come out clear", clear);
}

/* ---- 7. a save ------------------------------------------------------------------ */

static uint32_t get32(const uint8_t *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static float getf(const uint8_t *p)
{
    uint32_t v = get32(p);
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

/* A DXT3 block decoded, four-colour, into 16 RGBA. */
static void dxt3_decode(const uint8_t *b, uint8_t out[16][4])
{
    uint16_t c0 = get16(b + 8), c1 = get16(b + 10);
    uint32_t idx = get32(b + 12);
    int col[4][3];
    int e[2][3] = { { (c0 >> 11) * 255 / 31, (c0 >> 5 & 63) * 255 / 63, (c0 & 31) * 255 / 31 },
                    { (c1 >> 11) * 255 / 31, (c1 >> 5 & 63) * 255 / 63, (c1 & 31) * 255 / 31 } };
    for (int c = 0; c < 3; c++) {
        col[0][c] = e[0][c];
        col[1][c] = e[1][c];
        col[2][c] = (2 * e[0][c] + e[1][c]) / 3;
        col[3][c] = (e[0][c] + 2 * e[1][c]) / 3;
    }
    for (int k = 0; k < 16; k++) {
        int i = idx >> (2 * k) & 3;
        for (int c = 0; c < 3; c++) out[k][c] = (uint8_t)col[i][c];
        out[k][3] = (uint8_t)((b[k / 2] >> (k % 2 * 4) & 15) * 17);
    }
}

static const uint32_t blocks[GRAFFITI_SIZES][6] = {
    { 2048, 32768, 65536, 16384, 720, 16 },
    { 2048, 32768, 65536, 16384, 720, 16 },
    { 3072, 262144, 524288, 131072, 720, 16 },
    { 4096, 262144, 524288, 131072, 720, 16 },
    { 8192, 524288, 1048576, 262144, 720, 16 },
};

static void test_save(const char *dir)
{
    for (int s = 0; s < GRAFFITI_SIZES; s++) {
        const int w = graffiti_width(s), h = graffiti_height(s);
        size_t want = 16 + 140;
        for (int i = 0; i < 6; i++) want += blocks[s][i];
        CHECK(graffiti_save_size(s) == want, "%s's save is %zu, not %zu", graffiti_size_name(s),
              graffiti_save_size(s), want);

        /* The design: a horizontal gradient of red, clear in its last 16
         * columns and in a 4 x 4 block at (8, 8), and the clear colour itself
         * at (20, 20), which must stay solid. */
        uint8_t *d = picture(w, h, 0, 0, 0, 255);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                uint8_t *p = px(d, w, x, y);
                p[0] = (uint8_t)(x * 255 / (w - 1));
                p[1] = (uint8_t)(y * 255 / (h - 1));
                p[2] = 77;
                if (x >= w - 16 || (x >= 8 && x < 12 && y >= 8 && y < 12)) p[3] = 0;
            }
        memcpy(px(d, w, 20, 20), (uint8_t[]){ 198, 195, 198, 255 }, 4);
        size_t n = graffiti_save_size(s);
        uint8_t *f = malloc(n);
        unsigned slot = 41;
        CHECK(graffiti_save_build(d, s, slot, jsrf_signature_key, f) == 0, "build %d failed", s);
        CHECK(f[0] == s && f[1] == 28 + slot && f[2] == 0 && f[3] == 0 && !memcmp(f + 4, "JSRF", 4),
              "%s's header", graffiti_size_name(s));
        int zero = 1;
        for (int i = 8; i < 16; i++) zero &= f[i] == 0;
        CHECK(zero, "%s's header ends in more than zeros", graffiti_size_name(s));
        CHECK(graffiti_save_verifies(f, n, jsrf_signature_key) == 1, "%s doesn't verify", graffiti_size_name(s));
        CHECK(graffiti_save_class(f, n) == s, "%s's class", graffiti_size_name(s));

        size_t at[6], o = 156;
        for (int i = 0; i < 6; i++) {
            at[i] = o;
            o += blocks[s][i];
        }
        const uint8_t *design = f + at[1];
        /* The design: packed at its width, clear where the picture was. */
        CHECK(get16(design + ((size_t)10 * w + 10) * 2) == GRAFFITI_CLEAR, "a clear pixel isn't 0xC618");
        CHECK(get16(design + ((size_t)20 * w + 20) * 2) == 0xC619, "the clear colour wasn't moved");
        CHECK(get16(design + ((size_t)3 * w + w - 1) * 2) == GRAFFITI_CLEAR, "the last column isn't clear");
        uint8_t *p = px(d, w, 30, 3);
        CHECK(get16(design + ((size_t)3 * w + 30) * 2) == graffiti_rgb565(p[0], p[1], p[2]), "a pixel moved");
        int pad = 1;
        for (size_t i = (size_t)w * h * 2; i < blocks[s][1]; i++) pad &= design[i] == 0;
        CHECK(pad, "%s's design padding isn't 0", graffiti_size_name(s));

        /* The preview, every 4th or 8th pixel from the top left. */
        int step = s <= GRAFFITI_S ? 4 : 8, pw = w / step;
        CHECK(blocks[s][0] == (uint32_t)pw * 32 * 2, "%s's preview isn't %d x 32", graffiti_size_name(s), pw);
        int prev = 0;
        for (int y = 0; y < 32; y++)
            for (int x = 0; x < pw; x++)
                prev += get16(f + at[0] + ((size_t)y * pw + x) * 2) !=
                        get16(design + ((size_t)y * step * w + (size_t)x * step) * 2);
        CHECK(prev == 0, "%s's preview has %d pixels off", graffiti_size_name(s), prev);

        /* The canvas: B G R A at 128, 512 or 1024 a row, the design's pixel
         * coming back to its RGB565 by a shift and by rounding alike, clear
         * where the design is, and past M's 384 columns. */
        int pitch = s <= GRAFFITI_S ? 128 : s == GRAFFITI_XL ? 1024 : 512, off = 0;
        CHECK(blocks[s][2] == (uint32_t)pitch * h * 4, "%s's canvas isn't %d a row", graffiti_size_name(s), pitch);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < pitch; x++) {
                const uint8_t *c = f + at[2] + ((size_t)y * pitch + x) * 4;
                uint16_t v = x < w ? get16(design + ((size_t)y * w + x) * 2) : GRAFFITI_CLEAR;
                uint16_t shifted = (uint16_t)((c[2] >> 3) << 11 | (c[1] >> 2) << 5 | c[0] >> 3);
                uint16_t rounded = graffiti_rgb565(c[2], c[1], c[0]);
                int a = v == GRAFFITI_CLEAR ? 0 : 255;
                off += shifted != v || c[3] != a || (a && rounded != v);
            }
        CHECK(off == 0, "%s's canvas has %d pixels off", graffiti_size_name(s), off);

        /* The wall texture: the alpha half exact, a flat block exact, the
         * gradient near, and padding 0. */
        int bw = w / 4, worst = 0, alpha_off = 0;
        for (int by = 0; by < h / 4; by++)
            for (int bx = 0; bx < bw; bx++) {
                uint8_t dec[16][4];
                dxt3_decode(f + at[3] + ((size_t)by * bw + bx) * 16, dec);
                for (int k = 0; k < 16; k++) {
                    int x = bx * 4 + k % 4, y = by * 4 + k / 4;
                    uint16_t v = get16(design + ((size_t)y * w + x) * 2);
                    alpha_off += dec[k][3] != (v == GRAFFITI_CLEAR ? 0 : 255);
                    if (v == GRAFFITI_CLEAR) continue;
                    int want3[3] = { v >> 8 & 0xF8, v >> 3 & 0xFC, v << 3 & 0xF8 };
                    for (int c = 0; c < 3; c++) {
                        int e = abs(dec[k][c] - want3[c]);
                        if (e > worst) worst = e;
                    }
                }
            }
        CHECK(alpha_off == 0, "%s's wall alpha has %d pixels off", graffiti_size_name(s), alpha_off);
        CHECK(worst <= 24, "%s's wall colour is %d off", graffiti_size_name(s), worst);
        pad = 1;
        for (size_t i = (size_t)bw * (h / 4) * 16; i < blocks[s][3]; i++) pad &= f[at[3] + i] == 0;
        CHECK(pad, "%s's wall padding isn't 0", graffiti_size_name(s));

        /* The stickers: none, on the size's grid, at depth 10, the size's scale. */
        static const float grid[GRAFFITI_SIZES][3] = {
            { 1.1f, 1.6f, 0.7f }, { 1.1f, 1.6f, 0.7f }, { 1.2f, 1.2f, 0.6f }, { 1.0f, 1.0f, 0.5f }, { 0.7f, 0.5f, 0.3f },
        };
        int sticker_off = 0;
        for (int i = 0; i < 15; i++) {
            const uint8_t *k = f + at[4] + i * 48;
            float x = (i % 5 - 2) * grid[s][0], y = (1 - i / 5) * grid[s][1];
            sticker_off += get32(k) != 0xFFFFFFFFu || get32(k + 4) || get32(k + 8) || getf(k + 12) != x ||
                           getf(k + 16) != y || getf(k + 20) != 10.0f || get32(k + 24) || get32(k + 28) ||
                           get32(k + 32) || getf(k + 36) != grid[s][2] || getf(k + 40) != grid[s][2] ||
                           getf(k + 44) != grid[s][2];
        }
        CHECK(sticker_off == 0, "%s has %d stickers off", graffiti_size_name(s), sticker_off);
        CHECK(fabsf(getf(f + at[4] + 12) + 2 * grid[s][0]) < 1e-6f, "the first sticker isn't at the left");
        CHECK(get32(f + at[5]) == 0 && get32(f + at[5] + 4) == 0 && get32(f + at[5] + 8) == 0 &&
              get32(f + at[5] + 12) == 3, "%s's last 16 bytes", graffiti_size_name(s));

        /* For run.py: the file, to check every signature with Python's HMAC. */
        char path[1024];
        snprintf(path, sizeof path, "%s/save-%s.dat", dir, graffiti_size_name(s));
        CHECK(js_write_file(path, f, n) == 0, "couldn't write %s", path);
        CHECK(graffiti_save_build(d, s, GRAFFITI_SLOTS, jsrf_signature_key, f) == -1, "slot 280 built");
        free(f);
        free(d);
    }
    CHECK(graffiti_save_size(5) == 0, "a size that isn't one has a save");
}

/* ---- 8. re-sign and re-slot ------------------------------------------------------ */

static void test_resign(void)
{
    static const uint8_t other[16] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                                       0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xF0, 0x01 };
    int s = GRAFFITI_L;
    size_t n = graffiti_save_size(s);
    uint8_t *d = picture(512, 256, 10, 200, 30, 255), *f = malloc(n), *g = malloc(n);
    graffiti_save_build(d, s, 3, other, f);
    CHECK(graffiti_save_verifies(f, n, other) == 1, "signed with another key, it doesn't verify with it");
    CHECK(graffiti_save_verifies(f, n, jsrf_signature_key) == 0, "signed with another key, it verifies with 0s");
    memcpy(g, f, n);
    CHECK(jsrf_graffiti_resign(g, n, jsrf_signature_key) == 0, "re-sign failed");
    CHECK(graffiti_save_verifies(g, n, jsrf_signature_key) == 1, "re-signed, it doesn't verify");
    size_t changed = 0, outside = 0;
    for (size_t i = 0; i < n; i++)
        if (f[i] != g[i]) {
            changed++;
            outside += i < 16 || i >= 156;
        }
    CHECK(outside == 0 && changed > 100, "the re-sign changed %zu bytes, %zu outside the signatures", changed,
          outside);
    /* The same as one built with the zero key in the first place. */
    uint8_t *z = malloc(n);
    graffiti_save_build(d, s, 3, jsrf_signature_key, z);
    CHECK(!memcmp(z, g, n), "re-signed isn't the same as built with the zero key");

    /* Re-slot: the slot byte and the signatures only. */
    memcpy(f, g, n);
    CHECK(jsrf_graffiti_reslot(g, n, 200, jsrf_signature_key) == 0, "re-slot failed");
    CHECK(g[1] == (uint8_t)(28 + 200), "the slot byte is %d", g[1]);
    outside = 0;
    for (size_t i = 0; i < n; i++)
        outside += f[i] != g[i] && i != 1 && (i < 16 || i >= 156);
    CHECK(outside == 0 && graffiti_save_verifies(g, n, jsrf_signature_key) == 1, "re-slot changed more");
    CHECK(jsrf_graffiti_reslot(g, n, 280, jsrf_signature_key) == -1, "re-slotted to 280");

    /* Refused untouched: short, long, another class, no tag. */
    memcpy(f, g, n);
    CHECK(jsrf_graffiti_resign(g, n - 1, other) == -1 && !memcmp(f, g, n), "a short file re-signed");
    g[0] = GRAFFITI_XL;
    CHECK(jsrf_graffiti_resign(g, n, other) == -1, "the wrong class re-signed");
    g[0] = 5;
    CHECK(jsrf_graffiti_resign(g, n, other) == -1 && graffiti_save_class(g, n) == -1, "class 5 re-signed");
    g[0] = (uint8_t)s;
    g[5] = 'X';
    CHECK(jsrf_graffiti_resign(g, n, other) == -1, "no JSRF tag re-signed");
    g[5] = 'S';
    CHECK(!memcmp(f, g, n), "a refused re-sign changed the file");
    CHECK(graffiti_save_verifies(g, 100, jsrf_signature_key) == -1, "a short file verified");
    CHECK(jsrf_graffiti_resign(NULL, 0, other) == -1, "no file re-signed");

    /* The core's hook: jsrf_save_check tells a save signed here from one
     * that isn't, and jsrf_save_resign signs it. */
    jsrf_graffiti_resigner = jsrf_graffiti_resign;
    jsrf_save sv;
    CHECK(graffiti_save_make(&sv, d, s, 3, NULL, 0, other) == 0, "make failed");
    CHECK(sv.kind == JSRF_KIND_GRAFFITI && !sv.signed_here, "kind %d, signed here %d", sv.kind, sv.signed_here);
    CHECK(jsrf_save_resign(&sv, jsrf_signature_key) == 0 && sv.signed_here, "the core's re-sign");
    jsrf_file *sd = jsrf_save_file(&sv, "save.dat");
    CHECK(sd && graffiti_save_verifies(sd->data, sd->size, jsrf_signature_key) == 1, "not re-signed");
    jsrf_save_free(&sv);
    free(z);
    free(d);
    free(f);
    free(g);
}

/* ---- 9 and 10. slots, and a whole save written -------------------------------------- */

static void test_slots(const char *dir)
{
    char name[12];
    graffiti_slot_name(0, name);
    CHECK(!strcmp(name, "GRAFFITI001"), "slot 0 is %s", name);
    graffiti_slot_name(279, name);
    CHECK(!strcmp(name, "GRAFFITI280"), "slot 279 is %s", name);
    uint16_t n16[12];
    graffiti_slot_name16(9, n16);
    CHECK(n16[0] == 'G' && n16[10] == '0' && n16[9] == '1' && n16[11] == 0, "slot 9 in UTF-16");
    /* For run.py to check with its own hash. */
    unsigned shown[] = { 0, 1, 27, 28, 227, 228, 279 };
    for (size_t i = 0; i < sizeof shown / sizeof shown[0]; i++) {
        char folder[JSRF_FOLDER_CHARS + 1];
        graffiti_slot_folder(shown[i], folder);
        graffiti_slot_name(shown[i], name);
        printf("folder %s %s\n", name, folder);
    }
    int page, row, column;
    graffiti_slot_place(0, &page, &row, &column);
    CHECK(page == 0 && row == 0 && column == 0, "slot 0's place");
    graffiti_slot_place(27, &page, &row, &column);
    CHECK(page == 0 && row == 3 && column == 6, "slot 27's place %d %d %d", page, row, column);
    graffiti_slot_place(28, &page, &row, &column);
    CHECK(page == 1 && row == 0 && column == 0, "slot 28's place");
    graffiti_slot_place(100, &page, &row, &column);
    CHECK(page == 3 && row == 2 && column == 2, "slot 100's place %d %d %d", page, row, column);
    graffiti_slot_place(279, &page, &row, &column);
    CHECK(page == 9 && row == 3 && column == 6, "slot 279's place");

    char title[1024], path[4400];
    uint8_t taken[GRAFFITI_SLOTS];
    snprintf(title, sizeof title, "%s/UDATA/5345000a", dir);
    CHECK(graffiti_slots_taken(title, taken) == 0, "slots taken with no title folder");
    CHECK(js_mkdirs(title) == 0, "couldn't make %s", title);
    unsigned there[] = { 0, 1, 2, 4, 30 };
    for (size_t i = 0; i < sizeof there / sizeof there[0]; i++) {
        char folder[JSRF_FOLDER_CHARS + 1];
        graffiti_slot_folder(there[i], folder);
        snprintf(path, sizeof path, "%s/%s", title, folder);
        js_mkdirs(path);
    }
    snprintf(path, sizeof path, "%s/99271B32E8BB", title);   /* the game's own save: not a slot */
    js_mkdirs(path);
    CHECK(graffiti_slots_taken(title, taken) == 5, "taken isn't 5");
    CHECK(taken[0] && taken[1] && taken[2] && !taken[3] && taken[4] && taken[30], "the wrong slots taken");
    CHECK(graffiti_next_free(taken, 0) == 3 && graffiti_next_free(taken, 4) == 5 &&
          graffiti_next_free(taken, 279) == 279, "the first free");
    uint8_t full[GRAFFITI_SLOTS];
    memset(full, 1, sizeof full);
    CHECK(graffiti_next_free(full, 0) == -1, "a full set has a free slot");
    CHECK(graffiti_next_free(taken, 280) == -1, "a free slot past 280");

    /* The icon from a game folder, its names in any case. */
    char game[1024], mark[2048];
    snprintf(game, sizeof game, "%s/game", dir);
    snprintf(mark, sizeof mark, "%s/MEDIA/mark", game);
    js_mkdirs(mark);
    snprintf(path, sizeof path, "%s/saveimage_m.XBX", mark);
    js_write_file(path, "icon-M", 6);
    uint8_t *icon;
    size_t icon_n;
    CHECK(graffiti_icon_read(game, GRAFFITI_M, &icon, &icon_n) == 0 && icon_n == 6 && !memcmp(icon, "icon-M", 6),
          "M's icon not found");
    uint8_t *none;
    size_t none_n;
    CHECK(graffiti_icon_read(game, GRAFFITI_XL, &none, &none_n) == -1 && !none, "XL's icon found");
    CHECK(!strcmp(graffiti_icon_file(GRAFFITI_SS), "SaveImageSS.xbx") &&
          !strcmp(graffiti_icon_file(GRAFFITI_S), "SaveImage_S.xbx") && !graffiti_icon_file(5), "icon names");

    /* A design's save, written into slot 3, then again: never over it. */
    jsrf_graffiti_resigner = jsrf_graffiti_resign;
    uint8_t *d = picture(384, 256, 250, 80, 10, 255);
    jsrf_save sv;
    char why[200];
    CHECK(graffiti_save_make(&sv, d, GRAFFITI_M, 3, icon, icon_n, jsrf_signature_key) == 0, "make failed");
    char folder3[JSRF_FOLDER_CHARS + 1];
    graffiti_slot_folder(3, folder3);
    CHECK(sv.kind == JSRF_KIND_GRAFFITI && sv.signed_here && !strcmp(sv.name, "GRAFFITI004") &&
          !strcmp(sv.folder, folder3), "kind %d signed %d name %s folder %s", sv.kind, sv.signed_here, sv.name,
          sv.folder);
    CHECK(sv.nfiles == 3 && jsrf_save_file(&sv, "SAVEIMAGE.XBX"), "the save's files");
    CHECK(jsrf_save_write(title, &sv, 0, why, sizeof why) == JSRF_WROTE, "write: %s", why);
    CHECK(graffiti_slots_taken(title, taken) == 6 && taken[3], "slot 3 isn't taken after writing it");
    snprintf(path, sizeof path, "%s/%s/save.dat", title, folder3);
    unsigned char *back;
    size_t back_n;
    jsrf_file *sd = jsrf_save_file(&sv, "save.dat");
    CHECK(js_read_file(path, JSRF_FILE_MAX, &back, &back_n) == 0 && back_n == sd->size &&
          !memcmp(back, sd->data, back_n), "save.dat didn't come back the same");
    free(back);
    snprintf(path, sizeof path, "%s/%s/SaveMeta.xbx", title, folder3);
    CHECK(js_read_file(path, 4096, &back, &back_n) == 0, "no SaveMeta.xbx");
    uint16_t meta_name[32];
    int units = jsrf_meta_name(back, back_n, meta_name, 32);
    CHECK(units == 11 && meta_name[10] == '4', "SaveMeta.xbx's name");
    free(back);
    /* Again, over it: refused, and the first left as it was. */
    jsrf_save again;
    uint8_t *e = picture(384, 256, 0, 0, 0, 255);
    graffiti_save_make(&again, e, GRAFFITI_M, 3, NULL, 0, jsrf_signature_key);
    CHECK(jsrf_save_write(title, &again, 0, why, sizeof why) == JSRF_EXISTS, "written over slot 3");
    snprintf(path, sizeof path, "%s/%s/save.dat", title, folder3);
    CHECK(js_read_file(path, JSRF_FILE_MAX, &back, &back_n) == 0 && back_n == sd->size &&
          !memcmp(back, sd->data, back_n), "slot 3's save changed");
    free(back);
    jsrf_save_free(&again);
    jsrf_save_free(&sv);
    free(e);
    free(d);
    free(icon);
}

/* ---- 11. pictures ---------------------------------------------------------------- */

static uint8_t *load(const char *dir, const char *name, size_t *n)
{
    char path[2048];
    unsigned char *d = NULL;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (js_read_file(path, 1 << 20, &d, n) != 0) {
        CHECK(0, "couldn't read %s", path);
        return NULL;
    }
    return d;
}

/* The pixel at (x, y) within tol of (r, g, b), and its alpha solid (255) or
 * clear (0) as asked, within tol too. */
static int near(const graffiti_image *img, int x, int y, int r, int g, int b, int a, int tol)
{
    const uint8_t *p = img->rgba + ((size_t)y * img->width + x) * 4;
    if (abs(p[3] - a) > tol)
        return 0;
    return a == 0 || (abs(p[0] - r) <= tol && abs(p[1] - g) <= tol && abs(p[2] - b) <= tol);
}

static const int red[3] = { 220, 30, 40 }, green[3] = { 20, 200, 60 }, blue[3] = { 30, 40, 230 };
#define C(c) (c)[0], (c)[1], (c)[2]

static void test_kinds(void)
{
    static const struct { const char *head; size_t n; int kind; } k[] = {
        { "\xFF\xD8\xFF\xE0", 4, GRAFFITI_IMAGE_JPEG },
        { "\x89PNG\r\n\x1A\n", 8, GRAFFITI_IMAGE_PNG },
        { "RIFF\x10\0\0\0WEBPVP8L", 16, GRAFFITI_IMAGE_WEBP },
        { "\0\0\0\x1C" "ftypavif\0\0\0\0avifmif1", 28, GRAFFITI_IMAGE_AVIF },
        { "\0\0\0\x18" "ftypmif1\0\0\0\0mif1avif", 24, GRAFFITI_IMAGE_AVIF },
        { "\0\0\0\x18" "ftypheic\0\0\0\0mif1heic", 24, GRAFFITI_IMAGE_NONE },
        { "II*\0\x08\0\0\0", 8, GRAFFITI_IMAGE_TIFF },
        { "MM\0*\0\0\0\x08", 8, GRAFFITI_IMAGE_TIFF },
        { "MM\0+\0\x08\0\0", 8, GRAFFITI_IMAGE_TIFF },
        { "MMXX", 4, GRAFFITI_IMAGE_NONE },
        { "<svg xmlns=\"http://www.w3.org/2000/svg\"/>", 41, GRAFFITI_IMAGE_SVG },
        { "\xEF\xBB\xBF<?xml version=\"1.0\"?>\n<!-- a > b -->\n<!DOCTYPE svg>\n<svg:svg x>", 66, GRAFFITI_IMAGE_SVG },
        { "<?xml version=\"1.0\"?><html><svg/></html>", 41, GRAFFITI_IMAGE_NONE },
        { "<svgx/>", 7, GRAFFITI_IMAGE_NONE },
        { "GIF89a", 6, GRAFFITI_IMAGE_NONE },
        { "hello, world", 12, GRAFFITI_IMAGE_NONE },
    };
    for (size_t i = 0; i < sizeof k / sizeof k[0]; i++)
        CHECK(graffiti_image_kind((const uint8_t *)k[i].head, k[i].n) == k[i].kind, "case %zu is %d, not %d", i,
              graffiti_image_kind((const uint8_t *)k[i].head, k[i].n), k[i].kind);
    CHECK(graffiti_image_kind(NULL, 0) == GRAFFITI_IMAGE_NONE, "nothing is a picture");
    static const struct { const char *name; int ok; } names[] = {
        { "tag.PNG", 1 }, { "a.jpeg", 1 }, { "b.Jpg", 1 }, { "c.webp", 1 }, { "d.avif", 1 }, { "e.svg", 1 },
        { "f.tif", 1 }, { "g.TIFF", 1 }, { "h.jfif", 1 }, { "i.gif", 0 }, { "j.heic", 0 }, { "png", 0 },
        { ".png", 0 }, { "k.png.txt", 0 }, { "", 0 },
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        CHECK(graffiti_image_name_ok(names[i].name) == names[i].ok, "%s's name", names[i].name);
    CHECK(graffiti_image_name_ok(NULL) == 0, "no name");
    CHECK(!strcmp(graffiti_image_kind_name(GRAFFITI_IMAGE_WEBP), "WebP") && !strcmp(graffiti_image_kind_name(0), ""),
          "kinds' names");
}

static void test_orient(void)
{
    uint8_t p[6 * 4];
    for (int i = 0; i < 6; i++)
        memset(p + i * 4, i, 4);              /* 3 x 2: pixel i is (i % 3, i / 3) */
    /* For each orientation, the picture's size and its first row after. */
    static const struct { int o, w, h; uint8_t row[3]; } want[] = {
        { 1, 3, 2, { 0, 1, 2 } }, { 2, 3, 2, { 2, 1, 0 } }, { 3, 3, 2, { 5, 4, 3 } }, { 4, 3, 2, { 3, 4, 5 } },
        { 5, 2, 3, { 0, 3 } },    { 6, 2, 3, { 3, 0 } },    { 7, 2, 3, { 5, 2 } },    { 8, 2, 3, { 2, 5 } },
    };
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        int w = 3, h = 2;
        uint8_t *o = graffiti_orient(p, &w, &h, want[i].o);
        int ok = o && w == want[i].w && h == want[i].h;
        for (int x = 0; ok && x < w; x++)
            ok = o[x * 4] == want[i].row[x];
        CHECK(ok, "orientation %d", want[i].o);
        free(o);
    }
}

static void test_pictures(const char *dir)
{
    static const char *const exact[] = { "quarters.png", "quarters.webp", "quarters.tif", "quarters-lzw.tif" };
    static const int kinds[] = { GRAFFITI_IMAGE_PNG, GRAFFITI_IMAGE_WEBP, GRAFFITI_IMAGE_TIFF, GRAFFITI_IMAGE_TIFF };
    char why[200];
    graffiti_image img;
    size_t n;
    for (int i = 0; i < 4; i++) {
        uint8_t *d = load(dir, exact[i], &n);
        if (!d) continue;
        CHECK(graffiti_image_read(d, n, &img, why, sizeof why) == 0, "%s: %s", exact[i], why);
        if (img.rgba) {
            CHECK(img.width == 8 && img.height == 6 && img.kind == kinds[i] && !img.opaque, "%s is %dx%d kind %d",
                  exact[i], img.width, img.height, img.kind);
            CHECK(near(&img, 0, 0, C(red), 255, 0) && near(&img, 7, 0, C(green), 255, 0) &&
                  near(&img, 0, 5, C(blue), 255, 0) && near(&img, 7, 5, 0, 0, 0, 0, 0), "%s's pixels", exact[i]);
        }
        graffiti_image_free(&img);
        free(d);
    }

    uint8_t *d = load(dir, "quarters-rgb.tif", &n);
    if (d && graffiti_image_read(d, n, &img, why, sizeof why) == 0) {
        CHECK(img.opaque && near(&img, 0, 0, C(red), 255, 0) && near(&img, 7, 5, 0, 0, 0, 255, 0), "an RGB TIFF");
        graffiti_image_free(&img);
    } else
        CHECK(0, "quarters-rgb.tif: %s", why);
    free(d);

    d = load(dir, "grey16.png", &n);
    if (d && graffiti_image_read(d, n, &img, why, sizeof why) == 0) {
        CHECK(img.width == 4 && img.opaque && near(&img, 2, 2, 128, 128, 128, 255, 0), "16-bit grey");
        graffiti_image_free(&img);
    } else
        CHECK(0, "grey16.png: %s", why);
    free(d);

    d = load(dir, "quarters.jpg", &n);
    if (d && graffiti_image_read(d, n, &img, why, sizeof why) == 0) {
        CHECK(img.width == 32 && img.height == 16 && img.kind == GRAFFITI_IMAGE_JPEG && img.opaque, "the JPEG");
        CHECK(near(&img, 8, 4, C(red), 255, 12) && near(&img, 24, 4, C(green), 255, 12) &&
              near(&img, 8, 12, C(blue), 255, 12) && near(&img, 24, 12, 255, 255, 255, 255, 12), "the JPEG's pixels");
        CHECK(graffiti_jpeg_orientation(d, n) == 1, "a JPEG with no EXIF is turned");
        graffiti_image_free(&img);
    } else
        CHECK(0, "quarters.jpg: %s", why);
    free(d);

    d = load(dir, "turned.jpg", &n);
    if (d && graffiti_image_read(d, n, &img, why, sizeof why) == 0) {
        CHECK(graffiti_jpeg_orientation(d, n) == 6, "turned.jpg's orientation is %d", graffiti_jpeg_orientation(d, n));
        CHECK(img.width == 16 && img.height == 32, "turned, it is %dx%d", img.width, img.height);
        CHECK(near(&img, 8, 4, C(red), 255, 12) && near(&img, 8, 28, C(blue), 255, 12), "turned, red isn't on top");
        graffiti_image_free(&img);
    } else
        CHECK(0, "turned.jpg: %s", why);
    free(d);

    d = load(dir, "quarters.avif", &n);
    if (d && graffiti_image_read(d, n, &img, why, sizeof why) == 0) {
        CHECK(img.width == 32 && img.height == 16 && img.kind == GRAFFITI_IMAGE_AVIF && !img.opaque, "the AVIF");
        CHECK(near(&img, 8, 4, C(red), 255, 16) && near(&img, 24, 4, C(green), 255, 16) &&
              near(&img, 8, 12, C(blue), 255, 16) && near(&img, 24, 12, 0, 0, 0, 0, 16), "the AVIF's pixels");
        graffiti_image_free(&img);
    } else
        CHECK(0, "quarters.avif: %s", why);
    free(d);

    d = load(dir, "animated.webp", &n);
    if (d && graffiti_image_read(d, n, &img, why, sizeof why) == 0) {
        CHECK(img.width == 8 && near(&img, 4, 4, C(green), 255, 0), "an animation's first frame isn't green");
        graffiti_image_free(&img);
    } else
        CHECK(0, "animated.webp: %s", why);
    free(d);

    d = load(dir, "half.svg", &n);
    if (d && graffiti_image_read(d, n, &img, why, sizeof why) == 0) {
        CHECK(img.width == GRAFFITI_SVG_SIDE && img.height == GRAFFITI_SVG_SIDE / 2 && img.kind == GRAFFITI_IMAGE_SVG,
              "the SVG is %dx%d", img.width, img.height);
        CHECK(near(&img, 100, 500, C(red), 255, 1) && near(&img, 1500, 500, 0, 0, 0, 0, 0), "the SVG's pixels");
        graffiti_image_free(&img);
    } else
        CHECK(0, "half.svg: %s", why);
    free(d);

    /* Refused, each with a sentence. */
    d = load(dir, "quarters.png", &n);
    if (d) {
        why[0] = 0;
        CHECK(graffiti_image_read(d, 40, &img, why, sizeof why) == -1 && why[0] && !img.rgba, "a cut PNG was read");
        /* IHDR's width, at 16, made 20,000. */
        uint8_t *big = malloc(n);
        memcpy(big, d, n);
        big[16] = 0; big[17] = 0; big[18] = 0x4E; big[19] = 0x20;
        why[0] = 0;
        CHECK(graffiti_image_read(big, n, &img, why, sizeof why) == -1 && strstr(why, "too big"), "a big PNG: %s", why);
        free(big);
        free(d);
    }
    why[0] = 0;
    CHECK(graffiti_image_read((const uint8_t *)"hello", 5, &img, why, sizeof why) == -1 && why[0], "text was read");
    /* An SVG that gives no size is drawn at the size of what it draws, as
     * nanosvg reads it; one that gives none and draws nothing is refused. */
    static const char nosize[] =
        "<svg xmlns=\"http://www.w3.org/2000/svg\"><rect x=\"3\" y=\"4\" width=\"10\" height=\"5\"/></svg>";
    if (graffiti_image_read((const uint8_t *)nosize, sizeof nosize - 1, &img, why, sizeof why) == 0) {
        CHECK(img.width == GRAFFITI_SVG_SIDE && img.height == GRAFFITI_SVG_SIDE / 2, "no size: %d x %d",
              img.width, img.height);
        const uint8_t *mid = img.rgba + ((size_t)img.height / 2 * img.width + img.width / 2) * 4;
        CHECK(mid[0] == 0 && mid[3] == 255 && img.opaque, "no size: the rect doesn't fill it");
        graffiti_image_free(&img);
    } else
        CHECK(0, "an SVG with no size: %s", why);
    static const char empty[] = "<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>";
    why[0] = 0;
    CHECK(graffiti_image_read((const uint8_t *)empty, sizeof empty - 1, &img, why, sizeof why) == -1 &&
          strstr(why, "no size") && !img.rgba, "an empty SVG with no size was read");

    /* The whole way: a picture, Auto's size, fitted, built, verified. */
    d = load(dir, "quarters.jpg", &n);
    if (d && graffiti_image_read(d, n, &img, why, sizeof why) == 0) {
        int size = graffiti_auto_size(img.width, img.height);
        CHECK(size == GRAFFITI_L, "32 x 16 isn't L");
        uint8_t *o = fitted(size), *f = malloc(graffiti_save_size(size));
        CHECK(graffiti_fit(img.rgba, img.width, img.height, size, GRAFFITI_FIT_MARGINS, NULL, o) == 0, "fit");
        CHECK(graffiti_save_build(o, size, 0, jsrf_signature_key, f) == 0 &&
              graffiti_save_verifies(f, graffiti_save_size(size), jsrf_signature_key) == 1, "the whole way");
        free(o);
        free(f);
        graffiti_image_free(&img);
    }
    free(d);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: graffiti <folder> <pictures>\n");
        return 2;
    }
    test_sizes();
    test_modes();
    test_resample();
    test_white();
    test_rgb565();
    test_save(argv[1]);
    test_resign();
    test_slots(argv[1]);
    test_kinds();
    test_orient();
    test_pictures(argv[2]);
    if (failures) {
        printf("graffiti: %d failures\n", failures);
        return 1;
    }
    printf("graffiti: all cases pass\n");
    return 0;
}
