/* Run with: python tests/recomp_check/run.py
 *
 * RECOMP_CHECK, src/platform/recomp_check.h, built as every build is: with
 * NDEBUG, which takes assert() out. The test's own stderr goes to a file
 * (argv[1]), so each case can read what its checks wrote:
 *
 *   1. A check that holds writes nothing, is 1, and doesn't evaluate its
 *      message's arguments.
 *   2. One that fails is 0, writes one line naming its file, line, function,
 *      condition and message, and the code after it runs.
 *   3. The same place failing again, a thousand times, writes nothing more;
 *      another place writes its own line.
 *   4. The condition is evaluated once each time, failing or not.
 *   5. Eight threads failing one check at once write one line between them.
 *   6. In C++ (test_cpp.cpp, built as the menu is): the same line.
 */
#include "platform/xbox_winnt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform/recomp_check.h"

#define THREADS         8
#define EACH_FAILS      10000
#define AGAIN           1000

int recomp_check_from_cpp(int rows);

static const char *s_log;
static long s_read;          /* how much of the log the cases have read */
static int s_failures;

static void fail(const char *what)
{
    if (s_failures++ < 20)
        printf("recomp_check: %s\n", what);
    fflush(stdout);
}

/* What the checks have written since the last call: the [CHECK] lines, the
 * last of them in last. */
static int new_lines(char *last, size_t room)
{
    char line[1024];
    int n = 0;
    FILE *f;

    fflush(stderr);
    f = fopen(s_log, "r");
    if (!f) { fail("the log can't be read"); return -1; }
    fseek(f, s_read, SEEK_SET);
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "[CHECK] ", 8) != 0) continue;
        n++;
        if (last) {
            line[strcspn(line, "\n")] = 0;
            snprintf(last, room, "%s", line);
        }
    }
    s_read = ftell(f);
    fclose(f);
    return n;
}

static int s_said;
static int said(int v) { s_said++; return v; }

static int s_evaluated;
static int counted(int v) { s_evaluated++; return v; }

static int s_line_a;   /* place_a's line, for its check's */
static int place_a(int entries) { s_line_a = __LINE__; return RECOMP_CHECK(entries <= 8, "%d entries, room for %d", entries, 8); }
static int place_b(int entries) { return RECOMP_CHECK(entries <= 8, "another place: %d", entries); }

static void holds(void)
{
    if (RECOMP_CHECK(1 + 1 == 2, "%d", said(1)) != 1)
        fail("a check that holds isn't 1");
    if (s_said)
        fail("a check that holds evaluated its message");
    if (new_lines(NULL, 0) != 0)
        fail("a check that holds wrote to the log");
}

static void fails_once(void)
{
    char line[1024], want[256];
    int i, went_on = 0;

    if (place_a(9) != 0)
        fail("a check that fails isn't 0");
    went_on = 1;
    if (new_lines(line, sizeof line) != 1)
        fail("a check that fails didn't write one line");
    snprintf(want, sizeof want, "[CHECK] test_main.c:%d in place_a: entries <= 8 failed: 9 entries, room for 8",
             s_line_a);
    if (strcmp(line, want) != 0) {
        fail("a check's line isn't as it should be");
        printf("recomp_check:   wrote  \"%s\"\nrecomp_check:   wanted \"%s\"\n", line, want);
    }
    if (!went_on)
        fail("the code after a failed check didn't run");

    for (i = 0; i < AGAIN; i++)
        if (place_a(10 + i) != 0) fail("a check that fails again isn't 0");
    if (new_lines(NULL, 0) != 0)
        fail("a place's check that failed again wrote again");
    if (place_b(12) != 0 || new_lines(line, sizeof line) != 1 || !strstr(line, "another place: 12"))
        fail("another place's check didn't write its own line");
}

static void evaluated_once(void)
{
    s_evaluated = 0;
    RECOMP_CHECK(counted(1), "holds");
    RECOMP_CHECK(counted(0), "fails");
    RECOMP_CHECK(counted(0), "fails");
    if (s_evaluated != 3)
        fail("a check's condition wasn't evaluated once each time");
    new_lines(NULL, 0);
}

static volatile LONG s_go;
static int shared_check(int i) { return RECOMP_CHECK(i < 0, "thread's pass %d", i); }

static DWORD WINAPI hammer(LPVOID arg)
{
    int i, wrong = 0;
    (void)arg;
    while (!__atomic_load_n(&s_go, __ATOMIC_ACQUIRE)) { }
    for (i = 0; i < EACH_FAILS; i++)
        if (shared_check(i) != 0) wrong = 1;
    return (DWORD)wrong;
}

static void threads_once(void)
{
    HANDLE h[THREADS];
    DWORD code;
    int i, n;

    for (i = 0; i < THREADS; i++) {
        h[i] = CreateThread(NULL, 0, hammer, NULL, 0, NULL);
        if (!h[i]) { fail("a thread didn't start"); return; }
    }
    __atomic_store_n(&s_go, 1, __ATOMIC_RELEASE);
    for (i = 0; i < THREADS; i++) {
        WaitForSingleObject(h[i], INFINITE);
        if (GetExitCodeThread(h[i], &code) && code)
            fail("a check that failed on a thread wasn't 0");
        CloseHandle(h[i]);
    }
    n = new_lines(NULL, 0);
    if (n != 1) {
        char what[96];
        snprintf(what, sizeof what, "%d threads failing one check wrote %d lines, not 1", THREADS, n);
        fail(what);
    }
}

static void in_cpp(void)
{
    char line[1024];
    if (recomp_check_from_cpp(3) != 1 || new_lines(NULL, 0) != 0)
        fail("C++: a check that holds isn't 1, or wrote");
    if (recomp_check_from_cpp(5) != 0 || new_lines(line, sizeof line) != 1
            || !strstr(line, "[CHECK] test_cpp.cpp:") || !strstr(line, "rows <= 4 failed: 5 rows, room for 4"))
        fail("C++: a check that fails didn't write its line");
}

int main(int argc, char **argv)
{
    if (argc < 2) { printf("recomp_check: give the log's path\n"); return 2; }
    s_log = argv[1];
    if (!freopen(s_log, "w", stderr)) { printf("recomp_check: %s can't be written\n", s_log); return 2; }
    holds();
    fails_once();
    evaluated_once();
    threads_once();
    in_cpp();
    if (s_failures) {
        printf("recomp_check: %d failures\n", s_failures);
        return 1;
    }
    printf("recomp_check: all cases pass\n");
    return 0;
}
