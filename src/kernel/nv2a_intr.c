/**
 * nv2a_intr.c - The NV2A's interrupts: the display's vertical blank and the
 * GPU's software methods
 *
 * The two interrupts the runtime raises on the NV2A's vector, on every pass of
 * the kernel's timer thread (kernel_timer.c): the vertical blank, the guest's
 * frame clock, and the software methods the push-buffer executor queues
 * (xbox_nv2a_software_method), with their counts for the freeze report.
 *
 * Moved out of kernel_bridge.c as it was, so that it can be linked and tested
 * on its own.
 */

#include "kernel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xbox_memory_layout.h"
#include "kernel_bridge.h"

/* The GPU's vertical blank, delivered rather than merely enabled.
 *
 * The D3D8 library linked into a title installs an ISR for this and then waits
 * on it. Nothing ever raised it, so a title whose frame loop waits for vblank
 * rather than polling stops after its first clear -- which is exactly where
 * Half-Life 2's loader stops, with its videos open, its 23 MB of UI textures
 * loaded, and no second frame.
 *
 * The ISR reads the NV2A's own interrupt status to decide whether the
 * interrupt is its business, so the registers have to say vblank before the
 * routine is called: PCRTC_INTR_0 bit 0 for the vblank itself, and PMC_INTR_0
 * bit 24 to say the PCRTC block is the source. Without those the handler looks,
 * finds nothing, and correctly declines.
 *
 * ponytail: a fixed 60 Hz off the timer tick rather than anything tied to the
 * display mode, and no field or interlace handling. A title that measures
 * refresh rate from this will read 60; a title that needs the real one wants
 * the mode AvSetDisplayMode was given, which is recorded a few files away.
 */
#define XBOX_NV2A_REG_BASE     0xFD000000u
#define NV2A_PMC_INTR_0        0x00000100u
#define NV2A_PMC_INTR_PCRTC    (1u << 24)
#define NV2A_PCRTC_INTR_0      0x00600100u
#define NV2A_PCRTC_INTR_VBLANK (1u << 0)
#define NV2A_PMC_INTR_EN_0     0x00000140u
#define NV2A_VECTOR            3u

/* How many display interrupts have been delivered. Read by the renderer's
 * pacing report, so "the guest's frame clock" is a number and not a belief. */
unsigned long g_vblank_count;
/* And what the title's routine made of them, for the freeze report. D3D's
 * routine masks PMC_INTR_EN_0 when it claims one and only its DPC unmasks it,
 * so a run of declines with PMC_INTR_EN_0 at 0 is a routine waiting for a DPC.
 * Only the timer thread writes these. */
static unsigned long g_vblank_claimed, g_vblank_declined_since_claim;

void kernel_vblank_tick(void)
{
    static int enabled = -1;
    static long long next_ms;
    long long now;

    /* On unless RECOMP_VBLANK=0. */
    if (enabled < 0) {
        const char *e = getenv("RECOMP_VBLANK");
        enabled = e ? atoi(e) != 0 : 1;
    }
    if (!enabled)
        return;

    /* The period, in milliseconds, and why it is not simply 16.
     *
     * The console's display interrupt is the guest's frame clock, and a title
     * that paces anything by counting it is only as accurate as this number.
     * 16 with a 10 ms sleep under it is not 60 Hz, it is 50: the tick can only
     * fire on a sleep boundary, so a 16 ms deadline waits 20. RECOMP_VBLANK_HZ
     * makes the rate measurable rather than assumed -- if a title's boot
     * sequence is counted in display interrupts, doubling the rate halves the
     * boot, and if it is counted in anything else, nothing moves. */
    {
        static long long period = -1;
        if (period < 0) {
            const char *e = getenv("RECOMP_VBLANK_HZ");
            double hz = e ? atof(e) : 60.0;
            if (hz < 1.0) hz = 60.0;
            period = (long long)(1000.0 / hz + 0.5);
            if (period < 1) period = 1;
        }
        now = (long long)GetTickCount64();
        if (now < next_ms)
            return;
        /* Catch up rather than drift: a late tick should not push the next one
         * a full period further out. */
        next_ms = (next_ms && now - next_ms < period * 4) ? next_ms + period
                                                          : now + period;
    }

    if (!xbox_GetConnectedInterrupt(NV2A_VECTOR))
        return;

    /* Raised with the pending marker set, so the busy-bit thread can tell an
     * interrupt the title has not looked at yet from one it has acknowledged.
     * Without that distinction the status bits were wiped 200 us after this
     * line and the DPC below found nothing pending -- see vblank_intr_tick(). */
    BRIDGE_MEM32(XBOX_NV2A_REG_BASE + NV2A_PCRTC_INTR_0) |=
        NV2A_PCRTC_INTR_VBLANK | NV2A_VBLANK_PENDING_TAG;
    /* One atomic operation: PMC_INTR_0 is shared with the software methods
     * below, and the GPU thread clears it as this runs. */
    __atomic_fetch_or(&BRIDGE_MEM32(XBOX_NV2A_REG_BASE + NV2A_PMC_INTR_0),
                      NV2A_PMC_INTR_PCRTC, __ATOMIC_SEQ_CST);

    g_vblank_count++;
    {
        static unsigned n;
        int claimed = kernel_raise_interrupt(NV2A_VECTOR);
        if (claimed > 0) {
            g_vblank_claimed++;
            g_vblank_declined_since_claim = 0;
        } else if (claimed == 0) {
            g_vblank_declined_since_claim++;
        }
        if (n++ < 3) {
            fprintf(stderr, "  [NV2A] vblank -> ISR %s\n",
                    claimed < 0 ? "not callable" :
                    claimed ? "claimed it" : "declined it");
            fflush(stderr);
        }
    }
}

