
/* ---- the test --------------------------------------------------------------
 *
 * The start-up shader build (sdl_shader_cache) runs against a backend whose
 * programs take as long as the test says, on a clock the test moves. While
 * that clock moves, the freeze watch (kernel_freeze_watch, in the other
 * file) runs every 20 ms of it, as the report thread runs it in the game.
 *
 * The cases:
 *   - a build of ten seconds that shows its box all the way says nothing:
 *     known bug 45 was one [FROZEN] four seconds into such a build, with the
 *     game still at frame 0 and the box on screen;
 *   - a build that stops (one program doesn't come back for six seconds) is
 *     reported once, four seconds after the box was last shown, at frame 0;
 *   - after the build, the game presents three frames and stops: the report
 *     names frame 3, the game's own, whatever the box showed before it;
 *   - a frame the game presents without the window still counts.
 *
 * stderr, where the build and the watch write, goes to the file run.py
 * names, and is read back from there. */

#define WATCH_MS 20                     /* KERNEL_REPORT_MS, the report thread's */

static long long g_ms = 1000;           /* the clock: a second into the run */
static long long g_watch_at = 1000;     /* when the watch next runs */

static double mono_now(void) { return (double)g_ms / 1000.0; }
long long test_clock_ms(void);
long long test_clock_ms(void) { return g_ms; }
void test_watch(void);
void mcpx_apu_debug_print(FILE *f, const char *prefix);
void mcpx_apu_debug_print(FILE *f, const char *prefix) { (void)f; (void)prefix; }

/* Time passes, with the watch run on its 20 ms. */
static void pass(long long ms)
{
    long long end = g_ms + ms;
    while (g_watch_at <= end) {
        g_ms = g_watch_at;
        test_watch();
        g_watch_at += WATCH_MS;
    }
    g_ms = end;
}

/* The backend: programs that take g_cost_ms each to build, but for number
 * g_stuck_at (from 0; -1 for none), which takes g_stuck_ms. */
static int g_total, g_left, g_cost_ms, g_stuck_at = -1, g_stuck_ms;

static int be_cache_rebuild(void) { g_left = g_total; return g_total; }
static int be_cache_idle(double budget_ms)
{
    (void)budget_ms;
    pass(g_total - g_left == g_stuck_at ? g_stuck_ms : g_cost_ms);
    return --g_left;
}
static uint32_t be_target_new(uint32_t pw, uint32_t ph) { (void)pw; (void)ph; return 1; }
static void be_target_free(uint32_t rt) { (void)rt; }
static void be_target_bind(uint32_t rt) { (void)rt; }
static void be_clear(unsigned mask, const float rgba[4], float depth, uint32_t stencil,
                     const uint32_t rect[4])
{
    (void)mask; (void)rgba; (void)depth; (void)stencil; (void)rect;
}
static void be_state_invalidate(void) {}

static const Nv2aGfx g_backend = {
    .target_new = be_target_new, .target_free = be_target_free,
    .target_bind = be_target_bind, .clear = be_clear,
    .state_invalidate = be_state_invalidate,
    .cache_idle = be_cache_idle, .cache_rebuild = be_cache_rebuild,
};

/* The game's present, as far as these counters go: the frame to the window,
 * and one more flip. */
static void game_present(void)
{
    sdl_show(1, 640, 480, 0.0f);
    S.flips++;
}

/* stderr so far, from the file. */
static const char *g_err_path;
static char g_err[1 << 16];
static size_t err_now(void)
{
    FILE *f;
    size_t n = 0;
    fflush(stderr);
    f = fopen(g_err_path, "rb");
    if (f) {
        n = fread(g_err, 1, sizeof g_err - 1, f);
        fclose(f);
    }
    g_err[n] = 0;
    return n;
}

/* How many times text is in stderr from offset 'from' on, and the line of
 * the first, copied out. */
static int said(size_t from, const char *text, char *line, size_t size)
{
    size_t n = err_now();
    const char *p = from <= n ? g_err + from : g_err + n;
    int count = 0;
    if (size) line[0] = 0;
    while ((p = strstr(p, text)) != NULL) {
        if (!count++ && size) {
            const char *end = strchr(p, '\n');
            size_t len = end ? (size_t)(end - p) : strlen(p);
            if (len > size - 1) len = size - 1;
            memcpy(line, p, len);
            line[len] = 0;
        }
        p += strlen(text);
    }
    return count;
}

