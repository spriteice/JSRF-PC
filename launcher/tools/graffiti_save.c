/*
 * A graffiti design as the game saves one (graffiti_save.h), for JSRF-PC
 * Tools (b3 row 35).
 *
 * Laid out from the game's own code (docs/formats/graffiti-save.md, read
 * from default.xbe on 2026-11-26): the writer at
 * 0x12CCB0, the editor's finish at 0x120860, its wall texture at 0x12DFC0,
 * its canvas at 0x1304C9, its reset at 0x122240 and 0x1223B0, and the sheet
 * builder's reader, sub_0002D570. A design's save.dat is:
 *
 *   16 bytes   the header: the size class, the slot byte, 0, 0, "JSRF", 0 x 8
 *   7 x 20     HMAC-SHA1 signatures: the header's, then each block's in turn
 *   blocks     1. the preview, RGB565, 32 rows, point-sampled from the design
 *              2. the design, RGB565, packed at its own width
 *              3. the editor's canvas, B G R A, at the canvas's own pitch
 *              4. the wall's texture, DXT3, packed at the design's width
 *              5. the editor's 15 stickers, 48 bytes each
 *              6. 16 bytes of the editor's: 0, 0, 0, 3
 *
 * Each block is the size its table gives (0x2285B0), so M's 384-pixel rows
 * leave padding at the end of blocks 2 and 4, and sit in 512-pixel rows in
 * block 3. Padding is written as 0. RGB565's 0xC618 marks a clear pixel.
 * The game checks the header's signature and the wall texture's before it
 * draws a design, then "JSRF"; a signature under another key, a console's or
 * an emulator's, leaves the design out until it is signed again.
 *
 * The wall texture is made as the editor makes it: from the RGB565 design,
 * each channel widened by a shift, clear pixels alpha 0 and the rest 255,
 * compressed to DXT3. Its alpha half is exact, 0 or 15 a pixel; its colour
 * half is stb_dxt's (third_party/stb). The canvas holds the design widened
 * with its top bits repeated, so the editor's RGB565 comes back the same.
 */
#include "graffiti_save.h"
#include "graffiti_fit.h"
#include "setup.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_dxt.h"          /* built in graffiti_libs.c */

#define HEADER_BYTES        16
#define SIGNATURES          7
#define BLOCKS              6
#define SIGNED_BYTES        (HEADER_BYTES + SIGNATURES * JSRF_SIG_BYTES)
#define HEADER_TAG          4           /* "JSRF" from here */
#define HEADER_SLOT         1
/* The slot byte: the save function's first argument times 28, plus the
 * slot, kept to a byte (0x12EA88). The argument comes through a virtual call
 * that wasn't traced; the game's own saves have 1, so 28 plus the slot. */
#define SLOT_BYTE_BASE      28
#define PREVIEW_ROWS        32
#define STICKERS            15
#define STICKER_BYTES       48
#define STICKER_DEPTH       10.0f
#define TAIL_LAST           3           /* the editor's fourth int32 for a fresh design */
#define SOLID_ALPHA         128         /* a design pixel with alpha under this is clear */
#define DXT_BLOCK           4
#define DXT_BLOCK_BYTES     16
#define DXT_SOLID           15

enum { PREVIEW, DESIGN, CANVAS, WALL, STICKER_BLOCK, TAIL };

/* By size class: the blocks' sizes (0x2285B0), the editor's canvas's side,
 * the preview's step (every 4th or 8th pixel), and the stickers' grid
 * (0x225DE8: 5 across by 3 down, these steps apart about the centre) and
 * scale (0x226040). */
static const struct {
    uint32_t block[BLOCKS];
    int canvas, step;
    float grid_x, grid_y, scale;
} layout[GRAFFITI_SIZES] = {
    { { 2048, 32768, 65536, 16384, STICKERS * STICKER_BYTES, 16 }, 128, 4, 1.1f, 1.6f, 0.7f },
    { { 2048, 32768, 65536, 16384, STICKERS * STICKER_BYTES, 16 }, 128, 4, 1.1f, 1.6f, 0.7f },
    { { 3072, 262144, 524288, 131072, STICKERS * STICKER_BYTES, 16 }, 512, 8, 1.2f, 1.2f, 0.6f },
    { { 4096, 262144, 524288, 131072, STICKERS * STICKER_BYTES, 16 }, 512, 8, 1.0f, 1.0f, 0.5f },
    { { 8192, 524288, 1048576, 262144, STICKERS * STICKER_BYTES, 16 }, 1024, 8, 0.7f, 0.5f, 0.3f },
};

