/* The HUD at the screen's edges in widescreen: the game's half.
 *
 * The game places its HUD for a 4:3 screen, so in widescreen the renderer's
 * squeeze leaves it in the middle 4:3 with the rest of the 2D. Each HUD
 * piece is drawn by one game function, which draws all of it while it runs:
 * its 2D through a quad batch of its own, filled and drawn there, and its 3D
 * model, if it has one, the same way. So the override of that function
 * (recomp_manual.c) marks the start and the end of its draws in the command
 * stream, with the piece's side of the screen, and the renderer moves what
 * is drawn between the marks to that side's edge (nv2a_wide.h,
 * NV2A_WIDE_HUD_METHOD). The marks go in as D3D's own methods do, at its
 * push buffer's put pointer, so they reach the renderer in order with the
 * piece's draws. */
#ifndef JSRF_WIDESCREEN_HUD_H
#define JSRF_WIDESCREEN_HUD_H

#include <stdint.h>

/* The HUD pieces that move, each by the game function that draws it. */
enum {
    WIDE_HUD_HEALTH,    /* the health bar and its border, at the top right */
    WIDE_HUD_PIECES
};

typedef struct {
    uint32_t va;        /* the function that draws it */
    const char *name;   /* for the log */
    int side;           /* NV2A_WIDE_HUD_LEFT or _RIGHT */
} WideHudPiece;

extern const WideHudPiece widescreen_hud_pieces[WIDE_HUD_PIECES];

/* Where D3D's device keeps its push buffer's put pointer, and the limit
 * past which a method needs D3D's make-space call first: it is set short of
 * the buffer's real end, so a few words always fit below it. */
#define WIDE_HUD_D3D_PUT   0x00u
#define WIDE_HUD_D3D_LIMIT 0x04u

/* D3D's make-space call: makes room and returns the new put pointer. */
typedef uint32_t (*widescreen_hud_space_fn)(void);

/* One NV2A_WIDE_HUD_METHOD with value written into D3D's push buffer, as
 * D3D writes a method: at the put pointer of the device at device, after
 * make_space when the put pointer is at its limit or past it; then the put
 * pointer moved past it. mem is guest memory. Returns where it went, or 0
 * when make_space left no room below the limit, which is written nowhere
 * and said once in the log (RECOMP_CHECK). */
uint32_t widescreen_hud_push(uint8_t *mem, uint32_t device, uint32_t value,
                             widescreen_hud_space_fn make_space);

/* Before piece's function runs: its start mark, when on (the scene is
 * widened) and D3D has a device; the first time, a line in the log names the
 * piece. Returns whether it marked, for widescreen_hud_end. */
int widescreen_hud_begin(uint8_t *mem, uint32_t device, int piece, int on,
                         widescreen_hud_space_fn make_space);

/* After it: its end mark, when widescreen_hud_begin marked its start. */
void widescreen_hud_end(uint8_t *mem, uint32_t device, int piece, int marked,
                        widescreen_hud_space_fn make_space);

#endif
