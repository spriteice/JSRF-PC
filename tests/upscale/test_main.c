/* Run with: python tests/upscale/run.py
 *
 * Upscaling's rules and their census, src/nv2a_gl/nv2a_upscale.c, as the
 * engine builds it. 2D upscaling:
 *
 *   1. A font page, an icon sheet and a HUD piece a screen-space draw binds
 *      into the frame are taken: one level, swizzled or DXT, up to 512 a
 *      side, whatever the draw does with depth. So is the title screen's
 *      text, which a draw over the picture binds.
 *   2. A draw that is neither screen-space nor over the picture is the
 *      title's 3D, and takes nothing, whatever its texture or its depth; nor
 *      does a 2D draw into another surface (the boost's 512x512 and 128x128
 *      targets).
 *   3. A 2D draw's texture is left when it has a chain of its own (the
 *      world's), is linear, has a side over 512, or is a bump source; each
 *      says so, and the draw's reasons come before the texture's.
 *   4. The census logs the bindings of 2D draws into the frame, and those
 *      of other draws into the frame with depth off when a 2D draw's would
 *      be taken; not the rest.
 *   5. Its lines: "[UPSCALE] taken ..." and "[UPSCALE] left ...: why",
 *      with the texture's address, size, format, layout and levels, and the
 *      draw: screen-space, a vertex program or the fixed function, whether
 *      it is over the picture, and what it does with depth.
 *   6. The setting: 0, 2, 4 or 8 from whatever was saved or asked for;
 *      the Graphics row's choice for each and back, 4x as a new install
 *      starts.
 *   7. The budget and the bytes: a 512 page at 4x is the whole of a flip's
 *      texels; a copy holds its levels down to the game's size, and with
 *      mipmaps the chain below it, as RGBA8, at 8x too.
 *   8. The summary line a second.
 *   9. Over the picture, a surface's frame as the census showed it: the
 *      title (its clear, the world, a clear of depth alone, the overlay)
 *      and the load screen (its clear, a clear of depth alone straight
 *      after, everything) are over after that clear; the world before it
 *      isn't, nor a frame that starts with a clear of depth alone, nor
 *      anything after the next clear of colour.
 */
#include <stdio.h>
#include <string.h>

#include "nv2a_upscale.h"

static int s_failures;

#define CHECK(c) do { if (!(c)) { printf("upscale: FAIL %s:%d: %s\n", \
                                         __FILE__, __LINE__, #c); s_failures++; } } while (0)

/* A 2D draw into the frame binding a 256x256 DXT font page. */
static Nv2aUpBind font_page(void)
{
    Nv2aUpBind b;
    memset(&b, 0, sizeof b);
    b.screen = 1;
    b.into_frame = 1;
    b.w = 256;
    b.h = 256;
    b.levels = 1;
    b.layout = NV2A_UP_DXT;
    return b;
}

static void taken(void)
{
    Nv2aUpBind b = font_page();
    CHECK(nv2a_up_rule(&b) == NV2A_UP_TAKEN);
    b.layout = NV2A_UP_SWIZZLED;                 /* an icon sheet */
    b.w = 512; b.h = 128;
    CHECK(nv2a_up_rule(&b) == NV2A_UP_TAKEN);
    b.w = 512; b.h = 512;                        /* the largest */
    CHECK(nv2a_up_rule(&b) == NV2A_UP_TAKEN);
    b.w = 8; b.h = 32;                           /* a HUD piece */
    CHECK(nv2a_up_rule(&b) == NV2A_UP_TAKEN);
    /* Depth is the 2D draw's own business: the speed bar writes it, the
     * title's logo tests it. */
    b = font_page();
    b.depth_write = 1;
    CHECK(nv2a_up_rule(&b) == NV2A_UP_TAKEN);
    b.depth_test = 1;
    CHECK(nv2a_up_rule(&b) == NV2A_UP_TAKEN);
    /* The title screen's text, drawn over the picture through a vertex
     * program, testing and writing depth. */
    b = font_page();
    b.screen = 0; b.over = 1; b.depth_test = 1; b.depth_write = 1; b.w = 128; b.h = 128;
    CHECK(nv2a_up_rule(&b) == NV2A_UP_TAKEN);
}

static void draws(void)
{
    Nv2aUpBind b = font_page();
    b.screen = 0;
    CHECK(nv2a_up_rule(&b) == NV2A_UP_NOT_2D);
    b.depth_test = 1; b.depth_write = 1;
    CHECK(nv2a_up_rule(&b) == NV2A_UP_NOT_2D);
    b = font_page();
    b.into_frame = 0;
    CHECK(nv2a_up_rule(&b) == NV2A_UP_NOT_FRAME);
    /* A 3D draw of a world texture is the draw's verdict, not the texture's. */
    b = font_page();
    b.screen = 0; b.depth_test = 1; b.depth_write = 1; b.levels = 7; b.w = 128; b.h = 128;
    CHECK(nv2a_up_rule(&b) == NV2A_UP_NOT_2D);
    /* Over the picture is 2D, as screen-space is: into the frame, and the
     * texture's conditions, as for any 2D draw. */
    b.over = 1;
    CHECK(nv2a_up_rule(&b) == NV2A_UP_CHAIN);
    b = font_page();
    b.screen = 0; b.over = 1; b.into_frame = 0;
    CHECK(nv2a_up_rule(&b) == NV2A_UP_NOT_FRAME);
}

static void textures(void)
{
    Nv2aUpBind b = font_page();
    b.levels = 7; b.w = 128; b.h = 128;          /* the world's, with its chain */
    CHECK(nv2a_up_rule(&b) == NV2A_UP_CHAIN);
    b = font_page();
    b.layout = NV2A_UP_LINEAR;                   /* a backdrop, in texels */
    CHECK(nv2a_up_rule(&b) == NV2A_UP_LINEAR_TEX);
    b = font_page();
    b.w = 1024; b.h = 1024;                      /* the graffiti picker's sheet */
    CHECK(nv2a_up_rule(&b) == NV2A_UP_TOO_BIG);
    b.w = 513; b.h = 16;
    CHECK(nv2a_up_rule(&b) == NV2A_UP_TOO_BIG);
    b.w = 16; b.h = 513;
    CHECK(nv2a_up_rule(&b) == NV2A_UP_TOO_BIG);
    b = font_page();
    b.bump = 1;
    CHECK(nv2a_up_rule(&b) == NV2A_UP_BUMP);
    /* The first reason that applies is the one given. */
    b = font_page();
    b.levels = 2; b.layout = NV2A_UP_LINEAR; b.w = 1024; b.bump = 1;
    CHECK(nv2a_up_rule(&b) == NV2A_UP_CHAIN);
    b.into_frame = 0;
    CHECK(nv2a_up_rule(&b) == NV2A_UP_NOT_FRAME);
    b.screen = 0;
    CHECK(nv2a_up_rule(&b) == NV2A_UP_NOT_2D);
}

