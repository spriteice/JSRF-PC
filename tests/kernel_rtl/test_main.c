/* Run with: python tests/kernel_rtl/run.py
 *
 * What the game calls of the kernel's run-time library
 * (src/kernel/kernel_rtl.c), against known answers:
 *
 *   1. RtlTimeFieldsToTime: dates to their 100 ns count since 1 January
 *      1601, the kernel's system time. The answers are Python's datetime's
 *      (past 9999, by its 400-year cycle), and 1970's and 2000's are the
 *      well-known ones. 1601 itself, the years before 1970, leap days, the
 *      last millisecond of a year, 2038 (past a 32-bit time_t), 1900 and 2100
 *      (no leap day) and the last date Win32 takes, 30827, among them. A date
 *      that doesn't exist, or a field out of range, is refused.
 *   2. RtlTimeToTimeFields: the same counts back to their dates, the
 *      weekday too (0 is Sunday), and a count part way through a millisecond
 *      in the millisecond it is in; and the last count below 2^63.
 *   3. RtlEqualString: counted strings, with and without case; only Length
 *      bytes count, a 0 among them too, and strings of two lengths are never
 *      equal.
 *
 * The game keeps its saves' times with these (XapiGetSystemTime and
 * FILETIME's helpers in its XAPI), and compares its file names with
 * RtlEqualString. The functions take host pointers, as kernel_bridge.c's
 * bridges pass them after turning the guest's addresses.
 */
#include "kernel.h"
#include <stdio.h>
#include <string.h>
#include "xbox_memory_layout.h"

#define TICKS_PER_MS  10000ull

/* What kernel_rtl.c's critical sections and their reports take from the
 * rest of the runtime. None of it is used here. */
RECOMP_TLS uint32_t g_esp, g_xbox_kernel_caller;
uint32_t g_xbox_code_lo, g_xbox_code_hi;
ptrdiff_t g_xbox_mem_offset;
void xbox_kernel_call_waited(int waited) { (void)waited; }
void xbox_log(int level, const char *subsystem, const char *fmt, ...)
{
    (void)level; (void)subsystem; (void)fmt;
}

static int s_failures;

static void fail(const char *what, const char *which)
{
    if (s_failures++ < 20)
        printf("kernel_rtl: %s: %s\n", what, which);
    fflush(stdout);
}

/* ---- 1 and 2: times ------------------------------------------------------ */

typedef struct {
    const char *name;
    SHORT year, month, day, hour, minute, second, ms, weekday;
    unsigned long long ticks;
} TimeCase;

static const TimeCase k_times[] = {
    { "1601-01-01",              1601,  1,  1,  0,  0,  0,   0, 1,                   0ull },
    { "1601-12-31 23:59:59.999", 1601, 12, 31, 23, 59, 59, 999, 1,     315359999990000ull },
    { "1899-12-31 12:00",        1899, 12, 31, 12,  0,  0,   0, 0,   94354416000000000ull },
    { "1900-03-01",              1900,  3,  1,  0,  0,  0,   0, 4,   94405824000000000ull },
    { "1969-12-31 23:59:59.999", 1969, 12, 31, 23, 59, 59, 999, 3,  116444735999990000ull },
    { "1970-01-01",              1970,  1,  1,  0,  0,  0,   0, 4, 116444736000000000ull },
    { "2000-01-01",              2000,  1,  1,  0,  0,  0,   0, 6, 125911584000000000ull },
    { "2000-02-29 12:34:56.789", 2000,  2, 29, 12, 34, 56, 789, 2, 125963012967890000ull },
    { "2001-03-01",              2001,  3,  1,  0,  0,  0,   0, 4, 126278784000000000ull },
    { "2004-12-31 23:59:59.999", 2004, 12, 31, 23, 59, 59, 999, 5, 127490111999990000ull },
    { "2026-11-05 10:57:48.250", 2026, 11,  5, 10, 57, 48, 250, 4, 134383498682500000ull },
    { "2038-01-19 03:14:08",     2038,  1, 19,  3, 14,  8,   0, 2, 137919572480000000ull },
    { "2099-12-31 23:59:59",     2099, 12, 31, 23, 59, 59,   0, 4, 157469183990000000ull },
    { "2100-03-01",              2100,  3,  1,  0,  0,  0,   0, 1, 157520160000000000ull },
    { "9999-12-31 23:59:59.999", 9999, 12, 31, 23, 59, 59, 999, 5, 2650467743999990000ull },
    { "30827-12-31 23:59:59.999", 30827, 12, 31, 23, 59, 59, 999, 5, 9223149887999990000ull },
};

