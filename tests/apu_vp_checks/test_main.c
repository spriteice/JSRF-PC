/* Run with: python tests/apu_vp_checks/run.py
 *
 * The voice processor (src/apu/apu_vp.c) given numbers past the end of its
 * own state by the game: each is refused with one [CHECK] line in the log
 * (src/platform/recomp_check.h), and none of the voice processor's state
 * changes. The game's methods, as fe_method takes them through
 * mcpx_apu_vp_write:
 *
 *   1. A current voice past the last (300 of 256), then SET_VOICE_SSL_A and
 *      _B, which index the voices' stream lists with it, and VOICE_LOCK,
 *      which indexes the lock bits.
 *   2. A current voice of 2^31 and more, which as an int is below 0, so a
 *      check of "below 256" alone let it through.
 *   3. VOICE_ON for voice 300, which resets that voice's stream list.
 *   4. An HRTF entry past the last (200 of 128), then SET_HRIR and
 *      SET_HRIR_X, which write it, and SET_VOICE_TAR_HRTF naming it.
 *   5. A frame whose 2D list holds voice 300, active: the frame's lock wait
 *      and voice_process are given it.
 *
 * Each is done twice; the second time adds no line, as a check says once.
 * Then the same methods for voice 5 and entry 3 still do what they should.
 * run.py checks the [CHECK] lines, which go to stderr, as the log.
 *
 * "None of the state changes" is all of MCPXAPUState but its registers,
 * which the methods are written to, and 64 KB past its end, compared byte
 * for byte before and after each case.
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

#define VOICES       0x100000u      /* the voice array, in the game's memory */
#define PAST_VOICE   300u           /* past MCPX_HW_MAX_VOICES */
#define ABOVE_2_31   0x80000005u    /* below 0 as an int */
#define PAST_ENTRY   200u           /* past HRTF_ENTRY_COUNT */
#define GOOD_VOICE   5u
#define GOOD_ENTRY   3u
#define SSL_ARGUMENT 0x00001203u    /* base 0x12, 3 segments */
#define TAIL         (64u << 10)    /* bytes past the state compared too */

static MCPXAPUState *d;
static uint8_t *before, *after;
static int failures;

static void put(uint32_t addr, uint32_t value)
{
    memcpy(g_apu_ram_ptr + addr, &value, 4);
}

static void method(uint32_t m, uint32_t argument)
{
    mcpx_apu_vp_write(d, m, argument, 4);
}

/* Everything in *d but regs[], then TAIL bytes past it. */
static size_t snapshot(uint8_t *to)
{
    const uint8_t *s = (const uint8_t *)d;
    size_t head = offsetof(MCPXAPUState, regs);
    size_t rest = sizeof *d + TAIL - (head + sizeof d->regs);
    if (to) {
        memcpy(to, s, head);
        memcpy(to + head, s + head + sizeof d->regs, rest);
    }
    return head + rest;
}

static void unchanged(const char *what)
{
    size_t n = snapshot(NULL), i;
    if (!after) after = malloc(n);
    snapshot(after);
    for (i = 0; i < n && after[i] == before[i]; i++)
        ;
    if (i < n) {
        printf("apu_vp_checks: %s changed the voice processor's state (byte %zu)\n", what, i);
        failures++;
        memcpy(before, after, n);
    }
}

static void bad_methods(void)
{
    snapshot(before);
    method(NV1BA0_PIO_SET_CURRENT_VOICE, PAST_VOICE);
    method(NV1BA0_PIO_SET_VOICE_SSL_A, SSL_ARGUMENT);
    method(NV1BA0_PIO_SET_VOICE_SSL_B, SSL_ARGUMENT);
    method(NV1BA0_PIO_VOICE_LOCK, 1);
    method(NV1BA0_PIO_VOICE_LOCK, 0);
    unchanged("1, a current voice past the last,");

    method(NV1BA0_PIO_SET_CURRENT_VOICE, ABOVE_2_31);
    method(NV1BA0_PIO_SET_VOICE_SSL_A, SSL_ARGUMENT);
    method(NV1BA0_PIO_SET_VOICE_SSL_B, SSL_ARGUMENT);
    unchanged("2, a current voice of 2^31 and more,");

    method(NV1BA0_PIO_VOICE_ON, PAST_VOICE);
    unchanged("3, VOICE_ON past the last voice,");

    method(NV1BA0_PIO_SET_CURRENT_HRTF_ENTRY, PAST_ENTRY);
    snapshot(before);               /* current_entry is the game's to set */
    method(NV1BA0_PIO_SET_HRIR, 0x7F7F7F7Fu);
    method(NV1BA0_PIO_SET_HRIR_X, 0x00017F7Fu);
    method(NV1BA0_PIO_SET_CURRENT_VOICE, GOOD_VOICE);
    method(NV1BA0_PIO_SET_VOICE_TAR_HRTF, PAST_ENTRY);
    unchanged("4, an HRTF entry past the last,");

    float mixbins[NUM_MIXBINS][NUM_SAMPLES_PER_FRAME];
    uint32_t voice = VOICES + PAST_VOICE * NV_PAVS_SIZE;
    put(voice + NV_PAVS_VOICE_PAR_STATE, NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE);
    put(voice + NV_PAVS_VOICE_TAR_PITCH_LINK, 0x0000FFFF);   /* the last in its list */
    d->regs[NV_PAPU_TVL2D] = PAST_VOICE;
    d->regs[NV_PAPU_TVL3D] = 0xFFFF;
    d->regs[NV_PAPU_TVLMP] = 0xFFFF;
    memset(mixbins, 0, sizeof mixbins);
    mcpx_apu_vp_frame(d, mixbins);
    snapshot(before);               /* the frame clears sample_buf, as every frame does */
    mcpx_apu_vp_frame(d, mixbins);
    unchanged("5, a frame with a voice past the last,");
    d->regs[NV_PAPU_TVL2D] = 0xFFFF;
}

