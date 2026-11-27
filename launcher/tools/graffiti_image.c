/*
 * Pictures read for JSRF-PC Tools' graffiti page (graffiti_image.h), b3
 * row 35. spriteice's formats (7:18pm on 2026-11-26), HEIC left out for now
 * (7:21pm), each read by a library built into the tool (third_party):
 *
 *   JPEG, PNG   stb_image (public domain or MIT), with a JPEG's EXIF
 *               orientation read here, as stb_image leaves it
 *   WebP        libwebp (BSD-3-Clause), an animation's first frame through
 *               its demux library
 *   AVIF        libavif (BSD-2-Clause), decoding AV1 through dav1d
 *               (BSD-2-Clause), with the file's rotation and mirror
 *   SVG         nanosvg (zlib), drawn with its longer side GRAFFITI_SVG_SIDE
 *               pixels (spriteice, 8:19pm: "nanosvg"; lunasvg's renderer
 *               carries FreeType's licence, which can't join GPL-2.0 code)
 *   TIFF        libtiff (its own permissive licence), deflate through miniz
 *
 * Which reader a file gets is decided by its content, never by its name: a
 * dropped folder is walked for the formats' extensions, then each file's
 * first bytes are looked at.
 */
#include "graffiti_image.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image.h"          /* stb_image and nanosvg are built in graffiti_libs.c */
#include "nanosvg.h"
#include "nanosvgrast.h"
#include "webp/decode.h"
#include "webp/demux.h"
#include "avif/avif.h"
#include "tiffio.h"

static void say(char *why, size_t wn, const char *what)
{
    if (why && wn) {
        snprintf(why, wn, "%s", what);
        why[wn - 1] = 0;
    }
}

/* ---- what a file is ---------------------------------------------------------- */

static int starts(const uint8_t *d, size_t n, const char *s, size_t at)
{
    size_t k = strlen(s);
    return n >= at + k && !memcmp(d + at, s, k);
}

/* An ISO base media file whose ftyp box names an AVIF brand, as its major
 * brand or one of its compatible ones. */
static int is_avif(const uint8_t *d, size_t n)
{
    if (n < 16 || !starts(d, n, "ftyp", 4))
        return 0;
    size_t box = (size_t)d[0] << 24 | (size_t)d[1] << 16 | (size_t)d[2] << 8 | d[3];
    if (box < 16 || box > n)
        box = n;
    for (size_t at = 8; at + 4 <= box; at += 4) {
        if (at == 12)
            continue;               /* the minor version */
        if (starts(d, n, "avif", at) || starts(d, n, "avis", at))
            return 1;
    }
    return 0;
}

/* Past whitespace, an XML declaration, comments and a doctype, an <svg
 * element (or one with a namespace prefix, such as <svg:svg). */
static int is_svg(const uint8_t *d, size_t n)
{
    size_t i = 0, end = n < 4096 ? n : 4096;
    if (starts(d, n, "\xEF\xBB\xBF", 0))
        i = 3;
    while (i < end) {
        while (i < end && (d[i] == ' ' || d[i] == '\t' || d[i] == '\r' || d[i] == '\n'))
            i++;
        if (i >= end || d[i] != '<')
            return 0;
        const char *close = NULL;
        if (starts(d, end, "<?", i))
            close = "?>";
        else if (starts(d, end, "<!--", i))
            close = "-->";
        else if (starts(d, end, "<!", i))
            close = ">";
        if (!close) {
            size_t j = i + 1;
            while (j < end && j < i + 32 && d[j] != ':' && d[j] != ' ' && d[j] != '>' && d[j] != '/')
                j++;
            if (j < end && d[j] == ':')
                i = j;              /* <prefix:svg */
            return starts(d, end, "svg", i + 1) && i + 4 < end &&
                   (d[i + 4] == ' ' || d[i + 4] == '>' || d[i + 4] == '\t' || d[i + 4] == '\r' ||
                    d[i + 4] == '\n' || d[i + 4] == '/');
        }
        size_t k = strlen(close), j = i + 2;
        while (j + k <= end && memcmp(d + j, close, k))
            j++;
        if (j + k > end)
            return 0;
        i = j + k;
    }
    return 0;
}