static void census(void)
{
    int v;
    Nv2aUpBind b = font_page();
    for (v = 0; v < NV2A_UP_VERDICTS; v++)
        CHECK(nv2a_up_verdict_names[v] && nv2a_up_verdict_names[v][0]);
    CHECK(!nv2a_up_of_texture(NV2A_UP_NOT_2D));
    CHECK(!nv2a_up_of_texture(NV2A_UP_NOT_FRAME));
    for (v = NV2A_UP_TAKEN; v < NV2A_UP_VERDICTS; v++)
        if (v != NV2A_UP_NOT_2D && v != NV2A_UP_NOT_FRAME) {
            CHECK(nv2a_up_of_texture(v));
            CHECK(nv2a_up_census_wants(&b, v));
        }
    /* 2D art drawn some other way, into the frame with depth off: the rule
     * leaves it, and the census says so. */
    b.screen = 0;
    CHECK(nv2a_up_rule(&b) == NV2A_UP_NOT_2D);
    CHECK(nv2a_up_census_wants(&b, NV2A_UP_NOT_2D));
    /* Not a 3D draw, which tests or writes depth. */
    b.depth_test = 1;
    CHECK(!nv2a_up_census_wants(&b, NV2A_UP_NOT_2D));
    b.depth_test = 0; b.depth_write = 1;
    CHECK(!nv2a_up_census_wants(&b, NV2A_UP_NOT_2D));
    /* Nor one of a texture a 2D draw's wouldn't be taken: the world's, or a big one. */
    b.depth_write = 0; b.levels = 7;
    CHECK(!nv2a_up_census_wants(&b, NV2A_UP_NOT_2D));
    b.levels = 1; b.w = 1024;
    CHECK(!nv2a_up_census_wants(&b, NV2A_UP_NOT_2D));
    /* Nor a draw into another surface. */
    b = font_page();
    b.screen = 0; b.into_frame = 0;
    CHECK(!nv2a_up_census_wants(&b, NV2A_UP_NOT_2D));
    b.screen = 1;
    CHECK(!nv2a_up_census_wants(&b, NV2A_UP_NOT_FRAME));
}

static void lines(void)
{
    char buf[192];
    Nv2aUpBind b = font_page();
    int n = nv2a_up_census_line(buf, sizeof buf, 0x0254F000u, 0x0C, &b, 1, NV2A_UP_TAKEN);
    CHECK(n == (int)strlen(buf));
    CHECK(!strcmp(buf, "[UPSCALE] taken 0254F000 256x256 fmt 0C DXT, 1 level "
                       "(draw: screen-space, depth off)\n"));
    b.depth_test = 1; b.depth_write = 1;
    nv2a_up_census_line(buf, sizeof buf, 0x0254F000u, 0x0C, &b, 1, NV2A_UP_TAKEN);
    CHECK(!strcmp(buf, "[UPSCALE] taken 0254F000 256x256 fmt 0C DXT, 1 level "
                       "(draw: screen-space, depth tested and written)\n"));
    b = font_page();
    b.w = 1024; b.h = 1024; b.layout = NV2A_UP_SWIZZLED; b.depth_write = 1;
    nv2a_up_census_line(buf, sizeof buf, 0x03A00000u, 0x06, &b, 1, NV2A_UP_TOO_BIG);
    CHECK(!strcmp(buf, "[UPSCALE] left 03A00000 1024x1024 fmt 06 swizzled, 1 level "
                       "(draw: screen-space, depth written): it is larger than 512\n"));
    b = font_page();
    b.levels = 7; b.layout = NV2A_UP_LINEAR; b.depth_test = 1;
    nv2a_up_census_line(buf, sizeof buf, 0x00012340u, 0x12, &b, 1, NV2A_UP_CHAIN);
    CHECK(!strcmp(buf, "[UPSCALE] left 00012340 256x256 fmt 12 linear, 7 levels "
                       "(draw: screen-space, depth tested): it has a chain of its own\n"));
    /* Another draw, neither screen-space nor over the picture: through a
     * vertex program or the fixed function. */
    b = font_page();
    b.screen = 0;
    nv2a_up_census_line(buf, sizeof buf, 0x0254F000u, 0x0C, &b, 1, NV2A_UP_NOT_2D);
    CHECK(!strcmp(buf, "[UPSCALE] left 0254F000 256x256 fmt 0C DXT, 1 level "
                       "(draw: vp, depth off): the draw is neither screen-space nor over the picture\n"));
    nv2a_up_census_line(buf, sizeof buf, 0x0254F000u, 0x0C, &b, 0, NV2A_UP_NOT_2D);
    CHECK(!strcmp(buf, "[UPSCALE] left 0254F000 256x256 fmt 0C DXT, 1 level "
                       "(draw: ff, depth off): the draw is neither screen-space nor over the picture\n"));
    /* Over the picture. */
    b.over = 1; b.depth_test = 1; b.depth_write = 1;
    nv2a_up_census_line(buf, sizeof buf, 0x006D6000u, 0x0E, &b, 1, NV2A_UP_TAKEN);
    CHECK(!strcmp(buf, "[UPSCALE] taken 006D6000 256x256 fmt 0E DXT, 1 level "
                       "(draw: vp over the picture, depth tested and written)\n"));
    b.levels = 8;
    nv2a_up_census_line(buf, sizeof buf, 0x006D6000u, 0x0E, &b, 0, NV2A_UP_CHAIN);
    CHECK(!strcmp(buf, "[UPSCALE] left 006D6000 256x256 fmt 0E DXT, 8 levels "
                       "(draw: ff over the picture, depth tested and written): it has a chain of its own\n"));
}

