/**
 * nv2a_intr.c - The NV2A's interrupts: its interrupt registers, the display's
 * vertical blank and the GPU's software methods
 *
 * The NV2A's interrupt registers, which clear at the title's write as the
 * console's do (xbox_nv2a_intr_read and _write, which recomp_mmio_read and
 * _write in kernel_mmio.c ask first), and the two interrupts the runtime
 * raises on the NV2A's vector, on every pass of the kernel's timer thread
 * (kernel_timer.c): the vertical blank, the guest's frame clock, and the
 * software methods the push-buffer executor queues
 * (xbox_nv2a_software_method), with their counts for the freeze report.
 *
 * In a file of its own, out of kernel_bridge.c, so that it can be linked and
 * tested on its own (tests/vblank_ack).
 */

#include "kernel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xbox_memory_layout.h"
#include "kernel_bridge.h"

/* The interrupt registers, as the console has them.
 *
 * Each of the NV2A's units has an interrupt status register, with an enable
 * register 0x40 after it. The unit sets a bit in its status when something
 * happens, and the bit stays set until the driver writes a 1 to it: a write
 * clears the bits written as 1 and leaves the rest (write-1-to-clear). The
 * enables are stored as written. PMC_INTR_0 isn't stored at all: it has a
 * bit for each unit, set while that unit's status and enable share a bit,
 * and the line to the CPU is up while PMC_INTR_0 and PMC_INTR_EN_0 are both
 * non-zero. That is xemu's model (each unit's `pending_interrupts &= ~val`,
 * and nv2a_update_irq in hw/xbox/nv2a/nv2a.c), and it is what D3D's code
 * expects: each of its writes to a status register is an acknowledgement,
 * and each of its reads of PMC_INTR_0 asks which unit has an enabled
 * interrupt pending.
 *
 * Here the registers are words in the NV2A's aperture, and the lifted code
 * reaches them through recomp_mmio_read and _write (kernel_mmio.c), which ask
 * this model first. The runtime raises interrupts on the timer thread
 * (xbox_nv2a_intr_raise) while the title acknowledges them and sets the
 * enables on its own threads, so every access to these registers here is one
 * atomic operation, and neither side can undo the other's change. So is every
 * access to PGRAPH_FIFO, whose access bit says when the title's handler is
 * done with a software method (below). PBUS's status follows the same rule,
 * though nothing here raises it; PVIDEO's, which D3D's DPC also acknowledges,
 * isn't routed through PMC_INTR_0 by xemu and isn't here either.
 */
#define XBOX_NV2A_REG_BASE     0xFD000000u
#define XBOX_NV2A_REG_SPAN     0x01000000u
#define NV2A_PMC_INTR_0        0x00000100u
#define NV2A_PMC_INTR_EN_0     0x00000140u
#define NV2A_PMC_INTR_PFIFO    (1u << 8)
#define NV2A_PMC_INTR_PGRAPH   (1u << 12)
#define NV2A_PMC_INTR_PTIMER   (1u << 20)
#define NV2A_PMC_INTR_PCRTC    (1u << 24)
#define NV2A_PMC_INTR_PBUS     (1u << 28)
#define NV2A_PBUS_INTR_0       0x00001100u
#define NV2A_PFIFO_INTR_0      0x00002100u
#define NV2A_PTIMER_INTR_0     0x00009100u
#define NV2A_PGRAPH_INTR       0x00400100u
#define NV2A_PCRTC_INTR_0      0x00600100u
#define NV2A_INTR_EN(status)   ((status) + 0x40u)
#define NV2A_PGRAPH_FIFO       0x00400720u
#define NV2A_VECTOR            3u

#define NV2A_REG(offset) BRIDGE_MEM32(XBOX_NV2A_REG_BASE + (offset))

