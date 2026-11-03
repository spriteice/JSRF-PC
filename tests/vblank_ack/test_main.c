/* Run with: python tests/vblank_ack/run.py
 *
 * The NV2A's interrupt registers and the two interrupts the runtime raises
 * on them, the display's vertical blank and the GPU's software methods
 * (b1.4 row 11): src/kernel/nv2a_intr.c and src/kernel/kernel_mmio.c, linked
 * as they are, with no GPU thread. The game's side is JSRF's D3D, written out
 * in C below from its code in default.xbe, reading and writing the registers
 * through recomp_mmio_read and _write as the lifted code does.
 *
 *   1. The registers: a write of 1 to a status register clears that bit, a
 *      write of 0 doesn't, a byte write clears its byte, PMC_INTR_0 follows
 *      each unit's status and enable, a write to it changes nothing, and
 *      D3D's start-up leaves nothing pending.
 *   2. A vblank is done in one pass of D3D's handler: raised by
 *      kernel_vblank_tick, claimed by D3D's interrupt routine, and handled by
 *      its DPC, whose handler's first acknowledgement ends its loop. With the
 *      registers as plain memory that loop waited for a GPU thread to clear
 *      them, and here there is none.
 *   3. Software methods, raised by kernel_pgraph_tick and handled by D3D's
 *      PGRAPH handler: the next isn't raised between the handler's
 *      acknowledgement and its read of the parameter; then thousands, handled
 *      from the DPC and at the same time from a polling loop on a game
 *      thread, the two under one guest lock as in the runtime, while the
 *      timer thread raises without it. Each must reach D3D's dispatcher
 *      once, with its own parameter, in order.
 *   4. A race: two threads raise bits in one status register while a third
 *      acknowledges what it reads, and every bit raised is acknowledged
 *      exactly once.
 *
 * Each case says whether it passed, and the run fails if any didn't.
 */
#include "kernel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xbox_memory_layout.h"
#include "kernel_bridge.h"

uint32_t recomp_mmio_read(uint32_t addr, unsigned size);
void recomp_mmio_write(uint32_t addr, uint32_t val, unsigned size);
void xbox_nv2a_software_method(uint32_t subch, uint32_t param);
extern unsigned long g_vblank_count;
/* Weak, so that the test also builds against code without the register
 * model, and fails there rather than not building. */
extern void xbox_nv2a_intr_raise(uint32_t status, uint32_t bits) __attribute__((weak));

#define NV2A            0xFD000000u
#define REGS_BYTES      0x00700000u      /* up to PCRTC's, past 0x600140 */
#define PMC_INTR_0      0x00000100u
#define PMC_INTR_EN_0   0x00000140u
#define PMC_ENABLE      0x00000200u
#define PBUS_INTR_0     0x00001100u
#define PFIFO_INTR_0    0x00002100u
#define PFIFO_INTR_EN_0 0x00002140u
#define PTIMER_INTR_0   0x00009100u
#define PTIMER_INTR_EN_0 0x00009140u
#define PGRAPH_INTR     0x00400100u
#define PGRAPH_NSOURCE  0x00400108u
#define PGRAPH_INTR_EN  0x00400140u
#define PGRAPH_TRAP_ADDR 0x00400704u
#define PGRAPH_TRAP_DATA 0x00400708u
#define PGRAPH_FIFO     0x00400720u
#define PCRTC_INTR_0    0x00600100u
#define PCRTC_INTR_EN_0 0x00600140u
#define PMC_PFIFO       (1u << 8)
#define PMC_PGRAPH      (1u << 12)
#define PMC_PTIMER      (1u << 20)
#define PMC_PCRTC       (1u << 24)
#define PMC_PBUS        (1u << 28)
#define PGRAPH_ERROR    (1u << 20)
#define NV2A_VECTOR     3u
#define D3D_INTR_ENABLE 1u               /* what D3D keeps for PMC_INTR_EN_0 */
#define LOOP_LIMIT      100000           /* writes before a loop is stuck */

