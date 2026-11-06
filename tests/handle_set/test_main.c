/* Run with: python tests/handle_set/run.py
 *
 * src/platform/win32_compat.c keeps a set of every object it has made, and
 * follows a handle only if it is in the set, so a stale or bogus handle fails
 * the way it does on Win32 (known bug 23: a closed handle stayed in the set,
 * and found its object after it was freed).
 *
 * - closed: a closed event's handle fails, in SetEvent, in a wait and in a
 *   second close. Before the fix each read the freed object, which the
 *   address sanitizer reports.
 * - reused: the set gives each closed handle's slot back. It holds 8,192:
 *   with that many events open and then closed, events made after them are
 *   all followed. Before the fix the set stayed full of closed handles, so a
 *   new event at an address none of them had wasn't added, and failed. The
 *   test takes the freed objects' memory first, so the new events are at new
 *   addresses.
 * - probe runs: the objects after a closed one in its run are still found,
 *   as the set is probed in a line and its gap is filled.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "win32_compat.h"

#define SET_SLOTS 8192         /* W32_OBJ_SET in win32_compat.c */
#define NEW_EVENTS 64

static int failures;

static void check(int ok, const char *what)
{
    if (!ok) { printf("FAIL: %s\n", what); failures++; }
}

static int works(HANDLE e)
{
    return e && SetEvent(e) && WaitForSingleObject(e, 0) == WAIT_OBJECT_0;
}

int main(void)
{
    static HANDLE many[SET_SLOTS];
    static void *taken[SET_SLOTS * 2];
    HANDLE e;
    int i, n, ok;

    /* closed */
    e = CreateEventA(NULL, TRUE, FALSE, NULL);
    check(works(e), "closed: a new event works");
    CloseHandle(e);
    check(!SetEvent(e), "closed: SetEvent on a closed handle failed");
    check(WaitForSingleObject(e, 0) == WAIT_FAILED, "closed: a wait on a closed handle failed");
    CloseHandle(e);   /* a second close: nothing to follow */

    /* reused */
    for (i = 0; i < SET_SLOTS; i++)
        many[i] = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (i = 0; i < SET_SLOTS; i++)
        CloseHandle(many[i]);
    for (i = 0; i < SET_SLOTS * 2; i++)
        taken[i] = malloc(64 + (i % 8) * 128);
    for (i = 0, ok = 0; i < NEW_EVENTS; i++) {
        e = CreateEventA(NULL, TRUE, FALSE, NULL);
        ok += works(e);
        many[i] = e;
    }
    printf("handle_set: %d of %d events made after %d closed work\n", ok, NEW_EVENTS, SET_SLOTS);
    check(ok == NEW_EVENTS, "reused: every event made after the closed ones works");

    /* probe runs: close every other one of those, and the rest still work */
    for (i = 0; i < NEW_EVENTS; i += 2)
        CloseHandle(many[i]);
    for (i = 1, n = 0; i < NEW_EVENTS; i += 2)
        n += works(many[i]);
    check(n == NEW_EVENTS / 2, "probe runs: the events left open still work");
    for (i = 0; i < NEW_EVENTS; i += 2)
        check(!SetEvent(many[i]), "probe runs: a closed one fails");
    for (i = 1; i < NEW_EVENTS; i += 2)
        CloseHandle(many[i]);
    for (i = 0; i < SET_SLOTS * 2; i++)
        free(taken[i]);

    if (failures) return 1;
    printf("handle_set: all cases pass\n");
    return 0;
}
