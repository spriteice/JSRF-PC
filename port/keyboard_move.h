/* Keyboard moves: W A S D steer the skater from the way it faces, not the
 * way the camera faces (Input > Behaviour > Keyboard moves, Skater; Camera,
 * the default, keeps the keys as the left stick, as before).
 *
 * The game moves its skater the way the left stick points, turned by the
 * way its camera looks: up on the stick is away from the camera. The keys
 * are that stick (src/input/xinput_device.c), so W went away from the camera
 * however the skater faced, which the free camera (port/freecam.c) or a
 * camera still catching up after a turn made hard to follow. With Skater,
 * the keys send the skater a way worked out from the way it faces: W
 * straight on, A and D a turn to that side while held (a quarter turn off
 * its facing, so the game turns the skater at its own rate), W with A or D a
 * wider turn, and S once round (the way behind the skater as S went down,
 * kept until the keys change). The camera turning, the free camera or the
 * game's own, doesn't change that way.
 *
 * Two places, both hooks in recomp_manual.c:
 *
 * - At each pad read (sub_001C3E14, XInputGetState), the stick the keys
 *   make is replaced by one that points that way from the camera's look
 *   (kbmove_pad), so the stick is at full tilt and the game reads the keys
 *   as held.
 * - At the player manager's update (sub_000A76E0), where the game turns the
 *   stick's angle into the way the skater goes, the angle it reads is put
 *   in from the camera and the skater as that update sees them
 *   (kbmove_angle), so the way is exact: the game squares an angle near
 *   straight up, and its camera's heading can move between the pad read and
 *   the update. The stick at the read stays for its strength.
 *
 * The game's own code isn't changed: it is handed a stick and an angle. */
#ifndef JSRF_KEYBOARD_MOVE_H
#define JSRF_KEYBOARD_MOVE_H

#include <stdint.h>

/* At start, before the game runs: reads the setting and adds the row, in
 * Input's Behaviour submenu. */
void kbmove_start(void);

/* The pad a player manager names (its +0x8AC, recomp_manual.c's DRV_PAD) as
 * the port that pad is read on: 0 to 3 are ports, and 4 is the game's own
 * slot for the pad it was started with, whose port the game keeps at
 * 0x251F20 (main_port) and looks up each time, as sub_0004EBC0 and
 * sub_00067030 do. -1 for anything else: the manager's -1 (no pad), or a
 * main_port that isn't a port. */
int kbmove_game_port(uint32_t pad, uint32_t main_port);

/* Each camera update, for a camera whose skater the pad on `port` moves:
 * which way the camera looks on the ground (from its eye to its target, x
 * and z), and the skater's turn on the ground (its +0xCC0, 0x10000 a turn). */
void kbmove_view(unsigned port, double look_x, double look_z, uint32_t turn);

/* Each pad read: while the keys steer the skater, the left stick they make
 * replaced by the one that steers it. */
void kbmove_pad(unsigned port, short *lx, short *ly);

/* Each player manager's update, before the game's own, for the pad on
 * `port`: 1, with *angle the stick's angle that sends the skater the keys'
 * way, while the keys steered it at the last pad read; else 0. turn: the
 * skater's turn (+0xCC0). heading: the camera's heading the controls follow
 * (its +0x228). holds: the camera keeps an old heading, after it changed mode
 * (its +0x238), and held is that heading (its +0x234). All in the game's
 * angles, 0x10000 a turn; *angle as the game's pad entry holds it, 0 to
 * 0xFFFF. */
int kbmove_angle(unsigned port, uint32_t turn, uint32_t heading, int holds, uint32_t held,
                 uint32_t *angle);

/* The game's soft centre (0x000A7ABF), for kbmove_angle and
 * tests/keyboard_move: an angle from the heading within 60 degrees of
 * straight up (0x2AAA either side) is squared, keeping its sign, so the
 * skater goes nearer the heading than the stick points; any other is as it
 * is. In the game's angles, wrapped (-0x8000 to 0x7FFF). */
int kbmove_soft_centre(int angle);

/* The angle the game squares into `way` (wrapped, as above): the whole angle
 * whose soft centre lands nearest to it. */
int kbmove_soft_centre_undo(int way);

/* The steering itself, for tests/keyboard_move. keys_x: D minus A, keys_y: W
 * minus S, each -1, 0 or 1. heading: the skater's facing, radians, 0 along
 * +z and a quarter turn along +x (sub_000A32B0's frame). look: the camera's,
 * on the ground. *held: the way S keeps, radians, kept between calls with
 * *held_keys, the keys it was taken for (start them at 0). The stick, full
 * tilt. */
void kbmove_stick(int keys_x, int keys_y, double heading, double look_x, double look_z,
                  double *held, int *held_keys, short *lx, short *ly);

#endif