/* ---- What the runtime gives the code under test ------------------------ */

ptrdiff_t g_xbox_mem_offset;
static uint8_t *s_regs;                  /* the NV2A's registers, from 0 */

ptrdiff_t xbox_GetMemoryOffset(void) { return g_xbox_mem_offset; }
void xbox_Nv2aRegisterWritten(uint32_t addr, unsigned size) { (void)addr; (void)size; }
uint32_t xbox_GetConnectedInterrupt(uint32_t vector) { return vector == NV2A_VECTOR; }

/* The guest lock: the runtime runs guest code, and so D3D's interrupt
 * routine, its DPC and its polling loops, under one lock. The timer thread
 * raises an interrupt without it and takes it to call the routine, unless it
 * holds it already, as xbox_guest_lock_acquire does. */
static volatile LONG s_guest_lock;
static RECOMP_TLS int t_holds;
static void guest_lock(void)
{
    while (__atomic_exchange_n(&s_guest_lock, 1, __ATOMIC_ACQUIRE))
        SwitchToThread();
    t_holds = 1;
}
static void guest_unlock(void)
{
    t_holds = 0;
    __atomic_store_n(&s_guest_lock, 0, __ATOMIC_RELEASE);
}

static uint32_t rd(uint32_t reg) { return recomp_mmio_read(NV2A + reg, 4); }
static void wr(uint32_t reg, uint32_t v) { recomp_mmio_write(NV2A + reg, v, 4); }

/* ---- JSRF's D3D, as far as these interrupts go -------------------------- */

/* Start-up: 0x0019460A turns PCRTC's and PTIMER's interrupts off;
 * InitHardware (0x00194780 to 0x001948AB) sets PMC_ENABLE and PMC_INTR_EN_0,
 * turns PGRAPH's FIFO access off, clears PGRAPH_INTR and PFIFO_INTR_0 by
 * writing every bit and enables them; the context switch to channel 2
 * (0x00197AAC, from 0x00195043) turns FIFO access back on; 0x00196E80 clears
 * the vblank and enables it. */
#define D3D_PFIFO_ENABLES 0x11111111u    /* from D3D's configuration */
static void d3d_startup(void)
{
    wr(PCRTC_INTR_EN_0, 0);
    wr(PTIMER_INTR_EN_0, 0);
    wr(PMC_ENABLE, 0xFFFFFFFFu);
    wr(PMC_INTR_EN_0, D3D_INTR_ENABLE);
    wr(PGRAPH_FIFO, 0);
    wr(PGRAPH_INTR, 0xFFFFFFFFu);
    wr(PGRAPH_INTR_EN, 0xFFFFFFFFu);
    wr(PFIFO_INTR_0, 0xFFFFFFFFu);
    wr(PFIFO_INTR_EN_0, D3D_PFIFO_ENABLES);
    wr(PGRAPH_FIFO, rd(PGRAPH_FIFO) | 1u);
    wr(PCRTC_INTR_0, 1);
    wr(PCRTC_INTR_EN_0, 1);
}

/* The software-method dispatcher (0x00193F70): what each parameter does.
 * Here it records them, in the order they came. */
static volatile LONG s_dispatched;       /* how many, read by other threads */
static uint32_t s_expect = 1, s_wrong, s_wrong_got, s_wrong_want;
static int s_from_poll;                  /* the handler was called by the poll */
static volatile LONG s_by_poll, s_by_dpc;
static void d3d_dispatch(uint32_t param)
{
    if (param != s_expect && !s_wrong++) { s_wrong_got = param; s_wrong_want = s_expect; }
    s_expect = param + 1;
    __atomic_add_fetch(s_from_poll ? &s_by_poll : &s_by_dpc, 1, __ATOMIC_RELAXED);
    __atomic_add_fetch(&s_dispatched, 1, __ATOMIC_RELEASE);
}