static const struct { uint32_t status, pmc; } NV2A_INTR_UNITS[] = {
    { NV2A_PFIFO_INTR_0,  NV2A_PMC_INTR_PFIFO  },
    { NV2A_PGRAPH_INTR,   NV2A_PMC_INTR_PGRAPH },
    { NV2A_PTIMER_INTR_0, NV2A_PMC_INTR_PTIMER },
    { NV2A_PCRTC_INTR_0,  NV2A_PMC_INTR_PCRTC  },
    { NV2A_PBUS_INTR_0,   NV2A_PMC_INTR_PBUS   },
};
#define NV2A_INTR_UNIT_COUNT (sizeof NV2A_INTR_UNITS / sizeof NV2A_INTR_UNITS[0])

void xbox_nv2a_intr_raise(uint32_t status, uint32_t bits)
{
    __atomic_fetch_or(&NV2A_REG(status), bits, __ATOMIC_SEQ_CST);
}

/* PMC_INTR_0, worked out. */
static uint32_t intr_pending(void)
{
    uint32_t pmc = 0;
    size_t i;
    for (i = 0; i < NV2A_INTR_UNIT_COUNT; i++) {
        uint32_t status = NV2A_INTR_UNITS[i].status;
        if (__atomic_load_n(&NV2A_REG(status), __ATOMIC_SEQ_CST)
            & __atomic_load_n(&NV2A_REG(NV2A_INTR_EN(status)), __ATOMIC_SEQ_CST))
            pmc |= NV2A_INTR_UNITS[i].pmc;
    }
    return pmc;
}

/* What the model does with the register at an offset. */
enum { INTR_NOT_OURS, INTR_PMC, INTR_STATUS, INTR_STORED };
static int intr_kind(uint32_t offset)
{
    size_t i;
    if (offset == NV2A_PMC_INTR_0)
        return INTR_PMC;
    if (offset == NV2A_PMC_INTR_EN_0 || offset == NV2A_PGRAPH_FIFO)
        return INTR_STORED;
    for (i = 0; i < NV2A_INTR_UNIT_COUNT; i++) {
        if (offset == NV2A_INTR_UNITS[i].status)
            return INTR_STATUS;
        if (offset == NV2A_INTR_EN(NV2A_INTR_UNITS[i].status))
            return INTR_STORED;
    }
    return INTR_NOT_OURS;
}

/* The bits of the 32-bit register an access of `size` bytes at `addr`
 * covers, and where they start; 0 if it crosses into the next register. */
static uint32_t intr_lanes(uint32_t addr, unsigned size, unsigned *shift)
{
    unsigned byte = addr & 3u;
    if (size == 0 || byte + size > 4u)
        return 0;
    *shift = byte * 8u;
    return size >= 4u ? 0xFFFFFFFFu : ((1u << (size * 8u)) - 1u) << *shift;
}

int xbox_nv2a_intr_read(uint32_t addr, unsigned size, uint32_t *val)
{
    uint32_t offset = (addr & ~3u) - XBOX_NV2A_REG_BASE, lanes, v;
    unsigned shift;
    int kind;
    if (offset >= XBOX_NV2A_REG_SPAN || !(kind = intr_kind(offset)))
        return 0;
    if (!(lanes = intr_lanes(addr, size, &shift)))
        return 0;
    v = kind == INTR_PMC ? intr_pending()
                         : __atomic_load_n(&NV2A_REG(offset), __ATOMIC_SEQ_CST);
    *val = (v & lanes) >> shift;
    return 1;
}

