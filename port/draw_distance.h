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
 * Display > Building distance, a submenu right below Draw distance, holds two
 * rows. Far buildings, kept as farbuildingsahead= (0 to 4, 1 the default):
 * the buildings aren't cut by distance but by area. Each frame the camera
 * makes the set of the map's areas to draw, the ones the visible list of the
 * area it is in names, and nothing outside the set is drawn: neither the
 * area's buildings nor the objects in it. Off (0) is the game's set. 1x to 3x
 * (1 to 3) add what the game would draw from the areas that many crossings
 * ahead: the areas the lists of the areas in the set name, then theirs
 * (draw_buildings_widen). All (4) adds every area some list names, which
 * draws what the game never shows from where the eye is, such as Dogenzaka
 * Hill's top in its bottom's sky (known bug 57). An area no list names is
 * never added. Nor is an area a map's own rule hides from where the eye is,
 * where the game hides it on purpose and the lists can't tell, nor what lies
 * past it, reached only through its list: on Dogenzaka Hill, area 13, up
 * the hill, and the hill past it, from the street below it and the corner
 * between them (known bugs 66 and 69), and the rest of the hill from the low
 * street at its far corner, under its top, and on the way to it; on Hikage
 * Street, at All, the spaces above the street's three ends, from its other
 * zones (known bug 70); on Kibogaoka Hill, the areas past the bowl's far
 * side, and areas 4 and 6, in the map's middle, from its rim, the way to it
 * and just past it (known bug 71), and areas 96, 105 and 107, on, past and
 * for the bowl's west side, from four zones at and past the map's middle's
 * east edge (known bug 84); on Chuo Street, two pieces of scenery past the
 * halfpipe's west end, from a stretch further down it (known bug 75;
 * draw_buildings_stage). It changes at once.
 *
 * Far shadows, below it, kept as farshadowsahead= (the same steps, 1 the
 * default): the map's shadows are drawn from the game's set widened the same
 * way, as far as Far buildings goes and no further (draw_buildings_shadows),
 * so the areas Far buildings adds have their shadows and a far street's
 * shadows don't pop in as you come near, at the cost of minor visual
 * glitches: patches of light in the shadows (known bug 52), from the shadow
 * volumes of areas the game doesn't draw from where the eye is. A map's rule
 * can keep an area's shadows out where they make such patches: on Chuo
 * Street, those of areas 47, 48 and 50, behind the long halfpipe's end wall,
 * which they lit at All; on Kibogaoka Hill, those of areas 115 to 117,
 * which lit a walkway across the bowl from them (draw_buildings_stage). Off,
 * the shadows come from the game's own set, and an area only Far buildings
 * adds has no shadows until the game draws it itself. It does nothing with
 * Far buildings off. It changes at once.
 *
 * Before b2.1-hotfix the two rows were toggles, kept as farbuildings= and
 * farshadows= (1 on, 0 off, on drawing the whole map): with no step of its
 * own in the settings, a row takes Off for 0 and 3x for 1 (DRAW_FAR_ONCE_ON).
 *
 * Developer > Far buildings test holds two rows, not kept, to find which
 * area Far buildings adds draws what it shouldn't (draw_far_test_backdrop).
 *
 * Photo mode's camera (photo_mode.h) is the eye all of it goes from while
 * photo mode holds the world: the game's set is made from it, and Far
 * buildings, Far shadows and Draw distance go from there. In no area, the set
 * is made from the last point its eye was in one (recomp_manual.c,
 * area_set_from_photo).
 *
 * The hooks are in recomp_manual.c, under "draw distance"; the steps' maths,
 * the settings, the areas added, the shadows' set and the [DRAW] log are
 * here. */
#ifndef JSRF_DRAW_DISTANCE_H
#define JSRF_DRAW_DISTANCE_H

#include <stdint.h>

#define DRAW_DISTANCE_STEPS     5   /* 1x, 2x, 3x, 4x, Unlimited */
#define DRAW_DISTANCE_UNLIMITED 4
/* Far buildings' and Far shadows' steps: Off, 1x to 3x (crossings ahead),
 * and All (every listed area). */
#define DRAW_FAR_STEPS          5
#define DRAW_FAR_WHOLE          4
/* The defaults, with nothing for them in the settings: 1x, and Far buildings
 * and Far shadows 1x, which are File > Presets > Performance's (spriteice,
 * 23 November 2026: "New engine defaults to performance"). Before b2.2 they
 * were 2x, and 3x for the two. */
