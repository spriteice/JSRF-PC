/* tests/prompts: the button prompts' art (src/input/prompt_art.c, with the
 * pictures built in as the engine builds them) and the icons drawn in the
 * game's textures (src/nv2a_gl/nv2a_prompts.c), on made-up textures and a
 * made-up table (run.py says which cases). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input_layout.h"
#include "prompt_art.h"
#include "nv2a_prompts.h"
#include "../../third_party/miniz/miniz.h"

static int g_fail;
#define CHECK(cond, ...) do { if (!(cond)) { g_fail = 1; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

/* ---- art ------------------------------------------------------------------ */

/* Every picture a set can ask for is built in and decodes, from 64 to
 * NV2A_PROMPT_SIDE pixels a side; the built-in ones are in strcmp's
 * order, for the lookup, and each is one a set asks for (only the pictures
 * used are in the repository); the Guide has none; every set has the same
 * touchpad, and no paddle's picture but their names, P1 to P4 as the Xbox
 * Elite labels them; the PlayStation's Mute and a key without a picture
 * are NULL (the blank key's), the Mute with its name, and Nintendo's
 * Capture has its own; and the game's icons stand for the pad's inputs the
 * defaults put them on. */
static void case_art(void)
{
    static const int arts[] = { PROMPT_ART_XBOX, PROMPT_ART_PLAYSTATION, PROMPT_ART_NINTENDO,
                                PROMPT_ART_KEYS };
    char *used = (char *)calloc((size_t)prompt_art_nfiles, 1);
    size_t a;
    int i, input, n = 0;
    for (i = 1; i < prompt_art_nfiles; i++)
        CHECK(strcmp(prompt_art_files[i - 1].name, prompt_art_files[i].name) < 0,
              "%s before %s", prompt_art_files[i - 1].name, prompt_art_files[i].name);
    for (a = 0; a < sizeof arts / sizeof arts[0]; a++) {
        int last = arts[a] == PROMPT_ART_KEYS ? INPUT_KEYS : INPUT_PAD_INPUTS;
        for (input = 0; input < last; input++) {
            const char *name = prompt_art_name(arts[a], input);
            PromptImage im;
            if (!name) continue;
            n++;
            CHECK(prompt_art_image(name, &im) && im.w >= 64 && im.w <= NV2A_PROMPT_SIDE && im.h >= 64
               && im.h <= NV2A_PROMPT_SIDE, "art %d input %d: %s", arts[a], input, name);
            for (i = 0; i < prompt_art_nfiles; i++)
                if (!strcmp(prompt_art_files[i].name, name)) used[i] = 1;
        }
    }
    for (i = 0; i < prompt_art_nfiles; i++)
        if (!strcmp(prompt_art_files[i].name, PROMPT_ART_BLANK)) used[i] = 1;
    for (i = 0; i < prompt_art_nfiles; i++)
        CHECK(used[i], "%s.png is built in but no set asks for it", prompt_art_files[i].name);
    free(used);
    CHECK(n > 140, "only %d pictures asked for", n);
    for (a = 0; a + 1 < sizeof arts / sizeof arts[0]; a++) {
        CHECK(!prompt_art_name(arts[a], INPUT_PAD_GUIDE), "art %d has a Guide", arts[a]);
        CHECK(!prompt_art_name(arts[a], INPUT_PAD_PADDLE1) && !prompt_art_name(arts[a], INPUT_PAD_PADDLE4)
           && !strcmp(prompt_art_label(arts[a], INPUT_PAD_PADDLE1), "P1")
           && !strcmp(prompt_art_label(arts[a], INPUT_PAD_PADDLE2), "P3")
           && !strcmp(prompt_art_label(arts[a], INPUT_PAD_PADDLE3), "P2")
           && !strcmp(prompt_art_label(arts[a], INPUT_PAD_PADDLE4), "P4"),
              "art %d's paddles", arts[a]);
        CHECK(!strcmp(prompt_art_name(arts[a], INPUT_PAD_TOUCHPAD), "touchpad"), "art %d's touchpad",
              arts[a]);
        CHECK(!prompt_art_label(arts[a], INPUT_PAD_A), "art %d names its A", arts[a]);
    }
    CHECK(!strcmp(prompt_art_name(PROMPT_ART_XBOX, INPUT_PAD_A), "xbox_a"), "Xbox's A");
    CHECK(!strcmp(prompt_art_name(PROMPT_ART_PLAYSTATION, INPUT_PAD_A), "ps_cross"), "Cross");
    CHECK(!strcmp(prompt_art_name(PROMPT_ART_NINTENDO, INPUT_PAD_A), "nintendo_a"), "Nintendo's A");
    CHECK(!prompt_art_name(PROMPT_ART_PLAYSTATION, INPUT_PAD_MISC1)
       && !strcmp(prompt_art_label(PROMPT_ART_PLAYSTATION, INPUT_PAD_MISC1), "Mute"), "the PlayStation's Mute");
    CHECK(!strcmp(prompt_art_name(PROMPT_ART_NINTENDO, INPUT_PAD_MISC1), "nintendo_capture"),
          "Nintendo's Capture");
    CHECK(!prompt_art_label(PROMPT_ART_XBOX, INPUT_PAD_MISC1) && !prompt_art_label(PROMPT_ART_NINTENDO, INPUT_PAD_MISC1)
       && !prompt_art_label(PROMPT_ART_KEYS, 49) && !prompt_art_label(PROMPT_ART_PLAYSTATION, INPUT_PAD_INPUTS),
          "a label where there is a picture");
    CHECK(!strcmp(prompt_art_name(PROMPT_ART_KEYS, 57), "key_caps_lock")
       && !strcmp(prompt_art_name(PROMPT_ART_KEYS, 231), "key_win")
       && !strcmp(prompt_art_name(PROMPT_ART_KEYS, 49), "key_backslash")
       && !strcmp(prompt_art_name(PROMPT_ART_KEYS, 56), "key_slash")
       && !strcmp(prompt_art_name(PROMPT_ART_KEYS, INPUT_KEY_KP_ENTER), "key_enter"),
          "Caps Lock, the right Windows key, the backslash, the slash or the keypad's Enter");
    CHECK(!prompt_art_name(PROMPT_ART_KEYS, 71) && !prompt_art_name(PROMPT_ART_KEYS, 72),
          "Scroll Lock or Pause has a picture");
    CHECK(!strcmp(prompt_art_name(PROMPT_ART_KEYS, INPUT_KEY_SPACE), "key_space"), "Space");
    CHECK(!strcmp(prompt_art_name(PROMPT_ART_KEYS, INPUT_KEY_MOUSE_LEFT), "mouse_left"), "the mouse's left");
    CHECK(!prompt_art_name(PROMPT_ART_KEYS, INPUT_KEY_AC_BACK), "a keyboard's Back has a picture");
    CHECK(!prompt_art_name(PROMPT_ART_GAME, INPUT_PAD_A) && !prompt_art_label(PROMPT_ART_GAME, INPUT_PAD_PADDLE1),
          "the game's art from the packs");
    CHECK(!prompt_art_name(PROMPT_ART_XBOX, INPUT_PAD_INPUTS) && !prompt_art_name(PROMPT_ART_KEYS, -1),
          "out of range");
    CHECK(prompt_art_pad_input(INPUT_CTL_A) == INPUT_PAD_A
       && prompt_art_pad_input(INPUT_CTL_BLACK) == INPUT_PAD_LSHOULDER
       && prompt_art_pad_input(INPUT_CTL_RTRIGGER) == INPUT_PAD_RTRIGGER
       && prompt_art_pad_input(INPUT_CTL_RTHUMB) == INPUT_PAD_RSTICK
       && prompt_art_pad_input(INPUT_CTL_BACK) == INPUT_PAD_BACK
       && prompt_art_pad_input(INPUT_CTL_LSTICK_UP) == INPUT_NONE
       && prompt_art_pad_input(INPUT_CTL_FREECAM) == INPUT_NONE, "the game's icons' pad inputs");
    {
        PromptImage im;
        CHECK(!prompt_art_image("No_Such_Picture", &im), "a picture that isn't built in");
    }
}

/* ---- colours --------------------------------------------------------------- */

/* One look on light and dark (spriteice, 2026-11-22: the pictures chosen
 * from Juliocacko's and Xelu's packs, the Xbox and PlayStation ones
 * shaded "the same grey as the nintendo buttons, excluding the xbox face
 * buttons"): the face buttons in their colours, the PlayStation's symbols
 * on grey buttons; every other button, every key and the mouse grey all
 * through, dark grey with white letters, so each shows on the message
 * box's white as on a dark scene, and none mostly white (spriteice,
 * 2026-11-21: a white Nintendo A on the box's white was almost
 * unreadable); the Xbox and PlayStation ones the Nintendo ones' grey; and
 * the PlayStation D-pad's lit arm outlined in black. */
#define SOLID      128  /* a texel this opaque is part of the picture */
#define LIGHT      180  /* ... and over this in every channel, white */
#define WHITE_MOST 3    /* at most a third of a picture's texels white: its letters and lit parts */
#define NEUTRAL    24   /* a texel's channels this close is grey */
#define BODY_ALPHA 200  /* a grey texel this opaque ... */
#define BODY_UNDER 160  /* ... and darker than this is the body's, as make.py measures it */
#define DARK_LEAST 50   /* every grey picture's body, on average, at least this ... */
#define DARK_MOST  75   /* ... and at most this: dark grey */
#define SHADED     69   /* the Nintendo pictures' body, the Xbox and PlayStation ones' (make.py, TARGET) ... */
#define SHADED_NEAR 4   /* ... to within this (the PlayStation D-pad's outline takes its 3 darker) */
#define COLOUR_NEAR 40  /* a texel this close in each channel is of a colour (not NEAR:
                         * the Windows headers' own macro, which nv2a_prompts.h's
                         * includes bring in there) */
#define FACE_LEAST 700  /* a face button's texels of its colour, at least */
#define BLACK      40   /* a texel no brighter than this in any channel is black */
#define OUTLINE_LEAST 200 /* the PlayStation D-pad's black outline, at least this many texels */
#define DARK       110  /* a solid texel under this in every channel is dark */

static int near_rgb(const uint8_t *p, const uint8_t rgb[3])
{
    return abs(p[0] - rgb[0]) <= COLOUR_NEAR && abs(p[1] - rgb[1]) <= COLOUR_NEAR && abs(p[2] - rgb[2]) <= COLOUR_NEAR;
}

