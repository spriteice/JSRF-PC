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
 * depth and doesn't use the stencil (smoke, sparks, the HUD) is held back
 * with it, so it stays over the glass; what writes depth is drawn in place;
 * and what uses the stencil (the shadows) is drawn after the list, so a
 * shadow falls on the glass's colour as well.
 *
 * Some draws are left as the title draws them, by draw state and order, not
 * by program: the pieces an outline follows (characters, the level's
 * arrow), learnt from the outline's vertex count, so faded characters keep
 * hiding what is behind them, as on the console; draws without the alpha
 * test (the speed bar's cut), which write depth where they are invisible on
 * purpose; and draws over the finished picture, after a depth clear of a
 * surface the frame has already drawn to (the title's logo, menus, fades).
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
    uint8_t  skinned;       /* for the census */
    uint8_t  alpha_uniforms;/* its program reads the alpha test's function and reference */
} Nv2aSeeDraw;

/* What to do with a draw (nv2a_seethru_draw). */
enum {
    NV2A_SEE_KEEP,          /* as the title draws it, in place */
    NV2A_SEE_SPLIT,         /* in place with NV2A_PSH_ALPHA_OPAQUE added to its
                             * alpha function; then with NV2A_PSH_ALPHA_SEE added
                             * and no depth writes, between nv2a_seethru_hold_begin
                             * and _end */
    NV2A_SEE_HOLD           /* all of it between nv2a_seethru_hold_begin and _end */
};

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

/* A clear of surface `surf`'s depth in frame `frame`, after the device's. */
void nv2a_seethru_depth_clear(uint32_t surf, uint32_t frame);

#ifdef __cplusplus
}
#endif

#endif /* NV2A_SEETHRU_H */