int main(void)
{
    int i;

    d = calloc(1, sizeof *d + TAIL);
    g_apu_ram_ptr = calloc(1, 64u << 20);
    before = malloc(snapshot(NULL));
    if (!d || !g_apu_ram_ptr || !before) return 1;
    d->ram_ptr = g_apu_ram_ptr;
    /* VOICE_LOCK takes these. A zeroed CRITICAL_SECTION isn't one, so on
     * Windows locking it fails; on Linux a zeroed mutex happens to be one. */
    qemu_mutex_init(&d->lock);
    qemu_cond_init(&d->cond);
    d->regs[NV_PAPU_VPVADDR] = VOICES;
    d->regs[NV_PAPU_TVL2D] = 0xFFFF;
    d->regs[NV_PAPU_TVL3D] = 0xFFFF;
    d->regs[NV_PAPU_TVLMP] = 0xFFFF;
    mcpx_apu_vp_init(d);

    for (i = 0; i < 2; i++) {
        fprintf(stderr, "apu_vp_checks: round %d\n", i + 1);
        fflush(stderr);
        bad_methods();
    }

    /* The same methods, for a voice and an entry that exist. */
    method(NV1BA0_PIO_SET_CURRENT_VOICE, GOOD_VOICE);
    method(NV1BA0_PIO_SET_VOICE_SSL_A, SSL_ARGUMENT);
    method(NV1BA0_PIO_SET_VOICE_SSL_B, SSL_ARGUMENT);
    if (d->vp.ssl[GOOD_VOICE].base[0] != GET_MASK(SSL_ARGUMENT, NV1BA0_PIO_SET_VOICE_SSL_A_BASE)
            || d->vp.ssl[GOOD_VOICE].count[1] != GET_MASK(SSL_ARGUMENT, NV1BA0_PIO_SET_VOICE_SSL_A_COUNT)) {
        printf("apu_vp_checks: SET_VOICE_SSL_A and _B didn't set voice 5's lists\n");
        failures++;
    }
    method(NV1BA0_PIO_VOICE_LOCK, 1);
    if (!(d->vp.voice_locked[0] & 1ull << GOOD_VOICE)) {
        printf("apu_vp_checks: VOICE_LOCK didn't lock voice 5\n");
        failures++;
    }
    method(NV1BA0_PIO_VOICE_LOCK, 0);
    method(NV1BA0_PIO_SET_CURRENT_HRTF_ENTRY, GOOD_ENTRY);
    method(NV1BA0_PIO_SET_HRIR, 0x0000007Fu);                 /* the left ear's first tap */
    if (d->vp.hrtf.entries[GOOD_ENTRY].hrir[0][0] <= 0.0f) {
        printf("apu_vp_checks: SET_HRIR didn't set entry 3\n");
        failures++;
    }

    /* Everything given back, so the sanitizers' leak check sees any the
     * voice processor itself keeps. */
    mcpx_apu_vp_finalize(d);
    qemu_cond_destroy(&d->cond);
    qemu_mutex_destroy(&d->lock);
    free(after);
    free(before);
    free(g_apu_ram_ptr);
    g_apu_ram_ptr = NULL;
    free(d);

    if (failures) {
        printf("apu_vp_checks: %d failures\n", failures);
        return 1;
    }
    printf("apu_vp_checks: all cases pass\n");
    return 0;
}
