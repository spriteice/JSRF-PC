/* Draw distance: how far the game draws the objects in its world, and its
 * buildings.
 *
 * Display > Draw distance, kept as drawdistance= in the settings file: 1x (0)
 * is the game's own distances, 2x, 3x and 4x (1 to 3, 2x the default) twice to
 * four times them, and Unlimited (4) no distance at all, up to the camera's far
 * plane. It changes at once. The game decides whether to draw each object in a
 * few gates, each comparing the object's depth in front of the eye with
 * distances of its own: crowds, the pickups (spray cans), the race's
 * checkpoints, props and traffic, the Enemy bank's kinds (police, tanks), and
 * one attached part.
 *
 * Display > Building distance, a submenu right below Draw distance, holds two
 * rows. Far buildings, kept as farbuildings= (0, off, or 1, the default):
 * the buildings aren't cut by distance but by area. Each frame the camera
 * makes the set of the map's areas to draw, the ones the visible list of the
 * area it is in names, and nothing outside the set is drawn: neither the
 * area's buildings nor the objects in it. On, the set also has every area some
 * other area's list names; never one no list names. It changes at once.
 *
 * Far shadows, below it, kept as farshadows= (0, off, or 1, the default): on,
 * the map's shadows are drawn from the set Far buildings makes too, as b2
 * drew them, so the areas it adds have their shadows and a far street's
 * shadows don't pop in as you come near, at the cost of minor visual
 * glitches: patches of light in the shadows (known bug 52). Off, they are
 * drawn from the game's own set (draw_buildings_shadows), and an area only
 * Far buildings adds has no shadows until the game draws it itself. It does
 * nothing with Far buildings off. It changes at once.
 *
 * The hooks are in recomp_manual.c, under "draw distance"; the steps' maths,
 * the settings, the areas added, the shadows' set and the [DRAW] log are
 * here. */
#ifndef JSRF_DRAW_DISTANCE_H
#define JSRF_DRAW_DISTANCE_H

#include <stdint.h>

#define DRAW_DISTANCE_STEPS     5   /* 1x, 2x, 3x, 4x, Unlimited */
#define DRAW_DISTANCE_UNLIMITED 4
/* The defaults, with no drawdistance=, farbuildings= or farshadows= in the
 * settings: 2x, Far buildings on, and Far shadows on. */
#define DRAW_DISTANCE_DEFAULT   1
#define DRAW_FAR_DEFAULT        1
#define DRAW_SHADOWS_DEFAULT    1

/* At start, before the game runs: reads the settings and adds the in-game
 * menu's Display rows: Draw distance, then the Building distance submenu with
 * Far buildings and Far shadows. */
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

/* Far shadows: nonzero while on. */
int draw_far_shadows(void);

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

/* With Far buildings on, the map's shadows: with Far shadows on, runs run()
 * with set as it is, the widened set; off, runs run() with set holding game,
 * the set the game made, and puts set back as it was after it. The game
 * draws its shadows as volumes it counts in the stencil buffer, and a surface
 * is in shadow where the count isn't 0. That is right only for volumes whole
 * along every line of sight from the eye, which the game's own set gives it.
 * The volumes of areas only the widened set has, far off or behind what the
 * eye sees, take the shadow off the walls in front of them in patches that
 * move with the camera (known bug 52, on Chuo Street's halfpipe). Far shadows
 * off, an area only Far buildings adds is drawn without its own shadows until
 * the list of the area the eye is in names it. Each call counts for the
 * [DRAW] line. */
void draw_buildings_shadows(uint32_t set[DRAW_SET_WORDS], const uint32_t game[DRAW_SET_WORDS],
                            void (*run)(void));

/* The Misc group's objects (recomp_manual.c, "draw distance"), the best
 * candidate for the birds: one model drawn at many places, in one area. With
 * Far buildings on, each draw of one, for the log: the object, its model, the
 * area it is in and the places it is drawn at. The log names each model once
 * for each area it is drawn in, and again when its places rise past the most
 * it was named with, up to DRAW_MISC_LINES lines a map, and then says it
 * stops. A new map (draw_buildings_widen's first call on it) starts again.
 * After Far buildings is turned on from the menu, the log waits for
 * draw_buildings_widen's next call, which says which map it is. */
#define DRAW_MISC_LINES 2048
void draw_misc_drawn(uint32_t object, uint32_t model, uint32_t area, uint32_t places);

#endif
