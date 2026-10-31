/* The sound library's stream errors: JSRF_ADX_ERR_LOG=1, Developer > Logging.
 *
 * CRI's ADXT watchdog marks a stream that has had no data for too long with an
 * error code, runs the stream's recovery (a stop, or a restart from the top),
 * and clears the code again. The game checks its streams' codes every frame,
 * on the main thread, and raises the "disc can't be read" error for one that
 * isn't 0. On the console the main thread can't run while the recovery does;
 * these lines say whether it can here. The hooks are in recomp_manual.c:
 * sub_0013A6B0 and sub_0013D250 (the recoveries), sub_00116E30 (the sound
 * manager's update, where the game checks), sub_0006F730 (the error the
 * check raises), and sub_0013B0A0 and sub_0013B0E0 (CRI's lock and unlock,
 * which keep every other thread out on the console). */
#ifndef JSRF_ADX_ERR_H
#define JSRF_ADX_ERR_H

#include <stdint.h>

/* Whether JSRF_ADX_ERR_LOG is set: read once. */
int adx_err_on(void);

/* A recovery starts, at now (seconds) on the thread running it: handle h,
 * with error code code, restarted (or stopped) for the watchdog's call from
 * from. dev and sectors are the file it reads (the [CVFS] lines' h=, and its
 * size), and depth how deep CRI's own lock was held. From here until
 * adx_err_recovery_end, every take of the guest lock by another thread is
 * noted. */
void adx_err_recovery_begin(double now, uint32_t h, int code, int restart, uint32_t from,
                            uint32_t dev, uint32_t sectors, int depth);

/* The recovery of h is over: its lines go to the log. */
void adx_err_recovery_end(uint32_t h);

/* The sound manager's update starts, on the game's main thread. */
void adx_err_sound_update(void);

/* The game raised a stream's error code, at now, on this thread: says whether
 * a recovery was running then, and which. */
void adx_err_raised(double now, int code);

/* CRI's lock (sub_0013B0A0) and unlock (sub_0013B0E0), at the layer that
 * raises its holder to time-critical and resumes the spinner. depth is how
 * deep it was held as the call began; lock_end and unlock_end follow the
 * generated body, unlock_end with the depth after it. A hold runs from the
 * outermost lock to the unlock that brings it back to 0, and every take of
 * the guest lock by another thread in that time is counted, with when in the
 * hold it came: on the console no thread below time-critical runs then. The
 * counts go to the log every 10 s of the run's clock (now, in seconds). */
void adx_err_crs_lock(double now, int depth);
void adx_err_crs_lock_end(void);
void adx_err_crs_unlock(int depth);
void adx_err_crs_unlock_end(double now, int depth);

#endif
