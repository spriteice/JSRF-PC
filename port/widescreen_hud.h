/* The HUD at the screen's edges in widescreen, and at Display > HUD
 * scale's size: the game's half.
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
 * piece's draws.
 *
 * The exception is text: the game's font call queues it, and one draw
 * later in the frame holds all the text there is, the menus' with the
 * HUD's. A piece with text (WIDE_HUD_TEXT) has the font call's override
 * draw it at once instead, through the font's own call that does
 * (widescreen_hud_text_now), so it is drawn between the piece's marks and
 * moves with it.
 *
 * Display > HUD draws the HUD's pieces at another size (HUD scale), or
 * none of them (Disable HUD), through the same marks: each piece's carry
 * NV2A_WIDE_HUD_SIZED, and the renderer sizes what is drawn between them
 * about the inner corner of the HUD's margin on the piece's side, at the
 * top or, for a piece at the bottom (NV2A_WIDE_HUD_LOW), at the bottom
 * (nv2a_hud_scale.h). So the pieces are marked whenever the scene is
 * widened or the HUD isn't at the game's own size, at 4:3 too.
 *
 * The markers the HUD draws at points in the world (sub_00100D80,
 * sub_00105070, sub_001068E0) aren't marked: they are 3D models under the
 * HUD's camera, placed from the 4:3 projection of the point, and the
 * widened camera brings them onto the widened scene's point by itself; they
 * show where things are in the world, so HUD scale leaves them too. The
 * HUD's own signs (sub_000FC370, WIDE_HUD_SIGN), whose models put them in
 * the middle of the screen, stay there in widescreen, and so does the
 * counter in the middle (sub_00104AB0, WIDE_HUD_COUNTER): their marks have
 * the side NV2A_WIDE_HUD_MIDDLE, which only HUD scale reads, sizing them
 * about the screen's centre (spriteice, 12 November 2026, 06:19 UTC, on the
 * card: "Edges, plus middle counter and signs"). The one sign that moves is
 * an area's name, which the map makes, for the street pause menu and for
 * the banner as the player nears another area: it goes to the left edge
 * (widescreen_menus.h). */
#ifndef JSRF_WIDESCREEN_HUD_H
#define JSRF_WIDESCREEN_HUD_H

#include <stdint.h>

/* The HUD pieces that move, each by the game function that draws it. The
 * order is the marks' piece numbers, which the log shows: new pieces go on
 * the end. */
enum {
    WIDE_HUD_HEALTH,    /* the health bar and its border, at the top right */
    WIDE_HUD_GAUGE,     /* the gauge and its numbers, at the bottom right */
    WIDE_HUD_CAN,       /* the spray can (3D) and its count, on the right */
    WIDE_HUD_BOOST,     /* the boost icon (3D), on the right */
    WIDE_HUD_DIGIT,     /* a digit and the icon it picks, on the right */
    WIDE_HUD_DIGITS,    /* two digits and an icon, on the right */
    WIDE_HUD_NUMBERS,   /* two two-digit numbers, on the right, for two seconds */
    WIDE_HUD_SCORE,     /* the score and trick count, and its label, on the left */
    WIDE_HUD_TRICK,     /* the trick's name, on the left */
    WIDE_HUD_ROLLING,   /* a seven-digit number that rolls, at the top left */
    WIDE_HUD_MESSAGE,   /* a message that slides in from the left */
    WIDE_HUD_SIGN,      /* one of the HUD's own signs (3D), in the middle */
    WIDE_HUD_COUNTER,   /* a label and a three-digit number, in the middle */
    WIDE_HUD_PIECES
};

/* A piece's flags: NV2A_WIDE_HUD_OWN_VIEW and NV2A_WIDE_HUD_LOW
 * (nv2a_wide.h), which its marks carry to the renderer, with
 * NV2A_WIDE_HUD_SIZED, which every piece's marks carry; and WIDE_HUD_TEXT,
 * for one that writes text through the font call (see above). */
#define WIDE_HUD_TEXT 0x100u

typedef struct {
    uint32_t va;        /* the function that draws it */
    const char *name;   /* for the log */
    int side;           /* NV2A_WIDE_HUD_LEFT, _RIGHT or _MIDDLE */
    unsigned flags;     /* NV2A_WIDE_HUD_OWN_VIEW, NV2A_WIDE_HUD_LOW, WIDE_HUD_TEXT */
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
 * widened, or the HUD isn't at its own size) and D3D has a device; the first
 * time, a line in the log names the piece. Returns whether it marked, for
 * widescreen_hud_end. */
int widescreen_hud_begin(uint8_t *mem, uint32_t device, int piece, int on,
                         widescreen_hud_space_fn make_space);

/* After it: its end mark, when widescreen_hud_begin marked its start. */
void widescreen_hud_end(uint8_t *mem, uint32_t device, int piece, int marked,
                        widescreen_hud_space_fn make_space);

/* For the font call's override: whether the text it is given now should be
 * drawn at once, between the marks of the piece that is running, rather than
 * queued with the rest: while a piece with WIDE_HUD_TEXT runs, marked. The
 * first time for each piece, a line in the log says so. */
int widescreen_hud_text_now(void);

/* For the signs' draw (sub_000FC370), whose class the menus' signs share:
 * whether the sign at sign, in guest memory mem, is one of the HUD's own
 * (WIDE_HUD_SIGN): its flags (+WIDE_MENU_SIGN_FLAGS) have
 * WIDE_HUD_SIGN_FLAG, which only the HUD manager gives its signs (types 8
 * and 9, sub_000FBFD0 from sub_00108870), never the map's area names, a
 * result screen's banners or a garage screen's signs
 * (widescreen-3b/HUD-DIAGNOSIS-2.md, 3). 0 for no sign. */
#define WIDE_HUD_SIGN_FLAG 0x40000000u
int widescreen_hud_sign(uint8_t *mem, uint32_t sign);

#endif