static void setting(void)
{
    CHECK(nv2a_up_factor(0) == 0);
    CHECK(nv2a_up_factor(1) == 0);
    CHECK(nv2a_up_factor(-4) == 0);
    CHECK(nv2a_up_factor(2) == 2);
    CHECK(nv2a_up_factor(3) == 2);
    CHECK(nv2a_up_factor(4) == 4);
    CHECK(nv2a_up_factor(7) == 4);
    CHECK(nv2a_up_factor(8) == 8);
    CHECK(nv2a_up_factor(16) == 8);
}

static void choices(void)
{
    CHECK(NV2A_UP_DEFAULT == 4 && nv2a_up_choice(NV2A_UP_DEFAULT) == 2);
    CHECK(nv2a_up_choice(0) == 0 && nv2a_up_choice(2) == 1);
    CHECK(nv2a_up_choice(4) == 2 && nv2a_up_choice(8) == 3);
    CHECK(nv2a_up_choice(3) == 1 && nv2a_up_choice(16) == 3 && nv2a_up_choice(-1) == 0);
    CHECK(nv2a_up_choice_factor(0) == 0 && nv2a_up_choice_factor(1) == 2);
    CHECK(nv2a_up_choice_factor(2) == 4 && nv2a_up_choice_factor(3) == 8);
    CHECK(nv2a_up_choice_factor(NV2A_UP_CHOICES - 1) == 8);
}

/* Upscale sharpness: the row's three choices, a setting out of range
 * taken as Normal, and each choice's filter weight. */
static void sharpness(void)
{
    CHECK(NV2A_UP_SHARP_CHOICES == 3 && NV2A_UP_SHARP_DEFAULT == 1);
    CHECK(!strcmp(nv2a_up_sharp_names[0], "Soft") && !strcmp(nv2a_up_sharp_names[1], "Normal"));
    CHECK(!strcmp(nv2a_up_sharp_names[2], "Sharp"));
    CHECK(nv2a_up_sharp(0) == 0 && nv2a_up_sharp(1) == 1 && nv2a_up_sharp(2) == 2);
    CHECK(nv2a_up_sharp(-1) == 1 && nv2a_up_sharp(3) == 1 && nv2a_up_sharp(150) == 1);
    CHECK(nv2a_up_sharp_weight(0) == 0.5f && nv2a_up_sharp_weight(1) == 1.0f);
    CHECK(nv2a_up_sharp_weight(2) == 1.5f);
    CHECK(nv2a_up_sharp_weight(-1) == 1.0f && nv2a_up_sharp_weight(9) == 1.0f);
}

/* Edge strength: the row's three choices, a setting out of range taken as
 * Normal, and each choice's strength, lower to follow fainter edges. */
static void edge_strength(void)
{
    CHECK(NV2A_UP_EDGE_CHOICES == 3 && NV2A_UP_EDGE_DEFAULT == 1);
    CHECK(!strcmp(nv2a_up_edge_names[0], "Soft") && !strcmp(nv2a_up_edge_names[1], "Normal"));
    CHECK(!strcmp(nv2a_up_edge_names[2], "Strong"));
    CHECK(nv2a_up_edge(0) == 0 && nv2a_up_edge(1) == 1 && nv2a_up_edge(2) == 2);
    CHECK(nv2a_up_edge(-1) == 1 && nv2a_up_edge(3) == 1 && nv2a_up_edge(150) == 1);
    CHECK(nv2a_up_edge_strength(0) == 1.8f && nv2a_up_edge_strength(1) == 0.6f);
    CHECK(nv2a_up_edge_strength(2) == 0.2f);
    CHECK(nv2a_up_edge_strength(-1) == 0.6f && nv2a_up_edge_strength(9) == 0.6f);
}

/* A frame's draws to one surface: d for one with the depth test, n for one
 * without; C a clear of colour and depth, Z of depth alone. Each draw's
 * answer, 1 over and 0 not, in order. */
static const char *frame(const char *ops)
{
    static char out[64];
    Nv2aUpOver o;
    int k = 0;
    memset(&o, 0, sizeof o);
    for (; *ops && k < (int)sizeof out - 1; ops++) {
        if (*ops == 'C') nv2a_up_over_clear(&o, 1, 1);
        else if (*ops == 'Z') nv2a_up_over_clear(&o, 0, 1);
        else if (*ops == 'd') out[k++] = (char)('0' + nv2a_up_over_draw(&o, 1));
        else if (*ops == 'n') out[k++] = (char)('0' + nv2a_up_over_draw(&o, 0));
    }
    out[k] = 0;
    return out;
}

static void over(void)
{
    /* The title: the world, then its overlay after a clear of depth. */
    CHECK(!strcmp(frame("CddddZddd"), "0000111"));
    /* The load screen and its Yes / No prompt: everything after a clear of
     * depth straight after the frame's clear. */
    CHECK(!strcmp(frame("CZdddd"), "1111"));
    /* The world, with no clear of depth after it: never over. */
    CHECK(!strcmp(frame("Cdddnd"), "00000"));
    /* A frame that starts with a clear of depth alone isn't over, before or
     * after drawing, until it draws with the depth test. */
    CHECK(!strcmp(frame("ZddZd"), "001"));
    CHECK(!strcmp(frame("ZnZd"), "00"));
    /* A draw without the depth test isn't a picture: a fade alone, then a
     * clear of depth, is still not over. */
    CHECK(!strcmp(frame("nZd"), "00"));
    /* A clear of colour starts a new picture, which isn't over till its
     * own clear of depth. */
    CHECK(!strcmp(frame("CZddCddZd"), "11001"));
    /* A clear of colour alone counts as the frame's clear: the load
     * screen's clear could be colour and depth or colour alone. */
    {
        Nv2aUpOver o;
        memset(&o, 0, sizeof o);
        nv2a_up_over_clear(&o, 1, 0);
        CHECK(nv2a_up_over_draw(&o, 1) == 0);
        memset(&o, 0, sizeof o);
        nv2a_up_over_clear(&o, 1, 0);
        nv2a_up_over_clear(&o, 0, 1);
        CHECK(nv2a_up_over_draw(&o, 1) == 1);
        /* Neither colour nor depth: nothing changes. */
        memset(&o, 0, sizeof o);
        nv2a_up_over_clear(&o, 0, 0);
        nv2a_up_over_draw(&o, 1);
        nv2a_up_over_clear(&o, 0, 0);
        CHECK(nv2a_up_over_draw(&o, 1) == 0);
    }
}

