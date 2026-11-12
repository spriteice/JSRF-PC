/*
 * Upscaling: textures drawn from a sharper copy of themselves, made by
 * Super-xBR on the device (the passes below). Three rows ask for it, each
 * with its own rule, which says which textures are its own from how a draw
 * binds them, never from a list of them; nv2a_gl.c keeps the texture cache,
 * notes each binding on its entry and asks each.
 *
 * 2D upscaling: the game's 2D art -- its menus, fonts, HUD and icons.
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
 * 3D upscaling: the textures the 3D scene is drawn with -- walls, the
 * ground, signs, the characters' own.
 *
 * Its rule (nv2a_up3_rule) takes a texture that a draw with the depth test
 * on, of neither kind of 2D, binds to a stage that samples it as a plain 2D
 * texture (PROJECT2D or PROJECT3D: not a cube map, a bump target or a
 * dot-product mode), when the texture is swizzled or DXT, NV2A_UP3_SIDE_MIN
 * to NV2A_UP_SIDE_MAX a side (smaller is a ramp or a dot), not a bump
 * source, not alpha-only (its colour reads as white) and not one the 2D rule
 * took: an entry holds one copy, and the 2D rule wins. Its copy keeps the
 * game's alpha, so cut-out edges stay where they were, and the title's own
 * chain below the new levels, so a surface that isn't magnified samples what
 * it did. It is made at every render scale, as a surface close to the camera
 * magnifies its texture at any, and only for a texture uploaded before the
 * frame last shown, so one the title rewrites every frame is never copied.
 * A copy is at most NV2A_UP3_OUT_MAX a side (nv2a_up3_factor), and the
 * copies held stay under NV2A_UP3_HELD_MAX bytes (nv2a_up3_room).
 *
 * Graffiti upscaling: the graffiti tags, which the title builds into
 * sheets of 1024 x 1024, 16 tags of 256 x 256 each (the graffiti cache's
 * JSRF_TEXS0.JTX and JSRF_TEXS1.JTX, and the garage picker's own image).
 *
 * Its rule (nv2a_upg_rule) is the 3D rule's, but for size: it takes a
 * texture larger than NV2A_UP_SIDE_MAX a side, up to NV2A_UPG_SIDE_MAX,
 * which the 3D rule leaves. Every census of play so far found the graffiti
 * sheets, and nothing else, there. And into a picture the title keeps, a
 * surface other than the frame, it takes a flat draw's too, and a linear
 * texture: the garage's graffiti picker draws the chosen design each frame,
 * flat, from its own 1024 x 1024 image into a 1024 x 1024 surface, and its
 * preview shows that surface. Into the frame, a flat draw is the 2D rule's,
 * as for 3D. A sheet's copy is made the same way, the
 * game's alpha and chain kept, at most NV2A_UPG_OUT_MAX a side, and the
 * copies held stay under their own NV2A_UPG_HELD_MAX. The census also says
 * whether the draw's combiner uses blue-to-alpha, as the spray's reveal
 * does, to learn whether that tells graffiti apart better than its size.
 *
 * RECOMP_GL_UPSCALE_LOG=1 logs, under [UPSCALE], each texture a 2D draw
 * binds, once: taken, or left and why (nv2a_up_census_line), with which
 * kind of 2D draw it was. That is the census of what the rule takes in the
 * real game. It also logs, once, each texture the rule would take that
 * another draw binds into the frame with the depth test and depth writes
 * off: 2D art drawn some other way, which the rule leaves. And, as
 * "[UPSCALE] 3D ...", each texture the 3D rule takes or leaves for what it
 * is, once (nv2a_up3_census_line), and each it would take that a draw with
 * the depth test off binds; and, as "[UPSCALE] graffiti ...", the same for
 * the graffiti rule.
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

/* Sharpness, Graphics > Upscaling > Sharpness, for every row's
 * copies: Soft, Normal and Sharp, Super-xBR's filter weight at 0.5, 1 (its
 * author's) and 1.5 (the top of his range). A higher weight draws an edge
 * crisper, a lower one softer; its anti-ringing keeps each new texel within
 * the four it lies between, so no weight leaves a halo. Kept as
 * upscale_sharpness= (0, 1 or 2), Normal unless set otherwise. */
#define NV2A_UP_SHARP_CHOICES 3
#define NV2A_UP_SHARP_DEFAULT 1
extern const char *const nv2a_up_sharp_names[NV2A_UP_SHARP_CHOICES];

