/*
 * The button prompts: the game's button icons
 * drawn again, as their textures are made, to show the input that presses
 * each control now, in the style a player chose.
 *
 * The game draws its icons as sprites from its own textures, A's green
 * button among the rest of the HUD's art. Those textures are named here by
 * their content: their size, their format and the CRC-32 of their first
 * level's bytes, as tools/prompt_census.py lists them from the game's
 * files; and with each, its icons' rectangles and the Xbox pad's control
 * each stands for. The table holds numbers about the game's textures, never
 * their pixels. A texture of a size and format in the table has its first
 * level's first bytes hashed, and only one whose hash is there has the
 * whole first level hashed (nv2a_prompts_find); no other texture is
 * touched. One that is in it is decoded rather than handed over as DXT,
 * and its icons are drawn over in the decoded texels (nv2a_prompts_draw);
 * everything else in it stays the game's. Its upscaled copies (nv2a_gl.c,
 * up_take) get the pictures again at their own size, so they stay sharp
 * (nv2a_prompts_icon_at).
 *
 * Each icon shows what the input layer says of its control: in Default,
 * the game's own art, as it is (nothing changes); in every other style, a
 * picture of the art packs' (src/input/prompt_art.h), fitted to the icon's
 * rectangle, with a key or a pad's button the packs have no picture of
 * drawn on the blank key, with its name.
 *
 * No OpenGL in it, and no SDL: for tests/prompts.
 */
#ifndef NV2A_PROMPTS_H
#define NV2A_PROMPTS_H