static void budget(void)
{
    CHECK(nv2a_up_texels(512, 512, 4) == NV2A_UP_TEXELS_A_FLIP);
    CHECK(nv2a_up_texels(256, 256, 2) == 262144u);
    CHECK(nv2a_up_texels(8, 32, 4) == 4096u);
    /* 8x8 at 2x: 16x16 and 8x8. With mipmaps, 4x4, 2x2 and 1x1 too. */
    CHECK(nv2a_up_bytes(8, 8, 2, 0, 1) == (16 * 16 + 8 * 8) * 4u);
    CHECK(nv2a_up_bytes(8, 8, 2, 1, 1) == (16 * 16 + 8 * 8 + 4 * 4 + 2 * 2 + 1) * 4u);
    /* 8x2 at 4x: 32x8, 16x4, 8x2; then 4x1, 2x1, 1x1. */
    CHECK(nv2a_up_bytes(8, 2, 4, 0, 1) == (32 * 8 + 16 * 4 + 8 * 2) * 4u);
    CHECK(nv2a_up_bytes(8, 2, 4, 1, 1) == (32 * 8 + 16 * 4 + 8 * 2 + 4 + 2 + 1) * 4u);
    /* The largest: 512x512 at 4x, a little over 21 MB. */
    CHECK(nv2a_up_bytes(512, 512, 4, 0, 1) == (2048ull * 2048 + 1024 * 1024 + 512 * 512) * 4);
    /* 8x: 512x512 is a whole flip's budget four times over, and 85 MB. */
    CHECK(nv2a_up_texels(512, 512, 8) == 4u * NV2A_UP_TEXELS_A_FLIP);
    CHECK(nv2a_up_bytes(8, 2, 8, 0, 1) == (64 * 16 + 32 * 8 + 16 * 4 + 8 * 2) * 4u);
    CHECK(nv2a_up_bytes(512, 512, 8, 0, 1)
          == (4096ull * 4096 + 2048 * 2048 + 1024 * 1024 + 512 * 512) * 4);
    CHECK(nv2a_up_bytes(0, 8, 2, 0, 1) == 0);
}

/* ---- 3D upscaling ---------------------------------------------------------- */

/* A 3D draw binding a 256x256 DXT wall with its chain to a PROJECT2D stage. */
static Nv2aUp3Bind wall(void)
{
    Nv2aUp3Bind b;
    memset(&b, 0, sizeof b);
    b.depth_test = 1;
    b.depth_write = 1;
    b.mode = NV2A_UP3_MODE_PROJECT2D;
    b.w = 256;
    b.h = 256;
    b.levels = 9;
    b.layout = NV2A_UP_DXT;
    return b;
}

static void taken_3d(void)
{
    Nv2aUp3Bind b = wall();
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_TAKEN);
    b.mode = NV2A_UP3_MODE_PROJECT3D;            /* projected */
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_TAKEN);
    b = wall();
    b.levels = 1; b.layout = NV2A_UP_SWIZZLED;   /* a sign with one level */
    b.w = 128; b.h = 64;
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_TAKEN);
    b = wall();
    b.depth_write = 0;                           /* a fence, drawn after the world */
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_TAKEN);
    b = wall();
    b.w = 16; b.h = 16;                          /* the smallest */
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_TAKEN);
    b.w = 512; b.h = 512;                        /* the largest */
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_TAKEN);
    b.w = 512; b.h = 16;
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_TAKEN);
}

static void draws_3d(void)
{
    Nv2aUp3Bind b = wall();
    b.screen = 1;                                /* the HUD */
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_2D_DRAW);
    b.screen = 0; b.over = 1;                    /* the title screen's text */
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_2D_DRAW);
    b = wall();
    b.depth_test = 0;                            /* the sky, or an effect */
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_NO_DEPTH);
    b.depth_write = 0;
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_NO_DEPTH);
    CHECK(!nv2a_up3_of_texture(NV2A_UP3_2D_DRAW));
    CHECK(!nv2a_up3_of_texture(NV2A_UP3_NO_DEPTH));
}

static void textures_3d(void)
{
    static const int modes[] = { 0, 3, 4, 5, 6, 7, 8, 9, 15, 17 };
    unsigned i;
    Nv2aUp3Bind b = wall();
    for (i = 0; i < sizeof modes / sizeof modes[0]; i++) {
        b.mode = modes[i];                       /* cube, bump target, dot products, ... */
        CHECK(nv2a_up3_rule(&b) == NV2A_UP3_MODE);
    }
    b = wall();
    b.taken_2d = 1;
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_TAKEN_2D);
    b = wall();
    b.layout = NV2A_UP_LINEAR;
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_LINEAR_TEX);
    b = wall();
    b.w = 256; b.h = 1; b.levels = 1;            /* a tone ramp */
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_TOO_SMALL);
    b.w = 8; b.h = 8;                            /* a dot */
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_TOO_SMALL);
    b.w = 15; b.h = 512;
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_TOO_SMALL);
    b = wall();
    b.w = 1024; b.h = 1024;
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_TOO_BIG);
    b.w = 513; b.h = 64;
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_TOO_BIG);
    b = wall();
    b.bump = 1;                                  /* water's ripples */
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_BUMP);
    b = wall();
    b.alpha_only = 1;                            /* a shadow's mask */
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_ALPHA_ONLY);
    /* The first reason that applies is the one given: the draw's first,
     * then the 2D rule's, then how the stage samples it, then the texture. */
    b = wall();
    b.taken_2d = 1; b.mode = 3; b.layout = NV2A_UP_LINEAR; b.w = 4; b.bump = 1; b.alpha_only = 1;
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_TAKEN_2D);
    b.taken_2d = 0;
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_MODE);
    b.mode = NV2A_UP3_MODE_PROJECT2D;
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_LINEAR_TEX);
    b.layout = NV2A_UP_DXT;
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_TOO_SMALL);
    b.w = 600;
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_TOO_BIG);
    b.w = 64;
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_BUMP);
    b.bump = 0;
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_ALPHA_ONLY);
    b.depth_test = 0;
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_NO_DEPTH);
    b.screen = 1;
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_2D_DRAW);
}