/* A setting's choice: itself from 0 to 2, else Normal. */
int nv2a_up_sharp(int n);

/* A choice's filter weight (nv2a_up_sharp takes it): 0.5, 1 or 1.5. */
float nv2a_up_sharp_weight(int choice);

/* Edge strength, Graphics > Upscaling > Edge strength, for every row's
 * copies: Soft, Normal and Strong, Super-xBR's edge strength at 1.8, 0.6
 * (its author's) and 0.2, from his range of 0 to 5. Where the texels
 * around a new one show an edge along one diagonal more than along the
 * other by at least that much, that diagonal alone draws it; by less, it is
 * blended with the level or upright direction, the more the less it stands
 * out. So a lower strength follows fainter diagonals too (Strong), and a
 * higher one only clear ones, blending the rest (Soft). Kept as
 * upscale_edge_strength= (0, 1 or 2), Normal unless set otherwise. */
#define NV2A_UP_EDGE_CHOICES 3
#define NV2A_UP_EDGE_DEFAULT 1
extern const char *const nv2a_up_edge_names[NV2A_UP_EDGE_CHOICES];

/* A setting's choice: itself from 0 to 2, else Normal. */
int nv2a_up_edge(int n);

/* A choice's edge strength (nv2a_up_edge takes it): 1.8, 0.6 or 0.2. */
float nv2a_up_edge_strength(int choice);

/* What a flip may spend on copies, in texels of their level 0: one 512 x
 * 512 texture at 4x. The first copy of a flip is made however large, so at
 * 8x a 512 x 512 texture is one flip's whole copy. */
#define NV2A_UP_TEXELS_A_FLIP 4194304u

/* A copy's texels at level 0, which the budget counts: w x h at factor f. */
uint64_t nv2a_up_texels(uint32_t w, uint32_t h, int factor);

/* The bytes a copy holds on the device, RGBA8: its levels from factor
 * times w x h down to w x h, and with `mip` the chain below that: the
 * title's own, `levels` of them counting w x h, where it has one (2 or
 * more), else to 1x1. */
uint64_t nv2a_up_bytes(uint32_t w, uint32_t h, int factor, int mip, uint32_t levels);

/* A second's work, for its summary line. */
typedef struct {
    int      factor;            /* in use, 0 for none */
    uint32_t taken;             /* textures the rule took in it */
    uint32_t made, failed;      /* copies made, and those the device couldn't */
    uint64_t texels;            /* the made copies' texels (nv2a_up_texels) */
    uint32_t waiting;           /* textures taken and still without a copy */
    uint32_t held;              /* copies on the device */
    uint64_t held_bytes;        /* ... and their bytes (nv2a_up_bytes) */
    uint32_t dropped;           /* 3D: copies dropped to make room under the cap */
} Nv2aUpSecond;

/* The summary line for a second, "[UPSCALE] 2x: ...", and a newline; what
 * snprintf returns. */
int nv2a_up_second_line(char *buf, size_t n, const Nv2aUpSecond *s);

/* ---- 3D upscaling ---------------------------------------------------------- */

/* The smallest side of a texture the 3D rule takes. */
#define NV2A_UP3_SIDE_MIN 16

/* A stage's texture mode (the texture shader's), as the 3D rule reads it:
 * the two that the combiner samples as a plain 2D texture. */
enum { NV2A_UP3_MODE_PROJECT2D = 1, NV2A_UP3_MODE_PROJECT3D = 2 };

/* One draw's binding of one texture, as the 3D rule reads it. */
typedef struct {
    int      screen, over;              /* the draw is 2D: screen-space, or over the picture */
    int      kept;                      /* into a picture the title keeps, not the frame */
    int      depth_test, depth_write;   /* the draw's */
    int      mode;                      /* the stage's texture mode */
    uint32_t w, h;                      /* the texture's size */
    uint32_t levels;                    /* of the title's own chain, 1 for none */
    int      layout;                    /* NV2A_UP_SWIZZLED, _DXT or _LINEAR */
    int      bump;                      /* bound as a bump source */
    int      alpha_only;                /* A8: its colour reads as white */
    int      taken_2d;                  /* the 2D rule took it */
    int      blue_alpha;                /* the draw's combiner uses blue-to-alpha (the census's) */
} Nv2aUp3Bind;

