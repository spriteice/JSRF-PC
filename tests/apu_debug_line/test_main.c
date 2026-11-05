/* Run with: python tests/apu_debug_line/run.py
 *
 * The APU's line in the [APU], [LOCK] and [FROZEN] reports, at its longest:
 * every counter at its largest and every flag on, written by
 * src/apu/apu_core.c's mcpx_apu_debug_print after each prefix given on the
 * command line, which run.py takes from the three reports in
 * src/kernel/kernel_bridge.c. run.py checks each line is there whole. The
 * first line says what the largest counter is.
 */
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include "apu_state.h"

void mcpx_apu_debug_print(FILE *f, const char *prefix);

/* apu_core.c's sound output, apu_sdl.c, which needs SDL: none here. */
int xa2_init(void) { return 0; }
void xa2_shutdown(void) {}
int xa2_is_active(void) { return 0; }
int xa2_submit_samples(const int16_t *samples, int num_samples) { (void)samples; return num_samples; }
int xa2_get_buffer_size(void) { return 0; }
int xa2_ring_level(void) { return 0; }
int xa2_ms_since_pull(void) { return 0; }

/* apu_mmio_hook.c's, which the test doesn't link. */
MCPXAPUState *g_apu_state;

extern unsigned long g_apu_notifies, g_apu_voice_offs;
extern unsigned g_apu_fe_on, g_apu_fe_off, g_apu_fe_total;
extern unsigned long g_apu_seg_done, g_apu_notify_ssl, g_apu_notify_persist;
extern unsigned long g_apu_voices_seen, g_apu_stream_voices, g_apu_voices_paused;

int main(int argc, char **argv)
{
    static MCPXAPUState d;
    int i;

    /* Each register the line shows, all ones: both FECTL flags, and the
     * widest hex. */
    d.regs[NV_PAPU_SECTL] = 0xFFFFFFFFu;
    d.regs[NV_PAPU_FECTL] = 0xFFFFFFFFu;
    d.regs[NV_PAPU_IEN] = 0xFFFFFFFFu;
    d.regs[NV_PAPU_ISTS] = 0xFFFFFFFFu;
    g_apu_notifies = g_apu_voice_offs = ULONG_MAX;
    g_apu_fe_on = g_apu_fe_off = g_apu_fe_total = UINT_MAX;
    g_apu_seg_done = g_apu_notify_ssl = g_apu_notify_persist = ULONG_MAX;
    g_apu_voices_seen = g_apu_stream_voices = g_apu_voices_paused = ULONG_MAX;

    printf("max %lu %u\n", ULONG_MAX, UINT_MAX);
    mcpx_apu_debug_print(stdout, "no state: ");
    g_apu_state = &d;
    for (i = 1; i < argc; i++)
        mcpx_apu_debug_print(stdout, argv[i]);
    return 0;
}