/* CMiniport::ServiceGrInterrupt (0x00194210), for a trapped method: FIFO
 * access off; PGRAPH_INTR, the trapped method and its source read; the
 * acknowledgement, PGRAPH_INTR written back as read (0x0019427D); only then
 * the parameter (0x0019428F) and, for NO_OPERATION, the dispatcher; FIFO
 * access on (0x001942E3); PGRAPH_INTR returned as it reads then. Its
 * context-switch branch (bit 12) is left out: nothing raises it. */
static void (*s_after_ack)(void);        /* case 3's look between the two */
static uint32_t d3d_pgraph(void)
{
    uint32_t intr, method, source;
    wr(PGRAPH_FIFO, 0);
    intr = rd(PGRAPH_INTR);
    method = rd(PGRAPH_TRAP_ADDR) & 0x1FFCu;
    source = rd(PGRAPH_NSOURCE);
    wr(PGRAPH_INTR, intr);
    if (s_after_ack) s_after_ack();
    if (source && (intr & 0x00100001u)) {
        uint32_t param = rd(PGRAPH_TRAP_DATA);
        if (!(source & 0x40u) && method == 0x100u)
            d3d_dispatch(param);
    }
    wr(PGRAPH_FIFO, 1);
    return rd(PGRAPH_INTR);
}

/* The vblank handler (0x00193D90): its frame work, then from 0x00193E40 the
 * acknowledgement, written for as long as PMC_INTR_0 says PCRTC. */
static unsigned long s_vblanks_handled, s_vblank_writes;
static int s_vblank_stuck;
static uint32_t d3d_vblank(void)
{
    int n = 0;
    s_vblanks_handled++;
    do {
        wr(PCRTC_INTR_0, 1);
        s_vblank_writes++;
        if (++n == LOOP_LIMIT) { s_vblank_stuck = 1; break; }
    } while (rd(PMC_INTR_0) & PMC_PCRTC);
    return 0;
}

/* The interrupt routine (0x00193C50): claimed while PMC_INTR_EN_0 isn't 0,
 * which it masks; then its DPC is queued. */
static int s_dpc_queued;
static int d3d_isr(void)
{
    if (!rd(PMC_INTR_EN_0)) return 0;
    wr(PMC_INTR_EN_0, 0);
    s_dpc_queued = 1;
    return 1;
}

/* The DPC (0x00194480): PMC_INTR_0 read, each unit's handler called, round
 * again while any returned non-zero; then PMC_INTR_EN_0 restored. */
static int s_dpc_stuck;
static void d3d_dpc(void)
{
    uint32_t again;
    int n = 0;
    do {
        uint32_t pmc = rd(PMC_INTR_0);
        again = 0;
        if (pmc & PMC_PGRAPH)
            again = d3d_pgraph();
        if (pmc & ~PMC_PGRAPH) {
            if (pmc & PMC_PTIMER) { wr(PTIMER_INTR_0, 1); again |= rd(PTIMER_INTR_0); }
            if (pmc & PMC_PCRTC) again |= d3d_vblank();
        }
        if (++n == LOOP_LIMIT) { s_dpc_stuck = 1; break; }
    } while (again);
    wr(PMC_INTR_EN_0, D3D_INTR_ENABLE);
}

/* A polling loop on a game thread (0x00194A72's body): the PGRAPH handler
 * when PGRAPH_INTR isn't 0, the vblank handler when PMC_INTR_0 says PCRTC. */
static void d3d_poll(void)
{
    if (rd(PGRAPH_INTR)) {
        s_from_poll = 1;
        d3d_pgraph();
        s_from_poll = 0;
    }
    if (rd(PMC_INTR_0) & PMC_PCRTC)
        d3d_vblank();
}

/* The runtime's side: the routine called under the guest lock, its DPC run
 * after the timer thread's ticks (kernel_drain_dpcs), under it too. */
