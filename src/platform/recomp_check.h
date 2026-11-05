/*
 * recomp_check.h - a check that stays in release builds.
 *
 * CI, the test builds and the releases are all built with NDEBUG, so an
 * assert() is in none of them. RECOMP_CHECK is the check that is:
 *
 *     if (!RECOMP_CHECK(n <= ROOM, "%u entries, room for %u", n, ROOM))
 *         n = ROOM;
 *
 * tests its condition each time it is reached, in every build, and is the
 * condition's truth (1 or 0), so the code can carry on as it should when it
 * fails. The first time a place's check fails it says so in the log, once,
 * as the heap guard says each of its findings once:
 *
 *     [CHECK] kernel_bridge.c:1234 in bridge_Foo: n <= ROOM failed: 9 entries, room for 8
 *
 * and never again for that place, so a check on a hot path can't fill the
 * log. It never stops the game: going on safely from a failed check is the
 * caller's to do.
 *
 * The condition is evaluated once. The message's arguments are evaluated
 * only when the line is written. Safe on any thread: of two threads that
 * fail one check at once, one writes the line. GCC and clang (statement
 * expressions and __atomic built-ins), in C and C++.
 */
#ifndef RECOMP_CHECK_H
#define RECOMP_CHECK_H

#include <stdarg.h>
#include <stdio.h>

#define RECOMP_CHECK(cond, ...) __extension__ ({                                        \
    static int recomp_check_said_;                                                      \
    int recomp_check_ok_ = (cond) ? 1 : 0;                                              \
    if (!recomp_check_ok_ && !__atomic_exchange_n(&recomp_check_said_, 1, __ATOMIC_RELAXED)) \
        recomp_check_failed(__FILE__, __LINE__, __func__, #cond, __VA_ARGS__);          \
    recomp_check_ok_;                                                                   \
})

/* RECOMP_CHECK's line. The file is named without its folders, which are the
 * build machine's. One write for the line, so another thread's output can't
 * split it. */
static inline void recomp_check_failed(const char *file, int line, const char *func,
                                       const char *cond, const char *fmt, ...)
    __attribute__((format(printf, 5, 6), cold));
static inline void recomp_check_failed(const char *file, int line, const char *func,
                                       const char *cond, const char *fmt, ...)
{
    char text[512];
    const char *base = file, *p;
    va_list ap;
    int n;

    for (p = file; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;
    n = snprintf(text, sizeof text, "[CHECK] %s:%d in %s: %s failed: ", base, line, func, cond);
    if (n > 0 && (size_t)n < sizeof text) {
        va_start(ap, fmt);
        vsnprintf(text + n, sizeof text - (size_t)n, fmt, ap);
        va_end(ap);
    }
    fprintf(stderr, "%s\n", text);
    fflush(stderr);
}

#endif /* RECOMP_CHECK_H */