/* The display interrupt's counts and PMC_INTR_EN_0 now, for the freeze
 * report. */
void kernel_vblank_counts(unsigned long *raised, unsigned long *claimed,
                          unsigned long *declined_since_claim, uint32_t *enabled)
{
    *raised = g_vblank_count;
    *claimed = g_vblank_claimed;
    *declined_since_claim = g_vblank_declined_since_claim;
    *enabled = BRIDGE_MEM32(XBOX_NV2A_REG_BASE + NV2A_PMC_INTR_EN_0);
}

/* The GPU's software methods, delivered.
 *
 * NV097_NO_OPERATION with a non-zero parameter is how D3D asks the GPU to call
 * it back: the GPU stops on the method and raises a PGRAPH interrupt -- an
 * "error" whose trap registers hold the method (0x100) and the parameter --
 * and D3D's handler (CMiniport::ServiceGrInterrupt, sub_00194210 in JSRF) does
 * what the parameter says. Parameter 5 sets the push buffer's "busy block"
 * event, and the make-space path for a request of 32 KB or more inserts a
 * NOP(5) and sleeps on that event. Nothing here raised the interrupt, so once
 * in a while -- a big enough request at the wrong moment -- the game slept for
 * good: the main thread in KeWaitForSingleObject from 0x0019151B, the push
 * buffer drained (GET == PUT), the picture stopped.
 *
 * The push-buffer executor queues each one (xbox_nv2a_software_method) and
 * this tick, on the thread that delivers the other interrupts, raises the
 * oldest: trap registers, PGRAPH_INTR's error bit, PMC_INTR_0's PGRAPH bit,
 * then the NV2A vector. One at a time: the next waits until the title's
 * handler has acknowledged this one (xbox_nv2a_software_method_done). The
 * acknowledgement comes from the port's wrapper on that handler, which also
 * does what the console's write-1-to-clear register would -- here the
 * register is memory and keeps the bit, and D3D's DPC calls the handler for
 * as long as it reads anything pending. So nothing is raised for a title
 * without such a wrapper (nv2a_title_acks_pgraph): an interrupt nothing can
 * clear would hold its DPC in that loop for good.
 *
 * The two status bits a software method raises -- PGRAPH_INTR's error bit
 * and PMC_INTR_0's PGRAPH bit -- are set here and cleared by the title's
 * handler (the port's wrapper on it), both on this thread. The GPU thread
 * completes the same two registers for the other sources (vblank_intr_tick
 * and nv2a_ack_thread in xbox_memory_layout.c) and leaves these bits alone
 * while software methods are delivered (xbox_nv2a_swm_on); every change to
 * the registers is one atomic operation. It used to clear them whole when
 * nothing was in flight, and between its asking and its clearing, one could
 * be raised: the DPC then never called the handler, or the handler found
 * nothing to acknowledge, nothing more was delivered, and D3D slept for good
 * on the NOP(5) it had been sent. tests/nv2a_queue loses one that way within
 * a few hundred.
 *
 * Only the parameters a title is known to need are delivered -- JSRF's 5 --
 * because the others were never delivered either and the rest of the runtime
 * may already stand in for what their handlers do (parameter 1's, for one,
 * counts flips). RECOMP_NV2A_SWM=all delivers every one, =0 none.
 */
