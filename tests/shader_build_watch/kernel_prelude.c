/* What kernel_freeze_watch needs around it, cut out of
 * src/kernel/kernel_bridge.c by run.py: the clock, which is the test's own
 * (test_main.c), and the counts and the table of threads its report prints,
 * as stand-ins. The guest lock the shader build gives up is the kernel's
 * too, so its stand-in is here. */
#include <stdint.h>
#include <stdio.h>

long long test_clock_ms(void);
#define GetTickCount64() test_clock_ms()

int  xbox_guest_lock_drop(void);
void xbox_guest_lock_retake(int held);
int  xbox_guest_lock_drop(void) { return 1; }
void xbox_guest_lock_retake(int held) { (void)held; }

/* The counts the freeze report prints. */
static unsigned g_apu_raised, g_apu_claimed;
static void kernel_dpc_counts(unsigned long *queued, unsigned long *run, long *waiting)
{
    *queued = 0; *run = 0; *waiting = 0;
}
static void kernel_vblank_counts(unsigned long *raised, unsigned long *claimed,
                                 unsigned long *declined, uint32_t *enabled)
{
    *raised = 0; *claimed = 0; *declined = 0; *enabled = 0;
}
static void kernel_swm_counts(unsigned long *raised, unsigned long *waiting, int *in_flight,
                              uint32_t *param, uint32_t *intr, uint32_t *fifo)
{
    *raised = 0; *waiting = 0; *in_flight = 0; *param = 0; *intr = 0; *fifo = 0;
}
static void kthread_report(void)
{
    fprintf(stderr, "  [THREADS] the test's stand-in\n");
}
