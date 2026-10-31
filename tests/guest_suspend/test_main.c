/* Run with: python tests/guest_suspend/run.py
 *
 * Who holds the guest lock across NtSuspendThread and NtResumeThread
 * (bridge_NtSuspendThread, bridge_NtResumeThread and
 * guest_lock_hand_over_to_resumed in src/kernel/kernel_bridge.c, and
 * xbox_thread_is_current in src/kernel/kernel_thread.c, verbatim), with the
 * threads of src/platform/win32_compat.c. The guest lock is a plain mutex and
 * the hand-over is recorded, not made.
 *
 * - A thread that suspends itself, through -2, through its own handle and
 *   through a duplicate of it, gives the lock up while it is parked and takes
 *   it back: the main thread takes the lock meanwhile and resumes it. Had it
 *   parked holding the lock, the main thread would get it only once the park
 *   ran out (400 ms) and the thread had come back, which is checked.
 * - A suspend of another thread keeps the lock, and so does one of a null
 *   handle, which still reports success (bridge_NtSuspendThread says why).
 * - A resume that lets go a thread above the caller's priority hands the lock
 *   over, once, waiting even when nobody is queueing.
 * - A resume keeps the lock when the thread is at the same or a lower
 *   priority (also when the caller has raised itself above it, as CRI's lock
 *   does), when the thread stays suspended (its count was 2), when there was
 *   nothing to resume, at DISPATCH_LEVEL, inside a callback, and when the
 *   caller does not hold the lock.
 * - A resume writes the previous count back, and takes no pointer for it.
 * - NtSuspendThread (231) and NtResumeThread (224) are off kernel_call_blocks'
 *   list, and the other 13 calls stay on it.
 */

static void fail(const char *what)
{
    printf("FAIL: %s\n", what);
    fflush(stdout);
    _exit(1);
}

#define TOKEN(i)   (BRIDGE_HANDLE_TAG | (uint32_t)(i))
#define SELF_TOKEN 0xFFFFFFFEu   /* NtCurrentThread() */

/* Each thread's guest stack: the two arguments, and a word for the count. */
static uint32_t stack_for(int slot) { return 0x1000u + (uint32_t)slot * 0x100u; }
static uint32_t count_word(int slot) { return stack_for(slot) + 0x80u; }

static void call(void (*bridge)(void), int slot, uint32_t handle, uint32_t count_va)
{
    g_esp = stack_for(slot);
    BRIDGE_MEM32(g_esp) = handle;
    BRIDGE_MEM32(g_esp + 4) = count_va;
    if (count_va) BRIDGE_MEM32(count_va) = 0xAAAAAAAAu;
    bridge();
}

/* ---- a thread that suspends itself ------------------------------------- */

static volatile int g_go, g_returned;
static volatile uint32_t g_self_token;
static volatile uint32_t g_self_eax, g_self_count;
static volatile int g_self_holds_after, g_self_releases, g_self_acquires;

static DWORD WINAPI parks_itself(LPVOID arg)
{
    (void)arg;
    while (!__atomic_load_n(&g_go, __ATOMIC_ACQUIRE)) sched_yield();
    xbox_guest_lock_acquire();
    t_releases = t_acquires = 0;
    call(bridge_NtSuspendThread, 1, g_self_token, count_word(1));
    g_self_eax = g_eax;
    g_self_count = BRIDGE_MEM32(count_word(1));
    g_self_holds_after = t_guest_holds;
    g_self_releases = t_releases;
    g_self_acquires = t_acquires;
    __atomic_store_n(&g_returned, 1, __ATOMIC_RELEASE);
    xbox_guest_lock_release();
    return 0;
}

