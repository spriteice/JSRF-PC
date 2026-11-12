/*
 * 2D upscaling: the game's 2D art -- its menus, fonts, HUD and icons --
 * drawn from a sharper copy of each texture. This unit says which textures
 * are 2D art, from how a draw binds them (the rule), never from a list of
 * them; nv2a_gl.c keeps the texture cache, notes each binding on its entry
 * and asks.
 *
 * The rule takes a texture that a 2D draw binds, whatever the draw does
 * with depth (the HUD's speed bar writes it, and the title's logo tests
 * it), into the Xbox's 640x480 frame (not the boost's smaller targets). A
 * 2D draw is one of two kinds:
 *
 *   - screen-space: through D3D's XYZRHW pass-through program, which is how
 *     the title draws most of its 2D: the HUD, text and menus (nv2a_wide.h);
 *   - over the picture: any draw after a depth clear of a surface the frame
 *     has already drawn to with the depth test, or has already cleared the
 *     colour of. That is how the title draws the rest over its finished
 *     picture: the title screen's "press start" and its menu, the demo's
 *     badge, menus and fades; and the load screen and its Yes / No prompt,
 *     drawn whole after a depth clear that follows the frame's own clear
 *     with nothing between. The world and the characters are drawn before
 *     such a clear, never after (a census of every draw of two logs of
 *     play). A frame that starts with a clear of depth alone isn't over.
 *
 * The texture is taken when it also has one level of the title's own (the
 * world's textures come with their chain), is swizzled or DXT (a linear
 * texture's coordinates are in texels), is at most NV2A_UP_SIDE_MAX a side,
 * and isn't a bump source. A surface the title rendered into never reaches
 * the texture cache (surface_texture_for answers first), so no render target
 * is ever taken: not the title's copies of whole frames either, which come
 * through the screen-space program.
 *
 * RECOMP_GL_UPSCALE_LOG=1 logs, under [UPSCALE], each texture a 2D draw
 * binds, once: taken, or left and why (nv2a_up_census_line), with which
 * kind of 2D draw it was. That is the census of what the rule takes in the
 * real game. It also logs, once, each texture the rule would take that
 * another draw binds into the frame with the depth test and depth writes
 * off: 2D art drawn some other way, which the rule leaves.
 *
 * Plain C, with nothing of GL's or SDL's, so a test links it (tests/upscale).
 */
#ifndef NV2A_UPSCALE_H
#define NV2A_UPSCALE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The largest side of a texture the rule takes. */
#define NV2A_UP_SIDE_MAX 512

/* How a texture's texels are laid out in memory. */
enum { NV2A_UP_SWIZZLED, NV2A_UP_DXT, NV2A_UP_LINEAR };

/* One draw's binding of one texture, as the rule reads it. */
typedef struct {
    int      screen;                    /* the draw is screen-space: the title's 2D */
    int      over;                      /* ... or over the picture: its 2D after a depth clear */
    int      depth_test, depth_write;   /* the draw's */
    int      into_frame;                /* it draws into the Xbox's 640x480 frame */
    uint32_t w, h;                      /* the texture's size */
    uint32_t levels;                    /* of the title's own chain, 1 for none */
    int      layout;                    /* NV2A_UP_SWIZZLED, _DXT or _LINEAR */
    int      bump;                      /* bound as a bump source */
} Nv2aUpBind;

/* Over the picture: one colour surface's state in a frame, which
 * nv2a_gl.c keeps for each surface the frame draws to, new at each frame. */
typedef struct {
    uint8_t  drawn;                     /* drawn to with the depth test */
    uint8_t  cleared;                   /* its colour cleared */
    uint8_t  over;                      /* a clear of its depth alone came after either */
} Nv2aUpOver;

/* A clear of the surface's colour and or depth. A clear of its colour starts
 * a new picture; one of its depth alone, after the frame has drawn to it
 * with the depth test or cleared its colour, starts what is over it. */
void nv2a_up_over_clear(Nv2aUpOver *o, int colour, int depth);

/* A draw to the surface, with the depth test or not: whether it is over the
 * picture. One with the depth test before that is a draw of the picture. */
int nv2a_up_over_draw(Nv2aUpOver *o, int depth_test);

/* The rule's verdict on a binding: taken, or the first thing that leaves
 * it. The first two are about the draw (it isn't a 2D draw into the
 * frame), the rest about the texture. */
enum {
    NV2A_UP_TAKEN,
    NV2A_UP_NOT_2D,         /* the draw is neither kind of 2D: the title's 3D */
    NV2A_UP_NOT_FRAME,      /* the draw is into another surface */
    NV2A_UP_CHAIN,          /* the texture has a chain of its own: the world's */
    NV2A_UP_LINEAR_TEX,     /* addressed in texels */
    NV2A_UP_TOO_BIG,        /* a side over NV2A_UP_SIDE_MAX */
    NV2A_UP_BUMP,           /* a bump source */
    NV2A_UP_VERDICTS
};

/* Each verdict in words, as the census line gives it. */
extern const char *const nv2a_up_verdict_names[NV2A_UP_VERDICTS];

/* The rule: the verdict on binding b. */
int nv2a_up_rule(const Nv2aUpBind *b);

/* Whether a verdict is about the texture, so the draw was a 2D draw into
 * the frame and the texture is taken or left for good. */
int nv2a_up_of_texture(int verdict);

/* Whether the census logs binding b, of the rule's verdict: one with a
 * verdict about the texture, and one into the frame that isn't a 2D draw
 * but has the depth test and depth writes off, of a texture the rule would
 * take from a 2D draw. */