static void census_3d(void)
{
    int v;
    Nv2aUp3Bind b = wall();
    for (v = 0; v < NV2A_UP3_VERDICTS; v++)
        CHECK(nv2a_up3_verdict_names[v] && nv2a_up3_verdict_names[v][0]);
    for (v = NV2A_UP3_TAKEN; v < NV2A_UP3_VERDICTS; v++)
        if (v != NV2A_UP3_2D_DRAW && v != NV2A_UP3_NO_DEPTH) {
            CHECK(nv2a_up3_of_texture(v));
            CHECK(nv2a_up3_census_wants(&b, v));
        }
    /* Never a 2D draw's: the 2D census has those. */
    b.screen = 1;
    CHECK(!nv2a_up3_census_wants(&b, NV2A_UP3_2D_DRAW));
    /* A draw with depth off, of a texture the rule takes with it on. */
    b = wall();
    b.depth_test = 0;
    CHECK(nv2a_up3_census_wants(&b, NV2A_UP3_NO_DEPTH));
    /* Not of one it wouldn't: a ramp, or one the 2D rule took. */
    b.h = 1;
    CHECK(!nv2a_up3_census_wants(&b, NV2A_UP3_NO_DEPTH));
    b.h = 256; b.taken_2d = 1;
    CHECK(!nv2a_up3_census_wants(&b, NV2A_UP3_NO_DEPTH));
}

static void lines_3d(void)
{
    char buf[224];
    Nv2aUp3Bind b = wall();
    int n = nv2a_up3_census_line(buf, sizeof buf, 0x01A2B000u, 0x0C, &b, 1, NV2A_UP3_TAKEN);
    CHECK(n == (int)strlen(buf));
    CHECK(!strcmp(buf, "[UPSCALE] 3D taken 01A2B000 256x256 fmt 0C DXT, 9 levels, stage PROJECT2D "
                       "(draw: vp, depth tested and written)\n"));
    b.levels = 1; b.layout = NV2A_UP_SWIZZLED; b.depth_write = 0; b.mode = NV2A_UP3_MODE_PROJECT3D;
    nv2a_up3_census_line(buf, sizeof buf, 0x01A2B000u, 0x06, &b, 0, NV2A_UP3_TAKEN);
    CHECK(!strcmp(buf, "[UPSCALE] 3D taken 01A2B000 256x256 fmt 06 swizzled, 1 level, stage PROJECT3D "
                       "(draw: ff, depth tested)\n"));
    b = wall();
    b.w = 256; b.h = 1; b.levels = 1;
    nv2a_up3_census_line(buf, sizeof buf, 0x00C40000u, 0x0C, &b, 1, NV2A_UP3_TOO_SMALL);
    CHECK(!strcmp(buf, "[UPSCALE] 3D left 00C40000 256x1 fmt 0C DXT, 1 level, stage PROJECT2D "
                       "(draw: vp, depth tested and written): a side is under 16\n"));
    b = wall();
    b.mode = 6;
    nv2a_up3_census_line(buf, sizeof buf, 0x00C40000u, 0x0C, &b, 1, NV2A_UP3_MODE);
    CHECK(!strcmp(buf, "[UPSCALE] 3D left 00C40000 256x256 fmt 0C DXT, 9 levels, stage BUMPENVMAP "
                       "(draw: vp, depth tested and written): its stage doesn't sample it as a "
                       "plain 2D texture\n"));
    b.mode = 17;
    nv2a_up3_census_line(buf, sizeof buf, 0x00C40000u, 0x0C, &b, 1, NV2A_UP3_MODE);
    CHECK(!strcmp(buf, "[UPSCALE] 3D left 00C40000 256x256 fmt 0C DXT, 9 levels, stage mode 17 "
                       "(draw: vp, depth tested and written): its stage doesn't sample it as a "
                       "plain 2D texture\n"));
    b = wall();
    b.depth_test = 0; b.depth_write = 0;
    nv2a_up3_census_line(buf, sizeof buf, 0x00C40000u, 0x0C, &b, 0, NV2A_UP3_NO_DEPTH);
    CHECK(!strcmp(buf, "[UPSCALE] 3D left 00C40000 256x256 fmt 0C DXT, 9 levels, stage PROJECT2D "
                       "(draw: ff, depth off): the draw has the depth test off\n"));
    b = wall();
    b.taken_2d = 1; b.levels = 1;
    nv2a_up3_census_line(buf, sizeof buf, 0x0254F000u, 0x0C, &b, 1, NV2A_UP3_TAKEN_2D);
    CHECK(!strcmp(buf, "[UPSCALE] 3D left 0254F000 256x256 fmt 0C DXT, 1 level, stage PROJECT2D "
                       "(draw: vp, depth tested and written): the 2D rule took it\n"));
}

#define MB (1024ull * 1024)