static int neutral(const uint8_t *p)
{
    int hi = p[0] > p[1] ? p[0] : p[1], lo = p[0] < p[1] ? p[0] : p[1];
    hi = hi > p[2] ? hi : p[2];
    lo = lo < p[2] ? lo : p[2];
    return hi - lo <= NEUTRAL;
}

static void case_colours(void)
{
    /* Each face button's colour, more saturated than the pack's (make.py,
     * SATURATION: the Xbox ones' 1.75 times from their grey, the
     * PlayStation's symbols' HSV saturation 1.5 times, Nintendo's 1.25). */
    static const struct { const char *name; uint8_t rgb[3]; } faces[] = {
        { "xbox_a", { 0, 231, 76 } }, { "xbox_b", { 255, 23, 23 } },
        { "xbox_x", { 0, 118, 255 } }, { "xbox_y", { 255, 229, 0 } },
        { "ps_cross", { 83, 122, 245 } }, { "ps_circle", { 245, 20, 20 } },
        { "ps_square", { 245, 59, 245 } }, { "ps_triangle", { 0, 211, 173 } },
        { "nintendo_a", { 255, 0, 0 } }, { "nintendo_b", { 255, 229, 0 } },
        { "nintendo_x", { 0, 177, 255 } }, { "nintendo_y", { 98, 193, 9 } },
    };
    int i, pads = 0, greys = 0, keys = 0;
    size_t k;
    for (i = 0; i < prompt_art_nfiles; i++) {
        const char *name = prompt_art_files[i].name;
        PromptImage im;
        int face = -1, ps_dpad = !strncmp(name, "ps_dpad_", 8);
        int shaded = !strncmp(name, "xbox_", 5) || !strncmp(name, "ps_", 3) || !strcmp(name, "touchpad");
        int pad = shaded || !strncmp(name, "nintendo_", 9);
        size_t t, solid = 0, white = 0, coloured = 0, black = 0, body = 0, sum = 0, own = 0, others = 0;
        for (k = 0; k < sizeof faces / sizeof faces[0]; k++)
            if (!strcmp(name, faces[k].name)) face = (int)k;
        pads += pad;
        keys += !pad;
        if (!prompt_art_image(name, &im)) {
            CHECK(0, "%s doesn't decode", name);
            continue;
        }
        for (t = 0; t < (size_t)im.w * (size_t)im.h; t++) {
            const uint8_t *p = im.rgba + t * 4;
            if (p[3] < SOLID) continue;
            solid++;
            white += p[0] > LIGHT && p[1] > LIGHT && p[2] > LIGHT;
            black += p[0] <= BLACK && p[1] <= BLACK && p[2] <= BLACK;
            if (!neutral(p)) coloured++;
            else if (p[3] >= BODY_ALPHA && p[0] + p[1] + p[2] < 3 * BODY_UNDER) {
                body++;
                sum += (size_t)(p[0] + p[1] + p[2]);
            }
            if (face < 0) continue;
            for (k = 0; k < sizeof faces / sizeof faces[0]; k++) {
                /* The set's other face buttons: the first of its four. */
                if ((int)k / 4 != face / 4 || !near_rgb(p, faces[k].rgb)) continue;
                if ((int)k == face) own++;
                else others++;
            }
        }
        CHECK(solid > 1000, "%s: %u solid texels", name, (unsigned)solid);
        CHECK(white * WHITE_MOST < solid, "%s: %u of %u solid texels white", name, (unsigned)white,
              (unsigned)solid);
        if (face >= 0) {
            CHECK(own >= FACE_LEAST && !others, "%s: %u solid texels its colour, %u another face button's",
                  name, (unsigned)own, (unsigned)others);
            continue;
        }
        greys += pad;
        CHECK(!coloured, "%s: %u of %u solid texels not grey", name, (unsigned)coloured, (unsigned)solid);
        if (!body) {
            CHECK(0, "%s: no body", name);
            continue;
        }
        CHECK(sum / body >= 3 * DARK_LEAST && sum / body <= 3 * DARK_MOST, "%s: its body %u on average, "
              "not dark grey", name, (unsigned)(sum / body / 3));
        CHECK(!shaded || labs((long)(sum / body) - 3 * SHADED) <= 3 * SHADED_NEAR,
              "%s: its body %u on average, not about %d", name, (unsigned)(sum / body / 3), SHADED);
        CHECK(!ps_dpad || black >= OUTLINE_LEAST, "%s: %u black texels round its arm", name, (unsigned)black);
    }
    CHECK(pads == 51 && greys == 39 && keys == 87, "%d pads' pictures, %d grey, %d keys' and the mouse's, "
          "not 51, 39 and 87", pads, greys, keys);
}

/* A PNG that isn't 8-bit RGBA or a palette, or is cut short, isn't
 * decoded; one that is comes back as its texels, each row's filter undone:
 * made here with miniz's own writer (filter 0), and by hand with a row of
 * each of the other four (Sub, Up, Average and Paeth), as RGBA and as a
 * palette with its alpha (tRNS), whose colours past the alpha's are
 * opaque. A palette's index past its colours, or alpha for more colours
 * than it has, isn't decoded either. */
static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

static void be32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v;
}

/* A chunk of n bytes of data, of kind, at o: its length, kind, data and
 * CRC. Past it. */
static unsigned char *chunk(unsigned char *o, const char *kind, const unsigned char *data, uint32_t n)
{
    be32(o, n);
    memcpy(o + 4, kind, 4);
    if (n) memcpy(o + 8, data, n);
    be32(o + 8 + n, (uint32_t)mz_crc32(MZ_CRC32_INIT, o + 4, n + 4));
    return o + 12 + n;
}

/* px (w x h, bpp bytes a texel: 4 for RGBA, 1 for a palette's indices) as
 * a PNG whose row y has filter y % 5, into png, with the palette's
 * ncolours colours (RGB) and nalpha alphas where bpp is 1; its size. */
static size_t png_filtered(const uint8_t *px, int w, int h, int bpp, const uint8_t *colours,
                           int ncolours, const uint8_t *alphas, int nalpha, unsigned char *png,
                           size_t room)
{
    unsigned char raw[8 * (1 + 8 * 4)], *o = png, ihdr[13];
    mz_ulong n = (mz_ulong)(room - 128 - ncolours * 3 - nalpha);
    int x, y, stride = w * bpp;
    for (y = 0; y < h; y++) {
        const uint8_t *row = px + y * stride, *up = y ? row - stride : NULL;
        unsigned char *r = raw + y * (stride + 1);
        r[0] = (unsigned char)(y % 5);
        for (x = 0; x < stride; x++) {
            int a = x >= bpp ? row[x - bpp] : 0, b = up ? up[x] : 0, c = up && x >= bpp ? up[x - bpp] : 0;
            int pred = r[0] == 0 ? 0 : r[0] == 1 ? a : r[0] == 2 ? b : r[0] == 3 ? (a + b) / 2 : paeth(a, b, c);
            r[1 + x] = (unsigned char)(row[x] - pred);
        }
    }
    memcpy(o, "\x89PNG\r\n\x1a\n", 8); o += 8;
    be32(ihdr, (uint32_t)w); be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = bpp == 1 ? 3 : 6; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    o = chunk(o, "IHDR", ihdr, 13);
    if (bpp == 1) {
        o = chunk(o, "PLTE", colours, (uint32_t)ncolours * 3);
        if (nalpha) o = chunk(o, "tRNS", alphas, (uint32_t)nalpha);
    }
    if (mz_compress(o + 8, &n, raw, (mz_ulong)(h * (stride + 1))) != MZ_OK) return 0;
    be32(o, (uint32_t)n); memcpy(o + 4, "IDAT", 4);
    be32(o + 8 + n, (uint32_t)mz_crc32(MZ_CRC32_INIT, o + 4, n + 4)); o += 12 + n;
    o = chunk(o, "IEND", NULL, 0);
    return (size_t)(o - png);
}

static void case_png(void)
{
    uint8_t px[8 * 8 * 4], *back;
    unsigned char hand[1024], *png;
    size_t size;
    int i, w, h;
    for (i = 0; i < (int)sizeof px; i++) px[i] = (uint8_t)(i * 37 + (i >> 3) * 101 + 11);
    png = (unsigned char *)tdefl_write_image_to_png_file_in_memory(px, 8, 8, 4, &size);
    back = prompt_art_png(png, (unsigned)size, &w, &h);
    CHECK(back && w == 8 && h == 8 && !memcmp(back, px, sizeof px), "miniz's RGBA PNG");
    free(back);
    CHECK(!prompt_art_png(png, (unsigned)size / 2, &w, &h), "a PNG cut short");
    mz_free(png);
    png = (unsigned char *)tdefl_write_image_to_png_file_in_memory(px, 8, 8, 3, &size);
    CHECK(!prompt_art_png(png, (unsigned)size, &w, &h), "an RGB PNG");
    mz_free(png);
    CHECK(!prompt_art_png((const unsigned char *)"not a png", 9, &w, &h), "no PNG");
    size = png_filtered(px, 8, 8, 4, NULL, 0, NULL, 0, hand, sizeof hand);
    back = size ? prompt_art_png(hand, (unsigned)size, &w, &h) : NULL;
    CHECK(back && w == 8 && h == 8, "the filtered PNG");
    for (i = 0; back && i < 8; i++)
        CHECK(!memcmp(back + i * 32, px + i * 32, 32), "row %d, filter %d", i, i % 5);
    free(back);
    {
        /* Four colours, the first three with their alpha. */
        static const uint8_t colours[4 * 3] = { 71, 112, 76, 125, 182, 0, 255, 255, 255, 10, 20, 30 };
        static const uint8_t alphas[3] = { 0, 239, 31 };
        uint8_t index[8 * 8];
        for (i = 0; i < 64; i++) index[i] = (uint8_t)((i * 7 + (i >> 3)) % 4);
        size = png_filtered(index, 8, 8, 1, colours, 4, alphas, 3, hand, sizeof hand);
        back = size ? prompt_art_png(hand, (unsigned)size, &w, &h) : NULL;
        CHECK(back && w == 8 && h == 8, "the palette's PNG");
        for (i = 0; back && i < 64; i++) {
            const uint8_t *t = back + i * 4, *c = colours + index[i] * 3;
            CHECK(t[0] == c[0] && t[1] == c[1] && t[2] == c[2]
               && t[3] == (index[i] < 3 ? alphas[index[i]] : 255),
                  "palette texel %d (row %d, filter %d)", i, i / 8, (i / 8) % 5);
        }
        free(back);
        size = png_filtered(index, 8, 8, 1, colours, 3, alphas, 3, hand, sizeof hand);
        CHECK(size && !prompt_art_png(hand, (unsigned)size, &w, &h), "an index past the palette");
        size = png_filtered(index, 8, 8, 1, colours, 2, alphas, 3, hand, sizeof hand);
        CHECK(size && !prompt_art_png(hand, (unsigned)size, &w, &h), "alpha past the palette");
    }
}

