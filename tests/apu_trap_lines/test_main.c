/* Run with: python tests/apu_trap_lines/run.py
 *
 * The voice processor's lines for b3's baseline (src/apu/apu_vp.c), driven
 * as the game drives it: the title's methods through mcpx_apu_vp_write, and
 * frames, with RECOMP_APU_STATS and RECOMP_APU_WATCH set by run.py and the
 * ten-second report made every frame's (APU_G1_REPORT_MS=0), so each frame's
 * two [APU] lines say what happened since the last.
 *
 *   1. Line B: voice 5, an ADPCM mono buffer voice with EBO 12351, started
 *      on the 2D list: the bytes after its line said at VOICE_ON (all zero).
 *   2. Its LBO set by a method, then its CBO written in memory with no
 *      method, to 11640: two change lines, and the frame passing 11647
 *      says the bytes after the line again. The voice processor moves CBO
 *      as it fetches, 128 samples every other frame here, so it's the
 *      second frame after the write that passes, to 11768.
 *   3. A VOICE_LOCK with nothing under it (the game's look at the position),
 *      then VOICE_OFF: the off line, with the look.
 *   4. The next frame finds it idle and traps; a method run in the trap
 *      overwrites FEDECPARAM before the handler reads it; the handler takes
 *      the voice out of its list and clears the trap: out after 1 report,
 *      and the stop wait's VOICE_OFF to out of the list.
 *   5. Voice 6 off, reported twice (the trap cleared with it still in its
 *      list) before it leaves: out after 2 reports, the most 2.
 *   6. Voice 7 off, then started again before a frame finds it out.
 *   (5 and 6 are ADPCM mono too, with EBO 1000: not line B.)
 *
 * Each step's mark goes to stderr, where the lines go, for run.py to cut.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apu_state.h"

uint8_t *g_apu_ram_ptr;
int g_apu_window_only = 1;
struct McpxApuDebug g_dbg;
int g_dbg_voice_monitor = -1;

void mcpx_apu_vp_write(void *opaque, hwaddr addr, uint64_t val, unsigned int size);
void mcpx_apu_vp_fedecparam_read(MCPXAPUState *d, uint32_t value);

#define VOICES  0x100000u        /* the voice array */
#define SGE     0x300000u        /* the buffers' page table */
#define BA      0x10000u         /* line B's buffer, through the table */
#define PAGES   0x400000u        /* where the table puts its pages */

static MCPXAPUState *d;
static float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME];

static void put(uint32_t addr, uint32_t value) { memcpy(g_apu_ram_ptr + addr, &value, 4); }
static void method(uint32_t m, uint32_t argument) { mcpx_apu_vp_write(d, m, argument, 4); }
static void frame(void) { mcpx_apu_vp_frame(d, mixbins); }
static void mark(const char *what) { fprintf(stderr, "== %s\n", what); fflush(stderr); }

static void voice_on(uint32_t v, uint32_t fmt, uint32_t ebo)
{
    uint32_t at = VOICES + v * NV_PAVS_SIZE;
    put(at + NV_PAVS_VOICE_CFG_FMT, fmt);
    put(at + NV_PAVS_VOICE_PAR_NEXT, ebo);
    put(at + NV_PAVS_VOICE_CUR_PSL_START, BA);
    method(NV1BA0_PIO_SET_ANTECEDENT_VOICE, (NV1BA0_PIO_SET_ANTECEDENT_VOICE_LIST_2D_TOP << 16) | 0xFFFF);
    method(NV1BA0_PIO_VOICE_ON, v);
}

/* The handler: the trap's voice out of the 2D list (it's the top here, as
 * each voice is the only one when it's taken out), and the trap cleared. */
static void handled(int take_out)
{
    if (take_out) d->regs[NV_PAPU_TVL2D] = 0xFFFF;
    d->regs[NV_PAPU_FECTL] = NV_PAPU_FECTL_FEMETHMODE_FREE_RUNNING;
}

int main(void)
{
    uint32_t i, lineb = (uint32_t)NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE_ADPCM << 30;

    d = calloc(1, sizeof *d);
    g_apu_ram_ptr = calloc(1, 64u << 20);
    if (!d || !g_apu_ram_ptr) return 1;
    d->ram_ptr = g_apu_ram_ptr;
    qemu_mutex_init(&d->lock);
    qemu_cond_init(&d->cond);
    d->regs[NV_PAPU_VPVADDR] = VOICES;
    d->regs[NV_PAPU_VPSGEADDR] = SGE;
    for (i = 0; i < 8; i++) put(SGE + 8 * (BA / 4096 + i), PAGES + 4096 * i);
    d->regs[NV_PAPU_TVL2D] = d->regs[NV_PAPU_TVL3D] = d->regs[NV_PAPU_TVLMP] = 0xFFFF;
    d->regs[NV_PAPU_FETFORCE1] = NV_PAPU_FETFORCE1_SE2FE_IDLE_VOICE;
    d->regs[NV_PAPU_FECTL] = NV_PAPU_FECTL_FEMETHMODE_FREE_RUNNING;
    mcpx_apu_vp_init(d);

    mark("1 line B on");
    voice_on(5, lineb, 12351);
    frame();

    mark("2 LBO by a method");
    method(NV1BA0_PIO_SET_CURRENT_VOICE, 5);
    method(NV1BA0_PIO_SET_VOICE_CFG_BUF_LBO, 100);
    frame();
    mark("2 CBO in memory");
    put(VOICES + 5 * NV_PAVS_SIZE + NV_PAVS_VOICE_PAR_OFFSET, 11640);
    frame();
    frame();      /* the voice processor fetches 128 samples every other frame */

    mark("3 a look, then off");
    method(NV1BA0_PIO_VOICE_LOCK, 1);
    method(NV1BA0_PIO_VOICE_LOCK, 0);
    frame();
    method(NV1BA0_PIO_VOICE_OFF, 5);

    mark("4 trapped");
    frame();
    method(NV1BA0_PIO_SET_CURRENT_VOICE, 7);             /* a method in the trap */
    mcpx_apu_vp_fedecparam_read(d, d->regs[NV_PAPU_FEDECPARAM]);
    handled(1);
    mark("4 out");
    frame();

    mark("5 voice 6 off");
    voice_on(6, lineb, 1000);
    frame();
    method(NV1BA0_PIO_VOICE_OFF, 6);
    frame();                                             /* reported, trapped */
    handled(0);
    frame();                                             /* reported again */
    handled(1);
    mark("5 out");
    frame();

    mark("6 voice 7 off and on");
    voice_on(7, lineb, 1000);
    frame();
    method(NV1BA0_PIO_VOICE_OFF, 7);
    d->regs[NV_PAPU_TVL2D] = 0xFFFF;     /* so VOICE_ON doesn't link it after itself */
    method(NV1BA0_PIO_VOICE_ON, 7);
    mark("6 on again");
    frame();

    mcpx_apu_vp_finalize(d);
    qemu_cond_destroy(&d->cond);
    qemu_mutex_destroy(&d->lock);
    free(g_apu_ram_ptr);
    free(d);
    return 0;
}
