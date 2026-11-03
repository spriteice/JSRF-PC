/*
 * The game's side of the pushbuffer handshake, against the GPU thread as it
 * is: D3D's KickOff (flush, wait for it, DMA_PUT, kick) done the way the
 * lifted code does it, through the device window, while the GPU thread walks
 * two 32 KB segments of NOPs with src/kernel/nv2a_pb_scan.c.
 *
 * Each NOP's parameter is its own address, so the executor's stand-in can
 * check DMA_GET against the command it is running. One of them takes 30 ms
 * -- a slow draw, or frame pacing at a flip -- and the game kicks again while
 * the walk is held there. Run once with RECOMP_NV2A_QUEUE=0 and once without;
 * run.py says what each must print.
 *
 * With QUEUE_TEST_WAKE=1 as well, D3D's wait for the GPU instead (JSRF's
 * sub_00191440): a fence is kicked, D3D turns the NOP reserved after it into
 * NOP(5), flushes, reads GET again, and sleeps while GET is short of the NOP,
 * until the walk reads it. Twenty thousand times, with the walk at every
 * distance from the NOP, on top of it included; no wake-up may be lost.
 *
 * With QUEUE_TEST_RING=1 as well, a ring of 512 KB that ends in a jump to its
 * start, as D3D's has: the game kicks a PUT past the jump while the walk is
 * held near the ring's start, so the tail the walk finds after it is most of
 * the ring. Every command in it, and those after the jump up to PUT, must
 * run.
 */
#include <pthread.h>
#include <sys/mman.h>

#define SLOW_MS 30
#define SEG1 0x00010000u
#define SEG2 0x00018000u
#define END  0x00020000u
#define SLOW (SEG1 + 1000u * 8u)

#define WAKE_FROM   0x00100000u
#define WAKE_ROUNDS 20000u


#define RING_START 0x00040000u
#define RING_END   0x000C0000u
#define RING_JUMP  (RING_END - 8u)
#define RING_PUT1  (RING_START + 0x1000u)
#define RING_SLOW  (RING_PUT1 + 0x100u)
#define RING_PUT2  (RING_PUT1 + 0x2000u)
#define RING_PUT3  (RING_START + 0x800u)

static uint8_t *s_guest;                 /* guest address 0 */
static uint8_t *s_regs;                  /* the NV2A's 16 MB in it */
/* The stand-in executor's counters, which the GPU thread writes and the test
 * reads: each read and written atomically. */
static int s_walking, s_walks;
static unsigned s_ran, s_get_at, s_get_not;
static uint32_t s_woke_at;               /* GET when the walk ran a NOP(5) */
#define LOAD(v) __atomic_load_n(&(v), __ATOMIC_ACQUIRE)
#define STORE(v, x) __atomic_store_n(&(v), (x), __ATOMIC_RELEASE)
#define BUMP(v) __atomic_fetch_add(&(v), 1, __ATOMIC_ACQ_REL)
static uint32_t s_slow = SLOW;           /* the command that takes SLOW_MS */

ptrdiff_t xbox_GetMemoryOffset(void) { return (ptrdiff_t)(uintptr_t)s_guest; }
void nv2a_pb_time_begin(void) { }
void nv2a_pb_time_end(unsigned methods) { (void)methods; BUMP(s_walks); }
void nv2a_pb_exec_report(void) { }

void nv2a_pb_exec_method(uint32_t subch, uint32_t method, uint32_t param)
{
    uint32_t get = *(volatile uint32_t *)(s_regs + NV2A_USER_DMA_GET);
    (void)subch;
    if (method != 0x100u) return;
    if (param == 5u) STORE(s_woke_at, get);
    BUMP(s_ran);
    if (get == param) BUMP(s_get_at); else BUMP(s_get_not);
    if (param == s_slow) {
        struct timespec ts = { 0, SLOW_MS * 1000000L };
        STORE(s_walking, 1);
        nanosleep(&ts, NULL);
        STORE(s_walking, 0);
    }
}

/* A run of index words, handed over whole (none here: the stream is NOPs). */
void nv2a_pb_exec_indices(uint32_t subch, uint32_t method, const uint32_t *p, uint32_t n)
{
    while (n--)
        nv2a_pb_exec_method(subch, method, *p++);
}

