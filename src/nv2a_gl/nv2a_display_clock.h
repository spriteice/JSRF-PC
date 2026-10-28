/*
 * The display's refreshes, as the kernel counts them, for frame pacing on
 * Linux (nv2a_display_clock.c). Built where the window is SDL's with desktop
 * OpenGL on Linux; elsewhere NV2A_DISPLAY_CLOCK is not defined and none of
 * this is called.
 */
#ifndef NV2A_DISPLAY_CLOCK_H
#define NV2A_DISPLAY_CLOCK_H

#if defined(__linux__) && !defined(__ANDROID__) && defined(NV2A_GL_USE_SDL) && !defined(NV2A_GLES)
#define NV2A_DISPLAY_CLOCK 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Once, when the window is open. hz is the display's refresh rate as SDL
 * reports it, 0 if it does not. */
void nv2a_dclock_open(int hz);

/* For frame_pace, at each frame, as pres_lock: the period to pace at -- a
 * whole number of the display's refreshes within 3% of base -- with the time
 * of a refresh (ref), the refresh's length (refresh), and the point in it to
 * hand each frame over at (at, a fraction of a refresh). 0 when frames are
 * not to be paced by it. now is mono_now(). */
double nv2a_dclock_lock(double base, double now, double *ref, double *refresh, double *at);

/* When frame_pace let the frame go, for the report. */
void nv2a_dclock_handed_over(double t);

/* A line for the last 10 s, from the GL thread's report. */
void nv2a_dclock_report(void);

#ifdef __cplusplus
}
#endif

#endif
