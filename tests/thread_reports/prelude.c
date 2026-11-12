/* What the parts of src/kernel/kernel_bridge.c that run.py cuts out need
 * around them: the guest lock's reports and the counts the freeze report
 * prints, as stubs. The picture's frame counter (nv2a_gl_progress) is in
 * test_main.c, after the freeze watch's weak declaration of it. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "xbox_winnt.h"
#include "kernel/kernel.h"
#include "kernel_bridge.h"

/* The guest lock's own reports, on the report thread: each says nothing
 * here, and none of them takes the guest lock in the bridge either. */
void xbox_guest_lock_report(void) {}
void xbox_guest_lock_stats(void) {}
void xbox_guest_cs_report(void) {}
void recomp_fp_report(void);
void recomp_fp_report(void) {}

/* The counts the freeze report prints. */
static unsigned g_apu_raised, g_apu_claimed;
void kernel_dpc_counts(unsigned long *queued, unsigned long *run, long *waiting)
{
    *queued = 0; *run = 0; *waiting = 0;
}
void kernel_vblank_counts(unsigned long *raised, unsigned long *claimed,
                          unsigned long *declined, uint32_t *enabled)
{
    *raised = 0; *claimed = 0; *declined = 0; *enabled = 0;
}
void kernel_swm_counts(unsigned long *raised, unsigned long *waiting, int *in_flight,
                       uint32_t *param, uint32_t *intr, uint32_t *fifo)
{
    *raised = 0; *waiting = 0; *in_flight = 0; *param = 0; *intr = 0; *fifo = 0;
}
