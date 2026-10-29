/* Run with: python tests/apu_mix/run.py
 *
 * One stereo voice through src/apu/apu_vp.c and apu_dsp.c as they are: voice
 * 68 in the format of JSRF's music (16-bit stereo, looped), a constant on
 * each side (left 0.5, right -0.25), its channels at full volume into two
 * mixbins and the other six off. Twenty frames, two and a half times round
 * the eight sub-frames of the encode processor's frame, then the last sample
 * the device would get. The argument is the mixbin the right channel goes
 * to: 1 (front right), or 10 (the reverb send, which the fold to stereo
 * leaves out). run.py runs each with the fold and with RECOMP_APU_MIX=xemu.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apu_state.h"

uint8_t *g_apu_ram_ptr;
int g_apu_window_only = 1;
struct McpxApuDebug g_dbg;
int g_dbg_voice_monitor = -1;

#define VOICES 0x100000u            /* the voice array */
#define SGE    0x200000u            /* the page table the voice's buffer goes through */
#define DATA   0x300000u            /* the buffer */
#define FRAMES 4096                 /* stereo frames in it */

static void put(uint32_t addr, uint32_t value)
{
    memcpy(g_apu_ram_ptr + addr, &value, 4);
}

int main(int argc, char **argv)
{
    unsigned right_bin = argc > 1 ? (unsigned)atoi(argv[1]) : 1;
    MCPXAPUState *d = calloc(1, sizeof *d);
    float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME];
    uint32_t voice = VOICES + 68 * NV_PAVS_SIZE;
    const int16_t frame[2] = { 16384, -8192 };
    int i, last;

    g_apu_ram_ptr = calloc(1, 64u << 20);
    if (!d || !g_apu_ram_ptr) return 1;
    d->ram_ptr = g_apu_ram_ptr;
    for (i = 0; i < FRAMES; i++)
        memcpy(g_apu_ram_ptr + DATA + 4 * i, frame, 4);
    for (i = 0; i < FRAMES * 4 / 4096; i++)
        put(SGE + 8 * i, DATA + 4096 * i);

    d->regs[NV_PAPU_VPVADDR] = VOICES;
    d->regs[NV_PAPU_VPSGEADDR] = SGE;
    d->regs[NV_PAPU_TVL2D] = 68;
    d->regs[NV_PAPU_TVL3D] = 0xFFFF;
    d->regs[NV_PAPU_TVLMP] = 0xFFFF;

    put(voice + NV_PAVS_VOICE_CFG_VBIN, 0 | right_bin << 5);   /* left into 0 */
    put(voice + NV_PAVS_VOICE_CFG_FMT, 0x5A01E000);
    put(voice + NV_PAVS_VOICE_CFG_HRTF_TARGET, 0xFFFF);
    put(voice + NV_PAVS_VOICE_PAR_STATE, NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE);
    put(voice + NV_PAVS_VOICE_PAR_NEXT, FRAMES - 1);            /* the end of the buffer */
    put(voice + NV_PAVS_VOICE_TAR_VOLA, 0x000F000F);            /* 0 and 1 full, 6 and 7 off */
    put(voice + NV_PAVS_VOICE_TAR_VOLB, 0xFFFFFFFF);            /* 2 and 3 off */
    put(voice + NV_PAVS_VOICE_TAR_VOLC, 0xFFFFFFFF);            /* 4 and 5 off */
    put(voice + NV_PAVS_VOICE_TAR_PITCH_LINK, 0x0000FFFF);      /* 48 kHz, the last voice */

    mcpx_apu_vp_init(d);
    mcpx_apu_dsp_init(d);
    for (i = 0; i < 20; i++) {
        memset(mixbins, 0, sizeof mixbins);
        mcpx_apu_vp_frame(d, mixbins);
        mcpx_apu_dsp_frame(d, mixbins);
        d->ep_frame_div++;
    }
    last = (int)((d->ep_frame_div - 1) % 8) * NUM_SAMPLES_PER_FRAME + NUM_SAMPLES_PER_FRAME - 1;
    printf("right channel into bin %u: left %d, right %d\n", right_bin,
           d->monitor.frame_buf[last][0], d->monitor.frame_buf[last][1]);
    return 0;
}
