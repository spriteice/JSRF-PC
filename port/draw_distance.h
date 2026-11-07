/* Draw distance: how far the game draws the objects in its world, and its
 * buildings.
 *
 * Display > Draw distance, kept as drawdistance= in the settings file: 1x (0,
 * the default) is the game's own distances, 2x, 3x and 4x (1 to 3) twice to
 * four times them, and Unlimited (4) no distance at all, up to the camera's far
 * plane. It changes at once. The game decides whether to draw each object in a
 * few gates, each comparing the object's depth in front of the eye with
 * distances of its own: crowds, the pickups (spray cans), the race's
 * checkpoints, props and traffic, the Enemy bank's kinds (police, tanks), and
 * one attached part.
 *
 * Display > Far buildings, kept as farbuildings= (0, off, the default, or 1):
 * the buildings aren't cut by distance but by area. Each frame the camera
 * makes the set of the map's areas to draw, the ones the visible list of the
 * area it is in names, and nothing outside the set is drawn: neither the
 * area's buildings nor the objects in it. On, the set also has every area some
 * other area's list names; never one no list names. It changes at once.
 *
 * The hooks are in recomp_manual.c, under "draw distance"; the steps' maths,
 * the settings, the areas added and the [DRAW] log are here. */
#ifndef JSRF_DRAW_DISTANCE_H
#define JSRF_DRAW_DISTANCE_H

#include <stdint.h>

#define DRAW_DISTANCE_STEPS     5   /* 1x, 2x, 3x, 4x, Unlimited */
#define DRAW_DISTANCE_UNLIMITED 4

/* At start, before the game runs: reads the settings and adds the in-game
 * menu's two Display rows. */
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

/* Far buildings: nonzero while on. */
int draw_far_buildings(void);

#define DRAW_AREAS_MAX 256                      /* the game's set: a bit an area */
#define DRAW_SET_WORDS (DRAW_AREAS_MAX / 32)

/* An area of the map, as the game keeps it: its volume, 6 planes, the 6
 * points first and then the 6 normals, x, y and z each; its cells, smaller
 * volumes of the same shape inside it, one after another; the areas its
 * visible list names, by number; and its fog. */
typedef struct {
    const float    *volume;
    const float    *cells;
    unsigned        ncells;
    const uint32_t *list;
    unsigned        nlist;
    float           fog_start, fog_end, fog_density;
} DrawArea;

/* Nonzero when b is a copy of a: its volume and each of its cells are a's
 * moved by one offset, at least 1 long, which goes in d. */
int draw_area_copy(const DrawArea *a, const DrawArea *b, float d[3]);

/* With Far buildings on, after the game has made set (bit i for area i) from
 * the eye: adds every area some list names, nearest the eye first, but not a
 * copy of one the set has by then, where a copy is an area of the same shape
 * moved (draw_area_copy) that no list names together with it. That is a
 * safeguard: the game builds some stretches of a map twice and draws only
 * one, and on the maps seen the copy is an area no list names (Dogenzaka
 * Hill's far end), which is never added anyway. With JSRF_DRAW_COPIES set,
 * the copies too. Never takes an area out. The first call on a map logs its
 * areas; now and frame are for the [DRAW] line, as draw_distance_drawn's. */
void draw_buildings_widen(const DrawArea *areas, int n, const float eye[3],
                          uint32_t set[DRAW_SET_WORDS], double now, unsigned frame);

#endif
