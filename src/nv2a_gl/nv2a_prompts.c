/* The button prompts in the game's textures (nv2a_prompts.h). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nv2a_prompts.h"
#include "prompt_art.h"
#include "../../third_party/miniz/miniz.h"

/* ---- the table ------------------------------------------------------------ */

/* From tools/prompt_census.py on the game's Media folder (the census, in
 * the work's folder, button-prompts/census/): each texture's size, format,
 * head and key, and its icons' rectangles, found with find and sprites and
 * each named by reading it as text (show). Numbers only: no pixel of the
 * game's.
 *
 * The game's only button icons are the pad's A, B, X and Y, in the HUD's
 * 512 x 512 sheet: a row of the four at 18 x 18, and A and B again at
 * 30 x 30. Its other controls are named in words, which stay. The sheet
 * comes in three contents, each with the icons in the same places, the
 * same texel for texel: Disp/SprNorm.dat's (Garage00 and 01 load the
 * same), SprNorm1 to 4's (and Garage10 to 41's; it differs from the first
 * only in one 30 x 30 picture, no icon), and Mark/tex_*.dat's, one for
 * every language. The small row's tiles are 17.5 texels apart: each
 * rectangle is 18 across, so a tile and the next share the texel between
 * them. */
#define ICONS(a) (a), (int)(sizeof(a) / sizeof((a)[0]))

/* The sheet's icons: x, y, w, h, the control, the set (0 the small row, 1
 * the large pair). */
static const Nv2aPromptIcon hud_icons[] = {
    { 3, 312, 18, 18, INPUT_CTL_A, 0 },
    { 20, 312, 18, 18, INPUT_CTL_B, 0 },
    { 38, 312, 18, 18, INPUT_CTL_X, 0 },
    { 55, 312, 18, 18, INPUT_CTL_Y, 0 },
    { 4, 461, 30, 30, INPUT_CTL_A, 1 },
    { 34, 461, 30, 30, INPUT_CTL_B, 1 },
};

const Nv2aPromptTexture nv2a_prompt_textures[] = {
    { "Disp/SprNorm.dat 0x10070", NV2A_PROMPT_DXT3, 512, 512, 0x26DAFBA2u, 0x0D8FA26Bu,
      ICONS(hud_icons) },
    { "Disp/SprNorm1.dat 0x10070", NV2A_PROMPT_DXT3, 512, 512, 0x26DAFBA2u, 0xA4098753u,
      ICONS(hud_icons) },
    { "Mark/tex_eng.dat 0x2d0450", NV2A_PROMPT_DXT3, 512, 512, 0x1FD988F6u, 0x4CF600E2u,
      ICONS(hud_icons) },
};
const int nv2a_prompt_ntextures =
    (int)(sizeof(nv2a_prompt_textures) / sizeof(nv2a_prompt_textures[0]));

/* ---- finding one -------------------------------------------------------- */

static uint32_t crc(const uint8_t *p, size_t n)
{
    return (uint32_t)mz_crc32(MZ_CRC32_INIT, p, n);
}

/* Each texture of a table's size and format, once, by its head and, where
 * its head is the table's, its key: up to this many, then a line that says
 * the rest aren't. */
#define SAID_MAX 64

int nv2a_prompts_find(const Nv2aPromptTexture *table, int n, uint32_t format, uint32_t w,
                      uint32_t h, const uint8_t *level0, size_t bytes)
{
    static uint32_t said[SAID_MAX][2];
    static int nsaid;
    uint32_t head = 0, key = 0;
    int i, sized = 0, keyed = 0, found = -1, q;
    for (i = 0; i < n; i++) {
        const Nv2aPromptTexture *t = &table[i];
        if (t->format != format || t->w != w || t->h != h) continue;
        if (!sized) {
            sized = 1;
            head = crc(level0, bytes < NV2A_PROMPT_HEAD_BYTES ? bytes : NV2A_PROMPT_HEAD_BYTES);
        }
        if (t->head != head) continue;
        if (!keyed) {
            keyed = 1;
            key = crc(level0, bytes);
        }
        if (t->key == key) { found = i; break; }
    }
    if (!sized || nsaid > SAID_MAX) return found;
    for (q = 0; q < nsaid; q++)
        if (said[q][0] == head && said[q][1] == key) return found;
    if (nsaid == SAID_MAX) {
        nsaid++;
        fprintf(stderr, "  [GL] button prompts: more textures of the table's sizes, not said\n");
        return found;
    }
    said[nsaid][0] = head;
    said[nsaid++][1] = key;
    if (found >= 0)
        fprintf(stderr, "  [GL] button prompts: %ux%u format %02X head %08X key %08X: %s, "
                        "its icons drawn\n", w, h, format, head, key, table[found].name);
    else if (keyed)
        fprintf(stderr, "  [GL] button prompts: %ux%u format %02X head %08X key %08X: "
                        "not in the table\n", w, h, format, head, key);
    else
        fprintf(stderr, "  [GL] button prompts: %ux%u format %02X head %08X: not in the table\n",
                w, h, format, head);
    return found;
}

/* ---- fitting a picture ------------------------------------------------- */