/* Dates that don't exist and fields out of range: RtlTimeFieldsToTime says
 * FALSE, as the console's kernel and Win32 do. */
static const TimeCase k_refused[] = {
    { "29 February 2001",  2001,  2, 29,  0,  0,  0,    0, 0, 0 },
    { "30 February 2000",  2000,  2, 30,  0,  0,  0,    0, 0, 0 },
    { "29 February 2100",  2100,  2, 29,  0,  0,  0,    0, 0, 0 },
    { "31 April",          2001,  4, 31,  0,  0,  0,    0, 0, 0 },
    { "32 January",        2001,  1, 32,  0,  0,  0,    0, 0, 0 },
    { "day 0",             2001,  1,  0,  0,  0,  0,    0, 0, 0 },
    { "month 0",           2001,  0,  1,  0,  0,  0,    0, 0, 0 },
    { "month 13",          2001, 13,  1,  0,  0,  0,    0, 0, 0 },
    { "hour 24",           2001,  1,  1, 24,  0,  0,    0, 0, 0 },
    { "minute 60",         2001,  1,  1,  0, 60,  0,    0, 0, 0 },
    { "second 60",         2001,  1,  1,  0,  0, 60,    0, 0, 0 },
    { "1000 ms",           2001,  1,  1,  0,  0,  0, 1000, 0, 0 },
    { "-1 ms",             2001,  1,  1,  0,  0,  0,   -1, 0, 0 },
    { "1600",              1600, 12, 31, 23, 59, 59,  999, 0, 0 },
    { "30828",            30828,  1,  1,  0,  0,  0,    0, 0, 0 },
};

/* The last count below 2^63, which Win32 still turns into a date. */
static const TimeCase k_last_count =
    { "30828-09-14 02:48:05.477", 30828, 9, 14, 2, 48, 5, 477, 4, 0x7FFFFFFFFFFFFFFFull };

static unsigned long long ticks_of(const LARGE_INTEGER *t)
{
    return ((unsigned long long)(uint32_t)t->HighPart << 32) | t->LowPart;
}

static void set_ticks(LARGE_INTEGER *t, unsigned long long ticks)
{
    t->LowPart = (DWORD)ticks;
    t->HighPart = (LONG)(ticks >> 32);
}

static void times_known(void)
{
    size_t i;
    for (i = 0; i < sizeof k_times / sizeof k_times[0]; i++) {
        const TimeCase *c = &k_times[i];
        XBOX_TIME_FIELDS f, back;
        LARGE_INTEGER t;
        f.Year = c->year; f.Month = c->month; f.Day = c->day; f.Hour = c->hour;
        f.Minute = c->minute; f.Second = c->second; f.Milliseconds = c->ms;
        /* The weekday is the console's to work out, not the caller's to say:
         * a wrong one is ignored. */
        f.Weekday = (SHORT)((c->weekday + 3) % 7);
        set_ticks(&t, 0);
        if (!xbox_RtlTimeFieldsToTime(&f, &t))
            fail("RtlTimeFieldsToTime refused a date", c->name);
        else if (ticks_of(&t) != c->ticks)
            fail("RtlTimeFieldsToTime gave the wrong time", c->name);

        set_ticks(&t, c->ticks);
        memset(&back, 0x55, sizeof back);
        xbox_RtlTimeToTimeFields(&t, &back);
        if (back.Year != c->year || back.Month != c->month || back.Day != c->day
                || back.Hour != c->hour || back.Minute != c->minute || back.Second != c->second
                || back.Milliseconds != c->ms)
            fail("RtlTimeToTimeFields gave the wrong date", c->name);
        if (back.Weekday != c->weekday)
            fail("RtlTimeToTimeFields gave the wrong weekday", c->name);

        /* 99.99 microseconds more: still the same millisecond. */
        set_ticks(&t, c->ticks + TICKS_PER_MS - 1);
        xbox_RtlTimeToTimeFields(&t, &back);
        if (back.Second != c->second || back.Milliseconds != c->ms)
            fail("RtlTimeToTimeFields didn't keep a count to its millisecond", c->name);
    }
}

