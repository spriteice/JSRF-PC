/* Run with: python tests/vblank_ack/run.py
 *
 * A vblank raised as the bridge raises it, and a routine that spins on
 * PMC_INTR_0 as the title's vblank DPC does, at loop back edges. Inside an
 * interrupt or DPC, a back edge completes the acknowledgement once the
 * routine has written it, and not before, and offers no lock. Outside one,
 * a back edge offers the lock as before, and the acknowledgement is left to
 * the GPU thread.
 *
 * Then a race: the timer thread raises vblanks as the bridge does
 * (kernel_vblank_tick) and runs the title's handling of each, while the GPU
 * side completes acknowledgements as it runs, as frame pacing's last spin and
 * the ack thread do. Every vblank must reach the title's handler: one wiped
 * as it is raised never does.
 */
int mcpx_apu_irq_pending_global(void) { return 0; }

#define PCRTC_INTR_0 ((volatile uint32_t *)((char *)g_nv2a_memory + NV2A_PCRTC_INTR_0))
#define PMC_INTR_0   ((volatile uint32_t *)((char *)g_nv2a_memory + NV2A_PMC_INTR_0))

static void raise_vblank(void)
{
    *PCRTC_INTR_0 |= NV2A_PCRTC_INTR_VBLANK | NV2A_VBLANK_PENDING_TAG;
    *PMC_INTR_0   |= NV2A_PMC_INTR_PCRTC;
}

/* Back edges until PMC_INTR_0 says the PCRTC is quiet: how many, or -1 at
 * the limit. */
static int spin(int limit)
{
    int n;
    for (n = 0; n < limit; n++) {
        if (!(*PMC_INTR_0 & NV2A_PMC_INTR_PCRTC)) return n;
        xbox_GuestCpuPreempt(0x1000u);
    }
    return -1;
}

/* The title's side of a vblank, as JSRF has it, all on the timer thread here
 * as in the runtime. Its ISR (0x00193C50) masks the GPU's interrupts and
 * queues its DPC. The DPC (0x00194480) calls the vblank handler (0x00193D90)
 * if PMC_INTR_0 says PCRTC, and unmasks. The handler counts the vblank and
 * writes PCRTC_INTR_0 until PMC_INTR_0 is quiet. */
#define NV2A_PMC_INTR_EN_0 0x00000140u
#define PMC_INTR_EN_0 ((volatile uint32_t *)((char *)g_nv2a_memory + NV2A_PMC_INTR_EN_0))
#define RACE_VBLANKS 200000ul
static int s_dpc_queued;
static unsigned long s_handled;
static volatile int s_race_stop;

static int kernel_raise_interrupt(uint32_t vector)
{
    (void)vector;
    if (!*PMC_INTR_EN_0) return 0;
    *PMC_INTR_EN_0 = 0;
    s_dpc_queued = 1;
    return 1;
}

static void title_dpc(void)
{
    if (*PMC_INTR_0 & NV2A_PMC_INTR_PCRTC) {
        s_handled++;
        do {
            *PCRTC_INTR_0 = 1;
            xbox_GuestCpuPreempt(0x1000u);
        } while (*PMC_INTR_0 & NV2A_PMC_INTR_PCRTC);
    }
    *PMC_INTR_EN_0 = 1;
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* Frame pacing's last spin: the clock, then the vblank, flat out. */
static void *pace_spin(void *p)
{
    (void)p;
    while (!s_race_stop) {
        (void)now_s();
        xbox_nv2a_vblank_service();
    }
    return NULL;
}

/* The ack thread, between its sleeps. */
static void *ack_thread(void *p)
{
    struct timespec ts = { 0, 200000L };
    (void)p;
    while (!s_race_stop) {
        vblank_intr_tick((volatile uint32_t *)g_nv2a_memory);
        nanosleep(&ts, NULL);
    }
    return NULL;
}

/* How many vblanks reached the title's handler, of RACE_VBLANKS raised. */
static unsigned long race(void)
{
    pthread_t pace, ack;
    unsigned long r;
    unsigned seed = 1u;
    g_xbox_mem_offset = (ptrdiff_t)((uintptr_t)g_nv2a_memory - XBOX_NV2A_REG_BASE);
    *PCRTC_INTR_0 = 0;
    *PMC_INTR_0 = 0;
    *PMC_INTR_EN_0 = 1;
    t_in_callback = 1;
    pthread_create(&pace, NULL, pace_spin, NULL);
    pthread_create(&ack, NULL, ack_thread, NULL);
    for (r = 0; r < RACE_VBLANKS; r++) {
        double until;
        kernel_vblank_tick();
        if (s_dpc_queued) {
            s_dpc_queued = 0;
            title_dpc();
        }
        /* 0 to 20 us to the next, so that it lands anywhere in their loops. */
        seed = seed * 1103515245u + 12345u;
        until = now_s() + (double)((seed >> 8) % 20000u) * 1e-9;
        while (now_s() < until) { }
    }
    s_race_stop = 1;
    pthread_join(pace, NULL);
    pthread_join(ack, NULL);
    return s_handled;
}

static int fail(const char *what)
{
    printf("vblank_ack: %s\n", what);
    return 1;
}

int main(void)
{
    int n;
    if (!(g_nv2a_memory = calloc(1, NV2A_PCRTC_INTR_0 + 4))) return fail("no memory");
    t_guest_holds = 1;

    t_in_callback = 1;
    raise_vblank();
    if (spin(4096) >= 0) return fail("completed before the routine acknowledged it");
    *PCRTC_INTR_0 = 1;              /* the acknowledgement: the register written whole */
    n = spin(4096);
    if (n < 0 || n > 256) return fail("an acknowledged vblank was not completed at a back edge");
    if (*PCRTC_INTR_0) return fail("PCRTC_INTR_0 was left set");
    if (s_offers) return fail("a back edge inside a DPC offered the lock");
    if (!g_edge_vblank_checks) return fail("no look was counted");
    printf("inside a DPC: completed %d back edges after the acknowledgement\n", n);

    t_in_callback = 0;
    raise_vblank();
    *PCRTC_INTR_0 = 1;
    if (spin(1024) >= 0) return fail("completed outside an interrupt or DPC");
    if (!s_offers) return fail("a back edge outside a DPC did not offer the lock");
    printf("outside one: left to the GPU thread, the lock offered %d times\n", s_offers);

    if (race() != g_vblank_count) {
        printf("a race: %lu of %lu vblanks reached the title's handler\n", s_handled, g_vblank_count);
        return fail("vblanks raised as the GPU side completed the last were lost");
    }
    printf("a race: %lu vblanks raised as the GPU side completes them, all reached the title's handler\n",
           g_vblank_count);

    printf("vblank_ack: all cases pass\n");
    return 0;
}
