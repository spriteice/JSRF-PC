/* Draw distance: how far the game draws the objects in its world.
 *
 * Display > Draw distance, kept as drawdistance= in the settings file: 1x (0,
 * the default) is the game's own distances, 2x, 3x and 4x (1 to 3) twice to
 * four times them, and Unlimited (4) no distance at all, up to the camera's far
 * plane. It changes at once. The game decides whether to draw each object in a
 * few gates, each comparing the object's depth in front of the eye with
 * distances of its own: crowds, the pickups (spray cans), the race's
 * checkpoints, props and traffic, the Enemy bank's kinds (police, tanks), and
 * one attached part. The hooks are in recomp_manual.c, under "draw distance";
 * the steps' maths, the setting and the [DRAW] log are here. */
#ifndef JSRF_DRAW_DISTANCE_H
#define JSRF_DRAW_DISTANCE_H

#define DRAW_DISTANCE_STEPS     5   /* 1x, 2x, 3x, 4x, Unlimited */
#define DRAW_DISTANCE_UNLIMITED 4

/* At start, before the game runs: reads the setting and adds the in-game
 * menu's Display row. */
void draw_distance_start(void);

/* The step: 0 (1x, the game's own) to DRAW_DISTANCE_UNLIMITED. */
int draw_distance_step(void);

/* A distance the game draws an object to, or starts its fade at, for this
 * step: game times the step, or, for Unlimited, the largest float, which no
 * depth reaches. */
float draw_distance_far(float game);

/* A fade's stored 1 / (where it ends - where it starts), for this step: game
 * over the step. For Unlimited it is the game's, as a fade there starts at
 * the largest float and is never reached. */
float draw_distance_per(float game);

/* Runs run() with each float at[i] set to this step's -- a distance, or a
 * fade's 1 / length where per[i] -- and puts each back after it. For a gate
 * that reads its distances from the game's constants, and that calls nothing
 * that reads them too. */
void draw_distance_with(float *const at[], const unsigned char per[], int n, void (*run)(void));

/* A fade the game works out as (far - d) * per, from its own far and per, at
 * this step: 1 where it starts, 0 where it ends. */
float draw_distance_fade(float d, float game_far, float game_per);

/* The gates, for the log. */
enum {
    DRAW_CROWDS, DRAW_PICKUPS, DRAW_CHECKPOINTS, DRAW_PROPS, DRAW_ENEMIES, DRAW_PART,
    DRAW_GATES
};

/* At 2x and up, each object a gate lets through: past, beyond the game's own
 * distance. now is the time in seconds and frame the frames shown so far;
 * every 5 seconds the log says how many each gate let through a frame, and
 * how many of them past the game's distance. */
void draw_distance_drawn(int gate, int past, double now, unsigned frame);

#endif
