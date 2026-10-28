/* Run with: python tests/index_runs/run.py
 *
 * src/nv2a_gl/nv2a_gl.c whole, with no surface, so the renderer never starts
 * and every draw returns at once. A run of index words through
 * nv2a_gl_indices must leave the renderer's state (S, every byte of it) as
 * the same words one at a time through nv2a_gl_method leave it, from
 * whatever state the run starts in:
 * - ARRAY_ELEMENT16 and 32, inside Begin/End and outside it;
 * - runs of 1 to 2047 words, with the list empty, part full, at an odd
 *   count, and full, so the words past its end are dropped alike;
 * - after a constant or a vertex array moved, where the run's first word
 *   draws what the list holds first (split_batch_if_state_moved);
 * - with the renderer off, or failed;
 * - with the profile on, where the run must be counted word by word too.
 * Then the renderer's time per index word both ways, shown, not checked.
 */
#include "nv2a_gl.c"

#define RUN_MAX     2047        /* a method header's count: 11 bits */
#define ROUNDS      40000
#define TIMED_DRAWS 20000
#define TIMED_WORDS 120         /* about 99th Street's index words a draw */
#define PRIM_LIST   8           /* a primitive split_batch_if_state_moved splits */

static uint8_t s_before[sizeof S], s_one_by_one[sizeof S];
static uint32_t s_words[RUN_MAX];
static unsigned long s_runs, s_fails, s_split_runs, s_full_runs;
static uint32_t s_seed = 12345u;

static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}

/* The run both ways from the same state: what the words one at a time
 * leave, then the run whole from the state before, and the two compared. */
static void run(uint32_t method, uint32_t n, const char *what)
{
    unsigned long words = g_prof.pbk_words[PBK_INDEX], counted;
    unsigned splits = S.midbatch_splits;
    uint32_t i, had = S.idx_count, after;
    int failed = g_failed, failed_after;

    memcpy(s_before, &S, sizeof S);
    for (i = 0; i < n; i++)
        nv2a_gl_method(method, s_words[i]);
    memcpy(s_one_by_one, &S, sizeof S);
    after = S.idx_count;
    failed_after = g_failed;
    counted = g_prof.pbk_words[PBK_INDEX] - words;
    if (S.midbatch_splits != splits) s_split_runs++;
    if (S.idx_count + (method == M_ARRAY_ELEMENT16 ? 2 : 1) > MAX_INDICES) s_full_runs++;

    memcpy(&S, s_before, sizeof S);
    g_failed = failed;
    g_prof.pbk_words[PBK_INDEX] = words;
    nv2a_gl_indices(method, s_words, n);
    s_runs++;
    if (memcmp(&S, s_one_by_one, sizeof S) || g_failed != failed_after
        || g_prof.pbk_words[PBK_INDEX] - words != counted) {
        if (s_fails++ < 10)
            printf("FAIL %s: %04X x%u from %u indices: one at a time %u, whole %u%s%s\n",
                   what, method, n, had, after, S.idx_count, g_failed != failed_after ? ", failed differs" : "",
                   g_prof.pbk_words[PBK_INDEX] - words != counted ? ", counted differs" : "");
    }
}

static void fill(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) s_words[i] = rnd() ^ (rnd() << 16);
}

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec * 1e-9;
}