int nv2a_prompts_copy(const uint8_t *bgra, int w, int h, int f, uint8_t *rgba)
{
    size_t i, n;
    if (!bgra || !rgba || (f != 2 && f != 4 && f != 8) || w < 1 || h < 1 || w > NV2A_PROMPT_SIDE
     || h > NV2A_PROMPT_SIDE)
        return 0;
    n = (size_t)w * (size_t)f * (size_t)h * (size_t)f;
    for (i = 0; i < n; i++) {
        rgba[i * 4] = bgra[i * 4 + 2];
        rgba[i * 4 + 1] = bgra[i * 4 + 1];
        rgba[i * 4 + 2] = bgra[i * 4];
        rgba[i * 4 + 3] = bgra[i * 4 + 3];
    }
    return 1;
}

/* Texel (x, y) of src, premultiplied, into p[4]; outside it, its edge's. */
static void texel(const uint8_t *src, int sw, int sh, int x, int y, float p[4])
{
    const uint8_t *s;
    float a;
    x = x < 0 ? 0 : x >= sw ? sw - 1 : x;
    y = y < 0 ? 0 : y >= sh ? sh - 1 : y;
    s = src + ((size_t)y * (size_t)sw + (size_t)x) * 4;
    a = s[3] / 255.0f;
    p[0] = s[0] * a; p[1] = s[1] * a; p[2] = s[2] * a; p[3] = s[3];
}

static uint8_t byte_of(float v)
{
    return (uint8_t)(v <= 0.0f ? 0 : v >= 255.0f ? 255 : (int)(v + 0.5f));
}

