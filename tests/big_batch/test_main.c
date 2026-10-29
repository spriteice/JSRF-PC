/* Run with: python tests/big_batch/run.py
 *
 * src/nv2a_gl/nv2a_gl.c whole, with no surface, as tests/index_runs has it:
 * the renderer never starts, and every draw returns at once. A batch of
 * BATCH indices, the longest seen, is sent each way the title can send one
 * between Begin and End, and must be kept whole and in order:
 * - 16-bit and 32-bit index words one at a time (nv2a_gl_method), and in
 *   runs of up to RUN_MAX words (nv2a_gl_indices);
 * - DRAW_ARRAYS ranges of up to RANGE_MAX vertices;
 * - inline vertices of INLINE_WORDS words.
 * The renderer used to keep the first 16384 of either and drop the rest
 * (upstream's 9d21bd8).
 */
#include "nv2a_gl.c"

#define BATCH        35580u   /* indices: in the Fortified Residential Zone */
#define RUN_MAX      2047u    /* a method header's count: 11 bits */
#define RANGE_MAX    256u     /* DRAW_ARRAYS: an 8-bit count, plus one */
#define COUNT_SHIFT  24       /* DRAW_ARRAYS: count - 1 in the top 8 bits */
#define PAIR         2u       /* ARRAY_ELEMENT16: two indices a word, */
#define PAIR_SHIFT   16       /* the second in the top half */
#define INLINE_WORDS 8u       /* a vertex of the inline batch */

static uint32_t s_words[BATCH * INLINE_WORDS];
static int s_fails;

/* What the batch holds now, against 0, 1, 2, ... want - 1. */
static void kept(const char *way, const uint32_t *list, uint32_t count, uint32_t want)
{
    uint32_t i = 0;
    while (i < count && list[i] == i) i++;
    if (count == want && i == want) return;
    s_fails++;
    printf("FAIL %s: %u of %u kept, the first %u in order\n", way, count, want, i);
}

/* The index words of the batch, one at a time or in runs, kept whole. */
static void indices(uint32_t method, uint32_t words, int in_runs, const char *way)
{
    uint32_t k, n;
    nv2a_gl_method(M_SET_BEGIN_END, PRIM_TRIANGLES);
    for (k = 0; k < words; k += n) {
        n = in_runs ? (words - k < RUN_MAX ? words - k : RUN_MAX) : 1;
        if (in_runs) nv2a_gl_indices(method, s_words + k, n);
        else         nv2a_gl_method(method, s_words[k]);
    }
    kept(way, S.idx, S.idx_count, BATCH);
    nv2a_gl_method(M_SET_BEGIN_END, 0);
}

int main(void)
{
    uint32_t k, n;

    g_on = 1;
    g_prof_on = 0;

    for (k = 0; k < BATCH / PAIR; k++) s_words[k] = PAIR * k | (PAIR * k + 1) << PAIR_SHIFT;
    indices(M_ARRAY_ELEMENT16, BATCH / PAIR, 0, "16-bit, one at a time");
    indices(M_ARRAY_ELEMENT16, BATCH / PAIR, 1, "16-bit, in runs");
    for (k = 0; k < BATCH; k++) s_words[k] = k;
    indices(M_ARRAY_ELEMENT32, BATCH, 0, "32-bit, one at a time");
    indices(M_ARRAY_ELEMENT32, BATCH, 1, "32-bit, in runs");

    nv2a_gl_method(M_SET_BEGIN_END, PRIM_TRIANGLES);
    for (k = 0; k < BATCH; k += n) {
        n = BATCH - k < RANGE_MAX ? BATCH - k : RANGE_MAX;
        nv2a_gl_method(M_DRAW_ARRAYS, k | (n - 1) << COUNT_SHIFT);
    }
    kept("DRAW_ARRAYS ranges", S.idx, S.idx_count, BATCH);
    nv2a_gl_method(M_SET_BEGIN_END, 0);

    nv2a_gl_method(M_SET_BEGIN_END, PRIM_TRIANGLES);
    for (k = 0; k < BATCH * INLINE_WORDS; k++) nv2a_gl_method(M_INLINE_ARRAY, k);
    kept("inline vertices", S.inline_buf, S.inline_count, BATCH * INLINE_WORDS);
    nv2a_gl_method(M_SET_BEGIN_END, 0);

    if (s_fails) { printf("big_batch: %d way(s) FAIL\n", s_fails); return 1; }
    printf("big_batch: a batch of %u indices kept whole all six ways\n", BATCH);
    return 0;
}
