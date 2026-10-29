/* Run with: python tests/pace_spin/run.py
 *
 * Frame pacing on Linux spins only the last part of each wait, 0.3 ms to
 * start with. A sleep that ends close to where the spin begins widens it at
 * once (1.2 ms at most), frames on time narrow it again by 2 us each, and
 * RECOMP_PACE_SPIN_US fixes it. The sleep itself goes to an absolute time.
 */
static int fails;

static void check(int ok, const char *what, double got_ms)
{
    if (!ok) fails++;
    printf("%s: %s (%.3f ms)\n", ok ? "ok" : "FAIL", what, got_ms);
}

static int near(double a, double b) { return a > b - 1e-9 && a < b + 1e-9; }

int main(void)
{
    const char *fixed = getenv("RECOMP_PACE_SPIN_US");
    double t, m;
    int i;

    if (fixed) {
        check(near(pace_margin(), 0.0012), "RECOMP_PACE_SPIN_US=1200: the spin is 1.2 ms",
              1000.0 * pace_margin());
        pace_margin_learn(0.005);
        check(near(pace_margin(), 0.0012), "and a late sleep leaves it there", 1000.0 * pace_margin());
        check(near(g_pace_st.ran_over_max, 0.005), "though it is reported",
              1000.0 * g_pace_st.ran_over_max);
    } else {
        check(near(pace_margin(), 0.0003), "the spin starts at 0.3 ms", 1000.0 * pace_margin());
        pace_margin_learn(0.0001);
        check(near(pace_margin(), 0.0003), "a sleep 0.1 ms over leaves it", 1000.0 * pace_margin());
        pace_margin_learn(0.00025);
        check(near(pace_margin(), 0.00045) && g_pace_st.widened == 1,
              "one 0.25 ms over widens it to 0.45 ms", 1000.0 * pace_margin());
        for (i = 0; i < 50; i++) pace_margin_learn(0.0);
        check(near(pace_margin(), 0.00035), "50 frames on time narrow it by 0.1 ms",
              1000.0 * pace_margin());
        for (i = 0; i < 100; i++) pace_margin_learn(0.0);
        check(near(pace_margin(), 0.0003), "and no further than 0.3 ms", 1000.0 * pace_margin());
        pace_margin_learn(0.005);
        check(near(pace_margin(), 0.0012) && near(g_pace_st.margin_max, 0.0012),
              "a 5 ms overrun widens it to 1.2 ms at most", 1000.0 * pace_margin());
        pace_margin_learn(-0.0002);
        check(pace_margin() < 0.0012, "a sleep that ended early narrows it", 1000.0 * pace_margin());
    }

    t = mono_now() + 0.002;
    pace_sleep_until(t);
    m = mono_now();
    check(m >= t, "a sleep to 2 ms ahead ends at that time or after", 1000.0 * (m - t));
    /* "At once" is within 2 ms: a busy machine can hold the test up for a
     * while (0.66 ms once, just after a build), and a sleep that went wrong
     * takes far longer: a relative one to this time would sleep for hours. */
    t = mono_now() - 0.001;
    pace_sleep_until(t);
    m = mono_now();
    check(m - t < 0.001 + 0.002, "a sleep to a time already past returns at once",
          1000.0 * (m - t - 0.001));

    printf(fails ? "pace_spin: %d case(s) FAIL\n" : "pace_spin: all cases pass\n", fails);
    return fails ? 1 : 0;
}
