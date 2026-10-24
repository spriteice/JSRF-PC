/* Run with: python tests/vblank_ack/run.py
 *
 * A vblank raised as the bridge raises it, and a routine that spins on
 * PMC_INTR_0 as the title's vblank DPC does, at loop back edges. Inside an
 * interrupt or DPC, a back edge completes the acknowledgement once the
 * routine has written it, and not before, and offers no lock. Outside one,
 * a back edge offers the lock as before, and the acknowledgement is left to
 * the GPU thread.
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

    printf("vblank_ack: all cases pass\n");
    return 0;
}
