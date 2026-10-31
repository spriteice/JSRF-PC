/* Run with: python tests/heap_lock/run.py
 *
 * Four threads allocate from the guest heap at once and fill each block with
 * a byte of their own. A block handed out twice is overwritten by its other
 * owner, or zeroed when that one is allocated, so every block is checked
 * whole before it is freed. Sizes are mostly small, with the two JSRF's
 * workers ask for when they start: 4,500 bytes for a TIB and 512 KB for a
 * stack.
 */
#define THREADS 4
#define ROUNDS  20000
#define LIVE    24

static volatile int s_lost;

static uint32_t next_random(uint32_t *x)
{
    *x ^= *x << 13; *x ^= *x >> 17; *x ^= *x << 5;
    return *x;
}

static int intact(uint32_t va, uint32_t size, unsigned char fill)
{
    const unsigned char *p = (const unsigned char *)(g_memory_offset + va);
    uint32_t i;

    for (i = 0; i < size; i++)
        if (p[i] != fill) return 0;
    return 1;
}

static void *worker(void *arg)
{
    int t = (int)(intptr_t)arg, n = 0, big = 0, i;
    uint32_t x = 2463534242u + (uint32_t)t * 7919u;
    struct { uint32_t va, size; unsigned char fill; } live[LIVE];

    for (i = 0; i < ROUNDS && !s_lost; i++) {
        uint32_t r = next_random(&x), size, align = 16;
        if (n == LIVE || (n && r % 5 < 2)) {
            int k = (int)(next_random(&x) % (uint32_t)n);
            if (!intact(live[k].va, live[k].size, live[k].fill)) {
                printf("thread %d: the block at 0x%08X (%u bytes) changed under it: "
                       "another thread was handed the same memory\n",
                       t, live[k].va, live[k].size);
                s_lost = 1;
                break;
            }
            if (live[k].size >= 512 * 1024) big--;
            xbox_HeapFree(live[k].va);
            live[k] = live[--n];
            continue;
        }
        if (r % 64 == 0 && big < 2) { size = 512 * 1024; align = 4096; }
        else if (r % 8 == 0) size = 4500;
        else size = 16 + next_random(&x) % 4096;
        live[n].va = xbox_HeapAlloc(size, align);
        if (!live[n].va) continue;
        live[n].size = size;
        live[n].fill = (unsigned char)(1 + (t * 61 + i) % 255);
        memset((void *)(g_memory_offset + live[n].va), live[n].fill, size);
        if (size >= 512 * 1024) big++;
        n++;
    }
    return NULL;
}

int main(void)
{
    pthread_t threads[THREADS];
    int t;
    void *ram = calloc(1, XBOX_HEAP_TOP);

    if (!ram) { printf("heap_lock: no memory\n"); return 1; }
    g_memory_offset = g_xbox_mem_offset = (ptrdiff_t)ram;
    g_reserve_next = XBOX_HEAP_TOP;
    InitializeCriticalSection(&g_heap_lock);    /* as xbox_MemoryLayoutInit does */
    xbox_HeapFree(xbox_HeapAlloc(16, 16));      /* the settings, read once */

    for (t = 0; t < THREADS; t++)
        pthread_create(&threads[t], NULL, worker, (void *)(intptr_t)t);
    for (t = 0; t < THREADS; t++)
        pthread_join(threads[t], NULL);
    if (s_lost) return 1;
    printf("%d rounds on %d threads at once, no block handed out twice\n",
           THREADS * ROUNDS, THREADS);
    return 0;
}