/* The 3D rule's verdict: taken, or the first thing that leaves it. The
 * first two are about the draw, the rest about the texture or how its
 * stage samples it. */
enum {
    NV2A_UP3_TAKEN,
    NV2A_UP3_2D_DRAW,       /* the draw is 2D: screen-space or over the picture */
    NV2A_UP3_NO_DEPTH,      /* the draw has the depth test off */
    NV2A_UP3_TAKEN_2D,      /* the 2D rule took it */
    NV2A_UP3_MODE,          /* its stage doesn't sample it as a plain 2D texture */
    NV2A_UP3_LINEAR_TEX,    /* addressed in texels */
    NV2A_UP3_TOO_SMALL,     /* a side under NV2A_UP3_SIDE_MIN */
    NV2A_UP3_TOO_BIG,       /* a side over NV2A_UP_SIDE_MAX */
    NV2A_UP3_BUMP,          /* a bump source */
    NV2A_UP3_ALPHA_ONLY,    /* alpha-only */
    NV2A_UPG_NOT_GRAFFITI,  /* graffiti: at most NV2A_UP_SIDE_MAX a side, the 3D rule's */
    NV2A_UPG_TOO_BIG,       /* graffiti: a side over NV2A_UPG_SIDE_MAX */
    NV2A_UP3_VERDICTS
};

/* Each verdict in words, as the census line gives it. */
extern const char *const nv2a_up3_verdict_names[NV2A_UP3_VERDICTS];

/* The 3D rule: the verdict on binding b. */
int nv2a_up3_rule(const Nv2aUp3Bind *b);

/* Whether a verdict is about the texture or how its stage samples it, so
 * the texture is taken or left for good. */
int nv2a_up3_of_texture(int verdict);

/* Whether the census logs binding b, of the 3D rule's verdict: one with a
 * verdict about the texture, and one by a draw with the depth test off, of
 * a texture the rule would take from a draw with it on. Not a 2D draw's. */
int nv2a_up3_census_wants(const Nv2aUp3Bind *b, int verdict);

/* The census line for a texture at guest address addr, of the title's
 * format fmt, bound as b by a draw through a vertex program (vp) or the
 * fixed function, with the 3D rule's verdict: "[UPSCALE] 3D taken ..." or
 * "[UPSCALE] 3D left ...: why", and a newline. It names the stage's mode
 * and what the draw does with depth, and "blue-to-alpha" where the draw's
 * combiner uses it (to learn whether graffiti alone does). What snprintf
 * returns. */
int nv2a_up3_census_line(char *buf, size_t n, uint32_t addr, uint32_t fmt,
                         const Nv2aUp3Bind *b, int vp, int verdict);

/* The setting a new install starts with: 4x, whose copies of a busy scene
 * fit under NV2A_UP3_HELD_MAX whole. */
#define NV2A_UP3_DEFAULT 4

/* The longest side a 3D copy may have, in texels. A texture whose side
 * times the setting would pass it is copied at the largest smaller factor
 * that fits: at 8x, a 512 texture is copied at 4x, so 8x adds texels only
 * to textures up to 256 a side. A 4096 copy would show more only with one
 * texel of the game's covering eight pixels or more, and holds 85 MB. */
#define NV2A_UP3_OUT_MAX 2048

/* The factor a w x h texture's 3D copy is made at with the setting
 * `factor`: the setting, halved until a side times it is at most
 * NV2A_UP3_OUT_MAX; 0 when the setting is off or even 2x would pass it. */
int nv2a_up3_factor(uint32_t w, uint32_t h, int factor);

/* The most bytes the 3D copies may hold on the device, together: 4 GB, the
 * same at every setting. The title screen's demo binds about 150 textures
 * the rule takes, whose copies need about 1.2 GB at 4x and 2.1 GB at 8x
 * (NV2A_UP3_OUT_MAX); a busy scene in play, up to 2.5 GB at 4x and 4.3 GB
 * at 8x. Only what a scene draws is held, so 2x and 4x stay well under it. */
#define NV2A_UP3_HELD_MAX (4096ull * 1024 * 1024)

/* How long a 3D copy goes undrawn before the cap may drop it: 120 frames
 * shown, two seconds at 60 frames a second. Frames, not flips: a flip ends
 * each of the title's render passes, and the garage's graffiti picker ends
 * seven a frame. */
