/* Run with: python tests/index_runs/run.py
 *
 * src/kernel/nv2a_pb_scan.c, the walk, as it is, over a pushbuffer of random
 * commands, with an executor that logs every word it is handed and where
 * DMA_GET was then. Index runs come in every shape: ARRAY_ELEMENT16 and 32,
 * non-incrementing and incrementing, on subchannel 0 and others, of 0 to 2047
 * words, inside a call, and cut short by the end of the segment, at a word
 * and inside one. run.py runs it with RECOMP_PB_SCAN, where the walk hands
 * over each word on its own, and without, where it hands over runs of index
 * words whole: the two logs must be the same, and only the second may have
 * had runs.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

uint32_t nv2a_pb_scan(uint32_t start_va, uint32_t end_va);
void nv2a_pb_scan_keep_get(volatile uint32_t *get);

#define GUEST_SPAN  0x90000000u     /* up to the window calls land in, and past it */
#define STREAM      0x80100000u     /* the commands */
#define SUB         0x80800000u     /* a subroutine they call */
#define STREAM_END  0x80140000u     /* 256 KB of them */
#define SUB_END     0x80810000u
#define RUN_MAX     2047            /* a method header's count: 11 bits */
#define INDEX16     0x1800u
#define INDEX32     0x1808u
#define NONINC      0x40000000u
#define RETURN      0x00020000u
#define CALL        2u

static uint8_t *s_guest;
static volatile uint32_t s_get;
static FILE *s_log;
static unsigned long s_words, s_runs, s_run_words, s_walked;
static uint32_t s_seed = 4242u;

ptrdiff_t xbox_GetMemoryOffset(void) { return (ptrdiff_t)(uintptr_t)s_guest; }
void nv2a_pb_time_begin(void) { }
void nv2a_pb_time_end(unsigned methods) { s_walked += methods; }
void nv2a_pb_exec_report(void) { }

void nv2a_pb_exec_method(uint32_t subch, uint32_t method, uint32_t param)
{
    fprintf(s_log, "%u %04X %08X get %08X\n", subch, method, param, s_get);
    s_words++;
}

void nv2a_pb_exec_indices(uint32_t subch, uint32_t method, const uint32_t *p, uint32_t n)
{
    if (!n || (method != INDEX16 && method != INDEX32)) {
        printf("FAIL: handed a run of %u words to %04X\n", n, method);
        exit(1);
    }
    s_runs++;
    s_run_words += n;
    while (n--)
        nv2a_pb_exec_method(subch, method, *p++);
}

static uint32_t rnd(void)
{
    s_seed = s_seed * 1103515245u + 12345u;
    return s_seed >> 8;
}

static void put(uint32_t *va, uint32_t w)
{
    memcpy(s_guest + *va, &w, 4);
    *va += 4;
}

/* Commands from va up to end, at random; a call to SUB now and then. */
static uint32_t commands(uint32_t va, uint32_t end, int calls)
{
    static const uint32_t methods[] = { INDEX16, INDEX32, INDEX16, INDEX32, 0x1810u, 0x1818u,
                                        0x17FCu, 0x0B80u, 0x1D70u };
    while (va + 4 * (RUN_MAX + 1) < end) {
        uint32_t pick = rnd() % 100, count, method, subch, i;
        if (calls && pick < 2) {
            put(&va, (SUB & 0x0FFFFFFCu) | CALL);
            continue;
        }
        method = pick < 80 ? methods[rnd() % 9] : (rnd() % 0x800u) * 4;
        subch = rnd() % 8 ? 0 : 1 + rnd() % 7;
        count = rnd() % 20 ? 1 + rnd() % 300 : rnd() % 3 ? RUN_MAX : 0;
        put(&va, (rnd() % 4 ? NONINC : 0) | count << 18 | subch << 13 | method);
        for (i = 0; i < count; i++)
            put(&va, rnd());
    }
    return va;
}

int main(int argc, char **argv)
{
    uint32_t end, va, cut, last, i;
    int scan = argc > 2 && !strcmp(argv[2], "scan");

    s_guest = mmap(NULL, GUEST_SPAN, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (s_guest == MAP_FAILED || !(s_log = fopen(argv[1], "w"))) {
        printf("FAIL: no guest memory, or no log\n");
        return 1;
    }
    nv2a_pb_scan_keep_get(&s_get);

    va = commands(SUB, SUB_END, 0);
    put(&va, RETURN);
    end = commands(STREAM, STREAM_END, 1);

    /* The last run is long and the segment ends inside it: at a word, two
     * bytes into one, and two bytes into its header. */
    last = end;
    put(&end, NONINC | RUN_MAX << 18 | INDEX16);
    for (i = 0; i < RUN_MAX; i++)
        put(&end, rnd());
    cut = last + 4 + 4 * 1000;

    fprintf(s_log, "walk to %08X: %08X\n", cut, nv2a_pb_scan(STREAM, cut));
    fprintf(s_log, "walk to %08X: %08X\n", cut + 2, nv2a_pb_scan(STREAM, cut + 2));
    fprintf(s_log, "walk to %08X: %08X\n", last + 2, nv2a_pb_scan(STREAM, last + 2));
    fprintf(s_log, "walk to %08X: %08X\n", end, nv2a_pb_scan(STREAM, end));
    fprintf(s_log, "words walked %lu, handed over %lu\n", s_walked, s_words);
    fclose(s_log);
    printf("%s: %lu words handed over, %lu of them in %lu runs\n",
           scan ? "with the survey" : "without", s_words, s_run_words, s_runs);
    if (scan ? s_runs != 0 : s_runs == 0) {
        printf("FAIL: %s\n", scan ? "runs handed over whole under the survey"
                                  : "no run handed over whole");
        return 1;
    }
    return 0;
}