static void times_refused(void)
{
    size_t i;
    for (i = 0; i < sizeof k_refused / sizeof k_refused[0]; i++) {
        const TimeCase *c = &k_refused[i];
        XBOX_TIME_FIELDS f;
        LARGE_INTEGER t;
        f.Year = c->year; f.Month = c->month; f.Day = c->day; f.Hour = c->hour;
        f.Minute = c->minute; f.Second = c->second; f.Milliseconds = c->ms;
        f.Weekday = 0;
        set_ticks(&t, 0);
        if (xbox_RtlTimeFieldsToTime(&f, &t))
            fail("RtlTimeFieldsToTime took a date that doesn't exist", c->name);
    }
}

static void time_last_count(void)
{
    const TimeCase *c = &k_last_count;
    XBOX_TIME_FIELDS back;
    LARGE_INTEGER t;
    set_ticks(&t, c->ticks);
    memset(&back, 0x55, sizeof back);
    xbox_RtlTimeToTimeFields(&t, &back);
    if (back.Year != c->year || back.Month != c->month || back.Day != c->day
            || back.Hour != c->hour || back.Minute != c->minute || back.Second != c->second
            || back.Milliseconds != c->ms || back.Weekday != c->weekday)
        fail("RtlTimeToTimeFields gave the wrong date", c->name);
}

/* ---- 3: RtlEqualString --------------------------------------------------- */

static XBOX_ANSI_STRING counted(const char *s, USHORT length)
{
    XBOX_ANSI_STRING a;
    a.Length = length;
    a.MaximumLength = length;
    a.Buffer = (PCHAR)s;
    return a;
}

static void equal_strings(void)
{
    struct { const char *a, *b; USHORT la, lb; BOOLEAN nocase, equal; const char *name; } k[] = {
        { "default.xbe", "default.xbe", 11, 11, FALSE, TRUE,  "the same, with case" },
        { "Media", "MEDIA", 5, 5, TRUE,  TRUE,  "case left out" },
        { "Media", "MEDIA", 5, 5, FALSE, FALSE, "case kept" },
        { "Media", "Medium", 5, 6, TRUE, FALSE, "two lengths" },
        { "Media", "Media", 5, 4, FALSE, FALSE, "one cut short" },
        { "abcX", "abcY", 3, 3, FALSE, TRUE,  "only Length bytes count" },
        { "abcX", "ABCY", 3, 3, TRUE,  TRUE,  "only Length bytes count, case left out" },
        { "", "", 0, 0, FALSE, TRUE, "two empty strings" },
        { "a[b", "A{B", 3, 3, TRUE, FALSE, "case left out of letters only" },
        { "a\0b", "a\0c", 3, 3, FALSE, FALSE, "a 0 inside, then different" },
        { "a\0b", "a\0c", 3, 3, TRUE,  FALSE, "a 0 inside, then different, case left out" },
        { "a\0b", "a\0b", 3, 3, FALSE, TRUE,  "a 0 inside, the same" },
        { "a\0b", "A\0B", 3, 3, TRUE,  TRUE,  "a 0 inside, case left out" },
        { "ab\0", "ab\0", 3, 3, FALSE, TRUE,  "a 0 counted at the end" },
    };
    size_t i;
    for (i = 0; i < sizeof k / sizeof k[0]; i++) {
        XBOX_ANSI_STRING a = counted(k[i].a, k[i].la), b = counted(k[i].b, k[i].lb);
        if (!xbox_RtlEqualString(&a, &b, k[i].nocase) != !k[i].equal)
            fail(k[i].equal ? "RtlEqualString called equal strings different"
                            : "RtlEqualString called different strings equal", k[i].name);
    }
}

int main(void)
{
    times_known();
    times_refused();
    time_last_count();
    equal_strings();
    if (s_failures) {
        printf("kernel_rtl: %d failures\n", s_failures);
        return 1;
    }
    printf("kernel_rtl: all cases pass\n");
    return 0;
}