int graffiti_image_kind(const uint8_t *d, size_t n)
{
    if (!d)
        return GRAFFITI_IMAGE_NONE;
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF)
        return GRAFFITI_IMAGE_JPEG;
    if (starts(d, n, "\x89PNG\r\n\x1A\n", 0))
        return GRAFFITI_IMAGE_PNG;
    if (starts(d, n, "RIFF", 0) && starts(d, n, "WEBP", 8))
        return GRAFFITI_IMAGE_WEBP;
    if (is_avif(d, n))
        return GRAFFITI_IMAGE_AVIF;
    if (starts(d, n, "II*", 0) || starts(d, n, "MM", 0) || starts(d, n, "II+", 0)) {
        if (n >= 4 && ((d[0] == 'I' && (d[2] == 42 || d[2] == 43) && d[3] == 0) ||
                       (d[0] == 'M' && d[2] == 0 && (d[3] == 42 || d[3] == 43))))
            return GRAFFITI_IMAGE_TIFF;
    }
    if (is_svg(d, n))
        return GRAFFITI_IMAGE_SVG;
    return GRAFFITI_IMAGE_NONE;
}

int graffiti_image_name_ok(const char *name)
{
    static const char *const ext[] = { ".jpg", ".jpeg", ".jpe", ".jfif", ".png", ".webp", ".avif",
                                       ".svg", ".tif", ".tiff" };
    size_t n = name ? strlen(name) : 0;
    for (size_t e = 0; e < sizeof ext / sizeof ext[0]; e++) {
        size_t k = strlen(ext[e]);
        if (n <= k)
            continue;
        size_t i = 0;
        for (; i < k; i++) {
            char c = name[n - k + i];
            if ((c >= 'A' && c <= 'Z' ? c + 32 : c) != ext[e][i])
                break;
        }
        if (i == k)
            return 1;
    }
    return 0;
}

const char *graffiti_image_kind_name(int kind)
{
    static const char *const names[GRAFFITI_IMAGE_KINDS] = { "", "JPEG", "PNG", "WebP", "AVIF", "SVG", "TIFF" };
    return kind > 0 && kind < GRAFFITI_IMAGE_KINDS ? names[kind] : "";
}

/* ---- turning a picture upright ------------------------------------------------- */

static unsigned get16(const uint8_t *p, int big)
{
    return big ? (unsigned)p[0] << 8 | p[1] : (unsigned)p[1] << 8 | p[0];
}

static uint32_t get32(const uint8_t *p, int big)
{
    return big ? (uint32_t)get16(p, 1) << 16 | get16(p + 2, 1) : (uint32_t)get16(p + 2, 0) << 16 | get16(p, 0);
}

/* EXIF's Orientation tag, in the TIFF structure of an APP1 "Exif" segment. */
#define EXIF_ORIENTATION 0x0112
#define TIFF_SHORT       3

static int exif_orientation(const uint8_t *t, size_t n)
{
    if (n < 8 || !((t[0] == 'I' && t[1] == 'I') || (t[0] == 'M' && t[1] == 'M')))
        return 1;
    int big = t[0] == 'M';
    if (get16(t + 2, big) != 42)
        return 1;
    uint32_t ifd = get32(t + 4, big);
    if (ifd > n - 2)
        return 1;
    unsigned count = get16(t + ifd, big);
    for (unsigned i = 0; i < count; i++) {
        size_t e = ifd + 2 + (size_t)i * 12;
        if (e + 12 > n)
            break;
        if (get16(t + e, big) == EXIF_ORIENTATION && get16(t + e + 2, big) == TIFF_SHORT) {
            unsigned v = get16(t + e + 8, big);
            return v >= 1 && v <= 8 ? (int)v : 1;
        }
    }
    return 1;
}