/* ---- the table ---------------------------------------------------------- */

/* A made-up texture of the game's kind: 64 x 32, DXT3's one byte a texel,
 * whatever its content. */
#define TW 64
#define TH 32
static uint8_t g_tex[TW * TH];

static uint32_t crc_of(const uint8_t *p, size_t n)
{
    return (uint32_t)mz_crc32(MZ_CRC32_INIT, p, n);
}

/* Its icons: A, B and Y at 8 x 8 in set 0, A at 4 x 4 in set 1. */
static const Nv2aPromptIcon k_icons[] = {
    { 0, 0, 8, 8, INPUT_CTL_A, 0 }, { 8, 0, 8, 8, INPUT_CTL_B, 0 }, { 16, 0, 8, 8, INPUT_CTL_Y, 0 },
    { 0, 16, 4, 4, INPUT_CTL_A, 1 },
};
static Nv2aPromptTexture g_table[2];

static void table(void)
{
    int i;
    for (i = 0; i < TW * TH; i++) g_tex[i] = (uint8_t)(i * 13 + 5);
    memset(g_table, 0, sizeof g_table);
    g_table[0].name = "Other.dat 0x100";
    g_table[0].format = NV2A_PROMPT_DXT3;
    g_table[0].w = 128;
    g_table[0].h = 128;
    g_table[1].name = "Made/Up.dat 0x20";
    g_table[1].format = NV2A_PROMPT_DXT3;
    g_table[1].w = TW;
    g_table[1].h = TH;
    g_table[1].head = crc_of(g_tex, sizeof g_tex < NV2A_PROMPT_HEAD_BYTES ? sizeof g_tex
                                                                          : NV2A_PROMPT_HEAD_BYTES);
    g_table[1].key = crc_of(g_tex, sizeof g_tex);
    g_table[1].icons = k_icons;
    g_table[1].nicons = (int)(sizeof k_icons / sizeof k_icons[0]);
}

/* Found by its size, format and content, and only then: another size or
 * format isn't hashed or said; another content of the same size and format
 * is said once, not in the table, with its key where its head matched; the
 * texture itself is said once, found. The engine's own table is in order:
 * each entry's icons inside it, in a set each of one size. */
static void case_find(void)
{
    uint8_t other[TW * TH];
    int i, j;
    table();
    CHECK(nv2a_prompts_find(g_table, 2, NV2A_PROMPT_DXT3, TW, TH, g_tex, sizeof g_tex) == 1, "found");
    CHECK(nv2a_prompts_find(g_table, 2, NV2A_PROMPT_DXT3, TW, TH, g_tex, sizeof g_tex) == 1, "again");
    CHECK(nv2a_prompts_find(g_table, 2, NV2A_PROMPT_DXT3, TW, TW, g_tex, sizeof g_tex) == -1, "size");
    CHECK(nv2a_prompts_find(g_table, 2, 0x0C, TW, TH, g_tex, sizeof g_tex) == -1, "format");
    memcpy(other, g_tex, sizeof other);
    other[TW * TH - 1] ^= 1;                    /* past the head: its key differs */
    CHECK(nv2a_prompts_find(g_table, 2, NV2A_PROMPT_DXT3, TW, TH, other, sizeof other) == -1, "key");
    other[0] ^= 1;                              /* in the head */
    CHECK(nv2a_prompts_find(g_table, 2, NV2A_PROMPT_DXT3, TW, TH, other, sizeof other) == -1, "head");
    for (i = 0; i < nv2a_prompt_ntextures; i++) {
        const Nv2aPromptTexture *t = &nv2a_prompt_textures[i];
        CHECK(t->name && t->format == NV2A_PROMPT_DXT3 && t->w && t->h && t->nicons > 0,
              "the table's entry %d", i);
        for (j = 0; j < t->nicons; j++) {
            const Nv2aPromptIcon *c = &t->icons[j];
            int k;
            CHECK(c->w && c->h && c->x + c->w <= t->w && c->y + c->h <= t->h
               && c->control < INPUT_CTL_OWN, "%s: icon %d", t->name, j);
            for (k = 0; k < t->nicons; k++)
                if (t->icons[k].set == c->set)
                    CHECK(t->icons[k].w == c->w && t->icons[k].h == c->h,
                          "%s: set %d's icons %d and %d differ in size", t->name, c->set, j, k);
        }
    }
}

/* ---- drawing -------------------------------------------------------------- */

/* The decoded texture: two levels, 64 x 32 then 32 x 16, each texel its
 * own made-up colour. */
#define L0 (TW * TH * 4)
#define L1 ((TW / 2) * (TH / 2) * 4)
static uint8_t g_rgba[L0 + L1], g_was[L0 + L1];

static void decoded(void)
{
    size_t i;
    for (i = 0; i < sizeof g_rgba; i++) g_rgba[i] = (uint8_t)(i * 7 + (i >> 8));
    memcpy(g_was, g_rgba, sizeof g_rgba);
}

/* Each control as itself, in the game's art: as the default layouts give. */
static void shows_own(Nv2aPromptShows *s)
{
    int c;
    memset(s, 0, sizeof *s);
    for (c = 0; c < INPUT_CTLS; c++) {
        s->icon[c].art = PROMPT_ART_GAME;
        s->icon[c].input = c < INPUT_CTL_OWN ? c : INPUT_NONE;
    }
}

/* Texels changed in level 0 outside rectangle (x, y, w, h), and inside it. */
static int changed(int x, int y, int w, int h, int inside)
{
    int tx, ty, n = 0;
    for (ty = 0; ty < TH; ty++)
        for (tx = 0; tx < TW; tx++) {
            int in = tx >= x && tx < x + w && ty >= y && ty < y + h;
            size_t o = ((size_t)ty * TW + tx) * 4;
            if (in == inside && memcmp(g_rgba + o, g_was + o, 4)) n++;
        }
    return n;
}

/* With every control as itself nothing changes, at any level; nor with
 * A as Y in the game's art, which Default leaves as the game draws it,
 * whatever the layouts say. A shown as the Xbox's A, the pack's: its icons
 * are the pack's picture, and nothing else changes; as PlayStation's
 * Cross the same. A with no input keeps the game's. */
static void case_draw(void)
{
    Nv2aPromptShows s;
    table();
    shows_own(&s);
    decoded();
    nv2a_prompts_draw(&g_table[1], g_rgba, TW, TH, 2, &s);
    CHECK(!memcmp(g_rgba, g_was, sizeof g_rgba), "each as itself: changed");

    s.icon[INPUT_CTL_A].input = INPUT_CTL_Y;
    decoded();
    nv2a_prompts_draw(&g_table[1], g_rgba, TW, TH, 2, &s);
    CHECK(!memcmp(g_rgba, g_was, sizeof g_rgba), "A as Y in the game's art: changed");

    s.icon[INPUT_CTL_A].art = PROMPT_ART_XBOX;
    s.icon[INPUT_CTL_A].input = INPUT_PAD_A;
    decoded();
    nv2a_prompts_draw(&g_table[1], g_rgba, TW, TH, 2, &s);
    CHECK(changed(0, 0, 8, 8, 1) > 32, "A as the pack's Xbox A: %d texels", changed(0, 0, 8, 8, 1));
    CHECK(changed(0, 16, 4, 4, 1) > 0, "set 1's A too");
    CHECK(changed(0, 0, 8, 8, 0) == changed(0, 16, 4, 4, 1), "more than the A icons changed");

    s.icon[INPUT_CTL_A].art = PROMPT_ART_PLAYSTATION;
    s.icon[INPUT_CTL_A].input = INPUT_PAD_A;
    decoded();
    nv2a_prompts_draw(&g_table[1], g_rgba, TW, TH, 2, &s);
    CHECK(changed(0, 0, 8, 8, 1) > 32, "A as Cross: %d texels", changed(0, 0, 8, 8, 1));
    CHECK(changed(0, 0, 8, 8, 0) == changed(0, 16, 4, 4, 1), "more than the A icons changed");

    s.icon[INPUT_CTL_A].input = INPUT_NONE;
    decoded();
    nv2a_prompts_draw(&g_table[1], g_rgba, TW, TH, 2, &s);
    CHECK(!memcmp(g_rgba, g_was, sizeof g_rgba), "A with no input: changed");
}

/* ---- Upscaling's Buttons row: the copies ------------------------------- */

/* Every built-in picture is at most NV2A_PROMPT_SIDE a side, so a copy of
 * the whole of it at 8x is at most 1024. A copy read back from the device
 * is the same size, BGRA as RGBA, at 2x, 4x and 8x, for a square picture
 * and the Enter key's shape; another factor, a side out of range or
 * nothing to read refused, the copy left as it was. */
