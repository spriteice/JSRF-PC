/* Run with: python tests/widescreen_hud/run.py
 *
 * The HUD's marks, port/widescreen_hud.c, as the engine builds it, against a
 * guest memory that holds D3D's device (its put pointer and limit) and its
 * push buffer. space() plays D3D's make-space call.
 *
 *   1. The pieces: the health bar is sub_001060B0's, on the right.
 *   2. In 4:3 (or with no device yet, or no such piece), nothing is written
 *      and nothing is logged, and the end writes nothing either.
 *   3. Widened, the start mark goes where the put pointer is, as one method
 *      (one value, subchannel 0) with the piece and its side, and the put
 *      pointer moves past it; the end mark follows, with no side. The first
 *      start names the piece in the log, and no other does (run.py counts).
 *   4. At the limit, make-space is called first and the mark goes where it
 *      says; below it, it isn't called.
 *   5. Make-space that leaves no room below the limit gets no mark, and no
 *      end mark follows; the check says so once (run.py counts).
 */
#include <stdio.h>
#include <string.h>
#include "widescreen_hud.h"
#include "nv2a_wide.h"

#define DEVICE 0x0100u
#define BUFFER 0x1000u
#define LIMIT  0x2000u
#define ROOM   0x3000u     /* where make-space puts the pointer */

static uint32_t s_guest[0x4000 / 4];
static unsigned s_space_calls;
static uint32_t s_room = ROOM;
static int s_failures;

#define CHECK(c) do { if (!(c)) { printf("widescreen_hud: FAIL %s:%d: %s\n", \
                                         __FILE__, __LINE__, #c); s_failures++; } } while (0)

static uint8_t *mem(void) { return (uint8_t *)s_guest; }
static uint32_t word(uint32_t va) { return s_guest[va / 4]; }
static void set(uint32_t va, uint32_t v) { s_guest[va / 4] = v; }
static uint32_t space(void) { s_space_calls++; return s_room; }

static void fresh(uint32_t put)
{
    memset(s_guest, 0, sizeof s_guest);
    set(DEVICE + WIDE_HUD_D3D_PUT, put);
    set(DEVICE + WIDE_HUD_D3D_LIMIT, LIMIT);
    s_space_calls = 0;
    s_room = ROOM;
}

static int nothing_written(uint32_t put)
{
    uint32_t i;
    for (i = 0; i < sizeof s_guest / 4; i++)
        if (i * 4 != DEVICE + WIDE_HUD_D3D_PUT && i * 4 != DEVICE + WIDE_HUD_D3D_LIMIT
            && s_guest[i])
            return 0;
    return word(DEVICE + WIDE_HUD_D3D_PUT) == put;
}

static int mark_at(uint32_t va, unsigned piece, int side)
{
    unsigned p = 999;
    int sd = 9;
    return word(va) == ((1u << 18) | NV2A_WIDE_HUD_METHOD)
        && nv2a_wide_hud_read(word(va + 4), &p, &sd) && p == piece && sd == side;
}

static void pieces(void)
{
    CHECK(widescreen_hud_pieces[WIDE_HUD_HEALTH].va == 0x001060B0u);
    CHECK(widescreen_hud_pieces[WIDE_HUD_HEALTH].side == NV2A_WIDE_HUD_RIGHT);
    CHECK(widescreen_hud_pieces[WIDE_HUD_HEALTH].name != NULL);
}

static void off(void)
{
    fresh(BUFFER);
    CHECK(!widescreen_hud_begin(mem(), DEVICE, WIDE_HUD_HEALTH, 0, space));
    widescreen_hud_end(mem(), DEVICE, WIDE_HUD_HEALTH, 0, space);
    CHECK(nothing_written(BUFFER));
    CHECK(!widescreen_hud_begin(mem(), 0, WIDE_HUD_HEALTH, 1, space));        /* no device */
    CHECK(!widescreen_hud_begin(mem(), DEVICE, WIDE_HUD_PIECES, 1, space));   /* no piece */
    CHECK(!widescreen_hud_begin(mem(), DEVICE, -1, 1, space));
    CHECK(nothing_written(BUFFER));
    CHECK(s_space_calls == 0);
}