static const char *const icons[GRAFFITI_SIZES] = {
    "SaveImageSS.xbx", "SaveImage_S.xbx", "SaveImage_M.xbx", "SaveImage_L.xbx", "SaveImageXL.xbx",
};

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v);
    put16(p + 2, v >> 16);
}

static void putf(uint8_t *p, float f)
{
    uint32_t v;
    memcpy(&v, &f, sizeof v);
    put32(p, v);
}

uint16_t graffiti_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t v = (uint16_t)(((r * 31 + 127) / 255) << 11 | ((g * 63 + 127) / 255) << 5 | (b * 31 + 127) / 255);
    return v == GRAFFITI_CLEAR ? (uint16_t)(GRAFFITI_CLEAR + 1) : v;
}

size_t graffiti_save_size(int size)
{
    if (size < 0 || size >= GRAFFITI_SIZES)
        return 0;
    size_t n = SIGNED_BYTES;
    for (int i = 0; i < BLOCKS; i++)
        n += layout[size].block[i];
    return n;
}

/* Where each block starts in save.dat. */
static void block_offsets(int size, size_t at[BLOCKS])
{
    size_t o = SIGNED_BYTES;
    for (int i = 0; i < BLOCKS; i++) {
        at[i] = o;
        o += layout[size].block[i];
    }
}

/* An RGB565 colour widened as the editor's wall texture widens it: a shift,
 * the low bits 0 (0x12DFC0). */
static void widen_shift(uint16_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = (uint8_t)(v >> 8 & 0xF8);
    *g = (uint8_t)(v >> 3 & 0xFC);
    *b = (uint8_t)(v << 3);
}

/* Widened with the top bits repeated, so that taking it back to RGB565 by
 * either a shift or rounding gives v again. */
static void widen_exact(uint16_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    unsigned r5 = v >> 11, g6 = v >> 5 & 0x3F, b5 = v & 0x1F;
    *r = (uint8_t)(r5 << 3 | r5 >> 2);
    *g = (uint8_t)(g6 << 2 | g6 >> 4);
    *b = (uint8_t)(b5 << 3 | b5 >> 2);
}

static void build_blocks(const uint16_t *design, int size, uint8_t *const at[BLOCKS])
{
    const int w = graffiti_width(size), h = graffiti_height(size);
    const int step = layout[size].step, pw = w / step, pitch = layout[size].canvas;

    for (int y = 0; y < PREVIEW_ROWS; y++)
        for (int x = 0; x < pw; x++)
            put16(at[PREVIEW] + ((size_t)y * pw + x) * 2, design[(size_t)y * step * w + (size_t)x * step]);

    for (size_t i = 0; i < (size_t)w * h; i++)
        put16(at[DESIGN] + i * 2, design[i]);

    for (int y = 0; y < h; y++)
        for (int x = 0; x < pitch; x++) {
            uint16_t v = x < w ? design[(size_t)y * w + x] : GRAFFITI_CLEAR;
            uint8_t *p = at[CANVAS] + ((size_t)y * pitch + x) * 4;
            widen_exact(v, &p[2], &p[1], &p[0]);
            p[3] = v == GRAFFITI_CLEAR ? 0 : 255;
        }

    uint8_t *wall = at[WALL];
    for (int by = 0; by < h; by += DXT_BLOCK)
        for (int bx = 0; bx < w; bx += DXT_BLOCK, wall += DXT_BLOCK_BYTES) {
            uint8_t px[DXT_BLOCK * DXT_BLOCK * 4];
            memset(wall, 0, DXT_BLOCK_BYTES / 2);
            for (int k = 0; k < DXT_BLOCK * DXT_BLOCK; k++) {
                uint16_t v = design[(size_t)(by + k / DXT_BLOCK) * w + bx + k % DXT_BLOCK];
                uint8_t *p = px + k * 4;
                widen_shift(v, &p[0], &p[1], &p[2]);
                p[3] = 255;
                if (v != GRAFFITI_CLEAR)
                    wall[k / 2] |= (uint8_t)(DXT_SOLID << (k % 2 * 4));
            }
            stb_compress_dxt_block(wall + DXT_BLOCK_BYTES / 2, px, 0, STB_DXT_HIGHQUAL);
        }

    for (int i = 0; i < STICKERS; i++) {
        uint8_t *s = at[STICKER_BLOCK] + (size_t)i * STICKER_BYTES;
        int column = i % 5, row = i / 5;
        put32(s, 0xFFFFFFFFu);                      /* no sticker */
        putf(s + 12, (column - 2) * layout[size].grid_x);
        putf(s + 16, (1 - row) * layout[size].grid_y);
        putf(s + 20, STICKER_DEPTH);
        for (int k = 0; k < 3; k++)
            putf(s + 36 + k * 4, layout[size].scale);
    }

    put32(at[TAIL] + 12, TAIL_LAST);
}