int kernel_raise_interrupt(uint32_t vector)
{
    int claimed, took = !t_holds;
    if (vector != NV2A_VECTOR) return -1;
    if (took) guest_lock();
    claimed = d3d_isr();
    if (took) guest_unlock();
    return claimed;
}
static void drain_dpcs(void)
{
    guest_lock();
    if (s_dpc_queued) {
        s_dpc_queued = 0;
        d3d_dpc();
    }
    guest_unlock();
}

/* ---- The cases ---------------------------------------------------------- */

static int s_failed;
static int check(int ok, const char *what)
{
    if (!ok) { printf("  FAIL: %s\n", what); fflush(stdout); }
    return ok;
}
static void result(const char *name, int ok)
{
    printf("%s: %s\n", name, ok ? "pass" : "FAIL");
    fflush(stdout);
    if (!ok) s_failed = 1;
}

static void fresh_registers(void)
{
    memset(s_regs, 0, REGS_BYTES);
}

/* 1 */
static int registers(void)
{
    int ok = 1;
    fresh_registers();
    if (!check(xbox_nv2a_intr_raise != NULL, "no xbox_nv2a_intr_raise: no register model"))
        return 0;
    wr(PGRAPH_INTR_EN, 0xFFFFFFFFu);
    xbox_nv2a_intr_raise(PGRAPH_INTR, PGRAPH_ERROR | 1u);
    ok &= check(rd(PGRAPH_INTR) == (PGRAPH_ERROR | 1u), "a raised bit doesn't read back");
    ok &= check(rd(PMC_INTR_0) == PMC_PGRAPH, "PMC_INTR_0 doesn't say PGRAPH");
    wr(PGRAPH_INTR, 0);
    ok &= check(rd(PGRAPH_INTR) == (PGRAPH_ERROR | 1u), "a write of 0 changed the status");
    wr(PGRAPH_INTR, PGRAPH_ERROR);
    ok &= check(rd(PGRAPH_INTR) == 1u, "a write of 1 didn't clear just its bit");
    recomp_mmio_write(NV2A + PGRAPH_INTR, 1u, 1);
    ok &= check(rd(PGRAPH_INTR) == 0, "a byte write didn't clear");
    ok &= check(rd(PMC_INTR_0) == 0, "PMC_INTR_0 still says PGRAPH");
    xbox_nv2a_intr_raise(PCRTC_INTR_0, 0x00000101u);
    recomp_mmio_write(NV2A + PCRTC_INTR_0 + 1, 0x01u, 1);
    ok &= check(rd(PCRTC_INTR_0) == 1u, "a byte write to the second byte didn't clear its bit alone");
    ok &= check(!(rd(PMC_INTR_0) & PMC_PCRTC), "PMC_INTR_0 says PCRTC, which isn't enabled");
    wr(PCRTC_INTR_EN_0, 1u);
    ok &= check(rd(PMC_INTR_0) == PMC_PCRTC, "PMC_INTR_0 doesn't follow PCRTC's enable");
    ok &= check(recomp_mmio_read(NV2A + PMC_INTR_0 + 3, 1) == (PMC_PCRTC >> 24),
                "a byte read of PMC_INTR_0 isn't its byte");
    wr(PMC_INTR_0, 0xFFFFFFFFu);
    ok &= check(rd(PMC_INTR_0) == PMC_PCRTC && rd(PCRTC_INTR_0) == 1u,
                "a write to PMC_INTR_0 changed something");
    /* D3D's start-up, with every unit's status raised before it. */
    xbox_nv2a_intr_raise(PFIFO_INTR_0, 0xFFFFFFFFu);
    xbox_nv2a_intr_raise(PGRAPH_INTR, 0xFFFFFFFFu);
    xbox_nv2a_intr_raise(PTIMER_INTR_0, 1u);
    xbox_nv2a_intr_raise(PBUS_INTR_0, 1u);
    wr(PTIMER_INTR_EN_0, 1u);
    d3d_startup();
    ok &= check(rd(PMC_INTR_0) == 0, "something is pending after D3D's start-up");
    ok &= check(!rd(PGRAPH_INTR) && !rd(PFIFO_INTR_0) && !rd(PCRTC_INTR_0),
                "a status D3D's start-up clears is still set");
    ok &= check(rd(PGRAPH_FIFO) == 1u && rd(PMC_INTR_EN_0) == D3D_INTR_ENABLE,
                "an enable isn't stored as written");
    return ok;
}