static void case_copy(void)
{
    static const int sizes[][2] = { { NV2A_PROMPT_SIDE, NV2A_PROMPT_SIDE }, { 100, 79 } };
    int i, x, y, k, s;
    for (i = 0; i < prompt_art_nfiles; i++) {
        PromptImage im;
        if (!prompt_art_image(prompt_art_files[i].name, &im)) {
            CHECK(0, "%s doesn't decode", prompt_art_files[i].name);
            continue;
        }
        CHECK(im.w >= 1 && im.w <= NV2A_PROMPT_SIDE && im.h >= 1 && im.h <= NV2A_PROMPT_SIDE, "%s is %dx%d",
              prompt_art_files[i].name, im.w, im.h);
    }
    for (s = 0; s < (int)(sizeof sizes / sizeof sizes[0]); s++)
        for (k = 2; k <= 8; k *= 2) {
            const int w = sizes[s][0] * k, h = sizes[s][1] * k;
            uint8_t *bgra = (uint8_t *)malloc((size_t)w * h * 4), *rgba = (uint8_t *)malloc((size_t)w * h * 4);
            int wrong = 0;
            for (y = 0; y < h; y++)
                for (x = 0; x < w; x++) {
                    uint8_t *px = bgra + ((size_t)y * w + x) * 4;
                    px[0] = (uint8_t)x; px[1] = (uint8_t)y; px[2] = (uint8_t)(x + y); px[3] = (uint8_t)(k + s);
                }
            CHECK(nv2a_prompts_copy(bgra, sizes[s][0], sizes[s][1], k, rgba), "%dx%d at %dx: not copied",
                  sizes[s][0], sizes[s][1], k);
            for (y = 0; y < h; y++)
                for (x = 0; x < w; x++) {
                    const uint8_t *px = rgba + ((size_t)y * w + x) * 4;
                    wrong += px[0] != (uint8_t)(x + y) || px[1] != (uint8_t)y || px[2] != (uint8_t)x
                          || px[3] != (uint8_t)(k + s);
                }
            CHECK(!wrong, "%dx%d at %dx: %d pixels not the copy's, swapped", sizes[s][0], sizes[s][1], k, wrong);
            memset(rgba, 0x5A, 4);
            CHECK(!nv2a_prompts_copy(bgra, sizes[s][0], sizes[s][1], k + 1, rgba)
               && !nv2a_prompts_copy(bgra, sizes[s][0], sizes[s][1], k * 2 + 1, rgba) && rgba[0] == 0x5A,
                  "%dx: copied", k + 1);
            free(bgra);
            free(rgba);
        }
    {
        uint8_t px[4] = { 1, 2, 3, 4 }, out[4] = { 0x5A, 0x5A, 0x5A, 0x5A };
        CHECK(!nv2a_prompts_copy(px, 0, 1, 2, out) && !nv2a_prompts_copy(px, 1, NV2A_PROMPT_SIDE + 1, 2, out)
           && !nv2a_prompts_copy(NULL, 1, 1, 2, out) && !nv2a_prompts_copy(px, 1, 1, 2, NULL)
           && !nv2a_prompts_copy(px, 1, 1, 1, out) && !nv2a_prompts_copy(px, 1, 1, 16, out) && out[0] == 0x5A,
              "a side out of range, nothing, or another factor copied");
    }
}

/* The pictures from the window's copies where it gives them
 * (Nv2aPromptShows' picture): asked for by the picture's name, and fitted
 * as the built-in one is, at any size; where it gives none, nothing
 * drawn. */
static int g_pictures_asked;
static char g_picture_name[64];
static uint8_t g_copy[204 * 204 * 4];
static int a_copy(const char *name, PromptImage *out)
{
    size_t i;
    g_pictures_asked++;
    snprintf(g_picture_name, sizeof g_picture_name, "%s", name);
    if (strcmp(name, "xbox_a")) return 0;
    for (i = 0; i < sizeof g_copy; i += 4) {
        g_copy[i] = 10; g_copy[i + 1] = 220; g_copy[i + 2] = 30; g_copy[i + 3] = 255;
    }
    out->w = out->h = 204;
    out->rgba = g_copy;
    return 1;
}

static void case_copies(void)
{
    Nv2aPromptShows s;
    const uint8_t *mid;
    table();
    shows_own(&s);
    s.picture = a_copy;
    s.icon[INPUT_CTL_A].art = PROMPT_ART_XBOX;
    s.icon[INPUT_CTL_A].input = INPUT_PAD_A;
    decoded();
    nv2a_prompts_draw(&g_table[1], g_rgba, TW, TH, 2, &s);
    CHECK(g_pictures_asked > 0 && !strcmp(g_picture_name, prompt_art_name(PROMPT_ART_XBOX, INPUT_PAD_A)),
          "the copy of %s asked for %d times, the last %s", prompt_art_name(PROMPT_ART_XBOX, INPUT_PAD_A),
          g_pictures_asked, g_picture_name);
    mid = g_rgba + ((size_t)4 * TW + 4) * 4;
    CHECK(changed(0, 0, 8, 8, 1) > 32 && mid[0] == 10 && mid[1] == 220 && mid[2] == 30,
          "the copy's A: %d texels, the middle %d %d %d", changed(0, 0, 8, 8, 1), mid[0], mid[1], mid[2]);
    CHECK(changed(0, 0, 8, 8, 0) == changed(0, 16, 4, 4, 1), "more than the A icons changed");

    s.icon[INPUT_CTL_A].input = INPUT_PAD_B;
    g_pictures_asked = 0;
    decoded();
    nv2a_prompts_draw(&g_table[1], g_rgba, TW, TH, 2, &s);
    CHECK(g_pictures_asked > 0 && !memcmp(g_rgba, g_was, sizeof g_rgba), "no copy of B, and B drawn");
}

/* A key without a picture: the blank key, with its name written in white
 * where the text function draws it, and without one where there is no
 * text function. The PlayStation's Mute and a paddle, which have no
 * picture, the same with their names. */
static int g_texts;
static const char *g_text_want = "ScrollLock";
static int text(const char *t, uint8_t *alpha, int w, int h)
{
    g_texts++;
    CHECK(!strcmp(t, g_text_want), "the name %s, not %s", t, g_text_want);
    memset(alpha, 255, (size_t)w * h);
    return 1;
}

static void case_label(void)
{
    Nv2aPromptShows s;
    uint8_t blank[8 * 8 * 4];
    const uint8_t *with;
    int y;
    table();
    shows_own(&s);
    s.icon[INPUT_CTL_B].art = PROMPT_ART_KEYS;
    s.icon[INPUT_CTL_B].input = 71;            /* SDL's Scroll Lock: no picture */
    s.name[INPUT_CTL_B] = "ScrollLock";
    decoded();
    nv2a_prompts_draw(&g_table[1], g_rgba, TW, TH, 1, &s);
    for (y = 0; y < 8; y++) memcpy(blank + y * 32, g_rgba + ((size_t)y * TW + 8) * 4, 32);
    CHECK(changed(8, 0, 8, 8, 1) > 32, "B's icon kept");
    CHECK(changed(8, 0, 8, 8, 0) == 0, "more than B's icon changed");
    CHECK(g_texts == 0, "no text function, yet text drawn");
    s.text = text;
    decoded();
    nv2a_prompts_draw(&g_table[1], g_rgba, TW, TH, 1, &s);
    CHECK(g_texts == 1, "the name drawn %d times", g_texts);
    /* The middle of the cap is brighter with the name on it. */
    with = g_rgba + ((size_t)4 * TW + 12) * 4;
    CHECK(with[0] + with[1] + with[2] > blank[(4 * 8 + 4) * 4] + blank[(4 * 8 + 4) * 4 + 1]
                                        + blank[(4 * 8 + 4) * 4 + 2],
          "the name isn't brighter than the blank");
    s.icon[INPUT_CTL_B].art = PROMPT_ART_PLAYSTATION;
    s.icon[INPUT_CTL_B].input = INPUT_PAD_MISC1;
    s.name[INPUT_CTL_B] = NULL;
    g_text_want = "Mute";
    decoded();
    nv2a_prompts_draw(&g_table[1], g_rgba, TW, TH, 1, &s);
    CHECK(g_texts == 2, "Mute's name drawn %d times", g_texts - 1);
    CHECK(changed(8, 0, 8, 8, 1) > 32 && changed(8, 0, 8, 8, 0) == 0, "Mute's blank key");
    s.icon[INPUT_CTL_B].art = PROMPT_ART_NINTENDO;
    s.icon[INPUT_CTL_B].input = INPUT_PAD_PADDLE2;
    g_text_want = "P3";
    decoded();
    nv2a_prompts_draw(&g_table[1], g_rgba, TW, TH, 1, &s);
    CHECK(g_texts == 3, "the paddle's name drawn %d times", g_texts - 2);
    CHECK(changed(8, 0, 8, 8, 1) > 32 && changed(8, 0, 8, 8, 0) == 0, "the paddle's blank key");
}

/* The prompts' own font: "Ab" at one texel a texel in a mask just its
 * size is the two glyphs, with a gap; the lower case is the upper; a
 * larger mask takes whole multiples, centred; a name too long for it is
 * refused. */
static void case_text(void)
{
    uint8_t m[11 * 7], up[11 * 7], big[24 * 16];
    int x, y, on = 0;
    memset(m, 0, sizeof m);
    memset(up, 0, sizeof up);
    CHECK(nv2a_prompts_text("Ab", m, 11, 7), "Ab");
    CHECK(nv2a_prompts_text("AB", up, 11, 7) && !memcmp(m, up, sizeof m), "the lower case as the upper");
    for (y = 0; y < 7; y++) CHECK(!m[y * 11 + 5], "the gap, row %d", y);
    CHECK(!m[0] && m[1] && m[2] && m[3] && !m[4], "A's top row");
    CHECK(m[6 * 11 + 0] && m[6 * 11 + 4] && !m[6 * 11 + 2], "A's bottom row");
    memset(big, 0, sizeof big);
    CHECK(nv2a_prompts_text("A", big, 24, 16), "A, larger");
    for (y = 0; y < 16; y++)
        for (x = 0; x < 24; x++) on += big[y * 24 + x] != 0;
    /* At twice: A's 18 texels are 72, from (7, 1). */
    CHECK(on == 72 && big[1 * 24 + 9] && !big[0 * 24 + 9], "A at twice: %d texels", on);
    CHECK(!nv2a_prompts_text("Backslash", m, 11, 7), "too long");
    CHECK(!nv2a_prompts_text("", m, 11, 7), "nothing");
}

/* A picture fitted: an even 4 x 4 into 2 x 2 is its average; a 4 x 2 into
 * 4 x 4 keeps its shape, centred, clear above and below; up, a 2 x 2 into
 * 8 x 8 is its colour throughout where it is one colour. */
static void case_fit(void)
{
    uint8_t src[4 * 4 * 4], dst[8 * 8 * 4];
    int i;
    for (i = 0; i < 16; i++) {
        src[i * 4 + 0] = (uint8_t)(i & 1 ? 200 : 100);
        src[i * 4 + 1] = 50;
        src[i * 4 + 2] = 0;
        src[i * 4 + 3] = 255;
    }
    nv2a_prompts_fit(src, 4, 4, dst, 2, 2, 2 * 4);
    CHECK(dst[0] == 150 && dst[1] == 50 && dst[3] == 255, "averaged: %d %d %d", dst[0], dst[1], dst[3]);
    nv2a_prompts_fit(src, 4, 2, dst, 4, 4, 4 * 4);
    CHECK(dst[3] == 0 && dst[(3 * 4 + 3) * 4 + 3] == 0, "clear above and below");
    CHECK(dst[(1 * 4 + 0) * 4 + 3] == 255 && dst[(2 * 4 + 3) * 4 + 3] == 255, "the picture between");
    for (i = 0; i < 4; i++) { src[i * 4] = 80; src[i * 4 + 1] = 90; src[i * 4 + 2] = 100; src[i * 4 + 3] = 255; }
    nv2a_prompts_fit(src, 2, 2, dst, 8, 8, 8 * 4);
    for (i = 0; i < 64; i++)
        if (dst[i * 4] != 80 || dst[i * 4 + 1] != 90 || dst[i * 4 + 2] != 100 || dst[i * 4 + 3] != 255) break;
    CHECK(i == 64, "up: texel %d", i);
}

