/* The sticks' shape: Input > Behaviour > Stick shape, stick_shape= in the
 * settings file.
 *
 * Square, the default, stretches each stick's circle onto the Xbox
 * controller's square, so a full push reaches full on both axes at any
 * angle: its direction is kept, and its length divided by the larger of the
 * direction's two parts. Round passes the host pad's sticks to the game as
 * SDL reads them. JSRF makes the skater's push from the stick's
 * length (sub_000659C0: full strength at 99% of a straight push, in any
 * direction), so a round stick needs no square there; with Square, a part
 * push on a diagonal counts up to 41% stronger than the same push straight.
 * The game's other reads of one axis at a time stop at 0.5 (its menus) or
 * 0.7 (the right stick, the graffiti editor), under a round stick's 0.71 on
 * a diagonal. So Square is a choice of feel, not a fix: the default, as the
 * console's own pads reach the square, and play feels more responsive with it
 * (spriteice, 2026-11-21).
 *
 * Only the host pad's sticks, as xbox_InputGetState reads them, before the
 * keys go over them (the keys' diagonals stay on the circle) and before
 * input playback records the state, so a recording holds the stick as the
 * game got it and plays back the same whatever the setting is then. The free
 * camera takes the same right stick.
 *
 * No SDL in here, so a test links it (tests/stick_shape). */
#ifndef XBOX_STICK_SHAPE_H
#define XBOX_STICK_SHAPE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Its choices, in the row's order, and their values in the settings file. */
#define STICK_SHAPE_SQUARE  0
#define STICK_SHAPE_ROUND   1
#define STICK_SHAPE_CHOICES 2
#define STICK_SHAPE_DEFAULT STICK_SHAPE_SQUARE  /* as the console's pads reach */

/* One stick, x and y as the Xbox's (-32768 to 32767), stretched onto the
 * square: the axes unchanged, a diagonal's full push to the corner, each
 * axis clamped to its ends. */
void stick_shape_square(short *x, short *y);

/* The pad's sticks as the setting has them: the left stick's x and y, then
 * the right's. At each pad read, on the game's thread. */
void stick_shape_pad(short axes[4]);

/* The setting in use: STICK_SHAPE_SQUARE or STICK_SHAPE_ROUND. */
int stick_shape_get(void);

/* Once, at start (port/main.c): the setting from the settings file, said
 * in the log, and its row in Input > Behaviour, after Keyboard moves. */
void stick_shape_start(void);

#ifdef __cplusplus
}
#endif

#endif /* XBOX_STICK_SHAPE_H */