int xbox_nv2a_intr_write(uint32_t addr, uint32_t val, unsigned size)
{
    uint32_t offset = (addr & ~3u) - XBOX_NV2A_REG_BASE, lanes, bits, old;
    unsigned shift;
    int kind;
    if (offset >= XBOX_NV2A_REG_SPAN || !(kind = intr_kind(offset)))
        return 0;
    if (!(lanes = intr_lanes(addr, size, &shift)))
        return 0;
    bits = (val << shift) & lanes;
    switch (kind) {
    case INTR_STATUS:
        __atomic_fetch_and(&NV2A_REG(offset), ~bits, __ATOMIC_SEQ_CST);
        break;
    case INTR_STORED:
        old = __atomic_load_n(&NV2A_REG(offset), __ATOMIC_SEQ_CST);
        while (!__atomic_compare_exchange_n(&NV2A_REG(offset), &old, (old & ~lanes) | bits,
                                            0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
            ;
        break;
    default:
        break;                          /* PMC_INTR_0: worked out, not stored */
    }
    return 1;
}

/* The GPU's vertical blank, delivered rather than merely enabled.
 *
 * The D3D8 library linked into a title installs an ISR for this and then waits
 * on it. Nothing ever raised it, so a title whose frame loop waits for vblank
 * rather than polling stops after its first clear -- which is exactly where
 * Half-Life 2's loader stops, with its videos open, its 23 MB of UI textures
 * loaded, and no second frame.
 *
 * D3D's DPC reads PMC_INTR_0 to find out which unit interrupted, so the
 * vblank is raised in the registers before the routine is called:
 * PCRTC_INTR_0's bit 0, which makes PMC_INTR_0 say PCRTC (bit 24) while
 * PCRTC_INTR_EN_0, which D3D sets at start-up, has it too. D3D's vblank
 * handler acknowledges it by writing 1 to PCRTC_INTR_0, and loops until
 * PMC_INTR_0 no longer says PCRTC, which is at that first write.
 *
 * ponytail: a fixed 60 Hz off the timer tick rather than anything tied to the
 * display mode, and no field or interlace handling. A title that measures
 * refresh rate from this will read 60; a title that needs the real one wants
 * the mode AvSetDisplayMode was given, which is recorded a few files away.
 */
#define NV2A_PCRTC_INTR_VBLANK (1u << 0)

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

    xbox_nv2a_intr_raise(NV2A_PCRTC_INTR_0, NV2A_PCRTC_INTR_VBLANK);

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
    *enabled = __atomic_load_n(&NV2A_REG(NV2A_PMC_INTR_EN_0), __ATOMIC_SEQ_CST);
}

/* The GPU's software methods, delivered.
 *
 * NV097_NO_OPERATION with a non-zero parameter is how D3D asks the GPU to call
 * it back: the GPU stops on the method and raises a PGRAPH interrupt -- an
 * "error" whose trap registers hold the method (0x100) and the parameter --
 * and D3D's handler (CMiniport::ServiceGrInterrupt, 0x00194210 in JSRF) does
 * what the parameter says. Parameter 5 sets the push buffer's "busy block"
 * event, and the make-space path for a request of 32 KB or more inserts a
 * NOP(5) and sleeps on that event. Nothing here raised the interrupt, so once
 * in a while -- a big enough request at the wrong moment -- the game slept for
 * good: the main thread in KeWaitForSingleObject from 0x0019151B, the push
 * buffer drained (GET == PUT), the picture stopped.
 *
 * The push-buffer executor queues each one (xbox_nv2a_software_method) and
 * this tick, on the thread that delivers the other interrupts, raises the
 * oldest: trap registers, then PGRAPH_INTR's error bit, then the NV2A vector.
 * One at a time, as on the console, where the GPU stays stopped on the method
 * until the handler is done with it. D3D's handler turns PGRAPH's FIFO access
 * off (PGRAPH_FIFO, bit 0), acknowledges the error bit, and only then reads
 * the parameter from the trap registers and acts on it; then it turns FIFO
 * access back on. So a software method is done once its error bit is clear
 * and FIFO access is on again, and the next is raised only then: raised at
 * the acknowledgement, it would overwrite the parameter the handler has yet
 * to read. xemu holds PGRAPH the same way (waiting_for_nop, and the FIFO
 * isn't read while can_fifo_access is off). D3D calls the handler from its
 * DPC and also from loops on the title's own threads that poll PGRAPH_INTR,
 * and either may be the one that acknowledges.
 *
 * Only the parameters a title is known to need are delivered -- JSRF's 5 --
 * because the others were never delivered either and the rest of the runtime
 * may already stand in for what their handlers do (parameter 1's, for one,
 * counts flips). RECOMP_NV2A_SWM=all delivers every one, =0 none.
 */
#define NV2A_PGRAPH_NSOURCE    0x00400108u
#define NV2A_PGRAPH_TRAP_ADDR  0x00400704u
#define NV2A_PGRAPH_TRAP_DATA  0x00400708u
#define NV2A_PGRAPH_FIFO_ACCESS (1u << 0)
#define NV2A_PGRAPH_INTR_ERROR (1u << 20)
#define SWM_QUEUE 64u
static struct { uint32_t subch, param; } g_swm_q[SWM_QUEUE];
static volatile uint32_t g_swm_head, g_swm_tail;
static volatile int      g_swm_in_flight;
static unsigned long     g_swm_raised, g_swm_dropped;

static int swm_mode(void)
{
    static int mode = -1;                 /* 0 none, 1 the known ones, 2 all */
    if (mode < 0) {
        const char *e = getenv("RECOMP_NV2A_SWM");
        mode = (e && !strcmp(e, "all")) ? 2 : (e && e[0] == '0') ? 0 : 1;
    }
    return mode;
}

static int swm_wanted(uint32_t param)
{
    int mode = swm_mode();
    return mode == 2 || (mode == 1 && param == 5);
}

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

/* The one in flight is done: its error bit is clear and FIFO access is on.
 * Read in that order. The handler turns access off before it acknowledges,
 * so access seen on after the bit was seen clear is the handler's end, not
 * the moment before its start. */
static int swm_done(void)
{
    if (__atomic_load_n(&NV2A_REG(NV2A_PGRAPH_INTR), __ATOMIC_SEQ_CST)
        & NV2A_PGRAPH_INTR_ERROR)
        return 0;
    return (__atomic_load_n(&NV2A_REG(NV2A_PGRAPH_FIFO), __ATOMIC_SEQ_CST)
            & NV2A_PGRAPH_FIFO_ACCESS) != 0;
}

void kernel_pgraph_tick(void)
{
    uint32_t t = g_swm_tail;
    int claimed;
    if (g_swm_in_flight) {
        if (!swm_done()) return;
        __atomic_store_n(&g_swm_tail, ++t, __ATOMIC_RELEASE);
        g_swm_in_flight = 0;
    }
    if (t == __atomic_load_n(&g_swm_head, __ATOMIC_ACQUIRE)) return;
    if (!xbox_GetConnectedInterrupt(NV2A_VECTOR)) return;
    NV2A_REG(NV2A_PGRAPH_TRAP_ADDR) = 0x100u | (g_swm_q[t % SWM_QUEUE].subch << 16);
    NV2A_REG(NV2A_PGRAPH_TRAP_DATA) = g_swm_q[t % SWM_QUEUE].param;
    NV2A_REG(NV2A_PGRAPH_NSOURCE) = 1u;   /* a notification */
    g_swm_in_flight = 1;
    xbox_nv2a_intr_raise(NV2A_PGRAPH_INTR, NV2A_PGRAPH_INTR_ERROR);
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

/* The software methods now, for the freeze report. On the timer thread, as
 * kernel_pgraph_tick is. */
void kernel_swm_counts(unsigned long *raised, unsigned long *waiting, int *in_flight,
                       uint32_t *param, uint32_t *pgraph_intr, uint32_t *pgraph_fifo)
{
    uint32_t t = __atomic_load_n(&g_swm_tail, __ATOMIC_ACQUIRE);
    *raised = g_swm_raised;
    *in_flight = g_swm_in_flight;
    *waiting = __atomic_load_n(&g_swm_head, __ATOMIC_ACQUIRE) - t - (*in_flight ? 1u : 0u);
    *param = g_swm_q[t % SWM_QUEUE].param;
    *pgraph_intr = __atomic_load_n(&NV2A_REG(NV2A_PGRAPH_INTR), __ATOMIC_SEQ_CST);
    *pgraph_fifo = __atomic_load_n(&NV2A_REG(NV2A_PGRAPH_FIFO), __ATOMIC_SEQ_CST);
}