static void marks(void)
{
    int marked;
    fresh(BUFFER);
    marked = widescreen_hud_begin(mem(), DEVICE, WIDE_HUD_HEALTH, 1, space);
    CHECK(marked);
    CHECK(mark_at(BUFFER, WIDE_HUD_HEALTH, NV2A_WIDE_HUD_RIGHT));
    CHECK(word(DEVICE + WIDE_HUD_D3D_PUT) == BUFFER + 8);
    set(BUFFER + 8, 0xD3D0D3D0u);      /* the piece's own draws, as D3D writes them */
    set(DEVICE + WIDE_HUD_D3D_PUT, BUFFER + 12);
    widescreen_hud_end(mem(), DEVICE, WIDE_HUD_HEALTH, marked, space);
    CHECK(word(BUFFER + 8) == 0xD3D0D3D0u);
    CHECK(mark_at(BUFFER + 12, WIDE_HUD_HEALTH, NV2A_WIDE_HUD_NONE));
    CHECK(word(DEVICE + WIDE_HUD_D3D_PUT) == BUFFER + 20);
    CHECK(s_space_calls == 0);
    /* the next frame's: the same marks, and no second log line */
    marked = widescreen_hud_begin(mem(), DEVICE, WIDE_HUD_HEALTH, 1, space);
    widescreen_hud_end(mem(), DEVICE, WIDE_HUD_HEALTH, marked, space);
    CHECK(mark_at(BUFFER + 20, WIDE_HUD_HEALTH, NV2A_WIDE_HUD_RIGHT));
    CHECK(mark_at(BUFFER + 28, WIDE_HUD_HEALTH, NV2A_WIDE_HUD_NONE));
    CHECK(word(DEVICE + WIDE_HUD_D3D_PUT) == BUFFER + 36);
}

static void full(void)
{
    fresh(LIMIT);                       /* at the limit: make room first */
    s_room = BUFFER;                    /* D3D's room, below its limit */
    CHECK(widescreen_hud_push(mem(), DEVICE, 0x48550002u, space) == BUFFER);
    CHECK(s_space_calls == 1);
    CHECK(word(BUFFER) == ((1u << 18) | NV2A_WIDE_HUD_METHOD) && word(BUFFER + 4) == 0x48550002u);
    CHECK(word(LIMIT) == 0 && word(LIMIT + 4) == 0);
    CHECK(word(DEVICE + WIDE_HUD_D3D_PUT) == BUFFER + 8);
    fresh(ROOM + 0x100);                /* make-space can move the limit too */
    set(DEVICE + WIDE_HUD_D3D_LIMIT, ROOM + 0x100);
    CHECK(widescreen_hud_push(mem(), DEVICE, 0x48550001u, space) == ROOM);
    CHECK(word(ROOM + 4) == 0x48550001u && word(DEVICE + WIDE_HUD_D3D_PUT) == ROOM + 8);
    fresh(LIMIT + 4);                   /* past it */
    s_room = BUFFER;
    CHECK(widescreen_hud_push(mem(), DEVICE, 0x48550000u, space) == BUFFER);
    CHECK(s_space_calls == 1);
    fresh(LIMIT - 4);                   /* just below it: D3D's slack holds it */
    CHECK(widescreen_hud_push(mem(), DEVICE, 0x48550000u, space) == LIMIT - 4);
    CHECK(s_space_calls == 0);
    CHECK(word(DEVICE + WIDE_HUD_D3D_PUT) == LIMIT + 4);
}

static void no_room(void)
{
    int marked;
    fresh(LIMIT);
    s_room = LIMIT;                     /* no room below the limit */
    CHECK(widescreen_hud_push(mem(), DEVICE, 0x48550000u, space) == 0);
    CHECK(s_space_calls == 1);
    CHECK(word(LIMIT) == 0 && word(LIMIT + 4) == 0);
    CHECK(word(DEVICE + WIDE_HUD_D3D_PUT) == LIMIT);
    s_room = 0;                         /* or none at all */
    marked = widescreen_hud_begin(mem(), DEVICE, WIDE_HUD_HEALTH, 1, space);
    CHECK(!marked);
    widescreen_hud_end(mem(), DEVICE, WIDE_HUD_HEALTH, marked, space);
    CHECK(s_space_calls == 2);
    CHECK(word(0) == 0 && word(4) == 0);
}

int main(void)
{
    pieces();
    off();
    marks();
    full();
    no_room();
    fflush(stderr);
    if (s_failures) {
        printf("widescreen_hud: %d failed\n", s_failures);
        return 1;
    }
    printf("widescreen_hud: all cases pass\n");
    return 0;
}