/* An upscaled copy's icon: a picture at four times the icon's size, its
 * corner clear and its disc opaque round its see-through letter, and none
 * for the game's own art, moved or not. */
static void case_icon_at(void)
{
    Nv2aPromptShows s;
    static uint8_t out[32 * 32 * 4];
    table();
    shows_own(&s);
    CHECK(!nv2a_prompts_icon_at(&g_table[1], 0, 4, &s, out), "the game's own");
    s.icon[INPUT_CTL_A].input = INPUT_CTL_Y;
    CHECK(!nv2a_prompts_icon_at(&g_table[1], 0, 4, &s, out), "the game's Y");
    CHECK(!nv2a_prompts_icon_at(&g_table[1], 3, 4, &s, out), "set 1's A as Y in the game's art");
    s.icon[INPUT_CTL_A].art = PROMPT_ART_NINTENDO;
    s.icon[INPUT_CTL_A].input = INPUT_PAD_A;
    memset(out, 0, sizeof out);
    CHECK(nv2a_prompts_icon_at(&g_table[1], 0, 4, &s, out) && out[3] == 0
       && out[(16 * 32 + 6) * 4 + 3] > 200 && out[(6 * 32 + 16) * 4 + 3] > 200,
          "Nintendo's A: its corner %d, its disc %d and %d", out[3], out[(16 * 32 + 6) * 4 + 3],
          out[(6 * 32 + 16) * 4 + 3]);
    CHECK(!nv2a_prompts_icon_at(&g_table[1], 9, 4, &s, out), "no icon 9");
}

/* ---- the message boxes' A and B, flat ------------------------------------ */

#define FLAT 32
static uint8_t g_flat[FLAT * FLAT * 4], g_flat_was[FLAT * FLAT * 4];

/* Control c's flat picture, as art and input show it, at FLAT pixels, into
 * g_flat, the one before it kept in g_flat_was. 1 when drawn. */
static int flat(Nv2aPromptShows *s, int c, int art, int input)
{
    s->icon[c].art = art;
    s->icon[c].input = input;
    memcpy(g_flat_was, g_flat, sizeof g_flat_was);
    memset(g_flat, 0, sizeof g_flat);
    return nv2a_prompts_control_at(c, FLAT, s, g_flat);
}

static const uint8_t *flat_at(const uint8_t *px, int x, int y)
{
    return px + ((size_t)y * FLAT + x) * 4;
}

/* A flat picture: the packs', at the size asked, and none at 0 or past
 * NV2A_PROMPT_PICTURE_MAX, or for a control out of range. None in the
 * game's own art, Default, where the game's 3D button is drawn instead.
 * Nothing on the control gives the Xbox set's picture of the button the
 * game's art is of: A with no input the Xbox A, and a control of the
 * port's own with no input nothing. The same in the message box as out of it, as no pad's
 * picture is white (case colours): the Xbox A green, the PlayStation Cross
 * blue on a dark button, Nintendo's A red at (8, 16) and (16, 8) on its
 * top at 32 pixels, its letter darker in its middle, and its corner
 * clear; the Xbox LB grey wherever it is solid; a key and the blank one
 * with a key's name drawn. */
static void case_control_at(void)
{
    Nv2aPromptShows s;
    static uint8_t big[NV2A_PROMPT_PICTURE_MAX * NV2A_PROMPT_PICTURE_MAX * 4];
    static uint8_t xbox_a[FLAT * FLAT * 4], xbox_y[FLAT * FLAT * 4];
    const uint8_t *p;
    int i, solid;
    shows_own(&s);
    s.icon[INPUT_CTL_A].art = PROMPT_ART_XBOX;
    s.icon[INPUT_CTL_A].input = INPUT_PAD_A;
    CHECK(!nv2a_prompts_control_at(INPUT_CTL_A, 0, &s, g_flat), "size 0 drawn");
    CHECK(!nv2a_prompts_control_at(INPUT_CTL_A, NV2A_PROMPT_PICTURE_MAX + 1, &s, big),
          "past the largest drawn");
    CHECK(nv2a_prompts_control_at(INPUT_CTL_A, NV2A_PROMPT_PICTURE_MAX, &s, big), "the largest");
    CHECK(nv2a_prompts_control_at(INPUT_CTL_A, 1, &s, g_flat), "one pixel");
    CHECK(!nv2a_prompts_control_at(-1, FLAT, &s, g_flat)
       && !nv2a_prompts_control_at(INPUT_CTLS, FLAT, &s, g_flat), "a control out of range");

    /* The Xbox A and Y, coloured. */
    CHECK(flat(&s, INPUT_CTL_A, PROMPT_ART_XBOX, INPUT_PAD_A), "the Xbox A");
    memcpy(xbox_a, g_flat, sizeof xbox_a);
    p = flat_at(xbox_a, 10, 14);
    CHECK(p[3] > 200 && p[1] > p[0] && p[1] > p[2] + 40, "the Xbox A isn't green: %d %d %d %d",
          p[0], p[1], p[2], p[3]);
    CHECK(flat(&s, INPUT_CTL_A, PROMPT_ART_XBOX, INPUT_PAD_Y), "the Xbox Y");
    memcpy(xbox_y, g_flat, sizeof xbox_y);
    CHECK(memcmp(xbox_a, xbox_y, sizeof xbox_a), "the Xbox Y is its A");

    /* The game's art, as itself and moved: none; nothing on the control. */
    CHECK(!flat(&s, INPUT_CTL_A, PROMPT_ART_GAME, INPUT_CTL_A), "the game's A drawn flat");
    CHECK(!flat(&s, INPUT_CTL_A, PROMPT_ART_GAME, INPUT_CTL_Y), "the game's A on Y drawn flat");
    CHECK(flat(&s, INPUT_CTL_A, PROMPT_ART_NINTENDO, INPUT_NONE) && !memcmp(g_flat, xbox_a, sizeof xbox_a),
          "A with no input isn't the Xbox A");
    CHECK(!flat(&s, INPUT_CTL_OWN, PROMPT_ART_XBOX, INPUT_NONE), "a control of the port's own drawn");

    /* The PlayStation Cross, blue on a dark button. */
    CHECK(flat(&s, INPUT_CTL_A, PROMPT_ART_PLAYSTATION, INPUT_PAD_A), "the Cross");
    p = flat_at(g_flat, 6, 16);
    CHECK(p[3] > 200 && p[0] < 110 && p[1] < 110 && p[2] < 110, "the Cross's button isn't dark: %d %d %d %d",
          p[0], p[1], p[2], p[3]);
    p = flat_at(g_flat, 16, 15);
    CHECK(p[3] > 200 && p[2] > p[0] + 40 && p[2] > p[1], "the Cross isn't blue: %d %d %d %d", p[0], p[1],
          p[2], p[3]);

    /* Nintendo's A, red as the SNES's, its letter cut dark into it. */
    CHECK(flat(&s, INPUT_CTL_A, PROMPT_ART_NINTENDO, INPUT_PAD_A), "Nintendo's A");
    for (i = 0; i < 2; i++) {
        p = flat_at(g_flat, i ? 16 : 8, i ? 8 : 16);
        CHECK(p[3] == 255 && p[0] > p[1] + 80 && p[0] > p[2] + 80, "Nintendo's A at (%d, %d): "
              "%d %d %d %d, not red", i ? 16 : 8, i ? 8 : 16, p[0], p[1], p[2], p[3]);
    }
    for (i = 0, solid = 0; i < FLAT * FLAT; i++) {
        int x = i % FLAT, y = i / FLAT;
        p = g_flat + (size_t)i * 4;
        if (x >= 11 && x < 21 && y >= 11 && y < 15 && p[3] == 255 && p[0] < DARK && p[1] < DARK
            && p[2] < DARK)
            solid++;
    }
    CHECK(solid > 4 && flat_at(g_flat, 0, 0)[3] == 0, "Nintendo's letter: %d dark texels; its corner %d",
          solid, flat_at(g_flat, 0, 0)[3]);

    /* The Xbox LB, grey. */
    CHECK(flat(&s, INPUT_CTL_A, PROMPT_ART_XBOX, INPUT_PAD_LSHOULDER), "the Xbox LB");
    for (i = 0, solid = 0; i < FLAT * FLAT; i++) {
        p = g_flat + (size_t)i * 4;
        if (p[3] != 255) continue;
        solid++;
        CHECK(neutral(p), "the Xbox LB, texel %d: %d %d %d, not grey", i, p[0], p[1], p[2]);
    }
    CHECK(solid > 100, "the Xbox LB: %d solid texels", solid);

    /* A key, and the blank one with a key's name. */
    CHECK(flat(&s, INPUT_CTL_A, PROMPT_ART_KEYS, INPUT_KEY_SPACE), "the Space key");
    s.text = nv2a_prompts_text;
    s.name[INPUT_CTL_A] = "ScrollLock";
    CHECK(flat(&s, INPUT_CTL_A, PROMPT_ART_KEYS, 71), "the blank key");   /* SDL's Scroll Lock */
    CHECK(memcmp(g_flat, g_flat_was, sizeof g_flat), "the blank key is the Space key");
}

/* Where a mark's picture goes, in NDC: at the screen's middle with nothing
 * done, its half side 20 over 320 across and 240 down; at (480, 120), half
 * 16, (0.5, 0.5); squeezed 0.75 across, x and the width 0.75 as far; moved
 * 32 title pixels left, x 0.1 less; at half size about (1, -1), half as
 * far from it and half the size; and all three, in that order. */
