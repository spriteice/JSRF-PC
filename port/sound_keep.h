/* sound_keep.h -- the sound's slots kept to the game's passes while a run is
 * recorded or played.
 *
 * The sound manager (task 5) loads a track into one of its slots paused, the
 * slot's state 1: the music (+0xBC, or the next track while +0xA4 is set),
 * two for speech (+0xC0, +0xC4) and two more (+0xC8, +0xCC). Its update's
 * slot step (sub_00116C30) makes a slot 2, ready, on the first pass its
 * track has SOUND_KEEP_READY samples decoded (sub_0013A9A0), and unpauses it.
 * The data comes from the disc and the sound thread on the host's time, so
 * the pass a slot is ready on differs from run to run -- and the game waits
 * on it: the course's start sign for its speech line before it counts down
 * (sub_0010E2D0), the mission flow's steps 21, 27 and the rest for the music.
 * A play then let the skater go a pass or two sooner or later than its
 * recording did (input-playback/b2.2/PROPOSAL-4.md).
 *
 * While a run is recorded or played (playback_sound_kept: the passes its
 * recording keeps), a slot is ready that many passes after it was first seen
 * loading: held below ready before that pass, and on it waited for if its
 * data isn't there yet, the sound thread decoding while the game's thread
 * waits. The same pass in the recording and its plays, so nothing the game
 * waits on there moves. Outside them, as the game has it.
 *
 * Nothing here reads the guest: recomp_manual.c hands each pass's slots in
 * and asks for the count the slot step sees, as tests/playback's stand-in
 * sound does. The game's thread only.
 */
#ifndef SOUND_KEEP_H
#define SOUND_KEEP_H

#include <stdint.h>

#define SOUND_KEEP_SLOTS 6
#define SOUND_KEEP_READY 0x2000u    /* samples decoded: ready, to the slot step */
#define SOUND_KEEP_WAIT  10.0       /* seconds a slot's data is waited for at its pass, at most */

/* The slot a call from the slot step to the sample count is for, by where it
 * returns to; -1 for none. 0 is the music's next track, 1 the music, then
 * +0xC0, +0xC8, +0xCC and +0xC4, in the slot step's order. */
int sound_keep_site(uint32_t from);

/* Once a pass, before the slot step: the passes a slot is held after it is
 * first seen loading (0: keeping off), and each slot's state: loading, and
 * the track it loads (its ADXT), a new track starting the count again.
 * Turning keeping on or off, or changing its passes, starts afresh. */
void sound_keep_pass(int passes, const int loading[SOUND_KEEP_SLOTS], const uint32_t track[SOUND_KEEP_SLOTS]);

/* A run starts (playback's, recorded or played): keeping starts afresh, its
 * slots and what it did, at the next pass. Any thread. */
void sound_keep_start(void);

typedef struct {
    uint32_t (*count)(void *ctx);   /* the slot's samples decoded, now */
    void (*nap)(void *ctx);         /* a moment for the sound thread: about a millisecond */
    double (*now)(void);            /* seconds, for the wait's limit */
    void *ctx;
} SOUND_KEEP_IO;

/* The count the slot step sees for slot this pass, io's real one: below
 * ready before the slot's pass, and on it ready, waited for up to
 * SOUND_KEEP_WAIT (said in the log). As it is when keeping is off, the slot
 * isn't loading, or its wait ran out. */
uint32_t sound_keep_count(int slot, const SOUND_KEEP_IO *io);

typedef struct {
    int passes;                 /* kept to; 0: never kept */
    int ready;                  /* slots made ready on their pass */
    int data_least, data_most;  /* the passes after loading began their data was there (-1: none) */
    int waited;                 /* slots whose data was waited for at their pass */
    double wait_most;           /* seconds */
    int given_up;               /* not there after SOUND_KEEP_WAIT: ready on their data's time */
} SOUND_KEEP_STATS;

/* Since keeping last started, kept once it stops until it starts again, and
 * a line in the log saying them: playback's, as a run ends. */
const SOUND_KEEP_STATS *sound_keep_stats(void);
void sound_keep_say(void);

#endif