void nv2a_prompts_fit(const uint8_t *src, int sw, int sh, uint8_t *dst, int w, int h,
                      size_t stride)
{
    /* As large as fits, keeping its shape: scale r, the picture fw x fh at
     * (ox, oy) in the rectangle. */
    float r = (float)w / (float)sw < (float)h / (float)sh ? (float)w / (float)sw
                                                          : (float)h / (float)sh;
    int fw = (int)((float)sw * r + 0.5f), fh = (int)((float)sh * r + 0.5f), ox, oy, x, y;
    if (fw < 1) fw = 1;
    if (fh < 1) fh = 1;
    if (fw > w) fw = w;
    if (fh > h) fh = h;
    ox = (w - fw) / 2;
    oy = (h - fh) / 2;
    for (y = 0; y < h; y++) memset(dst + (size_t)y * stride, 0, (size_t)w * 4);
    for (y = 0; y < fh; y++)
        for (x = 0; x < fw; x++) {
            float acc[4] = { 0, 0, 0, 0 }, p[4], weight = 0.0f;
            uint8_t *o = dst + (size_t)(y + oy) * stride + (size_t)(x + ox) * 4;
            /* The texel's footprint in src: [x0, x1) by [y0, y1). */
            float x0 = (float)x * sw / fw, x1 = (float)(x + 1) * sw / fw;
            float y0 = (float)y * sh / fh, y1 = (float)(y + 1) * sh / fh;
            int i, j, k;
            if (x1 - x0 <= 1.0f) {
                /* Up, or about the same: filtered between the four nearest. */
                float cx = (x0 + x1) * 0.5f - 0.5f, cy = (y0 + y1) * 0.5f - 0.5f;
                int ix = (int)(cx < 0 ? cx - 1 : cx), iy = (int)(cy < 0 ? cy - 1 : cy);
                float fx = cx - ix, fy = cy - iy;
                for (j = 0; j < 2; j++)
                    for (i = 0; i < 2; i++) {
                        float wt = (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
                        texel(src, sw, sh, ix + i, iy + j, p);
                        for (k = 0; k < 4; k++) acc[k] += p[k] * wt;
                    }
                weight = 1.0f;
            } else {
                /* Down: the average of what the footprint covers. */
                for (j = (int)y0; j < (int)y1 + 1 && j < sh; j++) {
                    float wy = (j + 1 < y1 ? j + 1 : y1) - (j > y0 ? j : y0);
                    if (wy <= 0) continue;
                    for (i = (int)x0; i < (int)x1 + 1 && i < sw; i++) {
                        float wx = (i + 1 < x1 ? i + 1 : x1) - (i > x0 ? i : x0);
                        if (wx <= 0) continue;
                        texel(src, sw, sh, i, j, p);
                        for (k = 0; k < 4; k++) acc[k] += p[k] * wx * wy;
                        weight += wx * wy;
                    }
                }
            }
            if (weight <= 0.0f || acc[3] <= 0.0f) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
            /* Back from premultiplied: colour over alpha (a 0..1, as summed). */
            o[0] = byte_of(acc[0] / (acc[3] / 255.0f));
            o[1] = byte_of(acc[1] / (acc[3] / 255.0f));
            o[2] = byte_of(acc[2] / (acc[3] / 255.0f));
            o[3] = byte_of(acc[3] / weight);
        }
}

/* ---- a key's name --------------------------------------------------------- */

/* The names are written in a small font of the prompts' own: each glyph 5
 * texels across and 7 down, a row a byte, its left texel the highest of
 * the five bits; printable ASCII from the space to the grave accent, then
 * the four after the lower case, which is written as upper. A gap of one
 * texel between glyphs. */
#define LABEL_COLS 5
#define LABEL_ROWS 7
#define LABEL_GAP  1
#define FONT_FIRST ' '
#define FONT_UPPER_END '`'      /* the last before the lower case */
#define FONT_AFTER '{'          /* the first after it */
static const uint8_t k_font[][LABEL_ROWS] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /*   */
    { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 }, /* ! */
    { 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* " */
    { 0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A }, /* # */
    { 0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04 }, /* $ */
    { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 }, /* % */
    { 0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D }, /* & */
    { 0x04, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* quote */
    { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 }, /* ( */
    { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 }, /* ) */
    { 0x00, 0x04, 0x15, 0x0E, 0x15, 0x04, 0x00 }, /* '*' */
    { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 }, /* + */
    { 0x00, 0x00, 0x00, 0x00, 0x06, 0x02, 0x04 }, /* , */
    { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 }, /* - */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C }, /* . */
    { 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x00 }, /* '/' */
    { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E }, /* 0 */
    { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E }, /* 1 */
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F }, /* 2 */
    { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E }, /* 3 */
    { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 }, /* 4 */
    { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E }, /* 5 */
    { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E }, /* 6 */
    { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 }, /* 7 */
    { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E }, /* 8 */
    { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C }, /* 9 */
    { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00 }, /* : */
    { 0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08 }, /* ; */
    { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 }, /* < */
    { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 }, /* = */
    { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 }, /* > */
    { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 }, /* ? */
    { 0x0E, 0x11, 0x01, 0x0D, 0x15, 0x15, 0x0E }, /* @ */
    { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, /* A */
    { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E }, /* B */
    { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E }, /* C */
    { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C }, /* D */
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F }, /* E */
    { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 }, /* F */
    { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F }, /* G */
    { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 }, /* H */
    { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E }, /* I */
    { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C }, /* J */
    { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 }, /* K */
    { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F }, /* L */
    { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 }, /* M */
    { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 }, /* N */
    { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, /* O */
    { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 }, /* P */
    { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D }, /* Q */
    { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 }, /* R */
    { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E }, /* S */
    { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, /* T */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E }, /* U */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 }, /* V */
    { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A }, /* W */
    { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 }, /* X */
    { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 }, /* Y */
    { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F }, /* Z */
    { 0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E }, /* [ */
    { 0x00, 0x10, 0x08, 0x04, 0x02, 0x01, 0x00 }, /* backslash */
    { 0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E }, /* ] */
    { 0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00 }, /* ^ */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F }, /* _ */
    { 0x08, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* ` */
    { 0x02, 0x04, 0x04, 0x08, 0x04, 0x04, 0x02 }, /* { */
    { 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 }, /* | */
    { 0x08, 0x04, 0x04, 0x02, 0x04, 0x04, 0x08 }, /* } */
    { 0x00, 0x00, 0x08, 0x15, 0x02, 0x00, 0x00 }, /* ~ */
};

static const uint8_t *glyph(unsigned char c)
{
    if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
    if (c >= FONT_FIRST && c <= FONT_UPPER_END) return k_font[c - FONT_FIRST];
    if (c >= FONT_AFTER && c <= '~') return k_font[FONT_UPPER_END - FONT_FIRST + 1 + (c - FONT_AFTER)];
    return k_font['?' - FONT_FIRST];
}

int nv2a_prompts_text(const char *text, uint8_t *alpha, int w, int h)
{
    int n = (int)strlen(text), across, scale, x0, y0, i, x, y;
    if (!n || w < 1 || h < 1) return 0;
    across = n * (LABEL_COLS + LABEL_GAP) - LABEL_GAP;
    /* Whole texels a glyph texel, as many as fit, so it stays crisp before
     * it is fitted to the icon; at least one. */
    scale = w / across < h / LABEL_ROWS ? w / across : h / LABEL_ROWS;
    if (scale < 1) return 0;
    x0 = (w - across * scale) / 2;
    y0 = (h - LABEL_ROWS * scale) / 2;
    for (i = 0; i < n; i++) {
        const uint8_t *g = glyph((unsigned char)text[i]);
        for (y = 0; y < LABEL_ROWS * scale; y++)
            for (x = 0; x < LABEL_COLS * scale; x++)
                if (g[y / scale] & (1u << (LABEL_COLS - 1 - x / scale)))
                    alpha[(size_t)(y0 + y) * w + (size_t)(x0 + i * (LABEL_COLS + LABEL_GAP) * scale + x)] = 255;
    }
    return 1;
}

/* ---- what an icon shows --------------------------------------------------- */

/* Where a name is written on the blank key (Xelu's, 100 pixels square,
 * its face 28 to 71 across and down), as a fraction of the picture's
 * width and height: a band across its middle, as tall as three of the
 * font's texels a texel (21 pixels, as Xelu's letters are 18 to 23), and
 * wide enough for a name of ten letters at one (ScrollLock). */
#define LABEL_LEFT   0.20f
#define LABEL_TOP    0.38f
#define LABEL_WIDTH  0.60f
#define LABEL_HEIGHT 0.24f

/* What icon i of tex is drawn as: kept as it is (KEEP), or a picture of
 * the packs' (PICTURE), with a name written on it where it is the blank
 * key: a key's, or a pad's button the set has no picture of
 * (prompt_art_label). */
enum { KEEP, PICTURE };
typedef struct { int how; const char *picture, *name; } Look;

/* The packs' picture of s, an input in one of their arts, with name, the
 * window's name for a key, on the blank key where they have none. */
static Look look_picture(PromptIcon s, const char *name)
{
    Look l = { KEEP, NULL, NULL };
    l.picture = prompt_art_name(s.art, s.input);
    if (!l.picture && s.art == PROMPT_ART_KEYS) {
        l.picture = PROMPT_ART_BLANK;
        l.name = name;
    } else if (!l.picture && (l.name = prompt_art_label(s.art, s.input)) != NULL) {
        l.picture = PROMPT_ART_BLANK;
    }
    if (l.picture) l.how = PICTURE;
    return l;
}

static Look look(const Nv2aPromptTexture *tex, int i, const Nv2aPromptShows *shows)
{
    const Nv2aPromptIcon *icon = &tex->icons[i];
    PromptIcon s = shows->icon[icon->control];
    Look l = { KEEP, NULL, NULL };
    /* Nothing on it, or Default: the game's own icon, as it is. */
    if (s.input == INPUT_NONE || s.art == PROMPT_ART_GAME) return l;
    return look_picture(s, shows->name[icon->control]);
}

/* The picture a Look names, fitted to a w x h rectangle at dst, with the
 * key's name on the blank key. 0 where it isn't there to draw. */
static int draw_picture(const Look *l, const Nv2aPromptShows *shows, uint8_t *dst, int w, int h,
                        size_t stride)
{
    PromptImage im;
    uint8_t *cap = NULL, *mask = NULL;
    const uint8_t *src;
    if (!(shows->picture ? shows->picture : prompt_art_image)(l->picture, &im)) return 0;
    src = im.rgba;
    if (l->name && shows->text) {
        int mw = (int)(im.w * LABEL_WIDTH), mh = (int)(im.h * LABEL_HEIGHT);
        int mx = (int)(im.w * LABEL_LEFT), my = (int)(im.h * LABEL_TOP), x, y;
        cap = (uint8_t *)malloc((size_t)im.w * im.h * 4);
        mask = (uint8_t *)calloc((size_t)mw * mh, 1);
        if (cap && mask) {
            memcpy(cap, im.rgba, (size_t)im.w * im.h * 4);
            if (shows->text(l->name, mask, mw, mh)) {
                /* White over the face, as the packs' keys are lettered. */
                for (y = 0; y < mh; y++)
                    for (x = 0; x < mw; x++) {
                        uint8_t *p = cap + ((size_t)(y + my) * im.w + (size_t)(x + mx)) * 4;
                        int a = mask[(size_t)y * mw + x], k;
                        for (k = 0; k < 3; k++) p[k] = (uint8_t)((p[k] * (255 - a) + 255 * a) / 255);
                    }
            }
            src = cap;
        }
    }
    nv2a_prompts_fit(src, im.w, im.h, dst, w, h, stride);
    free(cap);
    free(mask);
    return 1;
}

/* ---- drawing them -------------------------------------------------------- */

/* Icons a texture can have, drawn at once (the table's have far fewer). */
#define ICONS_MAX 64

void nv2a_prompts_draw(const Nv2aPromptTexture *tex, uint8_t *rgba, uint32_t w, uint32_t h,
                       uint32_t nlev, const Nv2aPromptShows *shows)
{
    Look looks[ICONS_MAX];
    uint32_t l, wl = w, hl = h;
    size_t at = 0;
    int i, n = tex->nicons < ICONS_MAX ? tex->nicons : ICONS_MAX, drawn = 0;
    for (i = 0; i < n; i++) {
        looks[i] = look(tex, i, shows);
        drawn += looks[i].how != KEEP;
    }
    if (!drawn) return;
    for (l = 0; l < nlev; l++) {
        uint8_t *level = rgba + at;
        size_t bytes = (size_t)wl * hl * 4;
        for (i = 0; i < n; i++) {
            const Nv2aPromptIcon *icon = &tex->icons[i];
            int x = icon->x >> l, y = icon->y >> l, iw = icon->w >> l, ih = icon->h >> l;
            if (looks[i].how == KEEP || iw < 1 || ih < 1
             || (uint32_t)(x + iw) > wl || (uint32_t)(y + ih) > hl)
                continue;
            draw_picture(&looks[i], shows, level + ((size_t)y * wl + (size_t)x) * 4, iw, ih,
                         (size_t)wl * 4);
        }
        at += bytes;
        wl = wl > 1 ? wl / 2 : 1;
        hl = hl > 1 ? hl / 2 : 1;
    }
}

int nv2a_prompts_icon_at(const Nv2aPromptTexture *tex, int i, int factor,
                         const Nv2aPromptShows *shows, uint8_t *out)
{
    const Nv2aPromptIcon *icon;
    Look k;
    if (i < 0 || i >= tex->nicons || factor < 1) return 0;
    icon = &tex->icons[i];
    k = look(tex, i, shows);
    if (k.how != PICTURE) return 0;
    return draw_picture(&k, shows, out, icon->w * factor, icon->h * factor,
                        (size_t)icon->w * factor * 4);
}

/* ---- the message boxes' A and B, flat ------------------------------------ */

int nv2a_prompts_control_at(int c, int px, const Nv2aPromptShows *shows, uint8_t *out)
{
    PromptIcon s;
    Look l;
    if (c < 0 || c >= INPUT_CTLS || px < 1 || px > NV2A_PROMPT_PICTURE_MAX) return 0;
    s = shows->icon[c];
    /* Default draws no mark: the game's own 3D buttons stay. Nothing on c:
     * the Xbox set's picture of the button c is, as its 3D button isn't
     * drawn. */
    if (s.art == PROMPT_ART_GAME) return 0;
    if (s.input == INPUT_NONE) {
        s.input = prompt_art_pad_input(c);
        s.art = PROMPT_ART_XBOX;
        if (s.input == INPUT_NONE) return 0;
    }
    l = look_picture(s, shows->name[c]);
    return l.how == PICTURE && draw_picture(&l, shows, out, px, px, (size_t)px * 4);
}

int nv2a_prompts_group_at(int group, int art, int px, const Nv2aPromptShows *shows, uint8_t *out)
{
    Look l = { PICTURE, NULL, NULL };
    if (px < 1 || px > NV2A_PROMPT_PICTURE_MAX) return 0;
    l.picture = prompt_art_group(art, group);
    return l.picture && draw_picture(&l, shows, out, px, px, (size_t)px * 4);
}

/* The title's frame, in its pixels: NDC's 2 across is its 640, down its
 * 480. */
#define FRAME_HALF_W 320.0f
#define FRAME_HALF_H 240.0f

int nv2a_prompts_window_cut(const float rect[4], float left, float right, float cut[4], float uv[4])
{
    const float l = left / FRAME_HALF_W - 1.0f, r = right / FRAME_HALF_W - 1.0f;
    const float w = rect[2] - rect[0];
    float x0 = rect[0], x1 = rect[2];
    if (!(right > left) || !(w > 0.0f)) return 0;
    if (x0 < l) x0 = l;
    if (x1 > r) x1 = r;
    if (!(x1 > x0)) return 0;
    cut[0] = x0;
    cut[1] = rect[1];
    cut[2] = x1;
    cut[3] = rect[3];
    uv[0] = (x0 - rect[0]) / w;
    uv[1] = 0.0f;
    uv[2] = (x1 - rect[0]) / w;
    uv[3] = 1.0f;
    return 1;
}

void nv2a_prompts_place(float x, float y, float half, float s, float shift, float k, float ax,
                        float ay, float rect[4])
{
    /* A 3D draw in a widened scene: its x through the widened projection,
     * s times as far from the centre, then a HUD piece's move (the
     * shader's wideShift); then HUD scale's size about its anchor (the
     * shader's hudSize). */
    float cx = s * (x / FRAME_HALF_W - 1.0f) - shift / FRAME_HALF_W;
    float cy = 1.0f - y / FRAME_HALF_H;
    float hx = s * half / FRAME_HALF_W, hy = half / FRAME_HALF_H;
    cx = ax + (cx - ax) * k;
    cy = ay + (cy - ay) * k;
    hx *= k;
    hy *= k;
    rect[0] = cx - hx;
    rect[1] = cy - hy;
    rect[2] = cx + hx;
    rect[3] = cy + hy;
}

/* ---- the menus' buttons, upright ----------------------------------------- */

int nv2a_prompts_triangles(int strip, int n, int *tri, int max)
{
    int t = 0, k;
    if (!tri || n < 3) return 0;
    if (strip == NV2A_PROMPT_LIST) {
        for (k = 0; k + 2 < n && t < max; k += 3, t++) {
            tri[3 * t] = k; tri[3 * t + 1] = k + 1; tri[3 * t + 2] = k + 2;
        }
    } else if (strip == NV2A_PROMPT_STRIP) {
        for (k = 0; k + 2 < n && t < max; k++, t++) {
            tri[3 * t] = k; tri[3 * t + 1] = k + 1; tri[3 * t + 2] = k + 2;
        }
    } else if (strip == NV2A_PROMPT_FAN) {
        for (k = 1; k + 1 < n && t < max; k++, t++) {
            tri[3 * t] = 0; tri[3 * t + 1] = k; tri[3 * t + 2] = k + 1;
        }
    }
    return t;
}

static float span_in(float a0, float a1, float b0, float b1)
{
    float lo = fmaxf(a0, b0), hi = fminf(a1, b1);
    return hi > lo ? hi - lo : 0.0f;
}

int nv2a_prompts_tri_icon(const Nv2aPromptTexture *tex, const float u[3], const float v[3])
{
    float ulo, uhi, vlo, vhi, most = -1.0f;
    int i, k, best = -1;
    if (!tex || !u || !v) return -1;
    ulo = uhi = u[0];
    vlo = vhi = v[0];
    for (k = 0; k < 3; k++) {
        if (!(u[k] == u[k]) || !(v[k] == v[k])) return -1;
        ulo = fminf(ulo, u[k]); uhi = fmaxf(uhi, u[k]);
        vlo = fminf(vlo, v[k]); vhi = fmaxf(vhi, v[k]);
    }
    /* Some texels: not a line or a point of them. */
    if (fabsf((u[1] - u[0]) * (v[2] - v[0]) - (u[2] - u[0]) * (v[1] - v[0])) < 1e-3f) return -1;
    for (i = 0; i < tex->nicons; i++) {
        const Nv2aPromptIcon *c = &tex->icons[i];
        float x0 = (float)c->x, y0 = (float)c->y, x1 = x0 + (float)c->w, y1 = y0 + (float)c->h, on;
        float su = fmaxf(1.0f, NV2A_PROMPT_FACE_SLACK * (float)c->w);
        float sv = fmaxf(1.0f, NV2A_PROMPT_FACE_SLACK * (float)c->h);
        if (ulo < x0 - su || uhi > x1 + su || vlo < y0 - sv || vhi > y1 + sv) continue;
        on = span_in(ulo, uhi, x0, x1) * span_in(vlo, vhi, y0, y1);
        if (on > most) { most = on; best = i; }
    }
    return best;
}

/* The faces' union: each triangle's root. */
static int face_root(int *up, int t)
{
    while (up[t] != t) {
        up[t] = up[up[t]];
        t = up[t];
    }
    return t;
}

/* Triangle t's map from its texels to the screen, m[0] m[1] for x and
 * m[2] m[3] for y, and that map's determinant: 0 where its texels are in a
 * line. */
static float tri_map(const float *u, const float *v, const float *x, const float *y, const int *tri,
                     int t, float m[4])
{
    int a = tri[3 * t], b = tri[3 * t + 1], c = tri[3 * t + 2];
    float du1 = u[b] - u[a], dv1 = v[b] - v[a], du2 = u[c] - u[a], dv2 = v[c] - v[a];
    float dx1 = x[b] - x[a], dy1 = y[b] - y[a], dx2 = x[c] - x[a], dy2 = y[c] - y[a];
    float d = du1 * dv2 - du2 * dv1;
    if (!(fabsf(d) > 0.0f)) { m[0] = m[1] = m[2] = m[3] = 0.0f; return 0.0f; }
    m[0] = (dx1 * dv2 - dx2 * dv1) / d;
    m[1] = (dx2 * du1 - dx1 * du2) / d;
    m[2] = (dy1 * dv2 - dy2 * dv1) / d;
    m[3] = (dy2 * du1 - dy1 * du2) / d;
    return m[0] * m[3] - m[1] * m[2];
}

/* Whether triangles a and b are of one face: their maps from texels to the
 * screen turned the same way (neither seen edge on) and within
 * NV2A_PROMPT_FACE_AGREE of each other, as a face's are; a button's side,
 * or its back, isn't. */
static int tris_agree(const float *u, const float *v, const float *x, const float *y, const int *tri,
                      int a, int b)
{
    float ma[4], mb[4], da = tri_map(u, v, x, y, tri, a, ma), db = tri_map(u, v, x, y, tri, b, mb);
    float na = 0.0f, nb = 0.0f, diff = 0.0f, k = NV2A_PROMPT_FACE_AGREE;
    int i;
    if (!(da * db > 0.0f)) return 0;
    for (i = 0; i < 4; i++) {
        na += ma[i] * ma[i];
        nb += mb[i] * mb[i];
        diff += (ma[i] - mb[i]) * (ma[i] - mb[i]);
    }
    return diff <= k * k * fmaxf(na, nb);
}

/* A shell sort of the corners at, by where they land (kx, then ky). */
static void corners_sort(int *at, int n, const int *kx, const int *ky)
{
    static const int gaps[] = { 701, 301, 132, 57, 23, 10, 4, 1 };
    int g, i, j;
    for (g = 0; g < (int)(sizeof gaps / sizeof gaps[0]); g++) {
        int gap = gaps[g];
        for (i = gap; i < n; i++) {
            int c = at[i];
            for (j = i; j >= gap; j -= gap) {
                int p = at[j - gap];
                if (kx[p] < kx[c] || (kx[p] == kx[c] && ky[p] <= ky[c])) break;
                at[j] = p;
            }
            at[j] = c;
        }
    }
}

int nv2a_prompts_faces(const Nv2aPromptTexture *tex, const float *u, const float *v, const float *x,
                       const float *y, const int *tri, int ntri, int *face_of, int *work,
                       Nv2aPromptFace *out, int max)
{
    /* work: each triangle's root and face, and each corner's place and order. */
    int *up, *id, *kx, *ky, *at, ncorner = 0, nface = 0, t, i, k, found = 0;
    double acc[NV2A_PROMPT_FACES_MOST][12];
    int keep[NV2A_PROMPT_FACES_MOST];
    if (!tex || !u || !v || !x || !y || !tri || !face_of || !work || !out || ntri <= 0) return 0;
    if (max > NV2A_PROMPT_FACES_MOST) max = NV2A_PROMPT_FACES_MOST;
    up = work; id = up + ntri; kx = id + ntri; ky = kx + 3 * ntri; at = ky + 3 * ntri;

    /* Each triangle's icon, where it lands on the screen everywhere. */
    for (t = 0; t < ntri; t++) {
        float tu[3], tv[3];
        int ok = 1;
        up[t] = t;
        id[t] = -1;
        for (k = 0; k < 3; k++) {
            int vi = tri[3 * t + k];
            float px = x[vi], py = y[vi];
            tu[k] = u[vi]; tv[k] = v[vi];
            if (!(px == px) || !(py == py) || fabsf(px) > 1e6f || fabsf(py) > 1e6f) ok = 0;
        }
        face_of[t] = ok ? nv2a_prompts_tri_icon(tex, tu, tv) : -1;
        if (face_of[t] < 0) continue;
        for (k = 0; k < 3; k++) {
            int vi = tri[3 * t + k];
            kx[3 * t + k] = (int)lrintf(x[vi] * 64.0f);
            ky[3 * t + k] = (int)lrintf(y[vi] * 64.0f);
            at[ncorner++] = 3 * t + k;
        }
    }
    /* Triangles of one icon join where a corner of each lands at one place
     * (a shared vertex does) and their maps agree: each corner with the
     * NV2A_PROMPT_FACE_WINDOW before it at its place. */
    corners_sort(at, ncorner, kx, ky);
    for (i = 0; i < ncorner; ) {
        int g = i + 1, a, b;
        while (g < ncorner && kx[at[g]] == kx[at[i]] && ky[at[g]] == ky[at[i]]) g++;
        for (a = i + 1; a < g; a++)
            for (b = a - 1; b >= i && b >= a - NV2A_PROMPT_FACE_WINDOW; b--) {
                int ta = at[a] / 3, tb = at[b] / 3, ra, rb;
                if (ta == tb || face_of[ta] != face_of[tb]) continue;
                ra = face_root(up, ta);
                rb = face_root(up, tb);
                if (ra == rb || !tris_agree(u, v, x, y, tri, ta, tb)) continue;
                if (ra < rb) up[rb] = ra;
                else up[ra] = rb;
            }
        i = g;
    }
    /* Each face's texels, and the sums for the plane its landing fits. */
    for (t = 0; t < ntri; t++) {
        int r, f;
        if (face_of[t] < 0) continue;
        r = face_root(up, t);
        if (id[r] == -1) {
            if (nface >= max) { id[r] = -2; continue; }
            id[r] = nface;
            memset(acc[nface], 0, sizeof acc[nface]);
            out[nface].icon = face_of[t];
            out[nface].ntri = 0;
            out[nface].u0 = out[nface].u1 = u[tri[3 * t]];
            out[nface].v0 = out[nface].v1 = v[tri[3 * t]];
            nface++;
        }
        f = id[r];
        if (f < 0) continue;
        out[f].ntri++;
        for (k = 0; k < 3; k++) {
            int vi = tri[3 * t + k];
            double cu = u[vi], cv = v[vi], cx = x[vi], cy = y[vi];
            out[f].u0 = fminf(out[f].u0, u[vi]); out[f].u1 = fmaxf(out[f].u1, u[vi]);
            out[f].v0 = fminf(out[f].v0, v[vi]); out[f].v1 = fmaxf(out[f].v1, v[vi]);
            acc[f][0] += 1.0;
            acc[f][1] += cu; acc[f][2] += cv; acc[f][3] += cx; acc[f][4] += cy;
            acc[f][5] += cu * cu; acc[f][6] += cu * cv; acc[f][7] += cv * cv;
            acc[f][8] += cu * cx; acc[f][9] += cv * cx; acc[f][10] += cu * cy; acc[f][11] += cv * cy;
        }
    }
    /* The plane: x and y as u and v carry them, by least squares. A face too
     * little of its icon, or whose texels are in a line, isn't one. */
    for (i = 0; i < nface; i++) {
        const Nv2aPromptIcon *c = &tex->icons[out[i].icon];
        double n = acc[i][0], mu = acc[i][1] / n, mv = acc[i][2] / n, mx = acc[i][3] / n,
               my = acc[i][4] / n;
        double suu = acc[i][5] / n - mu * mu, suv = acc[i][6] / n - mu * mv, svv = acc[i][7] / n - mv * mv;
        double sux = acc[i][8] / n - mu * mx, svx = acc[i][9] / n - mv * mx;
        double suy = acc[i][10] / n - mu * my, svy = acc[i][11] / n - mv * my;
        double det = suu * svv - suv * suv;
        keep[i] = -1;
        if (out[i].u1 - out[i].u0 < NV2A_PROMPT_FACE_LEAST * (float)c->w
         || out[i].v1 - out[i].v0 < NV2A_PROMPT_FACE_LEAST * (float)c->h
         || !(det > 1e-6 * (suu + svv) * (suu + svv)))
            continue;
        out[i].m[0][0] = (float)((sux * svv - svx * suv) / det);
        out[i].m[0][1] = (float)((svx * suu - sux * suv) / det);
        out[i].m[1][0] = (float)((suy * svv - svy * suv) / det);
        out[i].m[1][1] = (float)((svy * suu - suy * suv) / det);
        out[i].o[0] = (float)(mx - out[i].m[0][0] * mu - out[i].m[0][1] * mv);
        out[i].o[1] = (float)(my - out[i].m[1][0] * mu - out[i].m[1][1] * mv);
        keep[i] = found;
        if (found != i) out[found] = out[i];
        found++;
    }
    for (t = 0; t < ntri; t++) {
        int f;
        if (face_of[t] < 0) continue;
        f = id[face_root(up, t)];
        face_of[t] = f >= 0 ? keep[f] : -1;
    }
    return found;
}

int nv2a_prompts_face_place(const Nv2aPromptFace *f, float grow, float most, float *cx, float *cy,
                            float *s)
{
    float mu, mv, p, q, r, big, wide, tall;
    if (!f || !cx || !cy || !s) return 0;
    mu = 0.5f * (f->u0 + f->u1);
    mv = 0.5f * (f->v0 + f->v1);
    *cx = f->m[0][0] * mu + f->m[0][1] * mv + f->o[0];
    *cy = f->m[1][0] * mu + f->m[1][1] * mv + f->o[1];
    /* The larger stretch: the root of m'm's larger eigenvalue. */
    p = f->m[0][0] * f->m[0][0] + f->m[1][0] * f->m[1][0];
    q = f->m[0][0] * f->m[0][1] + f->m[1][0] * f->m[1][1];
    r = f->m[0][1] * f->m[0][1] + f->m[1][1] * f->m[1][1];
    big = 0.5f * (p + r) + sqrtf(0.25f * (p - r) * (p - r) + q * q);
    *s = grow * sqrtf(big);
    wide = *s * (f->u1 - f->u0);
    tall = *s * (f->v1 - f->v0);
    return *cx == *cx && *cy == *cy && *s == *s && fmaxf(wide, tall) >= 1.0f && wide <= most
        && tall <= most;
}

int nv2a_prompts_twin(const Nv2aPromptLaid *laid, int n, const void *sheet, int icon, float *cx, float *cy,
                      float *s)
{
    int i, took = 0;
    if (!laid || !cx || !cy || !s) return 0;
    for (i = 0; i < n; i++) {
        const Nv2aPromptLaid *l = &laid[i];
        if (l->sheet != sheet || l->icon != icon || !(l->s > *s)) continue;
        if (fabsf(l->cx - *cx) > NV2A_PROMPT_TWIN_NEAR || fabsf(l->cy - *cy) > NV2A_PROMPT_TWIN_NEAR) continue;
        *cx = l->cx;
        *cy = l->cy;
        *s = l->s;
        took = 1;
    }
    return took;
}

void nv2a_prompts_reach(const Nv2aPromptFace *f, float cx, float cy, float s, Nv2aPromptReach *r)
{
    if (!f || !r) return;
    r->cx = cx;
    r->cy = cy;
    r->hw = 0.5f * NV2A_PROMPT_REACH * fabsf(s * (f->u1 - f->u0));
    r->hh = 0.5f * NV2A_PROMPT_REACH * fabsf(s * (f->v1 - f->v0));
}

int nv2a_prompts_in_reach(const Nv2aPromptReach *r, int n, const float x[3], const float y[3])
{
    int i, k;
    if (!r || !x || !y) return 0;
    for (i = 0; i < n; i++) {
        /* Written so that a NaN corner is out of every reach. */
        for (k = 0; k < 3; k++)
            if (!(fabsf(x[k] - r[i].cx) <= r[i].hw && fabsf(y[k] - r[i].cy) <= r[i].hh)) break;
        if (k == 3) return 1;
    }
    return 0;
}

static float det3(const float m[3][3])
{
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
         - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
         + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

int nv2a_prompts_solve(Nv2aPromptLand land, void *ctx, const float start[3], float h,
                       float x, float y, float w, float out[3])
{
    float p[3], s[3];
    int it, a, r;
    if (!land || !start || !out || !(h > 0.0f)) return 0;
    memcpy(p, start, sizeof p);
    for (it = 0; it < 8; it++) {
        float J[3][3], e[3], dp[3] = { 0.0f, 0.0f, 0.0f }, n[3], det;
        if (!land(ctx, p, s)) return 0;
        e[0] = x - s[0];
        e[1] = y - s[1];
        e[2] = w - s[2];
        if (fabsf(e[0]) <= 0.01f && fabsf(e[1]) <= 0.01f && fabsf(e[2]) <= 1e-5f * fabsf(w) + 1e-7f)
            break;
        for (a = 0; a < 3; a++) {
            float q[3], t[3];
            memcpy(q, p, sizeof q);
            q[a] += h;
            if (!land(ctx, q, t)) return 0;
            for (r = 0; r < 3; r++) J[r][a] = (t[r] - s[r]) / h;
        }
        for (r = 0; r < 3; r++) n[r] = sqrtf(J[r][0] * J[r][0] + J[r][1] * J[r][1] + J[r][2] * J[r][2]);
        det = det3(J);
        if (n[2] > 0.0f && fabsf(det) > 1e-4f * n[0] * n[1] * n[2]) {
            /* x, y and w: the 3x3 system, by Cramer's rule. */
            for (a = 0; a < 3; a++) {
                float M[3][3];
                memcpy(M, J, sizeof M);
                for (r = 0; r < 3; r++) M[r][a] = e[r];
                dp[a] = det3(M) / det;
            }
        } else {
            /* x and y: the least move, J^T (J J^T)^-1 e. */
            float a00 = J[0][0] * J[0][0] + J[0][1] * J[0][1] + J[0][2] * J[0][2];
            float a01 = J[0][0] * J[1][0] + J[0][1] * J[1][1] + J[0][2] * J[1][2];
            float a11 = J[1][0] * J[1][0] + J[1][1] * J[1][1] + J[1][2] * J[1][2];
            float dd = a00 * a11 - a01 * a01, w0, w1;
            if (!(fabsf(dd) > 1e-12f * a00 * a11) || !(dd != 0.0f)) return 0;
            w0 = ( a11 * e[0] - a01 * e[1]) / dd;
            w1 = (-a01 * e[0] + a00 * e[1]) / dd;
            for (a = 0; a < 3; a++) dp[a] = J[0][a] * w0 + J[1][a] * w1;
        }
        for (a = 0; a < 3; a++) p[a] += dp[a];
        if (!(p[0] == p[0]) || !(p[1] == p[1]) || !(p[2] == p[2])) return 0;
    }
    if (!land(ctx, p, s)) return 0;
    if (!(fabsf(x - s[0]) <= 0.5f) || !(fabsf(y - s[1]) <= 0.5f)) return 0;
    memcpy(out, p, sizeof p);
    return 1;
}