static void case_place(void)
{
    static const struct {
        float x, y, half, s, shift, k, ax, ay, want[4];
    } cases[] = {
        { 320, 240, 20, 1, 0, 1, 0, 0, { -0.0625f, -1.0f / 12, 0.0625f, 1.0f / 12 } },
        { 480, 120, 16, 1, 0, 1, 0, 0, { 0.45f, 0.5f - 1.0f / 15, 0.55f, 0.5f + 1.0f / 15 } },
        { 480, 120, 16, 0.75f, 0, 1, 0, 0, { 0.3375f, 0.5f - 1.0f / 15, 0.4125f, 0.5f + 1.0f / 15 } },
        { 480, 120, 16, 1, 32, 1, 0, 0, { 0.35f, 0.5f - 1.0f / 15, 0.45f, 0.5f + 1.0f / 15 } },
        { 480, 120, 16, 1, 0, 0.5f, 1, -1, { 0.725f, -0.25f - 1.0f / 30, 0.775f, -0.25f + 1.0f / 30 } },
        /* x: 0.75 * 0.5 - 0.1 = 0.275, then 1 + (0.275 - 1) / 2; half 0.0375 / 2. */
        { 480, 120, 16, 0.75f, 32, 0.5f, 1, 1, { 0.61875f, 0.75f - 1.0f / 30, 0.65625f, 0.75f + 1.0f / 30 } },
    };
    size_t i;
    int j;
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        float r[4];
        nv2a_prompts_place(cases[i].x, cases[i].y, cases[i].half, cases[i].s, cases[i].shift, cases[i].k,
                           cases[i].ax, cases[i].ay, r);
        for (j = 0; j < 4; j++) {
            float d = r[j] - cases[i].want[j];
            if (d < 0) d = -d;
            CHECK(d < 1e-5f, "case %zu: %.5f %.5f %.5f %.5f, not %.5f %.5f %.5f %.5f", i, r[0], r[1], r[2], r[3],
                  cases[i].want[0], cases[i].want[1], cases[i].want[2], cases[i].want[3]);
            if (d >= 1e-5f) break;
        }
    }
}

/* Which icon a triangle's texels are on: the B's, all of it or a wedge of
 * it; set 1's A; the B's with its corners a texel outside it (within
 * NV2A_PROMPT_FACE_SLACK); where it is on two icons' rectangles, the one it
 * covers more of; and none for texels across two icons, too far outside
 * one, off them all, in a line or not worked out. */
static void case_tri_icon(void)
{
    static const struct { float u[3], v[3]; int want; } tris[] = {
        { { 8, 16, 8 }, { 0, 0, 8 }, 1 },                 /* the B, half of it */
        { { 12, 16, 15 }, { 4, 4, 6 }, 1 },               /* a wedge of the B */
        { { 0, 4, 0 }, { 16, 16, 20 }, 3 },               /* set 1's A */
        { { 7, 16.5f, 7 }, { -1, -1, 8.5f }, 1 },         /* the B, a texel over */
        { { 7.8f, 8.8f, 8.3f }, { 0, 0, 8 }, 1 },         /* more of the B than the A */
        { { 7.2f, 8.2f, 7.7f }, { 0, 0, 8 }, 0 },         /* more of the A */
        { { 4, 12, 4 }, { 0, 0, 8 }, -1 },                /* across the A and the B */
        { { 8, 18, 8 }, { 0, 0, 8 }, -1 },                /* too far past the B */
        { { 40, 48, 40 }, { 20, 20, 28 }, -1 },           /* no icon's */
        { { 8, 12, 16 }, { 0, 4, 8 }, -1 },               /* a line */
        { { 8, 16, 8 }, { 0, 0, 8 }, -2 },                /* not worked out (below) */
    };
    size_t i;
    table();
    for (i = 0; i < sizeof tris / sizeof tris[0]; i++) {
        float u[3], v[3];
        int want = tris[i].want, got;
        memcpy(u, tris[i].u, sizeof u);
        memcpy(v, tris[i].v, sizeof v);
        if (want == -2) { u[1] = NAN; want = -1; }
        got = nv2a_prompts_tri_icon(&g_table[1], u, v);
        CHECK(got == want, "triangle %zu: icon %d, not %d", i, got, want);
    }
}

/* A test draw's vertices: texels, and where they land. */
#define FV 400
static float g_u[FV], g_v[FV], g_x[FV], g_y[FV];
static int g_n;

static void vtx(float u, float v, float x, float y)
{
    if (g_n >= FV) return;
    g_u[g_n] = u; g_v[g_n] = v; g_x[g_n] = x; g_y[g_n] = y;
    g_n++;
}

/* A map from texels to the screen: x = m00 u + m01 v + o0, y likewise. */
typedef struct { float m[2][2], o[2]; } Map;

static Map map_of(float scale, float turn_deg, float across, float shear_x, float ox, float oy)
{
    /* Scaled, squashed across by across (a turn about the upright), slanted
     * (x += shear_x v), then turned on the screen by turn_deg. */
    float a = turn_deg * 0.017453293f, c = cosf(a), s = sinf(a);
    float k[2][2] = { { scale * across, scale * shear_x }, { 0, scale } };
    Map m;
    m.m[0][0] = c * k[0][0] - s * k[1][0];
    m.m[0][1] = c * k[0][1] - s * k[1][1];
    m.m[1][0] = s * k[0][0] + c * k[1][0];
    m.m[1][1] = s * k[0][1] + c * k[1][1];
    m.o[0] = ox;
    m.o[1] = oy;
    return m;
}

/* A disc of n wedges, as a triangle list, on texels about (cu, cv) of
 * radius r (texel ru for its rim, mirrored across for a back), landing
 * through m; or a fan of it. */
static void disc(const Map *m, float cu, float cv, float r, int n, int back, int fan)
{
    int k;
    for (k = 0; k < n + (fan ? 1 : 0); k++) {
        float a0 = 6.2831853f * (float)k / (float)n, a1 = 6.2831853f * (float)(k + 1) / (float)n;
        float ru0 = cu + r * cosf(a0), rv0 = cv + r * sinf(a0);
        float ru1 = cu + r * cosf(a1), rv1 = cv + r * sinf(a1);
        float tu0 = back ? 2 * cu - ru0 : ru0, tu1 = back ? 2 * cu - ru1 : ru1, tc = cu;
        /* Where each lands: the texel's own place, the back's as the
         * front's at the same corner. */
        float x0 = m->m[0][0] * ru0 + m->m[0][1] * rv0 + m->o[0], y0 = m->m[1][0] * ru0 + m->m[1][1] * rv0 + m->o[1];
        float x1 = m->m[0][0] * ru1 + m->m[0][1] * rv1 + m->o[0], y1 = m->m[1][0] * ru1 + m->m[1][1] * rv1 + m->o[1];
        float xc = m->m[0][0] * cu + m->m[0][1] * cv + m->o[0], yc = m->m[1][0] * cu + m->m[1][1] * cv + m->o[1];
        if (fan) {
            if (k == 0) vtx(tc, cv, xc, yc);
            vtx(tu0, rv0, x0, y0);
            continue;
        }
        vtx(tc, cv, xc, yc);
        vtx(tu0, rv0, x0, y0);
        vtx(tu1, rv1, x1, y1);
    }
}

static int near_map(const Nv2aPromptFace *f, const Map *m, float tol)
{
    int r, c;
    for (r = 0; r < 2; r++) {
        for (c = 0; c < 2; c++) if (fabsf(f->m[r][c] - m->m[r][c]) > tol) return 0;
        if (fabsf(f->o[r] - m->o[r]) > tol * 20.0f) return 0;
    }
    return 1;
}

/* The faces in a draw: a button's disc of 24 wedges, turned about its
 * upright and on the screen, with its back (its texels mirrored) at the
 * same places and its side (texels in a line, or stretched along its rim)
 * sharing its corners, found as its front and its back apart, the side
 * none; the disc as a fan; a card in a strip with a join and the Y beside
 * it, mirrored; two of the B apart on the screen, two faces; a disc in
 * perspective, one face; and none for a corner of an icon, a triangle not
 * worked out, or past max. */
