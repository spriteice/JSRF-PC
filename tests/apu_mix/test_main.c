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
 *
 * With "lock" in place of the mixbin, the right channel starts in 10, and on
 * the twentieth frame another thread does what the title does to change a
 * voice: locks it with VOICE_LOCK, moves the right channel to 1 with
 * SET_VOICE_CFG_VBIN, and unlocks it. The voice is locked before the frame
 * starts and changed only once the frame has begun; the frame must wait
 * for the unlock (the thread finds it waiting, not finished) and mix the
 * channel into 1.
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

#define VOICES 0x100000u            /* the voice array */
#define SGE    0x200000u            /* the page table the voice's buffer goes through */
#define DATA   0x300000u            /* the buffer */
#define FRAMES 4096                 /* stereo frames in it */

static void put(uint32_t addr, uint32_t value)
{
    memcpy(g_apu_ram_ptr + addr, &value, 4);
}

/* The title's side of the "lock" case, and what the two threads share */
static struct {
    MCPXAPUState *d;
    QemuCond cond;                  /* with d->lock */
    int locked, frame_started, in_frame, waited;
} t;

static void *title_thread(void *arg)
{
    (void)arg;
    mcpx_apu_vp_write(t.d, NV1BA0_PIO_SET_CURRENT_VOICE, 68, 4);
    mcpx_apu_vp_write(t.d, NV1BA0_PIO_VOICE_LOCK, 1, 4);
    qemu_mutex_lock(&t.d->lock);
    t.locked = 1;
    qemu_cond_signal(&t.cond);
    /* d->lock is free again only while the frame waits, or once it is done */
    while (!t.frame_started)
        qemu_cond_wait(&t.cond, &t.d->lock);
    t.waited = t.in_frame;
    qemu_mutex_unlock(&t.d->lock);
    mcpx_apu_vp_write(t.d, NV1BA0_PIO_SET_VOICE_CFG_VBIN, 0 | 1 << 5, 4);
    mcpx_apu_vp_write(t.d, NV1BA0_PIO_VOICE_LOCK, 0, 4);
    return NULL;
}

int main(int argc, char **argv)
{
    int lock = argc > 1 && !strcmp(argv[1], "lock");
    unsigned right_bin = lock ? 10 : argc > 1 ? (unsigned)atoi(argv[1]) : 1;
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
        if (lock && i == 19) {
            QemuThread thread;
            qemu_mutex_init(&d->lock);
            qemu_cond_init(&d->cond);
            qemu_cond_init(&t.cond);
            t.d = d;
            qemu_mutex_lock(&d->lock);
            qemu_thread_create(&thread, "title", title_thread, NULL,
                               QEMU_THREAD_JOINABLE);
            while (!t.locked)
                qemu_cond_wait(&t.cond, &d->lock);
            t.frame_started = t.in_frame = 1;
            qemu_cond_signal(&t.cond);
            mcpx_apu_vp_frame(d, mixbins);       /* as the frame thread, under d->lock */
            t.in_frame = 0;
            qemu_mutex_unlock(&d->lock);
            qemu_thread_join(&thread);
            printf("lock: the frame %s, right channel in bin 1 %.4f, bin 10 %.4f\n",
                   t.waited ? "waited for the unlock" : "did not wait",
                   mixbins[1][NUM_SAMPLES_PER_FRAME - 1],
                   mixbins[10][NUM_SAMPLES_PER_FRAME - 1]);
            return 0;
        }
        mcpx_apu_vp_frame(d, mixbins);
        mcpx_apu_dsp_frame(d, mixbins);
        d->ep_frame_div++;
    }
    last = (int)((d->ep_frame_div - 1) % 8) * NUM_SAMPLES_PER_FRAME + NUM_SAMPLES_PER_FRAME - 1;
    printf("right channel into bin %u: left %d, right %d\n", right_bin,
           d->monitor.frame_buf[last][0], d->monitor.frame_buf[last][1]);
    return 0;
}
