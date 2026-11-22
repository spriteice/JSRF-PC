/* The game's own camera shots (camera_scenes.c).
 *
 * Most of the time the game's camera follows the player, and the free
 * camera (freecam.h) and the view kept out of walls (camera_walls.h) act on
 * that follow camera. Some shots the game frames itself, in a camera mode of
 * their own (the camera's +0x208), and those are the game's to place: this
 * names them. The hooks are in recomp_manual.c: sub_000A6100, the follow
 * camera's post-update. */
#ifndef JSRF_CAMERA_SCENES_H
#define JSRF_CAMERA_SCENES_H

#include <stdint.h>

/* The graffiti camera's mode: the spray code (sub_0008D280) asks for it,
 * and it frames the spot being sprayed, then goes back behind the player. */
#define CAMSCENE_MODE_GRAFFITI 13u

/* Whether camera mode mode, one the follow camera's post-update serves (3,
 * 4, 5, 9 and 13), is one of the game's own shots: graffiti's. The others
 * follow the player, or look from them at whoever they talk to, from where
 * the player stands in the open. The graffiti shot is aimed at the spot on
 * a wall, a bus or a column, so that surface is right by what it looks at:
 * kept out of walls, the eye would come in to a first-person view the
 * console never shows. It is left as the game frames it. */
int camscene_shot(uint32_t mode);

#endif