int graffiti_save_class(const uint8_t *file, size_t n)
{
    if (!file || n < SIGNED_BYTES)
        return -1;
    int size = file[0];
    if (size >= GRAFFITI_SIZES || n != graffiti_save_size(size) || memcmp(file + HEADER_TAG, "JSRF", 4))
        return -1;
    return size;
}

static void signatures(const uint8_t *file, int size, const uint8_t key[JSRF_KEY_BYTES],
                       uint8_t sig[SIGNATURES][JSRF_SIG_BYTES])
{
    size_t at[BLOCKS];
    block_offsets(size, at);
    jsrf_sign(key, file, HEADER_BYTES, sig[0]);
    for (int i = 0; i < BLOCKS; i++)
        jsrf_sign(key, file + at[i], layout[size].block[i], sig[1 + i]);
}

int jsrf_graffiti_resign(uint8_t *file, size_t n, const uint8_t key[JSRF_KEY_BYTES])
{
    int size = graffiti_save_class(file, n);
    if (size < 0)
        return -1;
    uint8_t sig[SIGNATURES][JSRF_SIG_BYTES];
    signatures(file, size, key, sig);
    memcpy(file + HEADER_BYTES, sig, sizeof sig);
    return 0;
}

int jsrf_graffiti_reslot(uint8_t *file, size_t n, unsigned slot, const uint8_t key[JSRF_KEY_BYTES])
{
    if (slot >= GRAFFITI_SLOTS || graffiti_save_class(file, n) < 0)
        return -1;
    file[HEADER_SLOT] = (uint8_t)(SLOT_BYTE_BASE + slot);
    return jsrf_graffiti_resign(file, n, key);
}

int graffiti_save_verifies(const uint8_t *file, size_t n, const uint8_t key[JSRF_KEY_BYTES])
{
    int size = graffiti_save_class(file, n);
    if (size < 0)
        return -1;
    uint8_t sig[SIGNATURES][JSRF_SIG_BYTES];
    signatures(file, size, key, sig);
    return memcmp(file + HEADER_BYTES, sig, sizeof sig) == 0;
}

int graffiti_save_build(const uint8_t *design, int size, unsigned slot,
                        const uint8_t key[JSRF_KEY_BYTES], uint8_t *out)
{
    if (!design || !out || size < 0 || size >= GRAFFITI_SIZES || slot >= GRAFFITI_SLOTS)
        return -1;
    const size_t px = (size_t)graffiti_width(size) * graffiti_height(size);
    uint16_t *v = malloc(px * sizeof *v);
    if (!v)
        return -1;
    for (size_t i = 0; i < px; i++) {
        const uint8_t *p = design + i * 4;
        v[i] = p[3] < SOLID_ALPHA ? GRAFFITI_CLEAR : graffiti_rgb565(p[0], p[1], p[2]);
    }
    memset(out, 0, graffiti_save_size(size));
    out[0] = (uint8_t)size;
    out[HEADER_SLOT] = (uint8_t)(SLOT_BYTE_BASE + slot);
    memcpy(out + HEADER_TAG, "JSRF", 4);
    size_t at[BLOCKS];
    uint8_t *blocks[BLOCKS];
    block_offsets(size, at);
    for (int i = 0; i < BLOCKS; i++)
        blocks[i] = out + at[i];
    build_blocks(v, size, blocks);
    free(v);
    return jsrf_graffiti_resign(out, graffiti_save_size(size), key);
}

/* ---- slots ---------------------------------------------------------------- */

void graffiti_slot_name(unsigned slot, char name[12])
{
    snprintf(name, 12, "GRAFFITI%03u", (slot + 1) % 1000);
}

void graffiti_slot_name16(unsigned slot, uint16_t name[12])
{
    char a[12];
    graffiti_slot_name(slot, a);
    for (int i = 0; i < 12; i++)
        name[i] = (uint8_t)a[i];
}

void graffiti_slot_folder(unsigned slot, char out[JSRF_FOLDER_CHARS + 1])
{
    uint16_t name[12];
    graffiti_slot_name16(slot, name);
    jsrf_folder_name(name, 11, out);
}