static void room(void)
{
    /* Four copies held: A idle 500 flips, B 200, C 1000, D drawn just now. */
    static const Nv2aUp3Held h[4] = {
        { 100 * MB, 500 }, { 300 * MB, 200 }, { 50 * MB, 1000 }, { 400 * MB, 10 } };
    const uint64_t held = 850 * MB, cap = 1000 * MB;
    int drop[4];
    /* It fits as things are: nothing goes. */
    CHECK(nv2a_up3_room(h, 4, held, 150 * MB, cap, drop) == 0);
    CHECK(nv2a_up3_room(h, 4, 0, cap, cap, drop) == 0);
    /* 150 MB more than the cap: C (the longest idle), then A. */
    CHECK(nv2a_up3_room(h, 4, held, 300 * MB, cap, drop) == 2);
    CHECK(drop[0] == 2 && drop[1] == 0);
    /* Only C. */
    CHECK(nv2a_up3_room(h, 4, held, 200 * MB, cap, drop) == 1 && drop[0] == 2);
    /* Every idle one: C, A, B; D, drawn just now, stays. */
    CHECK(nv2a_up3_room(h, 4, held, 600 * MB, cap, drop) == 3);
    CHECK(drop[0] == 2 && drop[1] == 0 && drop[2] == 1);
    /* One byte more than they hold: nothing goes, and it waits. */
    CHECK(nv2a_up3_room(h, 4, held, 600 * MB + 1, cap, drop) == -1);
    /* Larger than the cap itself. */
    CHECK(nv2a_up3_room(h, 4, 0, cap + 1, cap, drop) == -1);
    CHECK(nv2a_up3_room(NULL, 0, 0, cap + 1, cap, drop) == -1);
    /* Idle exactly two seconds may go; a flip less may not. */
    {
        Nv2aUp3Held e[2] = { { 10 * MB, NV2A_UP3_IDLE_FRAMES - 1 }, { 10 * MB, NV2A_UP3_IDLE_FRAMES } };
        CHECK(nv2a_up3_room(e, 2, 20 * MB, 5 * MB, 20 * MB, drop) == 1 && drop[0] == 1);
        CHECK(nv2a_up3_room(e, 2, 20 * MB, 15 * MB, 20 * MB, drop) == -1);
    }
    /* Equals: the first first. */
    {
        Nv2aUp3Held e[3] = { { 10 * MB, 300 }, { 10 * MB, 300 }, { 10 * MB, 300 } };
        CHECK(nv2a_up3_room(e, 3, 30 * MB, 15 * MB, 30 * MB, drop) == 2);
        CHECK(drop[0] == 0 && drop[1] == 1);
    }
}

static void bytes_3d(void)
{
    /* 32x16 with its 6 levels at 2x: 64x32, then the title's 32x16 to 1x1. */
    CHECK(nv2a_up_bytes(32, 16, 2, 1, 6)
          == (64 * 32 + 32 * 16 + 16 * 8 + 8 * 4 + 4 * 2 + 2 * 1 + 1) * 4u);
    /* 256x256 whose chain stops at 32x32 (4 levels) at 4x: 1024 to 256 new,
     * then 128, 64 and 32; not to 1x1. */
    CHECK(nv2a_up_bytes(256, 256, 4, 1, 4)
          == (1024ull * 1024 + 512 * 512 + 256 * 256 + 128 * 128 + 64 * 64 + 32 * 32) * 4);
    /* Without mipmaps, the chain isn't sampled and isn't there. */
    CHECK(nv2a_up_bytes(256, 256, 4, 0, 4) == nv2a_up_bytes(256, 256, 4, 0, 1));
    /* With one level and mipmaps, to 1x1, as for 2D. */
    CHECK(nv2a_up_bytes(8, 8, 2, 1, 1) == nv2a_up_bytes(8, 8, 2, 1, 4));
    /* The largest at 8x with its whole chain, about 85 MB. */
    CHECK(nv2a_up_bytes(512, 512, 8, 1, 10) / MB == 85);
    /* The setting: 4x as a new install starts, the Graphics row's third
     * choice. */
    CHECK(NV2A_UP3_DEFAULT == 4 && nv2a_up_choice(NV2A_UP3_DEFAULT) == 2);
    CHECK(NV2A_UP3_HELD_MAX == 4096 * MB);
}

static void factor_3d(void)
{
    /* Up to 256 a side, the setting as it is. */
    CHECK(nv2a_up3_factor(256, 256, 8) == 8);
    CHECK(nv2a_up3_factor(32, 32, 8) == 8);
    CHECK(nv2a_up3_factor(512, 512, 4) == 4);
    CHECK(nv2a_up3_factor(512, 512, 2) == 2);
    /* 512 at 8x would be 4096: halved to 4x, 2048. */
    CHECK(nv2a_up3_factor(512, 512, 8) == 4);
    CHECK(nv2a_up3_factor(512, 64, 8) == 4 && nv2a_up3_factor(64, 512, 8) == 4);
    /* The longer side decides, at the limit exactly. */
    CHECK(nv2a_up3_factor(256, 16, 8) == 8);
    CHECK(nv2a_up3_factor(NV2A_UP3_OUT_MAX / 8 + 1, 16, 8) == 4);
    /* Off stays off; a texture even 2x would pass isn't copied. */
    CHECK(nv2a_up3_factor(256, 256, 0) == 0);
    CHECK(nv2a_up3_factor(NV2A_UP3_OUT_MAX, 16, 2) == 0);
    /* At 8x the largest copy is about 22 MB, not 85. */
    CHECK(nv2a_up_bytes(512, 512, nv2a_up3_factor(512, 512, 8), 1, 10) / MB == 21);
}

static void second_3d(void)
{
    char buf[224];
    Nv2aUpSecond s;
    int n;
    memset(&s, 0, sizeof s);
    s.factor = 8; s.taken = 60; s.made = 3; s.texels = 50331648; s.failed = 0;
    s.waiting = 41; s.dropped = 2; s.held = 14; s.held_bytes = 1000 * MB;
    n = nv2a_up3_second_line(buf, sizeof buf, &s);
    CHECK(n == (int)strlen(buf));
    CHECK(!strcmp(buf, "[UPSCALE] 3D 8x: 60 taken, 3 made (50331648 texels), 0 failed, 41 waiting, "
                       "2 dropped for the cap; 14 copies held, 1000.0 MB of 4096 MB\n"));
    /* The 2D line leaves out what the cap dropped, as before. */
    nv2a_up_second_line(buf, sizeof buf, &s);
    CHECK(!strstr(buf, "dropped"));
}

/* ---- Graffiti upscaling ----------------------------------------------------- */

/* A 3D draw through a vertex program binding a 1024x1024 DXT sheet of tags,
 * one level, to a PROJECT2D stage, as every census of play has shown them. */
static Nv2aUp3Bind sheet(void)
{
    Nv2aUp3Bind b = wall();
    b.w = 1024;
    b.h = 1024;
    b.levels = 1;
    return b;
}