/* 2 */
static int one_vblank(void)
{
    int ok = 1, claimed;
    unsigned long raised = g_vblank_count;
    fresh_registers();
    d3d_startup();
    s_vblanks_handled = s_vblank_writes = 0;
    s_vblank_stuck = s_dpc_stuck = 0;
    kernel_vblank_tick();               /* the first is due at once */
    ok &= check(g_vblank_count == raised + 1, "kernel_vblank_tick raised nothing");
    claimed = s_dpc_queued;
    ok &= check(claimed, "D3D's interrupt routine didn't claim it");
    ok &= check(rd(PMC_INTR_0) == PMC_PCRTC, "PMC_INTR_0 doesn't say PCRTC before the DPC");
    drain_dpcs();
    ok &= check(s_vblanks_handled == 1, "the DPC didn't call the vblank handler once");
    if (!check(!s_vblank_stuck, "the vblank handler's acknowledgement never ended its loop"))
        printf("        %lu writes of PCRTC_INTR_0, and PMC_INTR_0 still says PCRTC\n",
               s_vblank_writes);
    else
        ok &= check(s_vblank_writes == 1, "the vblank handler wrote its acknowledgement more than once");
    ok &= check(!s_dpc_stuck, "the DPC never ended");
    ok &= check(rd(PMC_INTR_0) == 0 && rd(PCRTC_INTR_0) == 0, "the vblank is still pending after the DPC");
    ok &= check(rd(PMC_INTR_EN_0) == D3D_INTR_ENABLE, "the DPC didn't restore PMC_INTR_EN_0");
    printf("  one vblank: claimed %s, %lu write%s of the acknowledgement\n",
           claimed ? "yes" : "no", s_vblank_writes, s_vblank_writes == 1 ? "" : "s");
    return ok && !s_vblank_stuck;
}

/* 3, the first part: the timer thread's tick between the handler's
 * acknowledgement and its read of the parameter. It comes inside the DPC
 * here, so the routine it calls declines (the DPC has masked it), and the
 * DPC goes round again for whatever it raised. */
static void tick_after_ack(void) { kernel_pgraph_tick(); }

static int swm_after_ack(void)
{
    int ok = 1;
    fresh_registers();
    d3d_startup();
    s_expect = 1; s_wrong = 0;
    s_dpc_stuck = 0;
    xbox_nv2a_software_method(0, 1u);
    xbox_nv2a_software_method(0, 2u);
    kernel_pgraph_tick();               /* raises 1 */
    ok &= check(s_dpc_queued, "the first wasn't raised, or D3D's routine didn't claim it");
    s_after_ack = tick_after_ack;
    drain_dpcs();                       /* the tick in it must not raise 2 */
    s_after_ack = NULL;
    ok &= check(__atomic_load_n(&s_dispatched, __ATOMIC_ACQUIRE) >= 1, "the first never reached the dispatcher");
    ok &= check(!s_wrong, "the dispatcher was given the next one's parameter");
    kernel_pgraph_tick();               /* the first done: raises 2 */
    drain_dpcs();
    ok &= check(__atomic_load_n(&s_dispatched, __ATOMIC_ACQUIRE) == 2 && !s_wrong && s_expect == 3,
                "the two didn't reach the dispatcher once each, in order");
    ok &= check(!s_dpc_stuck, "the DPC never ended");
    if (s_wrong)
        printf("        the dispatcher was given %u where %u was due\n", s_wrong_got, s_wrong_want);
    kernel_pgraph_tick();               /* the second done */
    return ok;
}

/* 3, the second part. */
#define SWM_COUNT     20000u
#define SWM_AHEAD     16u                /* queued ahead of the dispatcher */
#define SWM_STALL_MS  3000u              /* no progress for this long: lost */
static volatile LONG s_swm_stop;