static void case_faces(void)
{
    static int tri[3 * FV], face_of[FV], work[10 * FV];
    Nv2aPromptFace f[8];
    Map m = map_of(3, 15, 0.5f, 0, 200, 100);
    int ntri, n, t, k, front = 0, back = 0, side = 0;
    table();

    /* Front, back and side: 24 + 24 wedges and 4 side triangles. */
    g_n = 0;
    disc(&m, 12, 4, 3.9f, 24, 0, 0);
    disc(&m, 12, 4, 3.9f, 24, 1, 0);
    {   /* The side: from a rim corner of the front, down 30 pixels; one in
         * a line of texels, one stretched along the rim. */
        float x = g_x[1], y = g_y[1], u = g_u[1], v = g_v[1];
        vtx(u, v, x, y); vtx(u, v, x, y + 30); vtx(g_u[2], g_v[2], g_x[2], g_y[2]);
        vtx(u, v, x, y); vtx(u - 0.3f, v + 0.3f, x + 1, y + 30); vtx(u - 0.3f, v, x, y + 30);
    }
    ntri = nv2a_prompts_triangles(NV2A_PROMPT_LIST, g_n, tri, FV);
    n = nv2a_prompts_faces(&g_table[1], g_u, g_v, g_x, g_y, tri, ntri, face_of, work, f, 8);
    for (t = 0; t < ntri; t++) {
        if (t < 24) front += face_of[t] == 0;
        else if (t < 48) back += face_of[t] == 1;
        else side += face_of[t] == -1;
    }
    CHECK(ntri == 50 && n == 2 && front == 24 && back == 24 && side == 2 && f[0].icon == 1 && f[0].ntri == 24
          && f[1].icon == 1 && f[1].ntri == 24 && near_map(&f[0], &m, 1e-3f)
          && f[1].m[0][0] * f[1].m[1][1] - f[1].m[0][1] * f[1].m[1][0] < 0,
          "disc: %d triangles, %d faces, front %d back %d side %d, %.3f %.3f %.3f %.3f", ntri, n, front,
          back, side, f[0].m[0][0], f[0].m[0][1], f[0].m[1][0], f[0].m[1][1]);
    CHECK(fabsf(f[0].u0 - 8.1f) < 0.01f && fabsf(f[0].u1 - 15.9f) < 0.01f && fabsf(f[0].v0 - 0.1f) < 0.01f
          && fabsf(f[0].v1 - 7.9f) < 0.01f, "disc: texels %.2f..%.2f by %.2f..%.2f", f[0].u0, f[0].u1,
          f[0].v0, f[0].v1);

    /* As a fan. */
    g_n = 0;
    disc(&m, 12, 4, 3.9f, 24, 0, 1);
    ntri = nv2a_prompts_triangles(NV2A_PROMPT_FAN, g_n, tri, FV);
    n = nv2a_prompts_faces(&g_table[1], g_u, g_v, g_x, g_y, tri, ntri, face_of, work, f, 8);
    CHECK(ntri == 24 && n == 1 && f[0].ntri == 24 && near_map(&f[0], &m, 1e-3f), "fan: %d triangles, %d faces",
          ntri, n);

    /* A strip: the B slanted (0 to 3), its last vertex again, the Y's first
     * twice, the Y mirrored (u from 24 down to 16). */
    {   static const float su[10] = { 8, 16, 8, 16, 16, 24, 24, 16, 24, 16 };
        static const float sv[10] = { 0, 0, 8, 8, 8, 0, 0, 0, 8, 8 };
        static const float sx[10] = { 52, 72, 48, 68, 68, 80, 80, 90, 80, 90 };
        static const float sy[10] = { 0, 0, 20, 20, 20, 0, 0, 0, 10, 10 };
        ntri = nv2a_prompts_triangles(NV2A_PROMPT_STRIP, 10, tri, FV);
        n = nv2a_prompts_faces(&g_table[1], su, sv, sx, sy, tri, ntri, face_of, work, f, 8);
        CHECK(ntri == 8 && n == 2 && f[0].icon == 1 && f[0].ntri == 2 && f[1].icon == 2 && f[1].ntri == 2
              && face_of[0] == 0 && face_of[1] == 0 && face_of[6] == 1 && face_of[7] == 1
              && fabsf(f[0].m[0][0] - 2.5f) < 1e-4f && fabsf(f[0].m[0][1] + 0.5f) < 1e-4f
              && fabsf(f[0].m[1][1] - 2.5f) < 1e-4f && fabsf(f[1].m[0][0] + 1.25f) < 1e-4f,
              "strip: %d triangles, %d faces", ntri, n);
        for (t = 2; t < 6; t++) CHECK(face_of[t] == -1, "strip: join %d on face %d", t, face_of[t]); }

    /* Two of the B, apart: two faces. */
    {   static const float qu[12] = { 8, 16, 8, 16, 8, 16, 8, 16, 8, 16, 8, 16 };
        static const float qv[12] = { 0, 0, 8, 0, 8, 8, 0, 0, 8, 0, 8, 8 };
        static const float qx[12] = { 0, 16, 0, 16, 0, 16, 40, 56, 40, 56, 40, 56 };
        static const float qy[12] = { 0, 0, 16, 0, 16, 16, 0, 0, 16, 0, 16, 16 };
        ntri = nv2a_prompts_triangles(NV2A_PROMPT_LIST, 12, tri, FV);
        n = nv2a_prompts_faces(&g_table[1], qu, qv, qx, qy, tri, ntri, face_of, work, f, 8);
        CHECK(n == 2 && f[0].ntri == 2 && f[1].ntri == 2 && fabsf(f[1].o[0] - 24) < 1e-3f,
              "apart: %d faces", n);
        /* At most max. */
        CHECK(nv2a_prompts_faces(&g_table[1], qu, qv, qx, qy, tri, ntri, face_of, work, f, 1) == 1
              && face_of[0] == 0 && face_of[2] == -1, "max: not 1");
        /* A corner of the B only: no face. */
        {   float cu[12], cv[12];
            for (k = 0; k < 12; k++) { cu[k] = 8 + (qu[k] - 8) * 0.375f; cv[k] = qv[k] * 0.375f; }
            CHECK(nv2a_prompts_faces(&g_table[1], cu, cv, qx, qy, tri, ntri, face_of, work, f, 8) == 0
                  && face_of[0] == -1, "corner: a face"); }
        /* A corner not worked out: that triangle no face's, its card's other
         * one still the B's. */
        {   float nx[12];
            memcpy(nx, qx, sizeof nx);
            nx[1] = NAN;
            n = nv2a_prompts_faces(&g_table[1], qu, qv, nx, qy, tri, ntri, face_of, work, f, 8);
            CHECK(n == 2 && face_of[0] == -1 && face_of[1] == 0 && f[0].ntri == 1, "NaN: %d faces", n); } }

    /* A disc in perspective: turned 50 degrees about its upright, 6 units
     * away with 400 pixels a unit at one, its radius a unit; one face. */
    {   const float a = 0.8726646f, ca = cosf(a), sa = sinf(a);
        g_n = 0;
        for (k = 0; k < 32; k++) {
            float p[3][2] = { { 0, 0 }, { cosf(6.2831853f * k / 32), sinf(6.2831853f * k / 32) },
                              { cosf(6.2831853f * (k + 1) / 32), sinf(6.2831853f * (k + 1) / 32) } };
            int j;
            for (j = 0; j < 3; j++) {
                float px = p[j][0] * ca, pz = 6 + p[j][0] * sa, py = p[j][1];
                vtx(12 + 3.9f * p[j][0], 4 + 3.9f * p[j][1], 320 + 400 * px / pz, 240 + 400 * py / pz);
            }
        }
        ntri = nv2a_prompts_triangles(NV2A_PROMPT_LIST, g_n, tri, FV);
        n = nv2a_prompts_faces(&g_table[1], g_u, g_v, g_x, g_y, tri, ntri, face_of, work, f, 8);
        CHECK(n == 1 && f[0].ntri == 32, "perspective: %d faces, %d triangles", n, n ? f[0].ntri : 0); }
}

/* Where a face goes upright: about its middle (its texels' middle through
 * its plane), at the plane's larger stretch a texel, which a turn about the
 * upright or the level leaves, and a turn on the screen, a mirror or a turn
 * over, times the size asked (NV2A_PROMPT_UPRIGHT_GROW, or a screen's own);
 * none under a pixel or over the most. */
static void case_face_place(void)
{
    static const struct { float scale, turn, across, shear; int back; float most; int placed; float s; } places[] = {
        { 3, 0, 1, 0, 0, 640, 1, 3 },          /* level */
        { 3, 30, 1, 0, 0, 640, 1, 3 },         /* turned on the screen */
        { 3, 15, 0.5f, 0, 0, 640, 1, 3 },      /* turned about its upright too */
        { 3, 0, 0.2f, 0, 1, 640, 1, 3 },       /* its back: mirrored, nearly edge on */
        { 3, 180, 1, 0, 0, 640, 1, 3 },        /* upside down */
        { 3, 0, 1, 0.25f, 0, 640, 1, 3.3983f }, /* slanted: its larger stretch */
        { 3, 0, 1, 0, 0, 16, 0, 0 },           /* over the most */
        { 0.1f, 0, 1, 0, 0, 640, 0, 0 },       /* under a pixel */
    };
    size_t i;
    for (i = 0; i < sizeof places / sizeof places[0]; i++) {
        Map m = map_of(places[i].scale, places[i].turn, places[i].across, places[i].shear, 100, 50);
        Nv2aPromptFace f;
        float cx, cy, s;
        int placed;
        memset(&f, 0, sizeof f);
        if (places[i].back) { m.m[0][0] = -m.m[0][0]; m.m[1][0] = -m.m[1][0]; }
        f.icon = 1; f.ntri = 2;
        f.u0 = 8; f.u1 = 16; f.v0 = 0; f.v1 = 8;
        memcpy(f.m, m.m, sizeof f.m);
        memcpy(f.o, m.o, sizeof f.o);
        placed = nv2a_prompts_face_place(&f, NV2A_PROMPT_UPRIGHT_GROW, places[i].most, &cx, &cy, &s);
        CHECK(placed == places[i].placed, "place %zu: placed %d, not %d", i, placed, places[i].placed);
        if (!placed || !places[i].placed) continue;
        CHECK(fabsf(cx - (m.m[0][0] * 12 + m.m[0][1] * 4 + m.o[0])) < 1e-3f
              && fabsf(cy - (m.m[1][0] * 12 + m.m[1][1] * 4 + m.o[1])) < 1e-3f
              && fabsf(s - places[i].s * NV2A_PROMPT_UPRIGHT_GROW) < 1e-3f,
              "place %zu: %.3f, %.3f at %.4f a texel, not %.4f", i, cx, cy, s, places[i].s);
        /* At a screen's own size (the VS screen's 0.956): the same middle,
         * that much smaller. */
        {
            float gx, gy, gs;
            CHECK(nv2a_prompts_face_place(&f, 0.956f, places[i].most, &gx, &gy, &gs) && gx == cx && gy == cy
                  && fabsf(gs - 0.956f * s / NV2A_PROMPT_UPRIGHT_GROW) < 1e-3f,
                  "place %zu at 0.956: %.4f a texel, not %.4f", i, gs, 0.956f * s);
        }
    }
}

/* A second copy of a face at the same place: the graffiti editor's size
 * picker's Back as prompts14's log had it (3:06pm on 22 November 2026,
 * UTC+8), the pulsing copy first at 0.89 a texel, then the still one at
 * 0.79, 0.3 pixels off. The still one takes the pulsing one's middle and
 * size; nothing else does. */
static void case_twin(void)
{
    static int sheet_a, sheet_b;   /* two sheets: only their places matter */
    Nv2aPromptLaid laid[3];
    float cx, cy, s;
    int took;
    memset(laid, 0, sizeof laid);
    laid[0].sheet = &sheet_a; laid[0].icon = 5; laid[0].cx = 137.9f; laid[0].cy = 184.2f; laid[0].s = 0.89f;
    /* The still copy, smaller, at the same place: drawn as the pulsing one. */
    cx = 137.6f; cy = 184.3f; s = 0.79f;
    took = nv2a_prompts_twin(laid, 1, &sheet_a, 5, &cx, &cy, &s);
    CHECK(took == 1 && cx == 137.9f && cy == 184.2f && s == 0.89f, "the still copy: %d, %.2f, %.2f at %.2f",
          took, cx, cy, s);
    /* Larger than the one before it, or the same size: left as it is. */
    cx = 137.6f; cy = 184.3f; s = 0.95f;
    took = nv2a_prompts_twin(laid, 1, &sheet_a, 5, &cx, &cy, &s);
    CHECK(!took && s == 0.95f && cx == 137.6f, "a larger copy: %d at %.2f", took, s);
    s = 0.89f;
    CHECK(!nv2a_prompts_twin(laid, 1, &sheet_a, 5, &cx, &cy, &s) && s == 0.89f && cx == 137.6f,
          "a copy the same size moved");
    /* Another icon, another sheet, or another place: left as it is. */
    cx = 137.6f; cy = 184.3f; s = 0.79f;
    CHECK(!nv2a_prompts_twin(laid, 1, &sheet_a, 4, &cx, &cy, &s) && s == 0.79f, "another icon moved");
    CHECK(!nv2a_prompts_twin(laid, 1, &sheet_b, 5, &cx, &cy, &s) && s == 0.79f, "another sheet moved");
    cx = 137.9f + NV2A_PROMPT_TWIN_NEAR + 0.5f;
    CHECK(!nv2a_prompts_twin(laid, 1, &sheet_a, 5, &cx, &cy, &s) && s == 0.79f, "a copy across moved");
    cx = 137.6f; cy = 184.2f - NV2A_PROMPT_TWIN_NEAR - 0.5f;
    CHECK(!nv2a_prompts_twin(laid, 1, &sheet_a, 5, &cx, &cy, &s) && s == 0.79f, "a copy below moved");
    /* The same button in a row of them (the VS menu's): far apart, each its own. */
    laid[1] = laid[0]; laid[1].cx = 198.9f;
    cx = 198.9f; cy = 184.2f; s = 0.89f;
    CHECK(!nv2a_prompts_twin(laid, 2, &sheet_a, 5, &cx, &cy, &s) && cx == 198.9f, "the row's next moved");
    /* Two larger at the place: the largest. None laid: nothing. */
    laid[1] = laid[0]; laid[1].s = 0.91f; laid[1].cx = 138.0f;
    cx = 137.6f; cy = 184.3f; s = 0.79f;
    took = nv2a_prompts_twin(laid, 2, &sheet_a, 5, &cx, &cy, &s);
    CHECK(took && s == 0.91f && cx == 138.0f, "of two: %.2f, not the largest", s);
    s = 0.79f;
    CHECK(!nv2a_prompts_twin(laid, 0, &sheet_a, 5, &cx, &cy, &s) && s == 0.79f, "with none laid");
    CHECK(!nv2a_prompts_twin(NULL, 1, &sheet_a, 5, &cx, &cy, &s), "with no table");
}