#define NV2A_UP3_IDLE_FRAMES 120u

/* A 3D copy on the device, as the cap weighs it. */
typedef struct {
    uint64_t bytes;             /* nv2a_up_bytes */
    uint32_t idle;              /* frames since it was last drawn */
} Nv2aUp3Held;

/* Room under the cap for a new copy of `need` bytes, with `held` bytes in
 * the n copies h: 0 when it fits as things are; else how many copies to
 * drop first, their indexes in drop[] (room for n): those idle for at
 * least NV2A_UP3_IDLE_FRAMES, the longest idle first, as few as make room.
 * -1 when dropping every idle one would still leave too little, or the
 * copy is larger than the cap: then nothing is dropped and it waits. */
int nv2a_up3_room(const Nv2aUp3Held *h, int n, uint64_t held, uint64_t need, uint64_t cap,
                  int *drop);

/* The summary line for a second of 3D work, "[UPSCALE] 3D 2x: ...", and a
 * newline; what snprintf returns. */
int nv2a_up3_second_line(char *buf, size_t n, const Nv2aUpSecond *s);

/* ---- Graffiti upscaling ----------------------------------------------------- */

/* The largest side of a texture the graffiti rule takes: the sheets'. */
#define NV2A_UPG_SIDE_MAX 1024

/* The graffiti rule: the verdict on binding b. First NV2A_UPG_NOT_GRAFFITI
 * for a texture at most NV2A_UP_SIDE_MAX a side, the 3D rule's to weigh;
 * then the 3D rule's verdicts, with NV2A_UPG_TOO_BIG for a side over
 * NV2A_UPG_SIDE_MAX, but none about the draw or the texture's layout where
 * it is into a picture the title keeps (b->kept): there a flat draw, one
 * with depth off, or a linear texture is taken too. So the two rules never take the same texture, and the
 * 2D rule, which stops at NV2A_UP_SIDE_MAX too, never one of graffiti's. */
int nv2a_upg_rule(const Nv2aUp3Bind *b);

/* Whether the census logs binding b, of the graffiti rule's verdict: as
 * nv2a_up3_census_wants, but never a texture of the 3D rule's size, which
 * the 3D census already names. */
int nv2a_upg_census_wants(const Nv2aUp3Bind *b, int verdict);

/* The census line for the graffiti rule, as nv2a_up3_census_line's, as
 * "[UPSCALE] graffiti taken ..." or "... left ...: why", with whether the
 * draw's combiner uses blue-to-alpha. What snprintf returns. */
int nv2a_upg_census_line(char *buf, size_t n, uint32_t addr, uint32_t fmt,
                         const Nv2aUp3Bind *b, int vp, int verdict);

/* The setting a new install starts with: off, until it has been tested. */
#define NV2A_UPG_DEFAULT 0

/* The longest side a graffiti copy may have: a 1024 sheet at 8x. Every
 * graphics card the port runs on takes 8192 a side or more; one whose limit
 * is lower gets the largest factor that fits (nv2a_gfx_gl.c's tex_upscale
 * refuses the rest). */
#define NV2A_UPG_OUT_MAX 8192

/* The factor a w x h texture's graffiti copy is made at with the setting
 * `factor`, as nv2a_up3_factor's under NV2A_UPG_OUT_MAX. */
int nv2a_upg_factor(uint32_t w, uint32_t h, int factor);

/* The most bytes the graffiti copies may hold on the device, together, apart
 * from the 3D copies: 1 GB. A sheet's copy with its chain is 21 MB at 2x,
 * 85 MB at 4x and 341 MB at 8x, and a scene has shown three sheets at most. */
#define NV2A_UPG_HELD_MAX (1024ull * 1024 * 1024)

/* The summary line for a second of graffiti work, "[UPSCALE] graffiti 2x:
 * ...", as nv2a_up3_second_line's under NV2A_UPG_HELD_MAX. */
int nv2a_upg_second_line(char *buf, size_t n, const Nv2aUpSecond *s);

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
 * (STORE: 1 to keep the game's alpha, and whether it is magnified linearly),
 * upLevel (COPY: the game's level to copy), upWeight (PASS0 to PASS2:
 * Super-xBR's filter weight, 1 his own; nv2a_up_sharp_weight) and upEdge
 * (PASS0 to PASS2: his edge strength, 0.6 his own; nv2a_up_edge_strength). */
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
