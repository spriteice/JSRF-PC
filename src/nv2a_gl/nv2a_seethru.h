/*
 * See-through surfaces stop hiding what is behind them (nv2a_seethru.c):
 * Display > Enhancements > See through fix, seethru= in the settings file,
 * on by default.
 *
 * The title draws a see-through surface (glass, water) in its own order,
 * blended, with depth writes on, so whatever it draws after it and behind
 * it fails the depth test there: the crowd behind a shop's glass, a moving
 * object behind water. The console does the same; this goes beyond it.
 *
 * A draw that writes depth and is see-through is split by its pixels'
 * alpha, with the alpha test the title asked for kept in both halves
 * (nv2a_psh.h): its opaque pixels (alpha at least NV2A_PSH_OPAQUE) are drawn
 * in place, as the title draws them, depth and colour; its see-through
 * pixels are held back with everything needed to draw them again (the
 * program and its uniforms, the textures, the vertices and the render
 * state), and drawn with no depth writes, in the title's order, at the end
 * of the pass: before the surface's next clear, a change of surface, the
 * surface read as a texture, or the flip. What is behind the glass has been
 * drawn by then, so the glass's colour falls on it, as through real glass.
 * Once a pass has something held back, every later draw that writes no
 * depth (smoke, sparks, the HUD) is held back with it, so it stays over the
 * glass, and what writes depth is drawn in place. What writes no depth but
 * uses the stencil test (the shadows) is drawn in place too, in the title's
 * order, so the glass's colour goes over a shadow behind it. A see-through
 * draw that writes depth is split whether it uses the stencil or not (the
 * characters do, and it seems the water); its see-through half keeps its
 * stencil state.
 *
 * A character, or the level's arrow, is drawn in pieces, then its dark
 * outline: a slightly larger copy of them, untextured, whose inside the
 * pieces' depth hides. When it fades, its pieces are split like glass, so it
 * stops hiding what is drawn after it and behind it; and so that its
 * see-through pixels still hide the inside of its outline, they are marked
 * in the stencil with a bit of the fix's own (NV2A_SEE_MARK) just before the
 * outline, the outline is drawn only where the bit is clear, and the bit is
 * cleared again right after it (NV2A_SEE_MASK), before the title reads the
 * stencil again. The title's own stencil write for those pixels is made with
 * the mark, at its moment, not at the pass's end. The outline itself is
 * drawn as the title draws it, depth writes and all, so its edge still hides
 * what is drawn after it and behind it. The pieces are learnt from the
 * outline's vertex count; where the mark can't be made (a piece's or its
 * outline's stencil state it can't work with, an outline without the alpha
 * test, or RECOMP_GL_SEETHRU_HOLD=0), a mesh's pieces are kept whole, and it
 * hides what is behind it, as on the console.
 *
 * Some draws are left as the title draws them, by draw state and order, not
 * by program: draws without the alpha test (the speed bar's cut), which
 * write depth where they are invisible on purpose; draws over the
 * finished picture, after a depth clear of a surface the frame has already
 * drawn to (the title's logo, menus, fades); and skinned meshes no outline
 * follows (the speed trail's copies of the skater, the crowd, the spray
 * paint and its arrows while a wall is painted, the police's mesh
 * barricade, cars' thin edges), which keep their place before the shadows,
 * so the shadow falls on them, and hide what is drawn after them and behind
 * them, as on the console. Over a faded character's see-through pixels,
 * held back, such a mesh's depth would hide them at the pass's end, and the
 * ground would show through it instead: there, marked as for an outline
 * (for the run of such meshes, not one draw), it is drawn with no depth
 * writes, so the character's pixels are drawn over it at the pass's end
 * (NV2A_SEE_TWICE). Elsewhere it is drawn as the title draws it.
 *
 * The held-back list lives in a wrapper round the device (Nv2aGfx): every
 * call goes through, and the wrapper keeps what a held-back draw needs, and
 * puts the device back as the front half left it after drawing them.
 *
 * RECOMP_GL_SEETHRU_HOLD=0 draws the see-through pixels in place instead,
 * with no depth writes (what shows behind glass is then drawn over it,
 * untinted, as test build seethru4 drew it), to compare when something
 * looks wrong. RECOMP_GL_SEETHRU_LOG=1
 * logs one frame every NV2A_SEE_EVERY frames: what was split, kept whole and
 * held back, and why.
 *
 * Plain C, with nothing of GL's or SDL's, so a test links it with a device
 * of its own (tests/seethru).
 */
#ifndef NV2A_SEETHRU_H
#define NV2A_SEETHRU_H