int graffiti_jpeg_orientation(const uint8_t *d, size_t n)
{
    size_t i = 2;
    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8)
        return 1;
    while (i + 4 <= n) {
        if (d[i] != 0xFF)
            return 1;
        uint8_t marker = d[i + 1];
        if (marker == 0xFF) {       /* padding */
            i++;
            continue;
        }
        if (marker == 0xD9 || marker == 0xDA)   /* the end, or the picture's data */
            return 1;
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            i += 2;
            continue;
        }
        size_t len = (size_t)d[i + 2] << 8 | d[i + 3];
        if (len < 2 || i + 2 + len > n)
            return 1;
        if (marker == 0xE1 && len >= 8 && starts(d, n, "Exif\0\0", i + 4))
            return exif_orientation(d + i + 10, len - 8);
        i += 2 + len;
    }
    return 1;
}

uint8_t *graffiti_orient(const uint8_t *rgba, int *w, int *h, int orientation)
{
    const int W = *w, H = *h, turned = orientation >= 5 && orientation <= 8;
    const int ow = turned ? H : W, oh = turned ? W : H;
    uint8_t *out = malloc((size_t)W * H * 4);
    if (!out)
        return NULL;
    for (int y = 0; y < oh; y++)
        for (int x = 0; x < ow; x++) {
            int sx, sy;
            switch (orientation) {
            case 2: sx = W - 1 - x; sy = y; break;
            case 3: sx = W - 1 - x; sy = H - 1 - y; break;
            case 4: sx = x; sy = H - 1 - y; break;
            case 5: sx = y; sy = x; break;
            case 6: sx = y; sy = H - 1 - x; break;
            case 7: sx = W - 1 - y; sy = H - 1 - x; break;
            case 8: sx = W - 1 - y; sy = x; break;
            default: sx = x; sy = y; break;
            }
            memcpy(out + ((size_t)y * ow + x) * 4, rgba + ((size_t)sy * W + sx) * 4, 4);
        }
    *w = ow;
    *h = oh;
    return out;
}

/* ---- the readers ------------------------------------------------------------------ */

static int too_big(long w, long h)
{
    return w <= 0 || h <= 0 || w > GRAFFITI_IMAGE_SIDE || h > GRAFFITI_IMAGE_SIDE || w * h > GRAFFITI_IMAGE_PIXELS;
}

static int take(graffiti_image *out, int kind, int w, int h, uint8_t *rgba, char *why, size_t wn)
{
    if (!rgba) {
        say(why, wn, "There wasn't enough memory to read it.");
        return -1;
    }
    out->width = w;
    out->height = h;
    out->rgba = rgba;
    out->kind = kind;
    out->opaque = 1;
    for (size_t i = 0; i < (size_t)w * h; i++)
        if (rgba[i * 4 + 3] != 255) {
            out->opaque = 0;
            break;
        }
    return 0;
}

static int read_stb(const uint8_t *d, size_t n, int kind, graffiti_image *out, char *why, size_t wn)
{
    int w, h, comp;
    if (n > 0x7FFFFFFF || !stbi_info_from_memory(d, (int)n, &w, &h, &comp)) {
        say(why, wn, "It couldn't be read: the file may be damaged, or use a form of the format that isn't read.");
        return -1;
    }
    if (too_big(w, h)) {
        say(why, wn, "It's too big: pictures up to 16,384 pixels a side and 64 megapixels are read.");
        return -1;
    }
    uint8_t *px = stbi_load_from_memory(d, (int)n, &w, &h, &comp, 4);
    if (!px) {
        say(why, wn, "It couldn't be read: the file may be damaged, or use a form of the format that isn't read.");
        return -1;
    }
    /* stb_image's memory is malloc's, freed with graffiti_image_free. */
    int turn = kind == GRAFFITI_IMAGE_JPEG ? graffiti_jpeg_orientation(d, n) : 1;
    if (turn != 1) {
        uint8_t *up = graffiti_orient(px, &w, &h, turn);
        stbi_image_free(px);
        px = up;
    }
    return take(out, kind, w, h, px, why, wn);
}