#include <stddef.h>
#include <stdint.h>
#include "prompt_art.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The NV2A's colour format for DXT3, the format of the game's icon
 * textures (the game's own files name it 6: tools/prompt_census.py). */
#define NV2A_PROMPT_DXT3 0x0Eu
/* The bytes of a first level the quick hash covers (nv2a_prompts_find),
 * the census's `head`. */
#define NV2A_PROMPT_HEAD_BYTES 1024u

/* An icon in one of the game's textures: its rectangle in the first
 * level's texels, the Xbox pad's control it stands for (INPUT_CTL_...),
 * and its set, the texture's icons of one size. */
typedef struct { uint16_t x, y, w, h; uint8_t control, set; } Nv2aPromptIcon;

/* A texture the game's icons are in, by its content. */
typedef struct {
    const char *name;           /* its file and offset, as the census found it, for the log */
    uint32_t format, w, h;      /* as the texture stage gives them */
    uint32_t head, key;         /* the CRC-32 of its first level's first
                                 * NV2A_PROMPT_HEAD_BYTES, and of all of it */
    const Nv2aPromptIcon *icons;
    int nicons;
} Nv2aPromptTexture;

/* The table (nv2a_prompts.c). */
extern const Nv2aPromptTexture nv2a_prompt_textures[];
extern const int nv2a_prompt_ntextures;

/* Which of table's n textures this one is, from its format, size and first
 * level (bytes of it at level0): its index, or -1. Says once a line for each
 * texture of a table's size and format, with its key and whether the table
 * has it, so a log shows whether the game's bytes are its files'. */
int nv2a_prompts_find(const Nv2aPromptTexture *table, int n, uint32_t format, uint32_t w,
                      uint32_t h, const uint8_t *level0, size_t bytes);

/* A key's name drawn into an alpha mask w x h (a row of w bytes each, 0 to
 * 255), as large as fits, centred: 1 if drawn, 0 where it doesn't fit. */
typedef int (*Nv2aPromptText)(const char *text, uint8_t *alpha, int w, int h);

/* The prompts' own: a small font of whole texels, upper case, scaled by a
 * whole number. */
int nv2a_prompts_text(const char *text, uint8_t *alpha, int w, int h);

/* A picture by name (prompt_art.h), as prompt_art_image gives it: 1 with
 * it in *out, 0 where there is none. */
typedef int (*Nv2aPromptPicture)(const char *name, PromptImage *out);

/* What each control's icon shows, as the input layer gives it, and for a
 * key the packs have no picture of, its name (NULL elsewhere), as the window
 * names it (SDL_GetScancodeName), with the text function above; and where
 * the pictures come from: prompt_art_image where `picture` is NULL, else
 * the renderer's, Upscaling's Buttons row's copies (nv2a_gl.c). */
typedef struct {
    PromptIcon icon[INPUT_CTLS];
    const char *name[INPUT_CTLS];
    Nv2aPromptText text;
    Nv2aPromptPicture picture;
} Nv2aPromptShows;

/* The built-in pictures' largest side (src/input/prompts/make.py): the
 * pads' are 128 pixels square, the keys' 100 (Enter 100 x 79). */
#define NV2A_PROMPT_SIDE 128

/* Upscaling's Buttons row (nv2a_gl.c, prompts_copy_make): a whole picture,
 * w x h, copied f times as large (2, 4 or 8) by the passes the other rows
 * use, read back from the device as BGRA8 (bgra, w * f x h * f, rows from
 * the top), into rgba as RGBA8, the same size: the picture then drawn in
 * its place, smoothed, the same shape. 1, or 0 for another factor, or a
 * side under 1 or over NV2A_PROMPT_SIDE. */
int nv2a_prompts_copy(const uint8_t *bgra, int w, int h, int f, uint8_t *rgba);

/* The icons of texture tex drawn over its decoded texels: rgba holds its
 * nlev levels, w x h first, each RGBA8 and half the last, one after another
 * (as nv2a_gl.c decodes them). Each icon as shows says; one with nothing
 * to show (INPUT_NONE) keeps the game's. */
void nv2a_prompts_draw(const Nv2aPromptTexture *tex, uint8_t *rgba, uint32_t w, uint32_t h,
                       uint32_t nlev, const Nv2aPromptShows *shows);

/* Icon i of texture tex at factor times its size, for an upscaled copy:
 * into out, (w * factor) x (h * factor) RGBA8, the packs' picture its
 * control shows. 0 where the copy keeps what it has: the game's own art,
 * which the copy was made from as nv2a_prompts_draw left it, or nothing
 * to show. */
int nv2a_prompts_icon_at(const Nv2aPromptTexture *tex, int i, int factor,
                         const Nv2aPromptShows *shows, uint8_t *out);

/* The largest side the in-game menu's strip asks a picture at
 * (nv2a_gl.c, nv_window_prompt_picture). */
#define NV2A_PROMPT_PICTURE_MAX 512

/* A picture (prompt_art.h) fitted to a w x h rectangle of RGBA8 texels at
 * dst, a row every `stride` bytes: as large as fits, keeping its shape,
 * centred, clear round it, averaged down or filtered up. */
void nv2a_prompts_fit(const uint8_t *src, int sw, int sh, uint8_t *dst, int w, int h,
                      size_t stride);

/* ---- the message boxes' A and B, flat (port/prompt_buttons.h) ------------ */

/* The mark the game side writes into D3D's push buffer where a box's A or
 * B (or Skip cutscenes' A) would have been drawn, or a control's name in
 * the game's text (port/prompt_text.h): one header of
 * NV2A_PROMPT_MARK_WORDS values, on NV2A_PROMPT_MARK_METHOD and the three
 * after it, past the highest method the title's D3D writes (0x1EA4) and
 * below the HUD's mark (nv2a_wide.h, NV2A_WIDE_HUD_METHOD).
 * Word 0 is NV2A_PROMPT_MARK_TAG, with NV2A_PROMPT_MARK_BOX for the message
 * box's, and under it what the picture shows (NV2A_PROMPT_MARK_WHAT: a
 * control, INPUT_CTL_..., drawn as the icons show it, the first input on
 * it now; or NV2A_PROMPT_MARK_GROUP and a pad's whole D-pad or left stick,
 * PROMPT_GROUP_..., prompt_art.h), the art (PROMPT_ART_...) and how far it
 * fades (NV2A_PROMPT_MARK_FADE: 0 not at all, as the box's buttons; a
 * name's fades with its text), so that anything else on the method is told
 * apart and left alone; words 1 to 3 are the picture's middle, x and y, and
 * half its side, in the title's 640 x 480 pixels, as a float's bits. The
 * renderer draws the picture on the fourth. */
#define NV2A_PROMPT_MARK_METHOD 0x1FE0u
#define NV2A_PROMPT_MARK_WORDS  4u
#define NV2A_PROMPT_MARK_TAG    0x50420000u   /* "PB" */
#define NV2A_PROMPT_MARK_BOX    0x10000u      /* the message box's: with the tag */
#define NV2A_PROMPT_MARK_WHAT   0x3Fu         /* what it shows */
#define NV2A_PROMPT_MARK_ART    0x1C0u        /* the art, from bit 6 */
#define NV2A_PROMPT_MARK_FADE   0xFE00u       /* (255 - its alpha) / 2, from bit 9 */
#define NV2A_PROMPT_MARK_LOW    0xFFFFu       /* what goes under the tag */
#define NV2A_PROMPT_MARK_GROUP  32            /* what, from here: PROMPT_GROUP_... */
/* A mark's word has room for every control and art. */
typedef char nv2a_prompt_mark_fits[INPUT_CTLS <= NV2A_PROMPT_MARK_GROUP && PROMPT_ARTS <= 8 ? 1 : -1];

/* Word 0 for what (a control, or NV2A_PROMPT_MARK_GROUP and a group), box
 * and art. */
static inline uint32_t nv2a_prompts_mark(int what, int box, int art)
{
    return NV2A_PROMPT_MARK_TAG | (box ? NV2A_PROMPT_MARK_BOX : 0u)
         | (((uint32_t)art << 6) & NV2A_PROMPT_MARK_ART) | ((uint32_t)what & NV2A_PROMPT_MARK_WHAT);
}

/* Word 0 with the alpha the picture is drawn at, 0 to 255 (its text's). */
static inline uint32_t nv2a_prompts_mark_alpha(uint32_t word, unsigned alpha)
{
    return (word & ~NV2A_PROMPT_MARK_FADE)
         | ((((255u - (alpha > 255u ? 255u : alpha)) >> 1) << 9) & NV2A_PROMPT_MARK_FADE);
}

/* The alpha word 0 says, 0 to 1: 1 for one that doesn't fade. */
static inline float nv2a_prompts_mark_opacity(uint32_t word)
{
    return 1.0f - (float)((word & NV2A_PROMPT_MARK_FADE) >> 9) * (2.0f / 255.0f);
}

/* Word 0 read: 1 with what it shows, its box and art; 0 for a word that
 * isn't one, or one that shows no control or group, or an art there
 * isn't. */
static inline int nv2a_prompts_mark_read(uint32_t word, int *what, int *box, int *art)
{
    const int w = (int)(word & NV2A_PROMPT_MARK_WHAT), a = (int)((word & NV2A_PROMPT_MARK_ART) >> 6);
    if ((word & ~(NV2A_PROMPT_MARK_LOW | NV2A_PROMPT_MARK_BOX)) != NV2A_PROMPT_MARK_TAG) return 0;
    if (a >= PROMPT_ARTS) return 0;
    if (w >= INPUT_CTLS && (w < NV2A_PROMPT_MARK_GROUP || w >= NV2A_PROMPT_MARK_GROUP + PROMPT_GROUPS))
        return 0;
    *what = w;
    *box = (word & NV2A_PROMPT_MARK_BOX) != 0;
    *art = a;
    return 1;
}

/* The menus' upright faces' size (nv2a_prompts_face_place's grow) for the
 * draws after it, a mark on the same method: word 0
 * NV2A_PROMPT_UPRIGHT_TAG, word 1 the size as a float's bits, words 2 and
 * 3 0. The game side writes one before a screen whose buttons are wanted
 * at another size, and one of NV2A_PROMPT_UPRIGHT_GROW after it
 * (port/prompt_buttons.h, prompt_buttons_upright); the renderer goes back
 * to NV2A_PROMPT_UPRIGHT_GROW at each frame's end as well, so one whose
 * end was lost lasts that frame only. */
#define NV2A_PROMPT_UPRIGHT_TAG  0x50550000u  /* "PU" */
#define NV2A_PROMPT_UPRIGHT_MOST 4.0f         /* the largest size read */

/* The size a mark's words say, into *grow: 1 for an upright size's mark
 * with a size above 0 and at most NV2A_PROMPT_UPRIGHT_MOST; 0 for anything
 * else, a flat button's mark among them. */
static inline int nv2a_prompts_upright_read(const uint32_t words[NV2A_PROMPT_MARK_WORDS], float *grow)
{
    union { uint32_t u; float f; } b;
    if (words[0] != NV2A_PROMPT_UPRIGHT_TAG || words[2] || words[3]) return 0;
    b.u = words[1];
    if (!(b.f > 0.0f && b.f <= NV2A_PROMPT_UPRIGHT_MOST)) return 0;
    *grow = b.f;
    return 1;
}

/* The window the pictures after it are cut to across, a mark on the same
 * method: word 0 NV2A_PROMPT_WINDOW_TAG, words 1 and 2 its left and right
 * edges in the title's pixels, as a float's bits, word 3 0. The game's own
 * window (a viewport, as the graffiti editor's ticker has) cuts the text
 * drawn in it, but not a picture, which the renderer draws over the frame;
 * so the game side marks the window before a text that scrolls through one
 * and marks none after it (right not above left). The edges go on the frame
 * as the window does, straight across it, whatever is squeezed in a
 * widened scene. The renderer drops it at each frame's end as well. */
#define NV2A_PROMPT_WINDOW_TAG 0x50570000u   /* "PW" */

/* The window a mark's words say, into *left and *right: 1 for a window's
 * mark, with *right above *left for a window and not for none; 0 for
 * anything else. */
static inline int nv2a_prompts_window_read(const uint32_t words[NV2A_PROMPT_MARK_WORDS], float *left,
                                           float *right)
{
    union { uint32_t u; float f; } l, r;
    if (words[0] != NV2A_PROMPT_WINDOW_TAG || words[3]) return 0;
    l.u = words[1];
    r.u = words[2];
    if (l.f != l.f || r.f != r.f) return 0;
    *left = l.f;
    *right = r.f;
    return 1;
}

/* A picture's rectangle rect (its left, bottom, right and top in the
 * frame's NDC, nv2a_prompts_place) cut to a window from left to right, in
 * the title's pixels across the frame (0 at its left edge, 640 at its
 * right): into cut, the rectangle left, and into uv, the part of the
 * picture that shows in it (its left, top, right and bottom, 0 to 1). 1
 * with them; 0 when none of it is inside, or the window is empty. */
int nv2a_prompts_window_cut(const float rect[4], float left, float right, float cut[4], float uv[4]);

/* The picture of what control c shows (shows: the game's A, INPUT_CTL_A,
 * for the box's A), fitted to a px x px square of RGBA8 at out: the packs'
 * picture of its input, or the blank key with the name. Where c shows
 * nothing, the Xbox set's picture of c itself, as the game's 3D button
 * isn't drawn; none in the game's own art (Default), which draws no mark. The same in the message box as out of it: the pads'
 * pictures are coloured or grey, none white, and the keys dark grey, so
 * each shows on the box's white (src/input/prompts/README.md). 1 when
 * drawn; 0 for a picture that isn't there, or a size outside 1 to
 * NV2A_PROMPT_PICTURE_MAX. */
int nv2a_prompts_control_at(int c, int px, const Nv2aPromptShows *shows, uint8_t *out);

/* The picture of a pad's whole D-pad or left stick, group (PROMPT_GROUP_...)
 * in art, fitted to a px x px square of RGBA8 at out, through shows'
 * pictures. 1 when drawn; 0 for an art with none (the keys', the game's
 * own), or a size outside 1 to NV2A_PROMPT_PICTURE_MAX. */
int nv2a_prompts_group_at(int group, int art, int px, const Nv2aPromptShows *shows, uint8_t *out);

/* Where a mark's picture goes on the frame, as a 3D draw there is placed
 * (nv2a_gl.c, do_draw): its middle (x, y) and half side half, in the
 * title's pixels, squeezed by s across about the screen's centre in a
 * widened scene (1 for none: nv2a_wide_squeeze_of), moved left by shift
 * title pixels (a HUD piece's move, nv2a_wide_hud_shift; 0 for none), and
 * sized k times about (ax, ay), in NDC (HUD scale's size and anchor,
 * nv2a_hud_size and nv2a_hud_anchor; k 1 for none). Into rect: its left,
 * bottom, right and top, in the frame's NDC (-1 to 1, y up). */
void nv2a_prompts_place(float x, float y, float half, float s, float shift, float k, float ax,
                        float ay, float rect[4]);

/* ---- the menus' buttons, upright (nv2a_gl.c, prompts_upright) ------------ */

/* The menus' buttons (the VS menu's, the graffiti menus') are small 3D
 * models with an icon's texels on their faces, turned as the menus move.
 * A face is the triangles on one icon's texels that join up; drawn upright,
 * each vertex lands where its texel puts it on the icon, flat to the
 * screen, about the face's middle. */

/* Every picture of the prompts' drawn this much larger than the size it was
 * first given, about its middle, but the VS menu's (spriteice, 1:34pm on
 * 22 November 2026, UTC+8: "make the new glyphs appear 10-15% larger everywhere,
 * except the VS menu"): the upright faces, the HUD's icons among them
 * (NV2A_PROMPT_UPRIGHT_GROW), and the flat buttons (port/prompt_buttons.c,
 * prompt_buttons_place). The in-game menu's strip has its own, as much
 * (src/menu/menu.cpp, MENU_PICTURE_SIZE). */
#define NV2A_PROMPT_LARGER 1.125f

/* An upright face's size against the face's own on the screen: the same
 * (spriteice, 01:37 UTC on 22 November 2026, of the game's size: "size
 * looks correct but tilt is still applied"), NV2A_PROMPT_LARGER times. So
 * a face upright already, as the HUD's icons are, is drawn at this size
 * too. A screen may want its own, which the game side marks
 * (NV2A_PROMPT_UPRIGHT_TAG, below): the VS menu's is its own
 * (port/prompt_buttons.h, PROMPT_BUTTONS_VS_UPRIGHT). */
#define NV2A_PROMPT_UPRIGHT_GROW NV2A_PROMPT_LARGER
/* How far a face's vertices may be off upright, in pixels, and it upright. */
#define NV2A_PROMPT_UPRIGHT_TOL  0.5f
/* How far a triangle's texels may be outside an icon's rectangle and be on
 * it: this much of the icon's side, and at least a texel. */
#define NV2A_PROMPT_FACE_SLACK   0.15f
/* How near two triangles' maps from texels to the screen are, against the
 * larger, and them of one face. */
#define NV2A_PROMPT_FACE_AGREE   0.35f
/* How many corners at one place each is checked against. */
#define NV2A_PROMPT_FACE_WINDOW  32
/* The least of its icon, across and down, a face shows, or it isn't one. */
#define NV2A_PROMPT_FACE_LEAST   0.5f
/* The most faces in one draw. */
#define NV2A_PROMPT_FACES_MOST   64

/* A draw's triangles, as the indices of its vertices, three a triangle, into
 * tri (max triangles): NV2A_PROMPT_LIST three vertices each, _STRIP each
 * vertex with the two before it, _FAN each pair with the first. How many. */
enum { NV2A_PROMPT_LIST, NV2A_PROMPT_STRIP, NV2A_PROMPT_FAN };
int nv2a_prompts_triangles(int strip, int n, int *tri, int max);

/* Which of tex's icons a triangle's texels (u[3], v[3], in the first
 * level's texels) are on: each within NV2A_PROMPT_FACE_SLACK of the icon's
 * rectangle, and not in a line. Where two icons' rectangles hold it
 * (the small row's share a texel), the one it covers more of. Its index, or
 * -1. */
int nv2a_prompts_tri_icon(const Nv2aPromptTexture *tex, const float u[3], const float v[3]);

/* A face: its icon, its triangles, its texels' extent, and the plane its
 * vertices land on, fitted: x = m[0][0] u + m[0][1] v + o[0], and y with
 * m[1] and o[1], in the surface's pixels with y down. */
typedef struct {
    int icon;
    int ntri;
    float u0, v0, u1, v1;
    float m[2][2], o[2];
} Nv2aPromptFace;

/* The faces among a draw's ntri triangles (tri, as nv2a_prompts_triangles
 * gives them), whose vertices show texels u, v and land at x, y (the
 * surface's pixels; NaN for one not worked out): triangles of one icon join
 * where a corner of each lands at one place (within 1/64 of a pixel) and
 * their maps from texels to the screen agree (NV2A_PROMPT_FACE_AGREE): a
 * button's side, or its back, is apart from its face. Each
 * triangle's face into face_of (-1 for none), and at most max faces (and
 * NV2A_PROMPT_FACES_MOST) into out, in the order their first triangles
 * come. One whose texels are under NV2A_PROMPT_FACE_LEAST of its icon's
 * across or down, or in a line, isn't one. work, 10 * ntri ints, is the
 * caller's to lend. How many. */
int nv2a_prompts_faces(const Nv2aPromptTexture *tex, const float *u, const float *v, const float *x,
                       const float *y, const int *tri, int ntri, int *face_of, int *work,
                       Nv2aPromptFace *out, int max);

/* Where face f goes upright: its middle (its texels' middle, through its
 * plane) at cx, cy, and s pixels a texel: the plane's larger stretch, which
 * a turn in 3D leaves as it was across the turn's axis, times grow
 * (NV2A_PROMPT_UPRIGHT_GROW, or a screen's own). A vertex showing texel
 * (u, v) goes to cx + s (u - mu), cy + s (v - mv), mu and mv the middle of
 * f's texels:
 * upright, the right way round. 1, or 0 for one under a pixel, or over
 * most pixels across or down. */
int nv2a_prompts_face_place(const Nv2aPromptFace *f, float grow, float most, float *cx, float *cy,
                            float *s);

/* A face laid upright in this frame: its sheet, its icon, its middle and its
 * pixels a texel (nv2a_prompts_face_place's). */
typedef struct {
    const void *sheet;
    int icon;
    float cx, cy, s;
} Nv2aPromptLaid;
#define NV2A_PROMPT_LAID_MOST 16     /* the faces a frame keeps */
#define NV2A_PROMPT_TWIN_NEAR 2.0f   /* pixels apart, across and down, that are one place */

/* A face of icon on sheet, to be laid at cx, cy, s pixels a texel, beside
 * the n laid before it in the frame: where one of the same sheet and icon
 * is at the same place (within NV2A_PROMPT_TWIN_NEAR) and larger, the face
 * takes that one's middle and size, the largest's if more than one, so the
 * two come out one picture. The graffiti editor's size picker draws its
 * Back twice a frame, a copy that pulses and then a still one on top at
 * the pulse's smallest (spriteice, 1:31pm and 1:32pm on 22 November 2026,
 * UTC+8; its log at 3:06pm): laid flat, the still one showed inside the
 * other's ring, and now it is drawn as the pulsing one. A face drawn
 * before a larger copy is covered by it. 1 if the face took another's
 * place. */
int nv2a_prompts_twin(const Nv2aPromptLaid *laid, int n, const void *sheet, int icon, float *cx, float *cy,
                      float *s);

/* Where the rest of a button whose face is laid upright may be: the
 * picture's rectangle about its middle (face f's texels at cx, cy, s pixels
 * a texel, nv2a_prompts_face_place's), NV2A_PROMPT_REACH times as large,
 * as half its width and height. */
typedef struct { float cx, cy, hw, hh; } Nv2aPromptReach;
#define NV2A_PROMPT_REACH 1.5f
void nv2a_prompts_reach(const Nv2aPromptFace *f, float cx, float cy, float s, Nv2aPromptReach *r);

/* Whether a triangle on no icon, its corners landing at x[3], y[3] (the
 * surface's pixels), is the rest of a button whose face was laid upright:
 * every corner within one of the n reaches. A face laid upright leaves its
 * button's side and back where the game turned them, and the side nearer
 * the screen then shows in front of the picture: the VS menu's Back, turned
 * about 50 degrees (spriteice, 7:08pm and 7:24pm on 22 November 2026,
 * UTC+8: "the keyboard key is shown through a stencil or window"). The
 * pack's picture is the whole button, so the renderer makes the rest empty
 * (nv2a_gl.c, prompts_upright_draw, in a draw of separate triangles). 1 or
 * 0; 0 with a corner not worked out (NaN). */
int nv2a_prompts_in_reach(const Nv2aPromptReach *r, int n, const float x[3], const float y[3]);

/* A draw's transform for one of its vertices: where the vertex lands with
 * its position's x, y and z at pos, as s[0] and s[1] in the surface's
 * pixels and s[2] its w. 0 where it lands nowhere. */
typedef int (*Nv2aPromptLand)(void *ctx, const float pos[3], float s[3]);

/* The position, from start, that land puts at (x, y), at w: Newton's method
 * on it, with steps of h for its slope, the last within half a pixel. A
 * transform whose w the position doesn't move (a 2D draw, an orthographic
 * one) keeps its w and takes the least move that lands. 1 with it in out,
 * 0 where none was found. */
int nv2a_prompts_solve(Nv2aPromptLand land, void *ctx, const float start[3], float h,
                       float x, float y, float w, float out[3]);

#ifdef __cplusplus
}
#endif

#endif /* NV2A_PROMPTS_H */