#define NV2A_PGRAPH_INTR       0x00400100u
#define NV2A_PGRAPH_NSOURCE    0x00400108u
#define NV2A_PGRAPH_TRAP_ADDR  0x00400704u
#define NV2A_PGRAPH_TRAP_DATA  0x00400708u
#define NV2A_PGRAPH_INTR_ERROR (1u << 20)
#define NV2A_PMC_INTR_PGRAPH   (1u << 12)
#define SWM_QUEUE 64u
/* Overridden (strong) by a port that acknowledges PGRAPH interrupts. */
__attribute__((weak)) int nv2a_title_acks_pgraph(void) { return 0; }
static struct { uint32_t subch, param; } g_swm_q[SWM_QUEUE];
static volatile uint32_t g_swm_head, g_swm_tail;
static volatile int      g_swm_in_flight;
static unsigned long     g_swm_raised, g_swm_dropped;

static int swm_mode(void)
{
    static int mode = -1;                 /* 0 none, 1 the known ones, 2 all */
    if (mode < 0) {
        const char *e = getenv("RECOMP_NV2A_SWM");
        mode = !nv2a_title_acks_pgraph() ? 0
             : (e && !strcmp(e, "all")) ? 2 : (e && e[0] == '0') ? 0 : 1;
    }
    return mode;
}

static int swm_wanted(uint32_t param)
{
    int mode = swm_mode();
    return mode == 2 || (mode == 1 && param == 5);
}

int xbox_nv2a_swm_on(void) { return swm_mode() != 0; }

int xbox_nv2a_swm_in_flight(void) { return g_swm_in_flight; }

void xbox_nv2a_software_method(uint32_t subch, uint32_t param)
{
    uint32_t h = g_swm_head;
    if (!swm_wanted(param)) return;
    if (h - __atomic_load_n(&g_swm_tail, __ATOMIC_ACQUIRE) >= SWM_QUEUE) {
        g_swm_dropped++;
        return;
    }
    g_swm_q[h % SWM_QUEUE].subch = subch;
    g_swm_q[h % SWM_QUEUE].param = param;
    __atomic_store_n(&g_swm_head, h + 1, __ATOMIC_RELEASE);
}

void xbox_nv2a_software_method_done(void)
{
    if (!g_swm_in_flight) return;
    __atomic_store_n(&g_swm_tail, g_swm_tail + 1, __ATOMIC_RELEASE);
    g_swm_in_flight = 0;
}

void kernel_pgraph_tick(void)
{
    uint32_t t = g_swm_tail;
    int claimed;
    if (g_swm_in_flight || t == __atomic_load_n(&g_swm_head, __ATOMIC_ACQUIRE)) return;
    if (!xbox_GetConnectedInterrupt(NV2A_VECTOR)) return;
    BRIDGE_MEM32(XBOX_NV2A_REG_BASE + NV2A_PGRAPH_TRAP_ADDR) =
        0x100u | (g_swm_q[t % SWM_QUEUE].subch << 16);
    BRIDGE_MEM32(XBOX_NV2A_REG_BASE + NV2A_PGRAPH_TRAP_DATA) = g_swm_q[t % SWM_QUEUE].param;
    BRIDGE_MEM32(XBOX_NV2A_REG_BASE + NV2A_PGRAPH_NSOURCE) = 1u;   /* a notification */
    g_swm_in_flight = 1;
    __atomic_fetch_or(&BRIDGE_MEM32(XBOX_NV2A_REG_BASE + NV2A_PGRAPH_INTR),
                      NV2A_PGRAPH_INTR_ERROR, __ATOMIC_SEQ_CST);
    __atomic_fetch_or(&BRIDGE_MEM32(XBOX_NV2A_REG_BASE + NV2A_PMC_INTR_0),
                      NV2A_PMC_INTR_PGRAPH, __ATOMIC_SEQ_CST);
    claimed = kernel_raise_interrupt(NV2A_VECTOR);
    g_swm_raised++;
    if (g_swm_raised <= 3 || claimed <= 0) {
        static unsigned said;
        if (said++ < 8)
            fprintf(stderr, "  [NV2A] software method %u (#%lu) -> ISR %s\n",
                    g_swm_q[t % SWM_QUEUE].param, g_swm_raised,
                    claimed < 0 ? "not callable" : claimed ? "claimed it" : "declined it");
        fflush(stderr);
    }
}

/* For the report. */
void xbox_nv2a_swm_stats(unsigned long *raised, unsigned long *dropped)
{
    if (raised) *raised = g_swm_raised;
    if (dropped) *dropped = g_swm_dropped;
}