/* MEMW32 and MEM32, as recomp_types.h routes them. */
static void memw32(uint32_t a, uint32_t v)
{
    if (RECOMP_IS_MMIO(a)) recomp_mmio_write(a, v, 4);
    else *(volatile uint32_t *)(s_guest + a) = v;
}
static uint32_t mem32(uint32_t a)
{
    if (RECOMP_IS_MMIO(a)) return recomp_mmio_read(a, 4);
    return *(volatile uint32_t *)(s_guest + a);
}

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* NOPs from one physical address to the next, in the contiguous window. */
static void write_nops(uint32_t from, uint32_t to)
{
    uint32_t a;
    for (a = from; a < to; a += 8) {
        memw32(XBOX_CONTIG_BASE + a, 0x00040100u);     /* NV097_NO_OPERATION, 1 */
        memw32(XBOX_CONTIG_BASE + a + 4, a);
    }
}

/* D3D's KickOff with the port's wrapper around it (port/recomp_manual.c).
 * Returns how long the flush took to be answered, in ms. */
static double kickoff(uint32_t put)
{
    const uint32_t wbc = 0xFD000000u + NV2A_PFB_WBC;
    double t0, waited;
    xbox_nv2a_hurry(1);
    memw32(wbc, mem32(wbc) | NV2A_PFB_WBC_FLUSH);
    t0 = now_ms();
    while (mem32(wbc) & NV2A_PFB_WBC_FLUSH)
        if (now_ms() - t0 > 2000.0) break;
    waited = now_ms() - t0;
    memw32(0xFD000000u + NV2A_USER_DMA_PUT, put);
    xbox_nv2a_hurry(0);
    xbox_nv2a_kick();
    return waited;
}

static int wait_for(int *v, int want)
{
    double t0 = now_ms();
    while (__atomic_load_n(v, __ATOMIC_ACQUIRE) < want)
        if (now_ms() - t0 > 2000.0) return 0;
    return 1;
}

static int wait_get(uint32_t want)
{
    double t0 = now_ms();
    while (*(volatile uint32_t *)(s_regs + NV2A_USER_DMA_GET) != want)
        if (now_ms() - t0 > 2000.0) return 0;
    return 1;
}

static void *gpu_thread(void *p) { nv2a_ack_thread(p); return NULL; }

/* A wrap with most of the ring behind it. The game kicks the first stretch,
 * and while the walk is held at the slow command in it, runs on to the
 * ring's end, jumps to its start and kicks again: the GPU thread's next PUT
 * is then behind the one it walked to, with the tail and the jump between. */
static void ring_test(void)
{
    unsigned want;
    write_nops(RING_START, RING_JUMP);
    memw32(XBOX_CONTIG_BASE + RING_JUMP, 0x20000000u | RING_START);   /* jump */
    s_slow = RING_SLOW;
    kickoff(RING_PUT1);
    if (!wait_get(RING_PUT1)) { printf("GET never reached the first PUT\n"); return; }
    STORE(s_ran, 0);
    STORE(s_get_at, 0);
    STORE(s_get_not, 0);
    kickoff(RING_PUT2);
    if (!wait_for(&s_walking, 1)) { printf("the walk never reached the slow command\n"); return; }
    kickoff(RING_PUT3);
    if (!wait_get(RING_PUT3)) { printf("GET never reached the PUT past the jump\n"); return; }
    want = (RING_JUMP - RING_PUT1) / 8u + (RING_PUT3 - RING_START) / 8u;
    printf("a wrap with %u KB of tail: commands run: %u of %u; DMA_GET at the command each time: %s\n",
           (RING_JUMP - RING_PUT2) / 1024u, LOAD(s_ran), want,
           !LOAD(s_get_not) ? "yes" : LOAD(s_get_at) ? "not always" : "never");
}

/* D3D's wait for the GPU, as sub_00191440 does it after the fence it waits
 * on is kicked: patch, flush, GET again, sleep or undo. The fence is three
 * commands -- the semaphore release and the two NOPs reserved after it --
 * and D3D measures from GET to the fence, while the NOP it patches is the
 * third of them. */
