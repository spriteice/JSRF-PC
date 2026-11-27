/* Run with: python tests/graffiti/run.py
 *
 * JSRF-PC Tools' graffiti page without the window: launcher/tools/
 * graffiti_image.c as it is, with the picture libraries it reads through
 * (launcher/tools/graffiti_libs.txt) and the setup's file helpers.
 *   1. The pictures: what a file is by its content, not its name; the names
 *      a folder is walked for; each format read pixel by pixel from
 *      tests/graffiti/pictures (made by make_pictures.py), the lossy ones
 *      within a tolerance; a JPEG's EXIF turn and the eight orientations;
 *      an animation's first frame; and a damaged file, one too big and one
 *      that isn't a picture refused with a sentence.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graffiti_image.h"
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

/* ---- 1. pictures ------------------------------------------------------------------- */

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
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: graffiti <folder> <pictures>\n");
        return 2;
    }
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