static void rule_g(void)
{
    Nv2aUp3Bind b = sheet();
    /* The sheet: graffiti's, and left by the 3D rule. */
    CHECK(nv2a_upg_rule(&b) == NV2A_UP3_TAKEN);
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_TOO_BIG);
    /* One side over 512 is enough; 1024 a side is the most. */
    b.w = 1024; b.h = 256;
    CHECK(nv2a_upg_rule(&b) == NV2A_UP3_TAKEN);
    b.w = 256; b.h = 1024;
    CHECK(nv2a_upg_rule(&b) == NV2A_UP3_TAKEN);
    b.w = 513; b.h = 16;
    CHECK(nv2a_upg_rule(&b) == NV2A_UP3_TAKEN);
    b.w = 2048; b.h = 1024;
    CHECK(nv2a_upg_rule(&b) == NV2A_UPG_TOO_BIG);
    b.w = 1024; b.h = 1025;
    CHECK(nv2a_upg_rule(&b) == NV2A_UPG_TOO_BIG);
    /* 512 or less is the 3D rule's to weigh, before anything about the
     * draw: the two rules never take the same texture. */
    b = wall();
    CHECK(nv2a_upg_rule(&b) == NV2A_UPG_NOT_GRAFFITI);
    b.w = b.h = 512;
    CHECK(nv2a_upg_rule(&b) == NV2A_UPG_NOT_GRAFFITI && nv2a_up3_rule(&b) == NV2A_UP3_TAKEN);
    b.screen = 1;
    CHECK(nv2a_upg_rule(&b) == NV2A_UPG_NOT_GRAFFITI);
    CHECK(nv2a_up3_of_texture(NV2A_UPG_NOT_GRAFFITI) && nv2a_up3_of_texture(NV2A_UPG_TOO_BIG));
    /* Then the 3D rule's own reasons. */
    b = sheet(); b.screen = 1;
    CHECK(nv2a_upg_rule(&b) == NV2A_UP3_2D_DRAW);
    b = sheet(); b.over = 1;
    CHECK(nv2a_upg_rule(&b) == NV2A_UP3_2D_DRAW);
    b = sheet(); b.depth_test = 0;
    CHECK(nv2a_upg_rule(&b) == NV2A_UP3_NO_DEPTH);
    b = sheet(); b.mode = 6;
    CHECK(nv2a_upg_rule(&b) == NV2A_UP3_MODE);
    b = sheet(); b.layout = NV2A_UP_LINEAR;
    CHECK(nv2a_upg_rule(&b) == NV2A_UP3_LINEAR_TEX);
    b = sheet(); b.h = 8;
    CHECK(nv2a_upg_rule(&b) == NV2A_UP3_TOO_SMALL);
    b = sheet(); b.bump = 1;
    CHECK(nv2a_upg_rule(&b) == NV2A_UP3_BUMP);
    b = sheet(); b.alpha_only = 1;
    CHECK(nv2a_upg_rule(&b) == NV2A_UP3_ALPHA_ONLY);
    /* Into a picture the title keeps, not the frame, neither the draw nor
     * the layout matters: the garage picker draws its design flat into its
     * preview's surface. The texture's other reasons still do; the 3D rule
     * weighs the draw as before, and a texture of its size is its own. */
    b = sheet(); b.kept = 1; b.screen = 1;
    CHECK(nv2a_upg_rule(&b) == NV2A_UP3_TAKEN);
    b.screen = 0; b.over = 1; b.depth_test = 0; b.depth_write = 0;
    CHECK(nv2a_upg_rule(&b) == NV2A_UP3_TAKEN);
    b.layout = NV2A_UP_LINEAR;
    CHECK(nv2a_upg_rule(&b) == NV2A_UP3_TAKEN);
    b.kept = 0; b.over = 0; b.depth_test = 1;
    CHECK(nv2a_upg_rule(&b) == NV2A_UP3_LINEAR_TEX);
    b = sheet(); b.kept = 1; b.mode = 6;
    CHECK(nv2a_upg_rule(&b) == NV2A_UP3_MODE);
    b = sheet(); b.kept = 1; b.bump = 1;
    CHECK(nv2a_upg_rule(&b) == NV2A_UP3_BUMP);
    b = sheet(); b.kept = 1; b.screen = 1; b.alpha_only = 1;
    CHECK(nv2a_upg_rule(&b) == NV2A_UP3_ALPHA_ONLY);
    b = sheet(); b.kept = 1; b.screen = 1; b.w = 2048;
    CHECK(nv2a_upg_rule(&b) == NV2A_UPG_TOO_BIG);
    b = wall(); b.kept = 1; b.screen = 1;
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_2D_DRAW && nv2a_upg_rule(&b) == NV2A_UPG_NOT_GRAFFITI);
    b = wall(); b.kept = 1; b.depth_test = 0;
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_NO_DEPTH);
    b = wall(); b.kept = 1; b.layout = NV2A_UP_LINEAR;
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_LINEAR_TEX);
    /* Blue-to-alpha is for the census: no verdict changes with it. */
    b = sheet(); b.blue_alpha = 1;
    CHECK(nv2a_upg_rule(&b) == NV2A_UP3_TAKEN);
    b = wall(); b.blue_alpha = 1;
    CHECK(nv2a_up3_rule(&b) == NV2A_UP3_TAKEN && nv2a_upg_rule(&b) == NV2A_UPG_NOT_GRAFFITI);
}