#define DRAW_DISTANCE_DEFAULT   0
#define DRAW_FAR_DEFAULT        1
#define DRAW_SHADOWS_DEFAULT    1
/* File > Presets > Balanced's: 2x, and Far buildings and Far shadows 3x,
 * the defaults before b2.2 (spriteice, 14 November 2026: "3 areas ahead" on
 * the card at 15:22 UTC, and at 15:23 UTC "with 3 areas ahead being the
 * default" for Far shadows; named 3x at 15:24 UTC). */
#define DRAW_DISTANCE_BALANCED  1
#define DRAW_FAR_BALANCED       3
#define DRAW_SHADOWS_BALANCED   3
/* What the toggle kept before b2.1-hotfix reads as, on (farbuildings=1,
 * farshadows=1): 3x, as b2.1-hotfix read it, whatever the default is now. */
#define DRAW_FAR_ONCE_ON        3

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

/* Far buildings' step: 0 while off, to DRAW_FAR_WHOLE. */
int draw_far_buildings(void);

/* Far shadows' step: 0 while off, to DRAW_FAR_WHOLE. */
int draw_far_shadows(void);

/* Input playback's (port/playback.c): the three steps set for a play, as a
 * recording has them, and the player's back after it, as the menu sets them
 * but for the settings file, which they leave as it is. Out of range: left
 * as they are. */
void draw_distance_set(int step);
void draw_far_buildings_set(int step);
void draw_far_shadows_set(int step);

#define DRAW_AREAS_MAX 256                      /* the game's set: a bit an area */
#define DRAW_SET_WORDS (DRAW_AREAS_MAX / 32)

/* An area of the map, as the game keeps it: its volume, 6 planes, the 6
 * points first and then the 6 normals, x, y and z each; the areas its
 * visible list names, by number; and its fog. */
typedef struct {
    const float    *volume;
    const uint32_t *list;
    unsigned        nlist;
    float           fog_start, fog_end, fog_density;
} DrawArea;

/* With Far buildings on, after the game has made set (bit i for area i) from
 * the eye: adds what the game would draw from the areas as many crossings
 * ahead as its step, from one area into another through the visible lists:
 * each crossing adds the areas the lists of the areas the last one added
 * name. The lists say what is seen from where, and also what never is:
 * Dogenzaka Hill is one turn of a spiral, its top right above its bottom,
 * and the game never draws the one from the other. By the lists the top is 8
 * crossings from the bottom, so 1x to 3x never reach it; All adds every
 * area some list names, the top in the bottom's sky too (known bug 57;
 * dogenzaka-sky/DIAGNOSIS.md has the maps' areas, and the two other maps
 * with an area over one the game never draws it from, Stg23 and Stg32, both
 * more than 3 crossings away). Never takes an area out, and never adds one
 * no list names. The first call on a map logs its areas; now and frame are
 * for the [DRAW] line, as draw_distance_drawn's. */
void draw_buildings_widen(const DrawArea *areas, int n, uint32_t set[DRAW_SET_WORDS],
                          double now, unsigned frame);

/* Adds to set every area some list names, as Far buildings' All has it,
 * whatever Far buildings' own step, and says how many set then holds. For
 * the free camera when its eye and the game's camera's are both in no area,
 * where the game's set is empty (known bug 61: recomp_manual.c,
 * area_set_from_game_eye), and for photo mode's camera when its eye and the
 * last point it was in an area give none (area_set_from_photo). The map's
 * rules (draw_buildings_stage) aren't
 * taken: they hold only while the game's set is the one some areas make,
 * and an empty set is none's. */
int draw_set_whole_map(const DrawArea *areas, int n, uint32_t set[DRAW_SET_WORDS]);

