/* The game's resource loads, checked (resource_load.h).
 *
 * The character loader's step (sub_00032C70) reads a character's
 * Media\Player\<name>.dat into the manager's file buffer, parses it and
 * releases it (0x00032F2E), leaving the pointer set. It then reads the shared
 * voice file and the character's own voice bank into the same buffer when
 * they aren't loaded yet, releasing and clearing it each time (0x00032FD7,
 * 0x0003309F). A character whose bank is loaded already skips both, so the
 * pointer is left naming released memory. The character unloads release it
 * on every call (0x00032A1A, 0x00033580), load in progress or not.
 *
 * The graffiti cache builder (resource type 25, sub_0002DBE0) reads each of
 * a sheet's textures into the manager's +0x1888, copies it into the sheet and
 * releases it (sub_0002DA70, at 0x0002DB57), leaving the pointer set. A step
 * later it writes the sheet and releases +0x1888 again (sub_0002D8D0 at
 * 0x0002D93E, sub_0002D950 at 0x0002D9BE), after the write has given the
 * guest lock up. Copying a ready-made sheet (sub_0002D9D0, at 0x0002DA50)
 * keeps it too. Each release is its function's last call, so the hook after
 * it finds the buffer as the release left it.
 *
 * The flow (task slot 10) copies the stage from the first word of its
 * mission's file as the file loads (sub_000520D0), and asks for the stage by
 * reading that word again (sub_0004AC80, or sub_0004ACB0 for the other kind
 * of stage). The two should be the same.
 *
 * The lines are always on: a few a scene at most, and one for each graffiti
 * sheet written, two a build. */
#include <stdio.h>
#include <string.h>
#include "resource_load.h"

#define RL_MGR_FILE_BUF   0x268u   /* the resource manager's file buffer, VirtualAlloc'd */
#define RL_MGR_GRAFFITI_BUF 0x1888u /* the graffiti cache builder's file buffer, VirtualAlloc'd */
#define RL_FLOW_MISSION   0x58u    /* the flow's mission id */
#define RL_FLOW_STATE     0x5Cu    /* its state, an index into the step table at 0x1F9888 */
#define RL_FLOW_FILE      0x1040u  /* its mission's file */
#define RL_FLOW_STAGE     0x104Cu  /* the stage, copied from the file as it loaded */

static uint32_t rd32(const uint8_t *mem, uint32_t at)
{
    uint32_t v;
    memcpy(&v, mem + at, 4);
    return v;
}

/* Forgets the buffer pointer at `slot` once the buffer it names has been
 * released, and returns that buffer: 0 when the slot is empty or a live
 * allocation holds its buffer. */
static uint32_t forget_released(uint8_t *mem, uint32_t slot, resource_load_released_fn released)
{
    uint32_t buf = rd32(mem, slot), none = 0;

    if (!buf || !released(buf))
        return 0;
    memcpy(mem + slot, &none, 4);
    return buf;
}

void resource_load_char_step(uint8_t *mem, uint32_t mgr, resource_load_released_fn released,
                             double t)
{
    uint32_t buf = forget_released(mem, mgr + RL_MGR_FILE_BUF, released);

    if (buf)
        fprintf(stderr, "[LOAD] t=%.1f the character loader released its file buffer %08X"
                        " and kept it: forgotten, so a character unload can't release it again\n",
                t, buf);
}

/* What the graffiti cache's releases forgot since the last sheet was written. */
static unsigned s_graffiti_forgotten;
static uint32_t s_graffiti_last;

void resource_load_graffiti_release(uint8_t *mem, uint32_t mgr, resource_load_released_fn released)
{
    uint32_t buf = forget_released(mem, mgr + RL_MGR_GRAFFITI_BUF, released);

    if (buf) {
        s_graffiti_forgotten++;
        s_graffiti_last = buf;
    }
}

void resource_load_graffiti_sheet(int sheet, double t)
{
    if (!s_graffiti_forgotten)
        return;
    fprintf(stderr, "[LOAD] t=%.1f writing graffiti sheet %d: the cache's file buffer was released"
                    " and kept %u time%s since the last sheet, last %08X, and forgotten each time,"
                    " so the write can't release it again\n",
            t, sheet, s_graffiti_forgotten, s_graffiti_forgotten == 1 ? "" : "s", s_graffiti_last);
    s_graffiti_forgotten = 0;
}

uint32_t resource_load_flow_state(const uint8_t *mem, uint32_t flow)
{
    return rd32(mem, flow + RL_FLOW_STATE);
}

void resource_load_stage_step(const uint8_t *mem, uint32_t flow, uint32_t state_before, double t)
{
    uint32_t file, asked, loaded;

    if (resource_load_flow_state(mem, flow) == state_before)
        return;                     /* not asked yet: the manager was busy */
    file = rd32(mem, flow + RL_FLOW_FILE);
    asked = rd32(mem, file);
    loaded = rd32(mem, flow + RL_FLOW_STAGE);
    if (asked == loaded)
        fprintf(stderr, "[LOAD] t=%.1f mission %08X asks for stage %08X (its file at %08X)\n",
                t, rd32(mem, flow + RL_FLOW_MISSION), asked, file);
    else
        fprintf(stderr, "[LOAD] t=%.1f mission %08X asks for stage %08X, but its file at %08X"
                        " said %08X when it loaded: something wrote over it since\n",
                t, rd32(mem, flow + RL_FLOW_MISSION), asked, file, loaded);
}