static DWORD WINAPI swm_producer(LPVOID p)
{
    uint32_t k;
    (void)p;
    for (k = 1; k <= SWM_COUNT; k++) {
        while (k - (uint32_t)__atomic_load_n(&s_dispatched, __ATOMIC_ACQUIRE) > SWM_AHEAD) {
            if (__atomic_load_n(&s_swm_stop, __ATOMIC_ACQUIRE)) return 0;
            SwitchToThread();
        }
        xbox_nv2a_software_method(0, k);
    }
    return 0;
}

static DWORD WINAPI swm_timer(LPVOID p)
{
    (void)p;
    while (!__atomic_load_n(&s_swm_stop, __ATOMIC_ACQUIRE)) {
        kernel_vblank_tick();
        kernel_pgraph_tick();
        drain_dpcs();
        SwitchToThread();
    }
    return 0;
}

static DWORD WINAPI swm_game(LPVOID p)
{
    (void)p;
    while (!__atomic_load_n(&s_swm_stop, __ATOMIC_ACQUIRE)) {
        guest_lock();
        d3d_poll();
        guest_unlock();
        SwitchToThread();
    }
    return 0;
}

static int swm_many(void)
{
    HANDLE t[3];
    LONG last = -1;
    ULONGLONG since = GetTickCount64();
    int ok = 1, i;
    fresh_registers();
    d3d_startup();
    __atomic_store_n(&s_dispatched, 0, __ATOMIC_RELEASE);
    s_expect = 1; s_wrong = 0;
    s_by_poll = s_by_dpc = 0;
    s_vblanks_handled = 0;
    s_vblank_stuck = s_dpc_stuck = 0;
    t[0] = CreateThread(NULL, 0, swm_producer, NULL, 0, NULL);
    t[1] = CreateThread(NULL, 0, swm_timer, NULL, 0, NULL);
    t[2] = CreateThread(NULL, 0, swm_game, NULL, 0, NULL);
    for (;;) {
        LONG n = __atomic_load_n(&s_dispatched, __ATOMIC_ACQUIRE);
        if ((uint32_t)n >= SWM_COUNT) break;
        if (n != last) { last = n; since = GetTickCount64(); }
        else if (GetTickCount64() - since > SWM_STALL_MS) break;
        Sleep(1);
    }
    /* What the timer thread still has to see done, before it stops. */
    Sleep(20);
    __atomic_store_n(&s_swm_stop, 1, __ATOMIC_RELEASE);
    for (i = 0; i < 3; i++) {
        WaitForSingleObject(t[i], INFINITE);
        CloseHandle(t[i]);
    }
    {
        uint32_t n = (uint32_t)__atomic_load_n(&s_dispatched, __ATOMIC_ACQUIRE);
        printf("  %u of %u software methods reached the dispatcher, %ld from the DPC and %ld "
               "from the game thread's poll; %lu vblanks handled\n",
               n, SWM_COUNT, (long)s_by_dpc, (long)s_by_poll, s_vblanks_handled);
        if (!check(n == SWM_COUNT, "software methods were lost"))
            printf("        none after parameter %u for %u ms; PGRAPH_INTR 0x%08X, "
                   "PGRAPH_FIFO 0x%08X\n", n, SWM_STALL_MS, rd(PGRAPH_INTR), rd(PGRAPH_FIFO));
        ok &= n == SWM_COUNT;
    }
    if (!check(!s_wrong, "the dispatcher was given a parameter out of turn"))
        printf("        %u where %u was due (%u times)\n", s_wrong_got, s_wrong_want, s_wrong);
    ok &= !s_wrong;
    ok &= check(!s_vblank_stuck && !s_dpc_stuck, "a handler's loop never ended");
    return ok;
}