int main(void)
{
    unsigned long r;
    uint32_t i;
    double t0, one_by_one, whole;

    g_on = 1;
    g_prof_on = 0;

    /* Off, and failed: nothing either way. */
    fill(100);
    g_on = 0;
    run(M_ARRAY_ELEMENT16, 100, "renderer off");
    g_on = 1;
    g_failed = 1;
    run(M_ARRAY_ELEMENT32, 100, "renderer failed");
    g_failed = 0;

    /* Full: an odd count, then 16-bit runs past the end, then 32-bit ones. */
    nv2a_gl_method(M_SET_BEGIN_END, PRIM_LIST);
    fill(1);
    run(M_ARRAY_ELEMENT32, 1, "one 32-bit index");
    for (i = 0; i < 6; i++) { fill(RUN_MAX); run(M_ARRAY_ELEMENT16, RUN_MAX, "16-bit, filling"); }
    fill(RUN_MAX);
    run(M_ARRAY_ELEMENT32, RUN_MAX, "32-bit, full");
    nv2a_gl_method(M_SET_BEGIN_END, 0);

    /* Three short of full, then a run that has room for one word. */
    for (i = 0; i < 2; i++) {
        uint32_t m = i ? M_ARRAY_ELEMENT32 : M_ARRAY_ELEMENT16;
        nv2a_gl_method(M_SET_BEGIN_END, PRIM_LIST);
        while (S.idx_count < MAX_INDICES - 3) {
            uint32_t n = MAX_INDICES - 3 - S.idx_count;
            fill(n > RUN_MAX ? RUN_MAX : n);
            nv2a_gl_indices(M_ARRAY_ELEMENT32, s_words, n > RUN_MAX ? RUN_MAX : n);
        }
        fill(5);
        run(m, 5, "three short of full");
        nv2a_gl_method(M_SET_BEGIN_END, 0);
    }

    /* A constant, then a vertex array, moved inside Begin/End. */
    nv2a_gl_method(M_SET_BEGIN_END, PRIM_LIST);
    fill(10);
    run(M_ARRAY_ELEMENT16, 10, "before a constant moves");
    nv2a_gl_method(M_SET_TRANSFORM_CONSTANT_LD, 96);
    nv2a_gl_method(M_SET_TRANSFORM_CONSTANT, 0x3F800000u);
    run(M_ARRAY_ELEMENT16, 10, "after a constant moved");
    nv2a_gl_method(M_SET_VERTEX_ARRAY_OFFSET, 0x00123400u);
    run(M_ARRAY_ELEMENT32, 10, "after an array moved");
    nv2a_gl_method(M_SET_BEGIN_END, 0);

    /* At random: any primitive or none, any run, things moving between,
     * and the profile on now and then. */
    for (r = 0; r < ROUNDS; r++) {
        uint32_t what = rnd() % 16, n;
        g_prof_on = (r / 1000) % 4 == 3;
        switch (what) {
        case 0: nv2a_gl_method(M_SET_BEGIN_END, 1 + rnd() % 10); break;
        case 1: nv2a_gl_method(M_SET_BEGIN_END, 0); break;
        case 2:
            nv2a_gl_method(M_SET_TRANSFORM_CONSTANT_LD, rnd() % 192);
            nv2a_gl_method(M_SET_TRANSFORM_CONSTANT, rnd());
            break;
        case 3: nv2a_gl_method(M_SET_VERTEX_ARRAY_OFFSET + 4 * (rnd() % 16), rnd()); break;
        default:
            n = 1 + rnd() % (what == 4 ? RUN_MAX : what < 10 ? 4 : 300);
            fill(n);
            run(rnd() & 1 ? M_ARRAY_ELEMENT16 : M_ARRAY_ELEMENT32, n, "at random");
        }
    }
    g_prof_on = 0;
    printf("%lu runs the same both ways, %lu of them after something moved, %lu past a "
           "full list\n", s_runs - s_fails, s_split_runs, s_full_runs);
    if (s_fails) { printf("index_runs renderer: %lu run(s) FAIL\n", s_fails); return 1; }
    if (!s_split_runs || !s_full_runs) { printf("FAIL: a case was never reached\n"); return 1; }

    /* The time per index word: draws of TIMED_WORDS 16-bit words. */
    fill(TIMED_WORDS);
    t0 = now_s();
    for (r = 0; r < TIMED_DRAWS; r++) {
        nv2a_gl_method(M_SET_BEGIN_END, PRIM_LIST);
        for (i = 0; i < TIMED_WORDS; i++) nv2a_gl_method(M_ARRAY_ELEMENT16, s_words[i]);
        nv2a_gl_method(M_SET_BEGIN_END, 0);
    }
    one_by_one = now_s() - t0;
    t0 = now_s();
    for (r = 0; r < TIMED_DRAWS; r++) {
        nv2a_gl_method(M_SET_BEGIN_END, PRIM_LIST);
        nv2a_gl_indices(M_ARRAY_ELEMENT16, s_words, TIMED_WORDS);
        nv2a_gl_method(M_SET_BEGIN_END, 0);
    }
    whole = now_s() - t0;
    printf("the renderer's time per index word, in draws of %d: %.1f ns one at a time, "
           "%.1f ns in a run\n", TIMED_WORDS, one_by_one * 1e9 / (TIMED_DRAWS * TIMED_WORDS),
           whole * 1e9 / (TIMED_DRAWS * TIMED_WORDS));
    printf("index_runs renderer: all cases pass\n");
    return 0;
}