static void census_g(void)
{
    char buf[240];
    Nv2aUp3Bind b = sheet();
    int n;
    CHECK(nv2a_upg_census_wants(&b, NV2A_UP3_TAKEN));
    CHECK(nv2a_upg_census_wants(&b, NV2A_UP3_MODE));
    b.w = 2048;
    CHECK(nv2a_upg_census_wants(&b, NV2A_UPG_TOO_BIG));
    /* Never one of the 3D rule's size, which the 3D census names, nor a 2D
     * draw's, which the 2D census names. */
    b = wall();
    CHECK(!nv2a_upg_census_wants(&b, NV2A_UPG_NOT_GRAFFITI));
    b = sheet(); b.screen = 1;
    CHECK(!nv2a_upg_census_wants(&b, NV2A_UP3_2D_DRAW));
    /* Into a picture the title keeps, a flat draw's verdict is the
     * texture's, and logged. */
    b.kept = 1;
    CHECK(nv2a_upg_census_wants(&b, nv2a_upg_rule(&b)));
    /* A draw with depth off, of a sheet the rule takes with it on; not of
     * one it wouldn't. */
    b = sheet(); b.depth_test = 0;
    CHECK(nv2a_upg_census_wants(&b, NV2A_UP3_NO_DEPTH));
    b.w = 4096;
    CHECK(!nv2a_upg_census_wants(&b, NV2A_UP3_NO_DEPTH));
    /* Its lines, with whether the draw's combiner uses blue-to-alpha. */
    b = sheet(); b.blue_alpha = 1;
    n = nv2a_upg_census_line(buf, sizeof buf, 0x01808000u, 0x0E, &b, 1, NV2A_UP3_TAKEN);
    CHECK(n == (int)strlen(buf));
    CHECK(!strcmp(buf, "[UPSCALE] graffiti taken 01808000 1024x1024 fmt 0E DXT, 1 level, stage "
                       "PROJECT2D (draw: vp, depth tested and written, blue-to-alpha)\n"));
    b.blue_alpha = 0; b.w = 2048; b.depth_write = 0;
    nv2a_upg_census_line(buf, sizeof buf, 0x02000000u, 0x0E, &b, 1, NV2A_UPG_TOO_BIG);
    CHECK(!strcmp(buf, "[UPSCALE] graffiti left 02000000 2048x1024 fmt 0E DXT, 1 level, stage "
                       "PROJECT2D (draw: vp, depth tested, no blue-to-alpha): it is larger than "
                       "1024\n"));
    /* And says a draw into a picture the title keeps is one. */
    b = sheet(); b.kept = 1; b.screen = 1; b.depth_test = 0; b.depth_write = 0;
    nv2a_upg_census_line(buf, sizeof buf, 0x01808000u, 0x0C, &b, 1, nv2a_upg_rule(&b));
    CHECK(!strcmp(buf, "[UPSCALE] graffiti taken 01808000 1024x1024 fmt 0C DXT, 1 level, stage "
                       "PROJECT2D (draw: screen-space, depth off, into a picture of its own, no "
                       "blue-to-alpha)\n"));
    CHECK(!strcmp(nv2a_up3_verdict_names[NV2A_UPG_NOT_GRAFFITI],
                  "it is 512 or less, the 3D rule's"));
    /* The 3D line says blue-to-alpha only where the draw uses it. */
    b = wall(); b.blue_alpha = 1;
    nv2a_up3_census_line(buf, sizeof buf, 0x01A2B000u, 0x0C, &b, 1, NV2A_UP3_TAKEN);
    CHECK(!strcmp(buf, "[UPSCALE] 3D taken 01A2B000 256x256 fmt 0C DXT, 9 levels, stage PROJECT2D "
                       "(draw: vp, depth tested and written, blue-to-alpha)\n"));
    b.blue_alpha = 0;
    nv2a_up3_census_line(buf, sizeof buf, 0x01A2B000u, 0x0C, &b, 1, NV2A_UP3_TAKEN);
    CHECK(!strstr(buf, "blue"));
}

static void factor_g(void)
{
    char buf[224];
    Nv2aUpSecond s;
    /* A sheet at each setting: 2048, 4096 and 8192 a side. */
    CHECK(nv2a_upg_factor(1024, 1024, 2) == 2);
    CHECK(nv2a_upg_factor(1024, 1024, 4) == 4);
    CHECK(nv2a_upg_factor(1024, 1024, 8) == 8);
    CHECK(nv2a_upg_factor(1024, 1024, 0) == 0);
    /* Past NV2A_UPG_OUT_MAX, halved, as for 3D. */
    CHECK(nv2a_upg_factor(NV2A_UPG_OUT_MAX / 8 + 1, 16, 8) == 4);
    /* A sheet's copy with a chain to 1x1: about 21, 85 and 341 MB; three
     * fit under the cap at 8x, with or without one. */
    CHECK(nv2a_up_bytes(1024, 1024, 2, 1, 11) / MB == 21);
    CHECK(nv2a_up_bytes(1024, 1024, 4, 1, 11) / MB == 85);
    CHECK(nv2a_up_bytes(1024, 1024, 8, 1, 11) / MB == 341);
    CHECK(3 * nv2a_up_bytes(1024, 1024, 8, 1, 11) <= NV2A_UPG_HELD_MAX);
    CHECK(3 * nv2a_up_bytes(1024, 1024, 8, 0, 1) <= NV2A_UPG_HELD_MAX);
    /* The setting: 4x as a new install starts, the row's third choice;
     * the cap, 1 GB. */
    CHECK(NV2A_UPG_DEFAULT == 4 && nv2a_up_choice(NV2A_UPG_DEFAULT) == 2);
    CHECK(NV2A_UPG_HELD_MAX == 1024 * MB);
    /* The summary line a second. */
    memset(&s, 0, sizeof s);
    s.factor = 8; s.taken = 2; s.made = 1; s.texels = 67108864; s.waiting = 1;
    s.held = 1; s.held_bytes = 341 * MB;
    CHECK(nv2a_upg_second_line(buf, sizeof buf, &s) == (int)strlen(buf));
    CHECK(!strcmp(buf, "[UPSCALE] graffiti 8x: 2 taken, 1 made (67108864 texels), 0 failed, "
                       "1 waiting, 0 dropped for the cap; 1 copies held, 341.0 MB of 1024 MB\n"));
}

static void second(void)
{
    char buf[192];
    Nv2aUpSecond s;
    int n;
    memset(&s, 0, sizeof s);
    s.factor = 4; s.taken = 12; s.made = 10; s.texels = 3145728; s.failed = 1;
    s.waiting = 2; s.held = 40; s.held_bytes = 44040192;
    n = nv2a_up_second_line(buf, sizeof buf, &s);
    CHECK(n == (int)strlen(buf));
    CHECK(!strcmp(buf, "[UPSCALE] 4x: 12 taken, 10 made (3145728 texels), 1 failed, 2 waiting; "
                       "40 copies held, 42.0 MB\n"));
}

int main(void)
{
    taken();
    draws();
    textures();
    census();
    lines();
    setting();
    choices();
    sharpness();
    edge_strength();
    over();
    budget();
    second();
    taken_3d();
    draws_3d();
    textures_3d();
    census_3d();
    lines_3d();
    room();
    bytes_3d();
    factor_3d();
    second_3d();
    rule_g();
    census_g();
    factor_g();
    if (s_failures) {
        printf("upscale: %d failed\n", s_failures);
        return 1;
    }
    printf("upscale: all cases pass\n");
    return 0;
}