/* how: 0 through -2, 1 through its own handle, 2 through a duplicate. */
static void self_suspend(int how)
{
    static const char *const name[] = { "-2", "its own handle", "a duplicate of its handle" };
    HANDLE h, dup = NULL;
    int n = g_hand_overs;

    g_go = 0;
    g_returned = 0;
    h = CreateThread(NULL, 0, parks_itself, NULL, 0, NULL);
    if (!h) fail("CreateThread");
    s_handle_table[1] = h;
    if (how == 2) {
        if (!DuplicateHandle(GetCurrentProcess(), h, GetCurrentProcess(), &dup,
                             0, FALSE, DUPLICATE_SAME_ACCESS))
            fail("DuplicateHandle");
        s_handle_table[2] = dup;
    }
    g_self_token = how == 0 ? SELF_TOKEN : TOKEN(how);
    __atomic_store_n(&g_go, 1, __ATOMIC_RELEASE);

    /* Until it has parked: once it has, its own suspend is in the count, so
     * a suspend from here finds 1 there, and the resume after it puts the
     * count back without letting it go. */
    for (;;) {
        DWORD p = SuspendThread(h);
        ResumeThread(h);
        if (p == (DWORD)-1) fail("SuspendThread");
        if (p >= 1) break;
        sched_yield();
    }

    xbox_guest_lock_acquire();
    if (__atomic_load_n(&g_returned, __ATOMIC_ACQUIRE)) {
        printf("suspending itself through %s\n", name[how]);
        fail("it parked holding the guest lock (the lock came free only "
             "when its park ran out)");
    }
    t_releases = t_acquires = 0;
    call(bridge_NtResumeThread, 0, TOKEN(1), count_word(0));
    if (g_eax != 0) fail("the resume failed");
    if (BRIDGE_MEM32(count_word(0)) != 1)
        fail("the resume did not find it parked (its count was not 1)");
    if (g_hand_overs != n) fail("a resume at the same priority handed the lock over");
    if (!t_guest_holds || t_releases) fail("a resume at the same priority gave the lock up");
    xbox_guest_lock_release();

    if (WaitForSingleObject(h, 5000) != WAIT_OBJECT_0) fail("it did not come back");
    if (g_self_eax != 0) fail("its suspend did not report success");
    /* Its count is the one before it parked: 0, or 1 when a look from here
     * (the loop above) came first. */
    if (g_self_count > 1) fail("its suspend did not write its previous count");
    if (!g_self_holds_after) fail("it came back without the guest lock");
    if (g_self_releases != 1 || g_self_acquires != 1)
        fail("it did not give the lock up once and take it back once");
    CloseHandle(h);
    if (dup) CloseHandle(dup);
    s_handle_table[1] = s_handle_table[2] = NULL;
    printf("guest_suspend: a thread suspending itself through %s gives the lock up "
           "and takes it back\n", name[how]);
}

/* ---- suspends and resumes of another thread ----------------------------- */

static HANDLE g_stop;

static DWORD WINAPI waits(LPVOID arg)
{
    (void)arg;
    WaitForSingleObject(g_stop, INFINITE);
    return 0;
}

static void kept(const char *what)
{
    if (!t_guest_holds || t_releases) { printf("%s\n", what); fail("the lock was given up"); }
}

static void suspend_other(void)
{
    call(bridge_NtSuspendThread, 0, TOKEN(3), count_word(0));
    if (g_eax != 0) fail("a suspend of another thread failed");
    kept("suspending another thread");
}

/* Resumes it and says whether that handed the lock over, checking the count
 * written back. */
static int resume_other(uint32_t want_count)
{
    int n = g_hand_overs;
    call(bridge_NtResumeThread, 0, TOKEN(3), count_word(0));
    if (g_eax != 0) fail("a resume failed");
    if (BRIDGE_MEM32(count_word(0)) != want_count) {
        printf("count %u, wanted %u\n", (unsigned)BRIDGE_MEM32(count_word(0)), (unsigned)want_count);
        fail("a resume wrote the wrong previous count");
    }
    if (g_hand_overs > n) {
        if (g_hand_overs != n + 1) fail("a resume handed the lock over more than once");
        if (!g_hand_over_even) fail("a resume's hand-over waited only with someone queueing");
        if (!g_hand_over_counters_ok) fail("a resume's hand-over was counted as a time slice's");
        return 1;
    }
    return 0;
}

