/* The HUD at the screen's edges in widescreen: the game's half
 * (widescreen_hud.h). Plain C on guest memory, so a test links it
 * (tests/widescreen_hud). */
#include "widescreen_hud.h"
#include "nv2a_wide.h"
#include "platform/recomp_check.h"

#include <stdio.h>

const WideHudPiece widescreen_hud_pieces[WIDE_HUD_PIECES] = {
    [WIDE_HUD_HEALTH] = { 0x001060B0u, "the health bar", NV2A_WIDE_HUD_RIGHT },
};

/* A method header as D3D writes one: one value, subchannel 0. */
#define WIDE_HUD_HEADER ((1u << 18) | NV2A_WIDE_HUD_METHOD)

static volatile uint32_t *guest32(uint8_t *mem, uint32_t va)
{
    return (volatile uint32_t *)(mem + va);
}

uint32_t widescreen_hud_push(uint8_t *mem, uint32_t device, uint32_t value,
                             widescreen_hud_space_fn make_space)
{
    uint32_t put = *guest32(mem, device + WIDE_HUD_D3D_PUT);
    if (put >= *guest32(mem, device + WIDE_HUD_D3D_LIMIT)) {
        uint32_t limit;
        put = make_space();
        limit = *guest32(mem, device + WIDE_HUD_D3D_LIMIT);
        if (!RECOMP_CHECK(put && put < limit && !(put & 3u),
                          "D3D's make-space gave %08X with the limit at %08X: no mark", put, limit))
            return 0;
    }
    *guest32(mem, put) = WIDE_HUD_HEADER;
    *guest32(mem, put + 4) = value;
    *guest32(mem, device + WIDE_HUD_D3D_PUT) = put + 8;
    return put;
}

int widescreen_hud_begin(uint8_t *mem, uint32_t device, int piece, int on,
                         widescreen_hud_space_fn make_space)
{
    static unsigned char told[WIDE_HUD_PIECES];
    const WideHudPiece *p;
    if (!on || !device || piece < 0 || piece >= WIDE_HUD_PIECES) return 0;
    p = &widescreen_hud_pieces[piece];
    if (!widescreen_hud_push(mem, device, nv2a_wide_hud_mark((unsigned)piece, p->side), make_space))
        return 0;
    if (!told[piece]) {
        told[piece] = 1;
        fprintf(stderr, "[WIDE] HUD piece %d, %s (sub_%08X): marked for the %s edge\n",
                piece, p->name, p->va, p->side < 0 ? "left" : "right");
    }
    return 1;
}

void widescreen_hud_end(uint8_t *mem, uint32_t device, int piece, int marked,
                        widescreen_hud_space_fn make_space)
{
    if (marked)
        widescreen_hud_push(mem, device, nv2a_wide_hud_mark((unsigned)piece, NV2A_WIDE_HUD_NONE),
                            make_space);
}