static int g_failed;
static void fail(const char *what, const char *line)
{
    printf("shader_build_watch: FAIL: %s%s%s\n", what, line && line[0] ? ": " : "",
           line ? line : "");
    g_failed = 1;
}

#define FROZEN "[FROZEN] nothing has been presented for "

/* The one [FROZEN] from 'from' on: after 4000 to 4020 ms (the watch's four
 * seconds, and its 20 ms), at the game's frame 'frame'. */
static void frozen_once(size_t from, unsigned long frame, const char *when)
{
    char line[200], want[48], what[160];
    long long ms = 0;
    int n = said(from, FROZEN, line, sizeof line);
    snprintf(want, sizeof want, "(still at frame %lu)", frame);
    if (n != 1) {
        snprintf(what, sizeof what, "%d [FROZEN] reports %s, not one", n, when);
        fail(what, line);
        return;
    }
    sscanf(line + strlen(FROZEN), "%lld", &ms);
    if (ms < 4000 || ms > 4000 + WATCH_MS) {
        snprintf(what, sizeof what, "[FROZEN] %s not four seconds after the last frame shown",
                 when);
        fail(what, line);
    } else if (!strstr(line, want)) {
        snprintf(what, sizeof what, "[FROZEN] %s doesn't say %s", when, want);
        fail(what, line);
    } else {
        printf("shader_build_watch: %s: %s\n", when, line);
    }
}

int main(int argc, char **argv)
{
    char line[200];
    size_t from;
    long long t0;
    unsigned long before;
    int shown;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2 || !freopen(argv[1], "wb", stderr)) {
        printf("shader_build_watch: FAIL: no file for stderr\n");
        return 1;
    }
    g_err_path = argv[1];
    G = &g_backend;

    /* A build of ten seconds, its box shown all the way: not a freeze. */
    from = err_now();
    g_total = 400; g_cost_ms = 25; g_stuck_at = -1;
    t0 = g_ms;
    before = nv2a_gl_progress();
    sdl_shader_cache();
    if (g_ms - t0 != 400 * 25 || g_left != 0)
        fail("the build didn't build its 400 programs in ten seconds", NULL);
    if (!said(from, "[SHADERS] rebuilt 400 programs from the cache before the first frame",
              line, sizeof line))
        fail("the build didn't say what it built", NULL);
    if (g_presented < 300 || g_box_up || g_box_total != 0)
        fail("the box wasn't shown with each program, and taken down after", NULL);
    if (nv2a_gl_flips() != 0)
        fail("the build counted as frames of the game's", NULL);
    if (nv2a_gl_progress() == before)
        fail("the picture's count didn't move while the box was shown", NULL);
    if (said(from, FROZEN, line, sizeof line))
        fail("[FROZEN] during a build that was showing its box", line);
    else
        printf("shader_build_watch: a build of %lld ms, the box shown %d times: nothing "
               "said\n", g_ms - t0, g_presented);

    /* A build that stops: program 10 of 40 doesn't come back for six
     * seconds. Said once, four seconds after the box was last shown. */
    from = err_now();
    g_total = 40; g_cost_ms = 25; g_stuck_at = 10; g_stuck_ms = 6000;
    sdl_shader_cache();
    frozen_once(from, 0, "a build that stopped for six seconds");

    /* The game's first three frames, then it stops. */
    from = err_now();
    shown = g_presented;
    game_present(); pass(17);
    game_present(); pass(17);
    game_present();
    if (g_presented != shown + 3)
        fail("the game's frames didn't reach the window", NULL);
    pass(6000);
    frozen_once(from, 3, "the game stopped after three frames");

    /* A frame the game presents without the window (it shows only every Nth
     * in a run told to) is the picture moving too. */
    before = nv2a_gl_progress();
    S.flips++;
    if (nv2a_gl_progress() == before)
        fail("a frame presented without the window didn't count", NULL);

    if (g_failed) return 1;
    printf("shader_build_watch: all cases pass\n");
    return 0;
}
