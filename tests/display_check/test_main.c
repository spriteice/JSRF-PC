/* Run with: python tests/display_check/run.py
 *
 * The presenter's display check (src/nv2a_gl/nv2a_gl.c, pres_measure and
 * pres_check), cut by text, with swap() playing the display: each call
 * hands back the time a swap ended, the gap since the last one taken from
 * the case's script for that try.
 *
 *   1. A steady 120 Hz display is measured on the first try, at 120 Hz,
 *      over every gap after the first 8 swaps.
 *   2. A first try held up by start-up's work (ten gaps three refreshes
 *      long) is measured again, and the second, steady, measures it.
 *   3. 23 gaps alike of 31 are enough; 22 are not.
 *   4. A swap that doesn't wait (0.2 ms) is said so after one try, and so
 *      is one slower than any refresh (100 ms).
 *   5. Three uneven tries: still uneven, after three, and no more swaps.
 */
#include <math.h>
#include <string.h>

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("display_check: FAIL %s:%d: %s\n", \
                                         __FILE__, __LINE__, #c); s_fail++; } } while (0)

#define HZ120 (1.0 / 120.0)

/* The script: for each try, the gap of each swap after the first. */
static double s_gaps[PRES_TRIES + 1][PRES_SWAPS];
static double s_now;
static int s_calls;

static double swap(void)
{
    int try = s_calls / PRES_SWAPS, i = s_calls % PRES_SWAPS;
    if (try > PRES_TRIES) try = PRES_TRIES;
    s_now += i == 0 ? 0.5 : s_gaps[try][i];   /* a try starts after a while */
    s_calls++;
    return s_now;
}

/* Every try's gaps g, and in try `held` (0 the first), n of those after the
 * skipped ones h long instead (none when held < 0). */
static void script(double g, int held, int n, double h)
{
    int t, i;
    s_now = 100.0;
    s_calls = 0;
    for (t = 0; t <= PRES_TRIES; t++)
        for (i = 0; i < PRES_SWAPS; i++)
            s_gaps[t][i] = (t == held && i > PRES_SWAPS_SKIP && i <= PRES_SWAPS_SKIP + n) ? h : g;
}

static int run(double *period, double *med, int *kept, int *gaps, int *tries)
{
    *period = 0.0;
    return pres_check(swap, period, med, kept, gaps, tries);
}

int main(void)
{
    double period, med;
    int kept, gaps, tries, v;

    script(HZ120, -1, 0, 0.0);                                         /* 1 */
    v = run(&period, &med, &kept, &gaps, &tries);
    CHECK(v == PRES_PACED);
    CHECK(tries == 1 && s_calls == PRES_SWAPS);
    CHECK(gaps == PRES_SWAPS - PRES_SWAPS_SKIP - 1 && kept == gaps);
    CHECK(fabs(1.0 / period - 120.0) < 1e-6);

    script(HZ120, 0, 10, 3 * HZ120);                                   /* 2 */
    v = run(&period, &med, &kept, &gaps, &tries);
    CHECK(v == PRES_PACED);
    CHECK(tries == 2 && s_calls == 2 * PRES_SWAPS);
    CHECK(kept == gaps && fabs(1.0 / period - 120.0) < 1e-6);

    script(HZ120, 0, 8, 3 * HZ120);                                    /* 3: 23 of 31 */
    v = run(&period, &med, &kept, &gaps, &tries);
    CHECK(v == PRES_PACED && tries == 1 && kept == 23);
    script(HZ120, 0, 9, 3 * HZ120);                                    /*    22 of 31 */
    v = run(&period, &med, &kept, &gaps, &tries);
    CHECK(v == PRES_PACED && tries == 2);

    script(0.0002, -1, 0, 0.0);                                        /* 4 */
    v = run(&period, &med, &kept, &gaps, &tries);
    CHECK(v == PRES_NO_WAIT && tries == 1 && period == 0.0);
    CHECK(fabs(med - 0.0002) < 1e-9);
    script(0.1, -1, 0, 0.0);
    v = run(&period, &med, &kept, &gaps, &tries);
    CHECK(v == PRES_SLOW && tries == 1 && period == 0.0);

    script(HZ120, -1, 0, 0.0);                                         /* 5 */
    { int t, i;
      for (t = 0; t < PRES_TRIES; t++)
          for (i = PRES_SWAPS_SKIP + 1; i <= PRES_SWAPS_SKIP + 12; i++) s_gaps[t][i] = 3 * HZ120; }
    v = run(&period, &med, &kept, &gaps, &tries);
    CHECK(v == PRES_UNEVEN && tries == PRES_TRIES && s_calls == PRES_TRIES * PRES_SWAPS);
    CHECK(kept == gaps - 12 && fabs(med - HZ120) < 1e-9 && period == 0.0);

    if (s_fail) {
        printf("display_check: %d failed\n", s_fail);
        return 1;
    }
    printf("display_check: all cases pass\n");
    return 0;
}