/* The rest of a button laid upright: the VS menu's Back as prompts17's log
 * had it (Disp/SprNorm1.dat's icon 5, texels 36.2..61.8 by 462.2..489.8,
 * its middle at 1742.2, 2394.9, turned about 50 degrees: 7.92 pixels a
 * texel across, 12.32 down), laid at 0.956 times its larger stretch, 12.37
 * (port/prompt_buttons.h, PROMPT_BUTTONS_VS_UPRIGHT). Its side, about its
 * turned face, is within reach; a triangle with a corner far off, or one
 * not worked out, is not. */
static void case_reach(void)
{
    Nv2aPromptFace f;
    Nv2aPromptReach r[2];
    const float s = 0.956f * 12.37f, cx = 1742.2f, cy = 2394.9f;
    float x[3], y[3];
    memset(&f, 0, sizeof f);
    f.icon = 5;
    f.u0 = 36.2f; f.u1 = 61.8f; f.v0 = 462.2f; f.v1 = 489.8f;
    nv2a_prompts_reach(&f, cx, cy, s, &r[0]);
    CHECK(fabsf(r[0].hw - 0.75f * s * 25.6f) < 0.01f && fabsf(r[0].hh - 0.75f * s * 27.6f) < 0.01f
          && r[0].cx == cx && r[0].cy == cy, "the reach: %.2f by %.2f about %.1f, %.1f", r[0].hw, r[0].hh,
          r[0].cx, r[0].cy);
    nv2a_prompts_reach(&f, cx, cy, -s, &r[1]);
    CHECK(r[1].hw == r[0].hw && r[1].hh == r[0].hh, "a negative size's reach: %.2f by %.2f", r[1].hw, r[1].hh);
    /* The turned face is about 203 pixels across and 340 down; its side,
     * just past its left edge, top to bottom: the rest of the button. */
    x[0] = cx - 110.0f; y[0] = cy - 175.0f;
    x[1] = cx - 95.0f;  y[1] = cy + 175.0f;
    x[2] = cx - 120.0f; y[2] = cy;
    CHECK(nv2a_prompts_in_reach(r, 1, x, y), "the turned button's side out of reach");
    /* One corner past the reach, across or down: not the button's. */
    x[2] = cx - r[0].hw - 1.0f;
    CHECK(!nv2a_prompts_in_reach(r, 1, x, y), "a triangle reaching past the side taken");
    x[2] = cx - 120.0f; y[1] = cy + r[0].hh + 1.0f;
    CHECK(!nv2a_prompts_in_reach(r, 1, x, y), "a triangle reaching below taken");
    y[1] = cy + 175.0f;
    x[1] = NAN;
    CHECK(!nv2a_prompts_in_reach(r, 1, x, y), "a corner not worked out taken");
    x[1] = cx - 95.0f;
    /* Within the second of two buttons laid; none laid; no table. */
    r[1] = r[0];
    r[0].cx = cx + 1000.0f;
    CHECK(nv2a_prompts_in_reach(r, 2, x, y), "the second button's side out of reach");
    CHECK(!nv2a_prompts_in_reach(r, 1, x, y), "the first button's reach taking the second's side");
    CHECK(!nv2a_prompts_in_reach(r, 0, x, y), "with none laid");
    CHECK(!nv2a_prompts_in_reach(NULL, 1, x, y), "with no table");
}

/* Transforms for nv2a_prompts_solve, as a draw's are. */
typedef struct { float m[4][4]; int nowhere, flat; } Land;

static int land(void *ctx, const float pos[3], float s[3])
{
    const Land *l = (const Land *)ctx;
    float o[4];
    int r;
    if (l->nowhere) return 0;
    for (r = 0; r < 4; r++)
        o[r] = l->flat ? l->m[r][3]
             : l->m[r][0] * pos[0] + l->m[r][1] * pos[1] + l->m[r][2] * pos[2] + l->m[r][3];
    if (!(o[3] > 0.0f)) return 0;
    s[0] = o[0] / o[3];
    s[1] = o[1] / o[3];
    s[2] = o[3];
    return 1;
}

/* Where the menus' icons are moved to: in 2D (a screen-space draw, its w
 * its own) the least move, z left; in perspective, at the face's w, a face
 * turned 30 degrees about its upright included; and none from a transform
 * that lands nowhere or that the position doesn't move. */
static void case_solve(void)
{
    /* 2D: x and y in pixels with D3D's nudge, w not the position's. */
    static const Land flat2d = { { { 1, 0, 0, 0.53125f }, { 0, 1, 0, 0.53125f }, { 0, 0, 1, 0 },
                                   { 0, 0, 0, 1 } }, 0, 0 };
    /* Perspective: a 640x480 screen, 400 pixels to a unit at a unit away,
     * y down; w the distance. */
    static const Land persp = { { { 400, 0, 320, 0 }, { 0, -400, 240, 0 }, { 0, 0, 1, 0 },
                                  { 0, 0, 1, 0 } }, 0, 0 };
    static const Land nowhere = { { { 0 } }, 1, 0 };
    static const Land still = { { { 1, 0, 0, 5 }, { 0, 1, 0, 5 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } }, 0, 1 };
    Land turned = persp;
    float out[3], s[3];
    const float c = cosf(0.5235988f), sn = sinf(0.5235988f);
    int r, ok;

    { const float start[3] = { 10, 20, 0.25f };
      ok = nv2a_prompts_solve(land, (void *)&flat2d, start, 0.1f, 100, 50, 1, out);
      CHECK(ok && fabsf(out[0] - 99.46875f) < 1e-3f && fabsf(out[1] - 49.46875f) < 1e-3f
            && out[2] == 0.25f, "2D: %d, %.4f %.4f %.4f", ok, out[0], out[1], out[2]);
      /* A corner whose own w isn't the card's middle's: placed all the same. */
      ok = nv2a_prompts_solve(land, (void *)&flat2d, start, 0.1f, 100, 50, 2, out);
      CHECK(ok && fabsf(out[0] - 99.46875f) < 1e-3f && fabsf(out[1] - 49.46875f) < 1e-3f,
            "2D, another w: %d, %.4f %.4f", ok, out[0], out[1]); }

    { const float start[3] = { 0, 0, 10 };
      ok = nv2a_prompts_solve(land, (void *)&persp, start, 0.01f, 340, 220, 10, out);
      CHECK(ok && fabsf(out[0] - 0.5f) < 1e-3f && fabsf(out[1] - 0.5f) < 1e-3f
            && fabsf(out[2] - 10) < 1e-3f, "perspective: %d, %.4f %.4f %.4f", ok, out[0], out[1], out[2]); }

    /* The model turned about its upright (y), 10 away: model x runs into
     * the screen as well as across it. */
    for (r = 0; r < 3; r++) {
        float a = persp.m[r][0], b = persp.m[r][2];
        turned.m[r][0] = a * c - b * sn;
        turned.m[r][2] = a * sn + b * c;
        turned.m[r][3] = persp.m[r][2] * 10.0f;
    }
    turned.m[3][0] = -sn; turned.m[3][2] = c; turned.m[3][3] = 10;
    { const float start[3] = { 0, 0, 0 };
      ok = nv2a_prompts_solve(land, &turned, start, 0.01f, 300, 230, 10, out);
      CHECK(ok && land(&turned, out, s) && fabsf(s[0] - 300) < 0.05f && fabsf(s[1] - 230) < 0.05f
            && fabsf(s[2] - 10) < 1e-3f, "turned: %d, lands %.3f %.3f w %.4f", ok, s[0], s[1], s[2]); }

    { const float start[3] = { 0, 0, 10 };
      CHECK(!nv2a_prompts_solve(land, (void *)&nowhere, start, 0.01f, 340, 220, 10, out), "nowhere: solved");
      CHECK(!nv2a_prompts_solve(land, (void *)&still, start, 0.01f, 340, 220, 1, out), "still: solved");
      CHECK(nv2a_prompts_solve(land, (void *)&still, start, 0.01f, 5, 5, 1, out), "still, there: not solved");
      CHECK(!nv2a_prompts_solve(land, (void *)&persp, start, 0, 340, 220, 10, out), "no step: solved"); }
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    if (!strcmp(c, "art")) case_art();
    else if (!strcmp(c, "colours")) case_colours();
    else if (!strcmp(c, "png")) case_png();
    else if (!strcmp(c, "find")) case_find();
    else if (!strcmp(c, "draw")) case_draw();
    else if (!strcmp(c, "label")) case_label();
    else if (!strcmp(c, "text")) case_text();
    else if (!strcmp(c, "fit")) case_fit();
    else if (!strcmp(c, "icon_at")) case_icon_at();
    else if (!strcmp(c, "control_at")) case_control_at();
    else if (!strcmp(c, "place")) case_place();
    else if (!strcmp(c, "tri_icon")) case_tri_icon();
    else if (!strcmp(c, "faces")) case_faces();
    else if (!strcmp(c, "face_place")) case_face_place();
    else if (!strcmp(c, "twin")) case_twin();
    else if (!strcmp(c, "reach")) case_reach();
    else if (!strcmp(c, "solve")) case_solve();
    else if (!strcmp(c, "copy")) case_copy();
    else if (!strcmp(c, "copies")) case_copies();
    else { printf("prompts: no case %s\n", c); return 2; }
    if (g_fail) return 1;
    printf("prompts %s: pass\n", c);
    return 0;
}