/* 4 */
#define RACE_ROUNDS 50000u
#define RACE_BIT_A  (1u << 4)
#define RACE_BIT_B  (1u << 24)
static volatile LONG s_race_stop;
static unsigned s_acked_a, s_acked_b, s_raised_a, s_raised_b;

struct raiser { uint32_t bit; unsigned *raised; };

static DWORD WINAPI race_raiser(LPVOID p)
{
    const struct raiser *r = p;
    unsigned k;
    for (k = 0; k < RACE_ROUNDS; k++) {
        unsigned spins = 0;
        xbox_nv2a_intr_raise(PFIFO_INTR_0, r->bit);
        (*r->raised)++;
        /* Raised again only once acknowledged, so that each is its own. */
        while (rd(PFIFO_INTR_0) & r->bit) {
            if (__atomic_load_n(&s_race_stop, __ATOMIC_ACQUIRE)) return 0;
            if (++spins > 64) SwitchToThread();
        }
    }
    return 0;
}

static DWORD WINAPI race_acker(LPVOID p)
{
    (void)p;
    while (!__atomic_load_n(&s_race_stop, __ATOMIC_ACQUIRE)) {
        uint32_t v;
        if (!(rd(PMC_INTR_0) & PMC_PFIFO)) { SwitchToThread(); continue; }
        v = rd(PFIFO_INTR_0);
        wr(PFIFO_INTR_0, v);            /* what it read, as D3D does */
        if (v & RACE_BIT_A) s_acked_a++;
        if (v & RACE_BIT_B) s_acked_b++;
    }
    return 0;
}

static int race(void)
{
    struct raiser a = { RACE_BIT_A, &s_raised_a }, b = { RACE_BIT_B, &s_raised_b };
    HANDLE t[3];
    ULONGLONG t0;
    int i, ok;
    fresh_registers();
    if (!check(xbox_nv2a_intr_raise != NULL, "no xbox_nv2a_intr_raise: no register model"))
        return 0;
    wr(PFIFO_INTR_EN_0, RACE_BIT_A | RACE_BIT_B);
    t[0] = CreateThread(NULL, 0, race_acker, NULL, 0, NULL);
    t[1] = CreateThread(NULL, 0, race_raiser, &a, 0, NULL);
    t[2] = CreateThread(NULL, 0, race_raiser, &b, 0, NULL);
    t0 = GetTickCount64();
    while (WaitForSingleObject(t[1], 10) != WAIT_OBJECT_0
           || WaitForSingleObject(t[2], 10) != WAIT_OBJECT_0) {
        if (GetTickCount64() - t0 > 60000) break;
    }
    __atomic_store_n(&s_race_stop, 1, __ATOMIC_RELEASE);
    for (i = 0; i < 3; i++) {
        WaitForSingleObject(t[i], INFINITE);
        CloseHandle(t[i]);
    }
    printf("  raised %u and %u, acknowledged %u and %u\n",
           s_raised_a, s_raised_b, s_acked_a, s_acked_b);
    ok = check(s_raised_a == RACE_ROUNDS && s_raised_b == RACE_ROUNDS,
               "a raiser never saw its bit acknowledged");
    ok &= check(s_acked_a == s_raised_a && s_acked_b == s_raised_b,
                "a bit was cleared without being acknowledged, or acknowledged twice");
    return ok;
}

int main(void)
{
    if (!(s_regs = calloc(1, REGS_BYTES))) return 2;
    g_xbox_mem_offset = (ptrdiff_t)((uintptr_t)s_regs - NV2A);
    (void)rd(PMC_INTR_0);               /* kernel_mmio.c's switches, read once here */

    result("1. the registers", registers());
    result("2. a vblank, done in one pass of the handler", one_vblank());
    result("3a. a software method's parameter, read before the next is raised", swm_after_ack());
    result("3b. software methods from the DPC and a game thread's poll", swm_many());
    result("4. two raising while one acknowledges", race());
    if (s_failed) {
        printf("vblank_ack: a case failed\n");
        return 1;
    }
    printf("vblank_ack: all cases pass\n");
    return 0;
}