/* The stage the mission asks for (0x0001000N for Stg1N), from the flow's
 * steps that ask for it (port/resource_load.c), before its map loads. Far
 * buildings keeps it with the next map it reads, and that map's own rules, if
 * it has any, each take areas out of what draw_buildings_widen and
 * draw_buildings_shadows add while the game's set is the one some areas
 * make: on Dogenzaka Hill (stage 0x00010002, its 30 areas), area 13, up the
 * hill, while the game's set is area 0's, the street below it, or area
 * 16's, the corner between them. The game draws area 13 from areas 13 and 14
 * only, and from the street its buildings hang in the sky with nothing under
 * them (known bug 66). A hidden area is a wall: Far buildings doesn't walk
 * through it, so the areas past it, reached only through its list, aren't
 * added either, as the game hides them too: from the street, areas 11 and
 * 12 further up the hill, whose traffic hung in the sky (known bug 69). And
 * areas 0 to 14, the rest of the hill, while the game's set is area 6's, the
 * low street at the hill's far corner, under its top, or area 4's, on the way
 * there, where its buildings hung in the sky as well. A rule can also hide
 * its areas from Far shadows only, and from every area: on Chuo Street (stage
 * 0x00010001, its 59 areas), Far shadows never adds areas 47, 48 and 50,
 * scenery past the long halfpipe's end wall, whose shadow volumes lit patches
 * on it at All (known bug 52); Far buildings still adds them, and where the
 * game draws them itself their shadows stay. Nor does Far buildings add 49
 * and 51, two more such pieces, while the game's set is area 25's, a bit
 * further down the halfpipe, or 28's or 24's beside it, from where All added
 * them (known bug 75). A rule's areas are runs, first
 * to last. On Hikage Street (stage 0x00010003, its 46 areas), areas 14 to 16,
 * the spaces above the street's three ends, named by no list but their own,
 * while the game's set is that of a street zone but the ends, 1, 7 and 12:
 * from there All drew what is built to be seen from up in them (known
 * bug 70). On Kibogaoka Hill (stage 0x00020002, its 124 areas), areas 56
 * to 121, past the bowl's far side, and 4 and 6, in the map's middle,
 * while the game's set is the rim's, area 31, that of 27 or 23 on the way
 * to it, or that of 32, just past it, where they hung in the air (known
 * bug 71); areas 96, on the bowl's west side, 105, far past it, and 107,
 * scenery the game draws only from there, while the game's set is that of
 * 7, 9, 13 or 20, at and past the map's middle's east edge, where 107 hung
 * in the air too (known bug 84); and the shadows of 115 to 117, from every
 * area, which lit patches on a walkway (known bug 72). A rule can also hold
 * only while one of the game's scenes plays (draw_buildings_scene): on 99th
 * Street (stage 0x00030000, its 63 areas), no area at all, from any area,
 * while scene 36 plays, the one the first time the player comes in, whose
 * camera in the street west of the plaza, area 8, showed thin lines across a
 * close-up (known bug 80); along the street the rest of the time Far
 * buildings adds as before. Each such rule is kept by hand, for one case seen
 * and named; the first call on the map logs each. Before any stage,
 * DRAW_STAGE_NONE, which no rule has. */
#define DRAW_STAGE_NONE 0xFFFFFFFFu
void draw_buildings_stage(uint32_t stage);

/* The game's scene playing now, as its mission's command 227 numbers it, or
 * DRAW_SCENE_NONE: from the game's set's hook (recomp_manual.c,
 * sub_0006A970) each time it makes the set, before draw_buildings_widen, for
 * the map's rules for one scene (draw_buildings_stage). The [DRAW] log has a
 * line as a scene one of them is for starts and ends. */
#define DRAW_SCENE_NONE 0xFFFFFFFFu
void draw_buildings_scene(uint32_t scene);

/* With Far buildings on, the map's shadows: runs run() with set holding
 * game, the set the game made, widened as draw_buildings_widen widens it to
 * Far shadows' step or Far buildings', the nearer, and puts set back as it
 * was after it, without the areas a map's rule hides from the shadows
 * (draw_buildings_stage); with the two at the same step and no such rule,
 * set is already that. Off (step 0), the game's own set. The game draws its shadows as volumes it
 * counts in the stencil buffer, and a surface is in shadow where the count
 * isn't 0. That is right only for volumes whole along every line of sight
 * from the eye, which the game's own set gives it. The volumes of areas only
 * a widened set has, far off or behind what the eye sees, take the shadow
 * off the walls in front of them in patches that move with the camera (known
 * bug 52, on Chuo Street's halfpipe). An area Far buildings adds and Far
 * shadows doesn't is drawn without its own shadows until Far shadows' step
 * reaches it. Each call counts for the [DRAW] line. */
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

/* Developer > Far buildings test, a submenu of two rows that change at once
 * and aren't kept, added by draw_distance_start, to find which area Far
 * buildings adds draws what it shouldn't, such as the buildings floating in
 * Dogenzaka Hill's sky (known bug 66). Leave out area steps through the
 * areas Far buildings adds from where the eye is now (+ the next, - the one
 * before, -1 none), and Far buildings and Far shadows don't add the one
 * shown, which is still drawn where the game's own set has it; a new map
 * starts again at none. Game's backdrop depth draws the backdrop with the
 * depth writes the game sets, not off as Far buildings sets them: nonzero
 * while ticked, for recomp_manual.c's backdrop hook.
 * While Far buildings is on, each [DRAW] line also says the eye's area (the
 * area that makes the game's set: the game doesn't keep its own number for
 * it), the areas it was in since the line before, what Far buildings adds
 * from it, and what the two rows are doing; and each change of the eye's
 * area has a line of its own, up to DRAW_EYE_LINES between two [DRAW] lines,
 * with what Far buildings adds there. */
int draw_far_test_backdrop(void);
#define DRAW_EYE_LINES 40

#endif