#include <stdint.h>
#include "nv2a_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The census (RECOMP_GL_SEETHRU_LOG): one frame in this many. */
#define NV2A_SEE_EVERY 600u

/* The device, wrapped: what the front half draws through from the start,
 * in place of `dev`. Once only. */
const Nv2aGfx *nv2a_seethru_wrap(const Nv2aGfx *dev);

/* The setting, read at each draw: on (1) or off (0), NV2A_SEE_DEFAULT until
 * set. Off, nothing more is split or held back; what is held back already
 * is drawn at the pass's end as before. */
#define NV2A_SEE_DEFAULT 1
void nv2a_seethru_set(int on);
int  nv2a_seethru_on(void);

/* A draw as the front half sees it, beside its render state. */
typedef struct {
    uint32_t surf;          /* its colour surface (where in memory) */
    uint32_t frame;         /* the frame: flips so far */
    uint32_t draw;          /* its number, for the census */
    uint32_t pos;           /* where its positions are; 0 sent inline */
    uint32_t verts;
    int      slot;          /* its program, for the census */
    uint8_t  textured;      /* its first stage samples a texture */
    uint8_t  skinned;       /* its program blends by a matrix palette (A0) */
    uint8_t  alpha_uniforms;/* its program reads the alpha test's function and reference */
} Nv2aSeeDraw;

/* What to do with a draw (nv2a_seethru_draw). */
enum {
    NV2A_SEE_KEEP,          /* as the title draws it, in place */
    NV2A_SEE_SPLIT,         /* in place with NV2A_PSH_ALPHA_OPAQUE added to its
                             * alpha function; then with NV2A_PSH_ALPHA_SEE added
                             * and no depth writes, between nv2a_seethru_hold_begin
                             * and _end */
    NV2A_SEE_HOLD,          /* all of it between nv2a_seethru_hold_begin and _end */
    NV2A_SEE_MASK,          /* an outline: in place, with nv2a_seethru_mask's stencil
                             * test, then nv2a_seethru_mask_end */
    NV2A_SEE_TWICE          /* a skinned mesh over marked pieces: in place twice, with
                             * nv2a_seethru_twice's state for 0, then for 1 */
};

/* The stencil bit the fix marks a faded mesh's see-through pixels with, for
 * the length of its outline's draw, or of a run of skinned meshes drawn
 * over it. The title keeps its shadow counts in the stencil too: one would
 * reach this bit only where 128 shadow volumes overlap. */
#define NV2A_SEE_MARK 0x80u

/* Just before a draw is made with `rs`: what to do with it. Called for
 * every draw, whatever the setting, as it learns the outlines and the
 * frame's depth clears. */
int nv2a_seethru_draw(const Nv2aRenderState *rs, const Nv2aSeeDraw *d);

/* Around a split draw's colour pass, or a draw held whole, and its
 * widescreen copies: the draw is held back to the pass's end. Between the
 * two, only state(), uniforms and draws. 0: it is drawn in place after all
 * (RECOMP_GL_SEETHRU_HOLD=0, or what it needs to be drawn again isn't at
 * hand); then _end needn't be called, though it may be. */
int  nv2a_seethru_hold_begin(void);
void nv2a_seethru_hold_end(void);

/* An outline's state, for NV2A_SEE_MASK: drawn only where NV2A_SEE_MARK is
 * clear; where it isn't, the outline's depth-fail stencil operation, as
 * where its body hides it on the console; its own stencil test (off, or
 * ALWAYS) and writes otherwise as the title set them, but on the mark's bit. */
void nv2a_seethru_mask(Nv2aRenderState *rs);

/* After an NV2A_SEE_MASK outline's draw and its widescreen copies: the mark
 * cleared, and the device put back as the front half left it. */
void nv2a_seethru_mask_end(void);

/* A skinned mesh's state, for NV2A_SEE_TWICE, from the title's: `marked` 0,
 * drawn only where NV2A_SEE_MARK is clear, as the title draws it; 1, only
 * where it is set, with no depth writes. Its own stencil test (off, or
 * ALWAYS) and writes otherwise as the title set them, but on the mark's
 * bit. The mark is cleared at the next draw of another kind. */
void nv2a_seethru_twice(Nv2aRenderState *rs, int marked);

/* A clear of surface `surf`'s depth in frame `frame`, after the device's. */
void nv2a_seethru_depth_clear(uint32_t surf, uint32_t frame);

#ifdef __cplusplus
}
#endif

#endif /* NV2A_SEETHRU_H */
