/* Run with: python tests/upscale/run.py
 *
 * 2D upscaling's rule and its census, src/nv2a_gl/nv2a_upscale.c, as the
 * engine builds it:
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
    CHECK(nv2a_up_bytes(8, 8, 2, 0) == (16 * 16 + 8 * 8) * 4u);
    CHECK(nv2a_up_bytes(8, 8, 2, 1) == (16 * 16 + 8 * 8 + 4 * 4 + 2 * 2 + 1) * 4u);
    /* 8x2 at 4x: 32x8, 16x4, 8x2; then 4x1, 2x1, 1x1. */
    CHECK(nv2a_up_bytes(8, 2, 4, 0) == (32 * 8 + 16 * 4 + 8 * 2) * 4u);
    CHECK(nv2a_up_bytes(8, 2, 4, 1) == (32 * 8 + 16 * 4 + 8 * 2 + 4 + 2 + 1) * 4u);
    /* The largest: 512x512 at 4x, a little over 21 MB. */
    CHECK(nv2a_up_bytes(512, 512, 4, 0) == (2048ull * 2048 + 1024 * 1024 + 512 * 512) * 4);
    /* 8x: 512x512 is a whole flip's budget four times over, and 85 MB. */
    CHECK(nv2a_up_texels(512, 512, 8) == 4u * NV2A_UP_TEXELS_A_FLIP);
    CHECK(nv2a_up_bytes(8, 2, 8, 0) == (64 * 16 + 32 * 8 + 16 * 4 + 8 * 2) * 4u);
    CHECK(nv2a_up_bytes(512, 512, 8, 0)
          == (4096ull * 4096 + 2048 * 2048 + 1024 * 1024 + 512 * 512) * 4);
    CHECK(nv2a_up_bytes(0, 8, 2, 0) == 0);
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
    over();
    budget();
    second();
    if (s_failures) {
        printf("upscale: %d failed\n", s_failures);
        return 1;
    }
    printf("upscale: all cases pass\n");
    return 0;
}