static void wake_test(void)
{
    const uint32_t wbc = 0xFD000000u + NV2A_PFB_WBC, getr = 0xFD000000u + NV2A_USER_DMA_GET;
    uint32_t put = WAKE_FROM, a, seed = 1;
    unsigned r, slept = 0, spun = 0, passed = 0, lost = 0;

    /* Every command a placeholder, as D3D reserves them after a fence. */
    for (a = WAKE_FROM; a < WAKE_FROM + WAKE_ROUNDS * 35u * 8u; a += 8) {
        memw32(XBOX_CONTIG_BASE + a, 0x00041D90u);     /* SET_COLOR_CLEAR_VALUE, 1 */
        memw32(XBOX_CONTIG_BASE + a + 4, 0);
    }
    kickoff(put);
    if (!wait_get(put)) { printf("GET never reached the first PUT\n"); return; }
    for (r = 0; r < WAKE_ROUNDS && !lost; r++) {
        uint32_t fence, slot, get;
        seed = seed * 1103515245u + 12345u;
        put += 8u * (3u + (seed >> 16) % 32u);
        fence = put - 24u;               /* kicked last */
        slot = fence + 16u;
        kickoff(put);
        if (mem32(getr) > fence) { passed++; continue; }
        memw32(XBOX_CONTIG_BASE + slot, 0x00040100u);  /* NV097_NO_OPERATION, 1 */
        memw32(XBOX_CONTIG_BASE + slot + 4, 5u);
        memw32(wbc, mem32(wbc) | NV2A_PFB_WBC_FLUSH);
        while (mem32(wbc) & NV2A_PFB_WBC_FLUSH) { }
        get = mem32(getr);
        if (get <= fence) {
            double t0 = now_ms();
            slept++;
            while (LOAD(s_woke_at) != slot)
                if (now_ms() - t0 > 500.0) { lost++; break; }
        } else {
            memw32(XBOX_CONTIG_BASE + slot, 0x00040100u);
            memw32(XBOX_CONTIG_BASE + slot + 4, 0);
            spun++;
        }
    }
    printf("D3D's wait for the GPU: %u slept, %u found the walk there first, %u after it; "
           "wake-ups lost: %u\n", slept, spun, passed, lost);
}

int main(void)
{
    pthread_t gpu;
    double flush_idle, flush_busy;
    void *guest;

    printf("window: KickOff's flush %s, DMA_GET %s, the pushbuffer %s\n",
           RECOMP_IS_MMIO(0xFD100410u) ? "in" : "out",
           RECOMP_IS_MMIO(0xFD800044u) ? "in" : "out",
           RECOMP_IS_MMIO(XBOX_CONTIG_BASE + SEG1) ? "in" : "out");

    /* The guest's 4 GB, as the runtime has it: pages only where touched. */
    guest = mmap(NULL, (size_t)1 << 32, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (guest == MAP_FAILED) return 2;
    s_guest = guest;
    s_regs = s_guest + XBOX_NV2A_BASE;
    g_nv2a_memory = s_regs;
    g_memory_offset = (uintptr_t)&s_tick_sink;
    s_nv2a_trace = 1;                    /* the executor is on */
    write_nops(SEG1, END);
    pthread_create(&gpu, NULL, gpu_thread, s_regs);

    if (getenv("QUEUE_TEST_WAKE") || getenv("QUEUE_TEST_RING")) {
        if (getenv("QUEUE_TEST_WAKE")) wake_test();
        else ring_test();
        __atomic_store_n(&g_nv2a_ack_stop, 1, __ATOMIC_RELEASE);
        pthread_join(gpu, NULL);
        return 0;
    }

    /* The first PUT is not walked (nothing came before it). */
    kickoff(SEG1);
    if (!wait_get(SEG1)) { printf("GET never reached the first PUT\n"); return 1; }

    flush_idle = kickoff(SEG2);
    if (!wait_for(&s_walking, 1)) { printf("the walk never reached the slow command\n"); return 1; }
    flush_busy = kickoff(END);
    if (!wait_for(&s_walks, 2) || !wait_get(END)) { printf("the last PUT was never walked\n"); return 1; }

    printf("flush with the GPU thread idle: %s\n", flush_idle < 5.0 ? "at once" : "late");
    printf("flush while the GPU thread walks: %s\n",
           flush_busy < 5.0 ? "at once" : flush_busy >= SLOW_MS / 2 ? "after the walk" : "late");
    printf("commands run: %u; DMA_GET at the command each time: %s\n", LOAD(s_ran),
           !LOAD(s_get_not) ? "yes" : LOAD(s_get_at) ? "not always" : "never");
    if (nv2a_queue_on()) nv2a_queue_report();

    __atomic_store_n(&g_nv2a_ack_stop, 1, __ATOMIC_RELEASE);
    pthread_join(gpu, NULL);
    return 0;
}