static int read_svg(const uint8_t *d, size_t n, graffiti_image *out, char *why, size_t wn)
{
    /* nanosvg parses in place, from a string. */
    char *text = malloc(n + 1);
    if (!text) {
        say(why, wn, "There wasn't enough memory to read it.");
        return -1;
    }
    memcpy(text, d, n);
    text[n] = 0;
    NSVGimage *svg = nsvgParse(text, "px", 96.0f);
    free(text);
    if (!svg || !(svg->width > 0) || !(svg->height > 0)) {
        say(why, wn, svg ? "It has no size and draws nothing: an SVG needs a width and height, a viewBox or a shape."
                         : "It couldn't be read as SVG: the file may be damaged.");
        if (svg)
            nsvgDelete(svg);
        return -1;
    }
    float scale = GRAFFITI_SVG_SIDE / (svg->width > svg->height ? svg->width : svg->height);
    int w = (int)(svg->width * scale + 0.5f), h = (int)(svg->height * scale + 0.5f);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    uint8_t *px = calloc((size_t)w * h, 4);
    NSVGrasterizer *r = px ? nsvgCreateRasterizer() : NULL;
    if (!r) {
        free(px);
        nsvgDelete(svg);
        say(why, wn, "There wasn't enough memory to read it.");
        return -1;
    }
    nsvgRasterize(r, svg, 0, 0, scale, px, w, h, w * 4);   /* alpha not premultiplied */
    nsvgDeleteRasterizer(r);
    nsvgDelete(svg);
    return take(out, GRAFFITI_IMAGE_SVG, w, h, px, why, wn);
}

static int read_webp(const uint8_t *d, size_t n, graffiti_image *out, char *why, size_t wn)
{
    WebPBitstreamFeatures f;
    if (WebPGetFeatures(d, n, &f) != VP8_STATUS_OK) {
        say(why, wn, "It couldn't be read as WebP: the file may be damaged.");
        return -1;
    }
    WebPData data = { d, n };
    WebPAnimDecoderOptions opt;
    WebPAnimDecoder *dec;
    WebPAnimInfo info;
    uint8_t *frame;
    int stamp;
    if (!WebPAnimDecoderOptionsInit(&opt)) {
        say(why, wn, "It couldn't be read as WebP.");
        return -1;
    }
    opt.color_mode = MODE_RGBA;
    opt.use_threads = 0;
    dec = WebPAnimDecoderNew(&data, &opt);   /* a still picture is an animation of one frame */
    if (!dec || !WebPAnimDecoderGetInfo(dec, &info) || too_big(info.canvas_width, info.canvas_height)) {
        say(why, wn, dec ? "It's too big: pictures up to 16,384 pixels a side and 64 megapixels are read."
                         : "It couldn't be read as WebP: the file may be damaged.");
        WebPAnimDecoderDelete(dec);
        return -1;
    }
    if (!WebPAnimDecoderGetNext(dec, &frame, &stamp)) {
        say(why, wn, "Its first frame couldn't be read: the file may be damaged.");
        WebPAnimDecoderDelete(dec);
        return -1;
    }
    size_t bytes = (size_t)info.canvas_width * info.canvas_height * 4;
    uint8_t *px = malloc(bytes);
    if (px)
        memcpy(px, frame, bytes);
    WebPAnimDecoderDelete(dec);
    return take(out, GRAFFITI_IMAGE_WEBP, (int)info.canvas_width, (int)info.canvas_height, px, why, wn);
}