void graffiti_slot_place(unsigned slot, int *page, int *row, int *column)
{
    *page = (int)(slot / GRAFFITI_PAGE_SLOTS);
    *row = (int)(slot % GRAFFITI_PAGE_SLOTS / GRAFFITI_COLUMNS);
    *column = (int)(slot % GRAFFITI_COLUMNS);
}

int graffiti_slots_taken(const char *title, uint8_t taken[GRAFFITI_SLOTS])
{
    int n = 0;
    for (unsigned s = 0; s < GRAFFITI_SLOTS; s++) {
        char folder[JSRF_FOLDER_CHARS + 1], path[4200];
        graffiti_slot_folder(s, folder);
        snprintf(path, sizeof path, "%s/%s", title, folder);
        taken[s] = (uint8_t)(js_is_dir(path) || js_is_file(path));
        n += taken[s];
    }
    return n;
}

int graffiti_next_free(const uint8_t taken[GRAFFITI_SLOTS], unsigned from)
{
    for (unsigned s = from; s < GRAFFITI_SLOTS; s++)
        if (!taken[s])
            return (int)s;
    return -1;
}

/* ---- the files a design's save folder holds -------------------------------- */

const char *graffiti_icon_file(int size)
{
    return size >= 0 && size < GRAFFITI_SIZES ? icons[size] : NULL;
}

typedef struct {
    const char *want;
    int dir;
    char got[256];
} find_ctx;

static int find_each(void *ctx, const char *name, int is_dir)
{
    find_ctx *f = ctx;
    const char *a = name, *b = f->want;
    if (!is_dir != !f->dir)
        return 0;
    for (; *a && *b; a++, b++) {
        int ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a, cb = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (ca != cb)
            return 0;
    }
    if (*a || *b || strlen(name) >= sizeof f->got)
        return 0;
    strcpy(f->got, name);
    return 1;
}

/* dir/want, its name matched without regard to case, into path. */
static int find(const char *dir, const char *want, int is_dir, char *path, size_t n)
{
    find_ctx f = { want, is_dir, "" };
    js_list_dir(dir, find_each, &f);
    if (!f.got[0])
        return -1;
    int k = snprintf(path, n, "%s/%s", dir, f.got);
    return k < 0 || (size_t)k >= n ? -1 : 0;
}

int graffiti_icon_read(const char *game, int size, uint8_t **data, size_t *n)
{
    char media[4200], mark[4200], icon[4200];
    const char *file = graffiti_icon_file(size);
    *data = NULL;
    *n = 0;
    if (!file || find(game, "Media", 1, media, sizeof media) || find(media, "Mark", 1, mark, sizeof mark) ||
        find(mark, file, 0, icon, sizeof icon))
        return -1;
    unsigned char *d = NULL;
    if (js_read_file(icon, JSRF_FILE_MAX, &d, n) != 0)
        return -1;
    *data = d;
    return 0;
}

int graffiti_save_make(jsrf_save *s, const uint8_t *design, int size, unsigned slot,
                       const uint8_t *icon, size_t icon_n, const uint8_t key[JSRF_KEY_BYTES])
{
    size_t n = graffiti_save_size(size);
    uint16_t name[12];
    uint8_t *dat = n ? malloc(n) : NULL, *meta = malloc(64), *img = NULL;
    memset(s, 0, sizeof *s);
    if (!dat || !meta || graffiti_save_build(design, size, slot, key, dat))
        goto fail;
    graffiti_slot_name16(slot, name);
    size_t mn = jsrf_meta_build(name, 11, meta, 64);
    if (!mn)
        goto fail;
    if (icon && icon_n) {
        img = malloc(icon_n);
        if (!img)
            goto fail;
        memcpy(img, icon, icon_n);
    }
    if (jsrf_save_add(s, JSRF_GRAFFITI_FILE, dat, n)) {
        dat = NULL;
        goto fail;
    }
    dat = NULL;
    if (jsrf_save_add(s, JSRF_META_FILE, meta, mn)) {
        meta = NULL;
        goto fail;
    }
    meta = NULL;
    if (img && jsrf_save_add(s, "SaveImage.xbx", img, icon_n)) {
        img = NULL;
        goto fail;
    }
    graffiti_slot_name(slot, s->from);
    jsrf_save_check(s);
    return 0;
fail:
    free(dat);
    free(meta);
    free(img);
    jsrf_save_free(s);
    return -1;
}
