/*
 * host_info.h - what the host says about a run, for the log.
 *
 * Three things every log needs to be worth keeping as a baseline, and none
 * had before (b3's baseline build, the log check's asks 6 and 7, and the
 * performance review's M1):
 *
 *   [RUN] started 2026-11-28 03:22:43 UTC; switches: RECOMP_LOCK_WATCH=1 ...
 *
 * once at the start, after the Developer menu's switches are in the
 * environment: when the run started, by the UTC clock, and every RECOMP_
 * and JSRF_ variable it started with, so a log says which switches it had;
 *
 *   [HOST] t=130.0s: on mains (battery 87%, charging); platform profile ...
 *
 * at the start and at the head of each ten-second report: the seconds since
 * the start, and the power state, which moves a run's result more than
 * anything else seen. On Linux: on mains or on battery, the platform's power
 * profile, and for the game's thread and the renderer's the core each last
 * ran on with its clock, governor and energy preference. On Windows: the
 * same from the power status, the active power plan and its power mode, and
 * the cores' clocks as Windows gives them, as Windows doesn't say which core
 * another thread is on.
 *
 * Always on, and cheap: a thread is named once, and the files are read only
 * when a line is written.
 */
#ifndef HOST_INFO_H
#define HOST_INFO_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The two threads the [HOST] line follows. */
enum { HOST_THREAD_GAME, HOST_THREAD_RENDERER, HOST_THREADS };

/* The start: the clock's zero, then the [RUN] line and the first [HOST]
 * line. Once, from the main thread, after the switches are set. */
void host_info_start(void);

/* Seconds since host_info_start (0 before it). */
double host_info_seconds(void);

/* The host's monotonic clock in microseconds, for the reports' latencies
 * and waits (the [LOCK] report's, the sound's). */
long long host_info_us(void);

/* The calling thread is the game's or the renderer's. */
void host_info_thread(int which);

/* The [HOST] line, with prefix before it ("  [HOST] " in the report). */
void host_info_report(FILE *f, const char *prefix);

/* For the tests (tests/host_info):
 *
 * host_info_switches: the RECOMP_ and JSRF_ variables in env, NULL-ended,
 * in name order, as "NAME=value NAME=value" into out (at most n bytes, cut
 * short with "..." if they don't fit), or "none"; returns their count.
 *
 * host_info_power_linux: the power part of the [HOST] line read from the
 * files under root ("" for the system's own), for the threads with these
 * ids (0 for one not started), into out. */
int host_info_switches(char *const *env, char *out, size_t n);
void host_info_power_linux(const char *root, const long tids[HOST_THREADS],
                           char *out, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* HOST_INFO_H */