static int read_avif(const uint8_t *d, size_t n, graffiti_image *out, char *why, size_t wn)
{
    avifDecoder *dec = avifDecoderCreate();
    avifRGBImage rgb;
    uint8_t *px = NULL;
    int w = 0, h = 0;
    memset(&rgb, 0, sizeof rgb);
    if (!dec) {
        say(why, wn, "There wasn't enough memory to read it.");
        return -1;
    }
    dec->maxThreads = 1;
    dec->imageSizeLimit = GRAFFITI_IMAGE_PIXELS;
    dec->imageDimensionLimit = GRAFFITI_IMAGE_SIDE;
    dec->strictFlags = AVIF_STRICT_DISABLED;    /* read what other programs write, as browsers do */
    if (avifDecoderSetIOMemory(dec, d, n) != AVIF_RESULT_OK || avifDecoderParse(dec) != AVIF_RESULT_OK ||
        avifDecoderNextImage(dec) != AVIF_RESULT_OK) {
        say(why, wn, "It couldn't be read as AVIF: the file may be damaged, or too big.");
        goto out;
    }
    avifRGBImageSetDefaults(&rgb, dec->image);
    rgb.format = AVIF_RGB_FORMAT_RGBA;
    rgb.depth = 8;
    if (avifRGBImageAllocatePixels(&rgb) != AVIF_RESULT_OK || avifImageYUVToRGB(dec->image, &rgb) != AVIF_RESULT_OK) {
        say(why, wn, "Its colours couldn't be worked out.");
        goto out;
    }
    w = (int)rgb.width;
    h = (int)rgb.height;
    px = malloc((size_t)w * h * 4);
    if (!px) {
        say(why, wn, "There wasn't enough memory to read it.");
        goto out;
    }
    for (int y = 0; y < h; y++)
        memcpy(px + (size_t)y * w * 4, rgb.pixels + (size_t)y * rgb.rowBytes, (size_t)w * 4);
    /* HEIF's transforms, in their order: the rotation (irot, a quarter turn
     * anticlockwise a step), then the mirror (imir: axis 0 mirrors left to
     * right, 1 top to bottom). As EXIF orientations: 8, 3 and 6 turn; 2 and
     * 4 mirror. */
    if (dec->image->transformFlags & AVIF_TRANSFORM_IROT) {
        static const int turn[4] = { 1, 8, 3, 6 };
        uint8_t *up = graffiti_orient(px, &w, &h, turn[dec->image->irot.angle & 3]);
        free(px);
        px = up;
    }
    if (px && (dec->image->transformFlags & AVIF_TRANSFORM_IMIR)) {
        uint8_t *up = graffiti_orient(px, &w, &h, dec->image->imir.axis ? 4 : 2);
        free(px);
        px = up;
    }
    if (!px) {
        say(why, wn, "There wasn't enough memory to read it.");
        goto out;
    }
out:
    avifRGBImageFreePixels(&rgb);
    avifDecoderDestroy(dec);
    if (!px)
        return -1;
    return take(out, GRAFFITI_IMAGE_AVIF, w, h, px, why, wn);
}

/* libtiff reads through these from memory. */
typedef struct {
    const uint8_t *d;
    toff_t n, at;
} tiff_mem;

static tmsize_t tiff_read(thandle_t h, void *buf, tmsize_t size)
{
    tiff_mem *m = h;
    toff_t left = m->at < m->n ? m->n - m->at : 0;
    if ((toff_t)size > left)
        size = (tmsize_t)left;
    memcpy(buf, m->d + m->at, (size_t)size);
    m->at += size;
    return size;
}

static tmsize_t tiff_write(thandle_t h, void *buf, tmsize_t size)
{
    (void)h;
    (void)buf;
    (void)size;
    return 0;
}

static toff_t tiff_seek(thandle_t h, toff_t off, int whence)
{
    tiff_mem *m = h;
    toff_t base = whence == SEEK_CUR ? m->at : whence == SEEK_END ? m->n : 0;
    m->at = base + off;
    return m->at;
}

static int tiff_close(thandle_t h)
{
    (void)h;
    return 0;
}

static toff_t tiff_size(thandle_t h)
{
    return ((tiff_mem *)h)->n;
}

static int tiff_map(thandle_t h, void **base, toff_t *size)
{
    tiff_mem *m = h;
    *base = (void *)m->d;
    *size = m->n;
    return 1;
}

static void tiff_unmap(thandle_t h, void *base, toff_t size)
{
    (void)h;
    (void)base;
    (void)size;
}

static int tiff_quiet(TIFF *tif, void *user, const char *module, const char *fmt, va_list ap)
{
    (void)tif;
    (void)user;
    (void)module;
    (void)fmt;
    (void)ap;
    return 1;                       /* handled: nothing printed */
}

