/*
 * rumble_hold.h -- a pad's motors, held as the console holds them.
 *
 * The console's pad keeps its motors at the speeds it was last sent until it
 * is sent others, and a game sets them once, when they change: JSRF's pad
 * update (sub_00167180) calls XInputSetState only then. SDL's rumble instead
 * runs for a set time and stops. So the input layer keeps what the game set,
 * and this says when to send it to the host's pad: as soon as it changes, to
 * a pad it hasn't been sent to yet (one found later, or found again), and
 * again before SDL's time runs out, for as long as a motor runs.
 *
 * No SDL in here, so a test links it (tests/rumble).
 */
#ifndef XBOX_RUMBLE_HOLD_H
#define XBOX_RUMBLE_HOLD_H

#define RUMBLE_HOLD_MS 0xFFFFu  /* each send asks SDL for this long: its longest */
#define RUMBLE_RENEW_S 30.0     /* and is made again this long after the last */

typedef struct {
    unsigned short left, right; /* the motors' speeds, as the game last set them */
    long pad;                   /* the host pad they were last sent to ... */
    int  has_pad;               /* ... once they have been sent to one */
    double sent;                /* when they were (seconds, on a clock that only grows) */
    int  changed;               /* set to something else since */
} RumbleHold;

/* The game set the motors. */
void rumble_hold_set(RumbleHold *h, unsigned short left, unsigned short right);

/* Whether to send them to `pad` (an id SDL gives one pad's connection) now,
 * at `now`: 1, and noted as sent, or 0. */
int rumble_hold_due(RumbleHold *h, long pad, double now);

#endif /* XBOX_RUMBLE_HOLD_H */