static void others(void)
{
    HANDLE h;

    g_stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    h = CreateThread(NULL, 0, waits, NULL, 0, NULL);
    if (!g_stop || !h) fail("CreateThread");
    s_handle_table[3] = h;

    if (xbox_thread_is_current(NULL)) fail("a null handle is the calling thread");
    if (xbox_thread_is_current(h)) fail("another thread's handle is the calling thread");
    if (!xbox_thread_is_current(GetCurrentThread())) fail("the calling thread is not itself");

    xbox_guest_lock_acquire();
    t_releases = t_acquires = 0;

    suspend_other();
    if (BRIDGE_MEM32(count_word(0)) != 0) fail("a suspend wrote the wrong previous count");
    if (resume_other(1)) fail("a resume at the same priority handed the lock over");
    kept("resuming a thread at the same priority");

    SetThreadPriority(h, THREAD_PRIORITY_ABOVE_NORMAL);
    suspend_other();
    if (!resume_other(1)) fail("a resume of a thread above the caller kept the lock");
    printf("guest_suspend: a suspend of another thread keeps the lock, and a resume "
           "hands it over only to a thread above the caller\n");

    suspend_other();
    suspend_other();
    if (resume_other(2)) fail("a resume that left the thread suspended handed the lock over");
    if (!resume_other(1)) fail("the resume that let it go kept the lock");
    if (resume_other(0)) fail("a resume with nothing to resume handed the lock over");

    t_irql = DISPATCH_LEVEL;
    suspend_other();
    if (resume_other(1)) fail("a resume at DISPATCH_LEVEL handed the lock over");
    t_irql = 0;
    t_in_callback = 1;
    suspend_other();
    if (resume_other(1)) fail("a resume inside a callback handed the lock over");
    t_in_callback = 0;

    xbox_guest_lock_release();
    call(bridge_NtSuspendThread, 0, TOKEN(3), count_word(0));
    if (resume_other(1)) fail("a resume without the lock handed it over");
    if (t_guest_holds) fail("a resume without the lock took it");
    xbox_guest_lock_acquire();
    t_releases = t_acquires = 0;

    SetThreadPriority(h, THREAD_PRIORITY_IDLE);
    suspend_other();
    if (resume_other(1)) fail("a resume of a thread below the caller handed the lock over");
    /* CRI's lock: its holder raised to time-critical, its spinner at HIGHEST. */
    SetThreadPriority(h, THREAD_PRIORITY_HIGHEST);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    suspend_other();
    if (resume_other(1)) fail("a resume from a raised holder handed the lock over");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    SetThreadPriority(h, THREAD_PRIORITY_NORMAL);
    kept("resuming at the same or a lower priority");

    suspend_other();
    call(bridge_NtResumeThread, 0, TOKEN(3), 0);
    if (g_eax != 0) fail("a resume with no pointer for the count failed");
    kept("resuming with no pointer for the count");
    printf("guest_suspend: a resume keeps the lock when the thread stays suspended, "
           "at DISPATCH_LEVEL, in a callback and without the lock\n");

    /* The bridge says when this fails, on stderr. */
    call(bridge_NtSuspendThread, 0, 0, count_word(0));
    if (g_eax != 0 || BRIDGE_MEM32(count_word(0)) != 0)
        fail("a suspend of a null handle did not report success");
    kept("suspending a null handle");
    printf("guest_suspend: a suspend of a null handle keeps the lock\n");

    xbox_guest_lock_release();
    SetEvent(g_stop);
    if (WaitForSingleObject(h, 5000) != WAIT_OBJECT_0) fail("the waiting thread did not end");
    CloseHandle(h);
    CloseHandle(g_stop);
    s_handle_table[3] = NULL;
}

/* ---- kernel_call_blocks -------------------------------------------------- */

static void call_list(void)
{
    static const int want[] = { 99, 159, 158, 233, 234, 235, 277, 219, 236, 190,
                                202, 255, 258 };
    int ordinal, i;
    for (ordinal = 0; ordinal < 512; ordinal++) {
        int listed = 0;
        for (i = 0; i < (int)(sizeof want / sizeof want[0]); i++)
            if (want[i] == ordinal) listed = 1;
        if (kernel_call_blocks((ULONG)ordinal) != listed) {
            printf("ordinal %d\n", ordinal);
            fail("kernel_call_blocks' list is not the 13 calls");
        }
    }
    printf("guest_suspend: kernel_call_blocks lists 13 calls, not 224 or 231\n");
}

int main(void)
{
    g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)g_guest;
    self_suspend(0);
    self_suspend(1);
    self_suspend(2);
    others();
    call_list();
    printf("guest_suspend: all cases pass\n");
    return 0;
}