static int read_tiff(const uint8_t *d, size_t n, graffiti_image *out, char *why, size_t wn)
{
    tiff_mem m = { d, n, 0 };
    TIFFOpenOptions *o = TIFFOpenOptionsAlloc();
    if (o) {
        TIFFOpenOptionsSetErrorHandlerExtR(o, tiff_quiet, NULL);
        TIFFOpenOptionsSetWarningHandlerExtR(o, tiff_quiet, NULL);
    }
    TIFF *tif = TIFFClientOpenExt("picture", "rm", &m, tiff_read, tiff_write, tiff_seek, tiff_close, tiff_size,
                                  tiff_map, tiff_unmap, o);
    TIFFOpenOptionsFree(o);
    if (!tif) {
        say(why, wn, "It couldn't be read as TIFF: the file may be damaged.");
        return -1;
    }
    uint32_t w = 0, h = 0;
    TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
    TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
    if (too_big(w, h)) {
        say(why, wn, "It's too big: pictures up to 16,384 pixels a side and 64 megapixels are read.");
        TIFFClose(tif);
        return -1;
    }
    uint32_t *raster = malloc((size_t)w * h * sizeof *raster);
    uint8_t *px = malloc((size_t)w * h * 4);
    int ok = raster && px && TIFFReadRGBAImageOriented(tif, w, h, raster, ORIENTATION_TOPLEFT, 0);
    TIFFClose(tif);
    if (!ok) {
        free(raster);
        free(px);
        say(why, wn, raster && px ? "It couldn't be read: the file may be damaged, or use a form of TIFF that isn't read."
                                  : "There wasn't enough memory to read it.");
        return -1;
    }
    /* The RGBA interface gives alpha premultiplied, as TIFF's associated
     * alpha is: taken back, as graffiti_fit.h has it. */
    for (size_t i = 0; i < (size_t)w * h; i++) {
        uint32_t p = raster[i];
        uint8_t r = (uint8_t)TIFFGetR(p), g = (uint8_t)TIFFGetG(p), b = (uint8_t)TIFFGetB(p), a = (uint8_t)TIFFGetA(p);
        if (a && a < 255) {
            r = (uint8_t)(r * 255u / a > 255 ? 255 : r * 255u / a);
            g = (uint8_t)(g * 255u / a > 255 ? 255 : g * 255u / a);
            b = (uint8_t)(b * 255u / a > 255 ? 255 : b * 255u / a);
        }
        px[i * 4 + 0] = r;
        px[i * 4 + 1] = g;
        px[i * 4 + 2] = b;
        px[i * 4 + 3] = a;
    }
    free(raster);
    return take(out, GRAFFITI_IMAGE_TIFF, (int)w, (int)h, px, why, wn);
}

int graffiti_image_read(const uint8_t *d, size_t n, graffiti_image *out, char *why, size_t wn)
{
    memset(out, 0, sizeof *out);
    say(why, wn, "");
    switch (graffiti_image_kind(d, n)) {
    case GRAFFITI_IMAGE_JPEG:
        return read_stb(d, n, GRAFFITI_IMAGE_JPEG, out, why, wn);
    case GRAFFITI_IMAGE_PNG:
        return read_stb(d, n, GRAFFITI_IMAGE_PNG, out, why, wn);
    case GRAFFITI_IMAGE_WEBP:
        return read_webp(d, n, out, why, wn);
    case GRAFFITI_IMAGE_AVIF:
        return read_avif(d, n, out, why, wn);
    case GRAFFITI_IMAGE_SVG:
        return read_svg(d, n, out, why, wn);
    case GRAFFITI_IMAGE_TIFF:
        return read_tiff(d, n, out, why, wn);
    default:
        say(why, wn, "It isn't a picture JSRF-PC Tools reads: JPEG, PNG, WebP, AVIF, SVG or TIFF.");
        return -1;
    }
}

void graffiti_image_free(graffiti_image *img)
{
    free(img->rgba);
    img->rgba = NULL;
}
