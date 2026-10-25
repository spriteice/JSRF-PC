
/* ---- the test ------------------------------------------------------------ */

/* mcpx_apu_frame_thread's loop, less the work: pace a voice processor frame
 * (throttle, verbatim), note when it ran, and every eighth frame send the
 * encode processor's 256 samples to the ring, as mcpx_apu_monitor_frame does.
 * The device is the SDL ring's fake sink (verbatim): 512 frames every
 * 10.7 ms. DirectSound waits for the next voice processor frame each time it
 * restarts a sound, so what matters is how long, from any moment, until the
 * next frame: the mean of that is the sum of the squared gaps over twice
 * their sum. */
#define SECONDS     3
#define WARM_US     500000      /* the ring fills at once at the start */
#define MAX_FRAMES  (SECONDS * 2000)

static int64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static int by_value(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

int main(void)
{
    static MCPXAPUState state;
    static int64_t at[MAX_FRAMES], gap[MAX_FRAMES];
    static int16_t eight[8 * NUM_SAMPLES_PER_FRAME * 2];
    MCPXAPUState *d = &state;
    int n = 0, first = -1, i, k = 0;
    uint32_t under0 = 0;
    int64_t t0, sum = 0, sq = 0;

    (void)g_dev; (void)sa_callback;         /* the real device's side: not here */
    pthread_mutex_init(&d->lock, NULL);
    pthread_cond_init(&d->cond, NULL);
    g_active = 1;
    if (pthread_create(&g_fake_thread, NULL, sa_fake_sink, NULL) != 0) return 2;

    pthread_mutex_lock(&d->lock);
    t0 = now_us();
    while (n < MAX_FRAMES && now_us() - t0 < SECONDS * 1000000LL) {
        throttle(d);
        at[n] = now_us();
        if (first < 0 && at[n] - t0 >= WARM_US) {
            first = n;
            under0 = atomic_load(&g_underruns);
        }
        n++;
        if ((d->ep_frame_div + 1) % 8 == 0)
            xa2_submit_samples(eight, 8 * NUM_SAMPLES_PER_FRAME);
        d->ep_frame_div++;
    }
    pthread_mutex_unlock(&d->lock);
    if (first < 0 || n - first < 100) return 3;

    for (i = first + 1; i < n; i++) {
        gap[k] = at[i] - at[i - 1];
        sum += gap[k];
        sq += gap[k] * gap[k];
        k++;
    }
    qsort(gap, (size_t)k, sizeof gap[0], by_value);
    printf("frames a second: %.0f\n", (n - 1 - first) * 1e6 / (double)(at[n - 1] - at[first]));
    printf("wait for the next frame: %.2f ms on average\n", sq / (2.0 * sum) / 1000.0);
    printf("gaps: 99%% under %.2f ms, the longest %.2f ms\n",
           gap[k * 99 / 100] / 1000.0, gap[k - 1] / 1000.0);
    printf("device underruns after the first half second: %u\n",
           atomic_load(&g_underruns) - under0);
    return 0;
}