int nv2a_up_census_wants(const Nv2aUpBind *b, int verdict);

/* The census line for a texture at guest address addr, of the title's
 * format fmt, bound as b by a draw through a vertex program (vp) or the
 * fixed function, with the rule's verdict: "[UPSCALE] taken ..." or
 * "[UPSCALE] left ...: why", and a newline. It names the draw as
 * screen-space, vp or ff, says when it is over the picture, and what it
 * does with depth. What snprintf returns. */
int nv2a_up_census_line(char *buf, size_t n, uint32_t addr, uint32_t fmt,
                        const Nv2aUpBind *b, int vp, int verdict);

/* ---- The setting, the budget, the summary ---------------------------------- */

/* The factor a setting asks for, as the Graphics row offers it: 8 from 8
 * up, 4 from 4 to 7, 2 from 2 or 3, and 0 (off) below 2. */
int nv2a_up_factor(int n);

/* The setting a new install starts with: 4x. */
#define NV2A_UP_DEFAULT 4

/* The Graphics row's choices, Off, 2x, 4x and 8x: a factor's (as
 * nv2a_up_factor takes it) and a choice's factor. */
#define NV2A_UP_CHOICES 4
int nv2a_up_choice(int factor);
int nv2a_up_choice_factor(int choice);

/* What a flip may spend on copies, in texels of their level 0: one 512 x
 * 512 texture at 4x. The first copy of a flip is made however large, so at
 * 8x a 512 x 512 texture is one flip's whole copy. */
#define NV2A_UP_TEXELS_A_FLIP 4194304u

/* A copy's texels at level 0, which the budget counts: w x h at factor f. */
uint64_t nv2a_up_texels(uint32_t w, uint32_t h, int factor);

/* The bytes a copy holds on the device, RGBA8: its levels from factor
 * times w x h down to w x h, and with `mip` the chain below that to 1x1. */
uint64_t nv2a_up_bytes(uint32_t w, uint32_t h, int factor, int mip);

/* A second's work, for its summary line. */
typedef struct {
    int      factor;            /* in use, 0 for none */
    uint32_t taken;             /* textures the rule took in it */
    uint32_t made, failed;      /* copies made, and those the device couldn't */
    uint64_t texels;            /* the made copies' texels (nv2a_up_texels) */
    uint32_t waiting;           /* textures taken and still without a copy */
    uint32_t held;              /* copies on the device */
    uint64_t held_bytes;        /* ... and their bytes (nv2a_up_bytes) */
} Nv2aUpSecond;

/* The summary line for a second, "[UPSCALE] 2x: ...", and a newline; what
 * snprintf returns. */
int nv2a_up_second_line(char *buf, size_t n, const Nv2aUpSecond *s);

/* ---- The copy's passes (nv2a_upscale_glsl.c) ------------------------------
 *
 * Super-xBR (Hyllian, 2015), in the three passes of his published shaders,
 * makes an image twice the size of its source; a 4x copy is two of them,
 * the second over the first's image, and an 8x copy three, each over the
 * one before's. Each pass reads its sources by whole texels (texelFetch), at
 * the source's wrap, so nothing depends on filtering:
 *
 *   PASS0  at the source's size: the value at each texel's lower right
 *          corner, between it and its three neighbours there.
 *   PASS1  at twice the size: the source's texels and pass 0's corners laid
 *          in a checkerboard (even x and y the source's, odd both pass 0's),
 *          and the two others between them worked out from those.
 *   PASS2  at twice the size again: every texel worked out once more from
 *          pass 1's, a half texel back, which puts the image where the
 *          source was.
 *   STORE  pass 2's image into a level of the copy: straight alpha again,
 *          with the colour of the game's own texel where the copy is clear;
 *          or, keeping the game's alpha, that alpha as the sampler
 *          magnifies it, under Super-xBR's colour.
 *   COPY   a level of the game's own texels into a level of the copy, as
 *          they are: its level 0 under the new levels, then the rest of the
 *          title's own chain where it has one.
 *
 * The passes work on colour premultiplied by alpha, in half floats, so a
 * clear texel's colour never bleeds into its neighbours, and find edges in
 * brightness and alpha together, so a glyph's outline is one even where its
 * colour is the same as the clear around it.
 *
 * Uniforms: samplers upSrc and upAux (pass 1: pass 0's image; STORE: the
 * game's texture), upWrap (the source's wrap on s and t: 0 clamp to edge,
 * 1 repeat, 2 mirrored repeat), upPremul (1: upSrc is the game's texture,
 * straight alpha, premultiplied as it is read), upShift (STORE: how many
 * halvings from the copy's level to the game's size), upKeep and upMagLin
 * (STORE: 1 to keep the game's alpha, and whether it is magnified linearly)
 * and upLevel (COPY: the game's level to copy). */
enum { NV2A_UP_PASS0, NV2A_UP_PASS1, NV2A_UP_PASS2, NV2A_UP_STORE, NV2A_UP_COPY,
       NV2A_UP_PASSES };

/* Each pass's name, for the backend's messages. */
extern const char *const nv2a_up_pass_names[NV2A_UP_PASSES];

/* GLSL for a pass: its vertex stage (fragment 0), the same for every pass,
 * one triangle over the target from gl_VertexID; or its fragment stage (1).
 * For NV2A_GLSL_VERSION. NULL for a pass there isn't. */
const char *nv2a_up_glsl(int pass, int fragment);

#ifdef __cplusplus
}
#endif

#endif /* NV2A_UPSCALE_H */
