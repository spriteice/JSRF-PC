/* Draw distance, Far buildings and Far shadows (draw_distance.h):
 * drawdistance=, farbuildingsahead= and farshadowsahead= in the settings file, or
 * Display > Draw distance, and Far buildings and Far shadows in Display >
 * Building distance.
 *
 * Read once, at start (draw_distance_start). The menu's rows change them at
 * once and keep them in the settings file; they run on the window's thread,
 * and the gates and the areas read them on the game's. A step multiplies
 * every distance a gate cuts or fades at by the same number, so each kind of
 * object keeps its own distances in proportion, and a fade keeps its length
 * in proportion too. Far buildings adds areas to the set the camera makes,
 * from the map's own visible lists, and Far shadows gives them the map's
 * shadows too. Developer > Far buildings test holds two rows, not kept, to
 * find which area Far buildings adds draws what it shouldn't, and the
 * [DRAW] lines say which area the eye is in and what Far buildings adds from
 * there (draw_distance.h). */
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "draw_distance.h"
#if defined(XBOXRECOMP_MENU)
#  include "menu.h"
#endif

#if defined(NV2A_GL_USE_SDL)
/* nv2a_gl.c: a number from the settings file the engine already keeps, and a
 * number kept there. */
extern int  nv_window_pref_num(const char *key, double *v);
extern void nv_window_pref_set_num(const char *key, double v);
#else
/* Without the SDL window (a headless build) there is no settings file: no
 * keys. */
static int nv_window_pref_num(const char *key, double *v)
{
    (void)key; (void)v;
    return 0;
}
#endif

/* The rows go right under the window's HUD submenu (nv2a_gl.c), its last
 * row: Display's order is Fullscreen, Resolution, Aspect ratio, HUD, then
 * Draw distance and Building distance (spriteice, 12 November 2026: "Render
 * scale up", on the card at 06:19 UTC, and HUD scaling "as it's own submenu
 * under aspect ratio", at 09:37 UTC, named HUD on the card at 09:43 UTC;
 * Render scale became Resolution on 21 November 2026).
 * Building distance is a submenu of Far buildings, then Far shadows, on by
 * default, its hint giving the glitches it can cause (spriteice, 13 November
 * 2026, 10:17 UTC, and 11:59 UTC: "the pop in reduction is worth the
 * glitches"). Each is a choice of how far, Off to All, since known bug
 * 57 (spriteice, 14 November 2026: "A choice of how far" on the card at 15:21
 * UTC, and at 15:22 UTC "apply the choice of how far to far shadows too"). */
#define DRAW_UNDER     "HUD"
#define DRAW_GROUP     "Building distance"
#define DRAW_TEST_GROUP "Far buildings test"     /* in Developer */
#define DRAW_LOG_EVERY 5.0      /* seconds between [DRAW] lines */

static const char *const k_names[DRAW_DISTANCE_STEPS] = { "1x", "2x", "3x", "4x", "Unlimited" };
/* Off, 1x to 3x (that many crossings ahead) and All (every listed area), the
 * same for both rows (spriteice, 14 November 2026, 15:24 UTC: "Off, 1x, 2x,
 * 3x, All"). */
static const char *const k_far_names[DRAW_FAR_STEPS] = { "Off", "1x", "2x", "3x", "All" };
static const char *const k_gates[DRAW_GATES] = {
    "crowds", "pickups", "checkpoints", "props and traffic", "enemies", "attached part" };

static volatile int s_step;
static volatile int s_far;      /* Far buildings' step */
static volatile int s_shadows_far;  /* Far shadows' step */
static int s_widened;           /* the step the game's set was last widened to */

/* Developer > Far buildings test's rows, not kept: the area Far buildings
 * doesn't add (-1 none), and the backdrop's depth writes as the game sets
 * them. They change on the window's thread, and are read on the game's. */
static volatile int s_leave_out = -1;
static volatile int s_backdrop_game;
/* Leave out area's press, +1 or -1, until the game's thread steps through
 * what Far buildings adds from the eye then (leave_step); 0 none. */
static volatile int s_leave_press;
/* What the [DRAW] lines say of the eye, worked out on the game's thread: its
 * area (eye_note; -1 unknown), whether another area makes the same set, the
 * areas it was in since the last line, and what Far buildings adds from it
 * now; and its changes logged, and not, since the line. */
static uint32_t s_added_now[DRAW_SET_WORDS];
static int s_eye = -1, s_eye_many;
static uint32_t s_eye_seen[DRAW_SET_WORDS];
static unsigned s_eye_lines, s_eye_unsaid;
static void set_print(char *out, int room, const uint32_t *set);

int draw_distance_step(void)
{
    return s_step;
}

int draw_far_buildings(void)
{
    return s_far;
}

int draw_far_shadows(void)
{
    return s_shadows_far;
}

float draw_distance_far(float game)
{
    int step = s_step;
    return step == DRAW_DISTANCE_UNLIMITED ? FLT_MAX : game * (float)(step + 1);
}

float draw_distance_per(float game)
{
    int step = s_step;
    return step == DRAW_DISTANCE_UNLIMITED ? game : game / (float)(step + 1);
}

void draw_distance_with(float *const at[], const unsigned char per[], int n, void (*run)(void))
{
    float was[8];
    int i;
    if (n > (int)(sizeof was / sizeof was[0])) n = (int)(sizeof was / sizeof was[0]);
    for (i = 0; i < n; i++) {
        was[i] = *at[i];
        *at[i] = per[i] ? draw_distance_per(was[i]) : draw_distance_far(was[i]);
    }
    run();
    for (i = 0; i < n; i++) *at[i] = was[i];
}

float draw_distance_fade(float d, float game_far, float game_per)
{
    return (float)(((double)draw_distance_far(game_far) - d) * draw_distance_per(game_per));
}

/* ---- the [DRAW] line ---------------------------------------------------- */

static unsigned long s_drawn[DRAW_GATES], s_past[DRAW_GATES];
static int s_objects;           /* a gate has counted since the line */
static double s_sets, s_game, s_added;     /* Far buildings' sets since the line */
static double s_shadows, s_shadows_added;  /* shadow passes, and the areas each adds to the game's */
static double s_since = -1;     /* the time the counts start from; -1: not yet */
static unsigned s_since_frame;

/* Every 5 seconds: each gate's objects a frame, and how many of them past
 * 1x's distance, while a step is on; and, while Far buildings is, the areas
 * of a set the camera makes and the areas it adds, the shadow passes a frame
 * and the areas each adds to the game's set (Far shadows), the eye's area,
 * the areas it was in since the line before, what Far buildings adds from
 * it, and what Developer > Far buildings test's rows are doing. */
static void draw_log(double now, unsigned frame)
{
    char line[2048];
    unsigned frames;
    int len, g;
    if (s_since < 0) { s_since = now; s_since_frame = frame; return; }
    if (now - s_since < DRAW_LOG_EVERY) return;
    frames = frame - s_since_frame;
    if (!frames) frames = 1;
    len = snprintf(line, sizeof line, "[DRAW] t=%.1f %s", now, k_names[s_step]);
    if (s_objects && len > 0 && len < (int)sizeof line)
        len += snprintf(line + len, sizeof line - len, ", a frame's objects (past 1x's distance):");
    for (g = 0; g < DRAW_GATES && len > 0 && len < (int)sizeof line; g++) {
        if (s_objects)
            len += snprintf(line + len, sizeof line - len, "%s %s %.1f (%.1f)", g ? "," : "",
                            k_gates[g], (double)s_drawn[g] / frames, (double)s_past[g] / frames);
        s_drawn[g] = s_past[g] = 0;
    }
    if (s_sets > 0 && len > 0 && len < (int)sizeof line)
        len += snprintf(line + len, sizeof line - len,
                        "%s Far buildings %s, a set's areas: the game's %.1f, added %.1f; "
                        "Far shadows %s, shadow passes a frame %.1f, each adding %.1f areas",
                        s_objects ? ";" : ",", k_far_names[s_far], s_game / s_sets, s_added / s_sets,
                        k_far_names[s_shadows_far], s_shadows / frames,
                        s_shadows > 0 ? s_shadows_added / s_shadows : 0.0);
    if (s_sets > 0 && len > 0 && len < (int)sizeof line) {
        char seen[512], added[512];
        set_print(seen, sizeof seen, s_eye_seen);
        set_print(added, sizeof added, s_added_now);
        if (s_eye >= 0)
            len += snprintf(line + len, sizeof line - len, "; the eye in area %d%s", s_eye,
                            s_eye_many ? " (another area's list is the set too)" : "");
        else
            len += snprintf(line + len, sizeof line - len,
                            "; the eye's area unknown (no area's list is the set)");
        if (len > 0 && len < (int)sizeof line)
            len += snprintf(line + len, sizeof line - len,
                            ", in %s since the last line%s; Far buildings adding %s",
                            seen, s_eye_unsaid ? " (some changes not logged)" : "", added);
        if (s_leave_out >= 0 && len > 0 && len < (int)sizeof line)
            len += snprintf(line + len, sizeof line - len, "; Far buildings test: leaving out area %d",
                            s_leave_out);
        if (s_backdrop_game && len > 0 && len < (int)sizeof line)
            snprintf(line + len, sizeof line - len,
                     "; Far buildings test: the backdrop's depth writes as the game sets them");
    }
    fprintf(stderr, "%s\n", line);
    memset(s_eye_seen, 0, sizeof s_eye_seen);
    if (s_eye >= 0) s_eye_seen[s_eye >> 5] |= 1u << (s_eye & 31);
    s_eye_lines = s_eye_unsaid = 0;
    s_objects = 0;
    s_sets = s_game = s_added = 0;
    s_shadows = s_shadows_added = 0;
    s_since = now;
    s_since_frame = frame;
}

void draw_distance_drawn(int gate, int past, double now, unsigned frame)
{
    if (gate < 0 || gate >= DRAW_GATES) return;
    s_drawn[gate]++;
    if (past) s_past[gate]++;
    s_objects = 1;
    draw_log(now, frame);
}

/* ---- the Misc group's log ------------------------------------------------ */

/* What the log has said on this map: each (area, model) it named, with the
 * most places it gave. A slot is taken only with a line, so the table, twice
 * the lines, is never full. */
#define MISC_BITS  12
#define MISC_SLOTS (1u << MISC_BITS)
#if MISC_SLOTS < 2 * DRAW_MISC_LINES
#  error "the Misc group's table must be twice its lines"
#endif
/* A pair's slot, by Fibonacci hashing: the area times 2^32 over the golden
 * ratio, mixed into the model, then the pair times Knuth's prime near it,
 * whose top MISC_BITS bits are the slot. */
#define MISC_HASH_AREA 0x9E3779B9u
#define MISC_HASH_PAIR 2654435761u
static struct { uint32_t area, model, places; int used; } s_misc[MISC_SLOTS];
static unsigned s_misc_lines;   /* written on this map; one more once it has stopped */
/* Far buildings has read the map since it was last turned on or off: until
 * then the table may be another map's, so the log waits. */
static volatile int s_misc_map;

static void misc_forget(void)
{
    memset(s_misc, 0, sizeof s_misc);
    s_misc_lines = 0;
}

void draw_misc_drawn(uint32_t object, uint32_t model, uint32_t area, uint32_t places)
{
    uint32_t i = ((model ^ area * MISC_HASH_AREA) * MISC_HASH_PAIR) >> (32 - MISC_BITS);
    if (!s_far || !s_misc_map || s_misc_lines > DRAW_MISC_LINES) return;
    while (s_misc[i].used && (s_misc[i].area != area || s_misc[i].model != model))
        i = (i + 1) & (MISC_SLOTS - 1);
    if (s_misc[i].used && places <= s_misc[i].places) return;
    if (s_misc_lines == DRAW_MISC_LINES) {
        fprintf(stderr, "[DRAW] Misc group: %d lines on this map, the most; no more until the next map\n",
                DRAW_MISC_LINES);
        s_misc_lines++;
        return;
    }
    s_misc[i].used = 1;
    s_misc[i].area = area;
    s_misc[i].model = model;
    s_misc[i].places = places;
    s_misc_lines++;
    fprintf(stderr, "[DRAW] Misc group %08X: model %X, in area %u, at %u places\n",
            object, model, area, places);
}

/* ---- Far buildings ------------------------------------------------------- */

static int set_has(const uint32_t *set, int i) { return (set[i >> 5] >> (i & 31)) & 1u; }
static void set_add(uint32_t *set, int i) { set[i >> 5] |= 1u << (i & 31); }
static int set_count(const uint32_t *set)
{
    int i, n = 0;
    for (i = 0; i < DRAW_AREAS_MAX; i++) n += set_has(set, i);
    return n;
}

/* What Far buildings keeps of a map, worked out on the first call on it:
 * the stage asked for before it, its rules (k_rules, a bit each), the areas
 * some list names, and each area's list. */
static struct {
    int      built, n;
    uint32_t key, stage, rules;
    uint32_t listed[DRAW_SET_WORDS];
    uint32_t list[DRAW_AREAS_MAX][DRAW_SET_WORDS];
} s_map;

/* The stage the mission last asked for (draw_buildings_stage), on the game's
 * thread. */
static uint32_t s_stage = DRAW_STAGE_NONE;

/* The maps' own rules: areas Far buildings never adds while the game's set
 * is the one some areas make, where the game hides them on purpose and the
 * lists can't tell (draw_distance.h, known bug 66). The stage files don't say
 * which areas the game hides from which side, and no rule from the lists
 * tells these from the areas Far buildings is there to add
 * (far-buildings-1x/DIAGNOSIS.md in the shared folder): each one is kept by
 * hand, for a case seen and named, with spriteice's OK. A map can have
 * several, each with the areas it hides and the areas it hides them from, or
 * from every area; and a rule can hide its areas from Far shadows only,
 * where the game's own lists are right for the buildings but the areas'
 * shadow volumes, drawn from where the game never draws them, make patches
 * of light (known bug 52). A rule's areas are runs, first to last: one area
 * is a run of one.
 *
 * Dogenzaka Hill (Stg12): area 13, up the hill, is drawn by the game from
 * areas 13 and 14 only, as 14's list names it and the lists of the street
 * below it, area 0, and of the corner between them, area 16, don't. Far
 * buildings adds it from 0 and 16 through 14's list, and from the street its
 * buildings hang in the sky with nothing under them (Adri, GitHub issue 2,
 * 15 November 2026; named with Developer > Far buildings test by spriteice's
 * run, 16 November 2026). Hidden from both, the game's own line (spriteice,
 * 16 November 2026, 01:44 UTC: "Test build to name the area first", for "a
 * hand-kept rule you OK for this one case"; 06:04 UTC: "From areas 0 and
 * 16"). It goes both ways: back up the hill in area 14 the game draws area 13
 * itself. */
#define STAGE_DOGENZAKA_HILL 0x00010002u    /* as the mission asks for it */
#define DOGENZAKA_AREAS      30             /* its map's, so it's the one */
#define DOGENZAKA_UPHILL     13
#define DOGENZAKA_STREET     0
#define DOGENZAKA_CORNER     16
/* A run of a map's areas, first to last, for a rule. */
typedef struct { int first, last; } RuleRun;
#define RULE_ONE(a) { (a), (a) }    /* a run of one area */
static const RuleRun k_dogenzaka_never[] = { RULE_ONE(DOGENZAKA_UPHILL) },
                     k_dogenzaka_from[] = { RULE_ONE(DOGENZAKA_STREET), RULE_ONE(DOGENZAKA_CORNER) };
/* And from area 6, the low street at the hill's far corner, under the
 * hill's top, and from 4 beside it, on the way there from 2, the rest of the
 * hill, areas 0 to 14, is never added, nor what lies past it, where from
 * there its buildings and traffic hang in the sky; what the game draws
 * itself there stays: 4 and 6 from 6, and 2, 4 and 6 from 4 (spriteice, 20
 * November 2026, 09:15 and 09:18 UTC: "lets also do the same to dogenzaka
 * hill for all", "we want 0-14 left out of the draw"; the zones named with
 * the in-game menu, 6 at 09:56 UTC and 4 at 11:29 UTC: "dogenzaka hill needs
 * to extend into the zone where i pressed the menu a few times in a row"). */
#define DOGENZAKA_HILL_FIRST 0
#define DOGENZAKA_HILL_LAST  14
#define DOGENZAKA_TO_FAR     4              /* then 6, the far corner's street */
#define DOGENZAKA_FAR_STREET 6
static const RuleRun k_dogenzaka_hill[] = { { DOGENZAKA_HILL_FIRST, DOGENZAKA_HILL_LAST } },
                     k_dogenzaka_far[] = { RULE_ONE(DOGENZAKA_TO_FAR), RULE_ONE(DOGENZAKA_FAR_STREET) };
/* Chuo Street (Stg11): areas 47, 48 and 50 are scenery only, with no space
 * of their own, and only areas at the long halfpipe's west end and past it
 * name them: 47 areas 6, 8, 31 and 32; 48 areas 8 and 10; 50 area 30. From
 * the halfpipe's middle and east end they are 4 to 7 crossings away, so at
 * All, Far shadows draws their shadow volumes from there, behind the
 * halfpipe's end wall, and each takes the shadow off the wall in front of it
 * in a patch of light (known bug 52, at All only since b2.1-hotfix; the
 * three named with Developer > Far buildings test by Adri, GitHub issue 2,
 * 19 November 2026). Far shadows never adds them, from any area: they keep
 * their shadows where the game draws them itself, and Far buildings still
 * draws their scenery (spriteice, 20 November 2026, 01:13 UTC: "Hill fix +
 * Chuo shadows rule"). */
#define STAGE_CHUO_STREET    0x00010001u    /* as the mission asks for it */
#define CHUO_AREAS           59             /* its map's, so it's the one */
#define CHUO_SCENERY_WEST    47
#define CHUO_SCENERY_SOUTH   48
#define CHUO_SCENERY_END     50
static const RuleRun k_chuo_never[] = { RULE_ONE(CHUO_SCENERY_WEST), RULE_ONE(CHUO_SCENERY_SOUTH),
                                        RULE_ONE(CHUO_SCENERY_END) };
/* And 49 and 51, scenery with no space of its own that the game draws only
 * from the halfpipe's west end (49 from area 10; 51 from 10, 11, 12 and
 * 30), are never added from area 25, a bit further down the halfpipe, nor
 * from 28 on the way to it or 24 past it, so they don't come in as you step
 * out of 25: Far buildings added them there at All only (spriteice, 20
 * November 2026, 11:48 UTC, the two named with Developer > Far buildings
 * test in 25: "49, 51 need to be removed from the zone where i pressed menu
 * 10 times in chuo street. it is a bit further down the half pipe"). */
#define CHUO_PAST_WEST       49             /* and 51 */
#define CHUO_PAST_WEST_TOO   51
#define CHUO_TO_DOWN         28             /* then 25, then 24 */
#define CHUO_DOWN_HALFPIPE   25
#define CHUO_PAST_DOWN       24
static const RuleRun k_chuo_past[] = { RULE_ONE(CHUO_PAST_WEST), RULE_ONE(CHUO_PAST_WEST_TOO) },
                     k_chuo_down[] = { RULE_ONE(CHUO_PAST_DOWN), RULE_ONE(CHUO_DOWN_HALFPIPE),
                                       RULE_ONE(CHUO_TO_DOWN) };
/* Hikage Street (Stg13): areas 14, 15 and 16 are the spaces above the
 * street's three ends: over the east end, area 1, the west end, area 12, and
 * the north end, area 7. No list but its own names each, so the game draws
 * each only while the eye is up in it, and 1x to 3x never add them. All adds
 * every listed area, and from the street what is built for the view from up
 * there stands in the street's view: a tower block in the gap by the north
 * end, chimneys behind the near building by the east end (Adri, GitHub issue
 * 2, 20 November 2026, named with Developer > Far buildings test; known bug
 * 70). Never added from the street's other zones, 0, 2 to 6, 8 to 11 and 17
 * (13 is scenery, with no space of its own): from the ends and from up in 14
 * to 16 all three stay, so each end's buildings come in as you reach it
 * (spriteice, 20 November 2026, 08:48 UTC: "all buildings stay on in 1,7,12,
 * and their zones above"; 08:50 UTC, on the card: "On only from 1, 7 and
 * 12"). */
#define STAGE_HIKAGE_STREET  0x00010003u    /* as the mission asks for it */
#define HIKAGE_AREAS         46             /* its map's, so it's the one */
#define HIKAGE_MIDDLE        0
#define HIKAGE_EAST_END      1
#define HIKAGE_NORTH_END     7
#define HIKAGE_WEST_END      12
#define HIKAGE_SOUTH         17
#define HIKAGE_ABOVE_EAST    14             /* then the west end's, 15, and the north end's */
#define HIKAGE_ABOVE_NORTH   16
static const RuleRun k_hikage_never[] = { { HIKAGE_ABOVE_EAST, HIKAGE_ABOVE_NORTH } },
                     k_hikage_from[] = { RULE_ONE(HIKAGE_MIDDLE),
                                         { HIKAGE_EAST_END + 1, HIKAGE_NORTH_END - 1 },
                                         { HIKAGE_NORTH_END + 1, HIKAGE_WEST_END - 1 },
                                         RULE_ONE(HIKAGE_SOUTH) };
/* Kibogaoka Hill (stg22): from area 31, on the bowl's rim, and from 27 and 23
 * on the way to it, Far buildings adds the areas past the bowl's west side,
 * 56 to 121, at 1x to All: among them a group high over the west side, 56 to
 * 68, and scenery with no space of its own, which from the rim hang in the
 * air, a tall green tower and other blocks (Adri, GitHub issue 2, 20 November
 * 2026; named with Developer > Far buildings test by spriteice's run that
 * day; known bug 71). None of them is added from 31, 27 or 23, or from 32,
 * just past the rim, nor are areas 4 and 6, in the map's middle, or what lies
 * past them; from 35, where the tunnel starts, they are added as before
 * (spriteice, 20 November 2026, 08:46 UTC: "all areas not listed 56 through
 * 121 can be left out of this zone too, and the once in the tunnel they can
 * be enabled again"; 09:56 UTC: "it needs to work in the earlier zone or two
 * as well"; zone 32 named with the in-game menu at 11:34 UTC: "you can just
 * get out of the zone and the buildings pop in"; 11:37 UTC: "we also need to
 * leave out 4, 6 in the same area on k hill"). 108 and 109, far backdrops the
 * game draws from all four, stay, 110 and 111 from all but 32, and 115 to 117
 * where it does. And from area 74, on the bowl's west side, Far shadows draws
 * the shadow volumes of 115, 116 and 117, scenery the game draws from the
 * east side, and each takes the shadow off a walkway there in a pale patch
 * (known bug 72, at 3x and All; named the same way). Far shadows never adds
 * them, from any area, as on Chuo Street: where the game draws them itself
 * their shadows stay, and Far buildings still draws their scenery. And 107,
 * scenery with no space of its own that the game draws only from the bowl's
 * west side (from 71 to 93), Far buildings adds at 3x and All from 9 and 13,
 * east of the map's middle, and at All from 20, past 13, where it hangs in
 * the air (Adri, GitHub issue 2, 20 November 2026; named with Developer >
 * Far buildings test from 9 by spriteice, 25 November 2026; known bug 84).
 * It is never added from those three, where spriteice opened the menu ("We
 * also need to leave out 107 from the kibogoaga hill from the zones I opened
 * the menu a few times"); it lists no area, so nothing past it goes too.
 * Nor is 96, on the bowl's west side, which the game draws from the map's
 * middle (1, 3, 4 and 6) and from the west side, and which Far buildings
 * adds from 9 and 13 at 1x to All and from 20 at 2x to All (spriteice, 25
 * November 2026: "we also need to leave out area 96 on kibogoaga hill in
 * the same areas"). What lies past it from there goes with it: at 2x from 9
 * and 13 and at 3x from 20, 74, 75, 80, 82, 84 and 120, and at 3x from 9,
 * 66 and 68, which come in through its list only; at All, 96 alone. Nor is
 * 105, far past the west side, with a space but an empty list, which the
 * game draws from the west side (71 to 96) and 100, and which Far buildings
 * adds from 9 and 13 at 2x to All and from 20 at 3x and All (spriteice, 25
 * November 2026: "We also need to leave out 105. I can't see anything
 * else"). It lists no area, so nothing past it goes too. Nor are the three
 * added from 7, at the east edge of the map's middle on the way to 9, where
 * spriteice saw 96 next with Developer > Far buildings test (25 November
 * 2026: "96 also needs to go", "from the same areas as before"): from 7 that
 * takes out 96 and 105 at 1x and all three from 2x, and nothing past them. */
#define STAGE_KIBOGAOKA_HILL 0x00020002u    /* as the mission asks for it */
#define KIBOGAOKA_AREAS      124            /* its map's, so it's the one */
#define KIBOGAOKA_TO_RIM     23             /* then 27, by the rim */
#define KIBOGAOKA_BY_RIM     27
#define KIBOGAOKA_RIM        31
#define KIBOGAOKA_PAST_RIM   32
#define KIBOGAOKA_MIDDLE     4              /* and 6, in the map's middle */
#define KIBOGAOKA_MIDDLE_TOO 6
#define KIBOGAOKA_PAST_FIRST 56
#define KIBOGAOKA_PAST_LAST  121
#define KIBOGAOKA_LIT_FIRST  115
#define KIBOGAOKA_LIT_LAST   117
#define KIBOGAOKA_EAST_EDGE  7              /* the middle's east edge, then 9 */
#define KIBOGAOKA_EAST       9              /* then 13, and 20 past it */
#define KIBOGAOKA_EAST_ON    13
#define KIBOGAOKA_EAST_PAST  20
#define KIBOGAOKA_WEST_SIDE  96             /* on the bowl's west side */
#define KIBOGAOKA_WEST_FAR   105            /* far past it */
#define KIBOGAOKA_WEST_SIGHT 107
static const RuleRun k_kibogaoka_never[] = { RULE_ONE(KIBOGAOKA_MIDDLE), RULE_ONE(KIBOGAOKA_MIDDLE_TOO),
                                             { KIBOGAOKA_PAST_FIRST, KIBOGAOKA_PAST_LAST } },
                     k_kibogaoka_from[] = { RULE_ONE(KIBOGAOKA_TO_RIM), RULE_ONE(KIBOGAOKA_BY_RIM),
                                            RULE_ONE(KIBOGAOKA_RIM), RULE_ONE(KIBOGAOKA_PAST_RIM) },
                     k_kibogaoka_lit[] = { { KIBOGAOKA_LIT_FIRST, KIBOGAOKA_LIT_LAST } },
                     k_kibogaoka_west[] = { RULE_ONE(KIBOGAOKA_WEST_SIDE), RULE_ONE(KIBOGAOKA_WEST_FAR),
                                            RULE_ONE(KIBOGAOKA_WEST_SIGHT) },
                     k_kibogaoka_east[] = { RULE_ONE(KIBOGAOKA_EAST_EDGE), RULE_ONE(KIBOGAOKA_EAST),
                                            RULE_ONE(KIBOGAOKA_EAST_ON), RULE_ONE(KIBOGAOKA_EAST_PAST) };
/* 99th Street (Stg30): the scene the first time the player comes in, scene
 * 36, which mission 0250's setup starts (its command 227 [0x24]), takes its
 * camera from the plaza, area 0, into the street west of it, area 8, for a
 * close-up of the dragon on Benten Tower. From area 8 Far buildings adds 30
 * areas the game doesn't draw there, and one of them puts four thin straight
 * lines across that shot, in front of the dragon (Adri, GitHub issue 2, 24
 * November 2026; gone with Far buildings Off on spriteice's PC, 25 November
 * 2026; known bug 80). The game frames a scene's shots with its own set, so
 * while scene 36 plays Far buildings and Far shadows add no area of the map,
 * from any area; the rest of the time, along the street too, they add as
 * before (spriteice, 25 November 2026: "The street (in and out of the level)
 * is where we need far buildings the most in that level. Can we disable them
 * just for that cutscene?"). Which of the 30 draws the lines isn't named. */
#define STAGE_99TH_STREET    0x00030000u    /* as the mission asks for it */
#define STREET99_AREAS       63             /* its map's, so it's the one */
#define STREET99_OPENING     0x24u          /* scene 36, as mission 0250's command 227 names it */
static const RuleRun k_street99_all[] = { { 0, STREET99_AREAS - 1 } };
/* A list of runs, and how many it holds, for a rule. */
#define RULE_AREAS(a) (a), (int)(sizeof (a) / sizeof (a)[0])
/* A rule for no one scene: it holds whatever the game plays. */
#define RULE_ANY_SCENE DRAW_SCENE_NONE
static const struct {
    const char *map;            /* its name, for the log */
    uint32_t    stage;          /* as the mission asks for it */
    int         areas;          /* its map's, so it's the one */
    const RuleRun *never;       /* the areas it hides */
    int         nnever;
    const RuleRun *from;        /* from the sets these areas make; none: from every set */
    int         nfrom;
    int         shadows_only;   /* hides them from Far shadows, not Far buildings */
    uint32_t    scene;          /* only while this scene of the game's plays (draw_buildings_scene); or
                                   RULE_ANY_SCENE */
} k_rules[] = {
    { "Dogenzaka Hill", STAGE_DOGENZAKA_HILL, DOGENZAKA_AREAS,
      RULE_AREAS(k_dogenzaka_never), RULE_AREAS(k_dogenzaka_from), 0, RULE_ANY_SCENE },
    { "Dogenzaka Hill", STAGE_DOGENZAKA_HILL, DOGENZAKA_AREAS,
      RULE_AREAS(k_dogenzaka_hill), RULE_AREAS(k_dogenzaka_far), 0, RULE_ANY_SCENE },
    { "Chuo Street", STAGE_CHUO_STREET, CHUO_AREAS, RULE_AREAS(k_chuo_never), NULL, 0, 1, RULE_ANY_SCENE },
    { "Chuo Street", STAGE_CHUO_STREET, CHUO_AREAS, RULE_AREAS(k_chuo_past), RULE_AREAS(k_chuo_down), 0,
      RULE_ANY_SCENE },
    { "Hikage Street", STAGE_HIKAGE_STREET, HIKAGE_AREAS,
      RULE_AREAS(k_hikage_never), RULE_AREAS(k_hikage_from), 0, RULE_ANY_SCENE },
    { "Kibogaoka Hill", STAGE_KIBOGAOKA_HILL, KIBOGAOKA_AREAS,
      RULE_AREAS(k_kibogaoka_never), RULE_AREAS(k_kibogaoka_from), 0, RULE_ANY_SCENE },
    { "Kibogaoka Hill", STAGE_KIBOGAOKA_HILL, KIBOGAOKA_AREAS, RULE_AREAS(k_kibogaoka_lit), NULL, 0, 1,
      RULE_ANY_SCENE },
    { "Kibogaoka Hill", STAGE_KIBOGAOKA_HILL, KIBOGAOKA_AREAS,
      RULE_AREAS(k_kibogaoka_west), RULE_AREAS(k_kibogaoka_east), 0, RULE_ANY_SCENE },
    { "99th Street", STAGE_99TH_STREET, STREET99_AREAS, RULE_AREAS(k_street99_all), NULL, 0, 0,
      STREET99_OPENING },
};
#define DRAW_RULES ((int)(sizeof k_rules / sizeof k_rules[0]))
_Static_assert(sizeof k_rules / sizeof k_rules[0] <= 32, "a bit of s_map.rules for each rule");

void draw_buildings_stage(uint32_t stage)
{
    s_stage = stage;
}

/* The game's scene now (draw_buildings_scene), and the one the log last
 * said (scene_note). */
static uint32_t s_scene = DRAW_SCENE_NONE, s_scene_said = DRAW_SCENE_NONE;

void draw_buildings_scene(uint32_t scene)
{
    s_scene = scene;
}

/* The map, told from another: FNV-1a over its areas' volumes and lists. */
static uint32_t map_key(const DrawArea *a, int n)
{
    uint32_t h = 2166136261u, w;
    int i;
    unsigned k;
#define MIX(v) (h = (h ^ (uint32_t)(v)) * 16777619u)
    MIX(n);
    for (i = 0; i < n; i++) {
        if (a[i].volume)
            for (k = 0; k < 36; k++) { memcpy(&w, &a[i].volume[k], 4); MIX(w); }
        MIX(a[i].nlist);
        for (k = 0; a[i].list && k < a[i].nlist; k++) MIX(a[i].list[k]);
    }
#undef MIX
    return h;
}

/* "13", "0 and 16", "47, 48 and 50", or "56 to 121": a rule's runs of
 * areas, for the log. */
static void areas_said(char *out, int room, const RuleRun *a, int n)
{
    int i, len = 0;
    out[0] = 0;
    for (i = 0; i < n && len >= 0 && len < room; i++) {
        const char *sep = !i ? "" : i == n - 1 ? " and " : ", ";
        if (a[i].first == a[i].last) len += snprintf(out + len, room - len, "%s%d", sep, a[i].first);
        else len += snprintf(out + len, room - len, "%s%d to %d", sep, a[i].first, a[i].last);
    }
}

/* Whether a rule's runs hold more than one area, for the log's plurals. */
static int runs_many(const RuleRun *a, int n)
{
    return n > 1 || (n == 1 && a[0].last > a[0].first);
}

static void map_take(const DrawArea *a, int n, uint32_t key)
{
    char line[2048];
    int i, k, len, listed = 0;
    unsigned e;
    memset(&s_map, 0, sizeof s_map);
    misc_forget();      /* area numbers are the map's own */
    /* Developer > Far buildings test's Leave out area: area numbers are the
     * map's own. */
    if (s_leave_out >= 0)
        fprintf(stderr, "[DRAW] Far buildings test: a new map, so area %d is no longer left out\n",
                s_leave_out);
    s_leave_out = -1;
    s_leave_press = 0;
    s_eye = -1;
    s_eye_many = 0;
    memset(s_eye_seen, 0, sizeof s_eye_seen);
    memset(s_added_now, 0, sizeof s_added_now);
    s_map.built = 1;
    s_map.n = n;
    s_map.key = key;
    s_map.stage = s_stage;      /* asked for before the map loads */
    for (i = 0; i < n; i++)
        for (e = 0; a[i].list && e < a[i].nlist; e++)
            if (a[i].list[e] < (uint32_t)n) {
                set_add(s_map.listed, (int)a[i].list[e]);
                set_add(s_map.list[i], (int)a[i].list[e]);
            }
    for (i = 0; i < n; i++) listed += set_has(s_map.listed, i);
    len = snprintf(line, sizeof line, "[DRAW] Far buildings %s: the map has %d areas; lists name %d of them",
                   k_far_names[s_far], n, listed);
    /* An area no list names is never drawn. */
    for (i = 0, k = 0; i < n && len > 0 && len < (int)sizeof line; i++)
        if (!set_has(s_map.listed, i))
            len += snprintf(line + len, sizeof line - len, "%s %d", k++ ? "," : "; no list names", i);
    fprintf(stderr, "%s\n", line);
    for (i = 0; i < n; i++)
        if (set_has(s_map.listed, i))
            fprintf(stderr, "[DRAW] area %d: fog %g to %g, density %g\n",
                    i, a[i].fog_start, a[i].fog_end, a[i].fog_density);
    for (i = 0; i < DRAW_RULES; i++) {
        char never[512], from[512];
        int many = runs_many(k_rules[i].never, k_rules[i].nnever);
        if (k_rules[i].stage != s_map.stage) continue;
        if (k_rules[i].areas != n) {
            fprintf(stderr, "[DRAW] Far buildings: stage %08X has %d areas, not %s's %d, so its rule is "
                    "left out\n", s_map.stage, n, k_rules[i].map, k_rules[i].areas);
            continue;
        }
        s_map.rules |= 1u << i;
        areas_said(never, sizeof never, k_rules[i].never, k_rules[i].nnever);
        if (k_rules[i].nfrom) {
            int said = snprintf(from, sizeof from, "area%s ",
                                runs_many(k_rules[i].from, k_rules[i].nfrom) ? "s" : "");
            areas_said(from + said, (int)sizeof from - said, k_rules[i].from, k_rules[i].nfrom);
        } else snprintf(from, sizeof from, "any area");
        if (k_rules[i].scene != RULE_ANY_SCENE)
            fprintf(stderr, "[DRAW] Far buildings: %s (stage %08X), area%s %s never added from %s while "
                    "the game's scene %u plays, whose shots it frames without them\n", k_rules[i].map,
                    s_map.stage, many ? "s" : "", never, from, (unsigned)k_rules[i].scene);
        else if (k_rules[i].shadows_only)
            fprintf(stderr, "[DRAW] Far shadows: %s (stage %08X), the shadows of area%s %s never added "
                    "from %s, where they make patches of light\n", k_rules[i].map, s_map.stage,
                    many ? "s" : "", never, from);
        else
            fprintf(stderr, "[DRAW] Far buildings: %s (stage %08X), area%s %s and what lies past %s never "
                    "added from %s, where the game hides them\n", k_rules[i].map, s_map.stage,
                    many ? "s" : "", never, many ? "them" : "it", from);
    }
}

/* Up to `crossings` crossings ahead from set: each adds the areas the lists
 * of the areas the last one added name, from set itself, and stops once one
 * adds nothing. The walk never steps into hidden, a wall the map's rules make
 * (rules_hidden), so it never reads a hidden area's list either. s_map is the
 * map's. */
static void set_walk(uint32_t set[DRAW_SET_WORDS], int crossings, const uint32_t hidden[DRAW_SET_WORDS])
{
    uint32_t from[DRAW_SET_WORDS], next[DRAW_SET_WORDS], more;
    int i, w, cross;
    memcpy(from, set, sizeof from);
    for (cross = 0; cross < crossings; cross++) {
        memcpy(next, set, sizeof next);
        for (i = 0; i < s_map.n; i++)
            if (set_has(from, i))
                for (w = 0; w < DRAW_SET_WORDS; w++) next[w] |= s_map.list[i][w];
        for (w = 0, more = 0; w < DRAW_SET_WORDS; w++) {
            next[w] &= ~hidden[w];
            from[w] = next[w] & ~set[w];
            set[w] = next[w];
            more |= from[w];
        }
        if (!more) break;
    }
}

/* Widens set to step: for 1 to 3, as many crossings ahead (set_walk); for
 * the whole map, every area some list names. Never into hidden, nor past it:
 * at 1x to 3x the walk doesn't cross it, and for the whole map the areas the
 * walk would reach from set only through it are left out too, as they are
 * behind the line the game draws there (known bug 69: area 13 of Dogenzaka
 * Hill is hidden from the street below it, and the areas up the hill past
 * it, reached only through its list, hang in the sky from there). With
 * nothing hidden, the whole map is every listed area, as before. s_map is
 * the map's. */
static void set_reach(uint32_t set[DRAW_SET_WORDS], int step, const uint32_t hidden[DRAW_SET_WORDS])
{
    static const uint32_t k_none[DRAW_SET_WORDS];
    uint32_t open[DRAW_SET_WORDS], walled[DRAW_SET_WORDS], any = 0;
    int w;
    if (step < DRAW_FAR_WHOLE) {
        set_walk(set, step, hidden);
        return;
    }
    for (w = 0; w < DRAW_SET_WORDS; w++) any |= hidden[w];
    if (!any) {
        for (w = 0; w < DRAW_SET_WORDS; w++) set[w] |= s_map.listed[w];
        return;
    }
    memcpy(open, set, sizeof open);
    memcpy(walled, set, sizeof walled);
    set_walk(open, DRAW_AREAS_MAX, k_none);
    set_walk(walled, DRAW_AREAS_MAX, hidden);
    for (w = 0; w < DRAW_SET_WORDS; w++)
        set[w] |= s_map.listed[w] & ~hidden[w] & ~(open[w] & ~walled[w]);
}

/* "2, 11, 12", or "none". */
static void set_print(char *out, int room, const uint32_t *set)
{
    int i, len = 0;
    out[0] = 0;
    for (i = 0; i < DRAW_AREAS_MAX && len >= 0 && len < room; i++)
        if (set_has(set, i)) len += snprintf(out + len, room - len, "%s%d", len ? ", " : "", i);
    if (!out[0]) snprintf(out, room, "none");
}

/* Whether set is the one the game makes from an eye in area i: the area and
 * the areas its list names. s_map is the map's. */
static int set_from_area(const uint32_t set[DRAW_SET_WORDS], int i)
{
    int w, same = set_has(set, i);
    for (w = 0; same && w < DRAW_SET_WORDS; w++)
        same = (s_map.list[i][w] | (w == (i >> 5) ? 1u << (i & 31) : 0u)) == set[w];
    return same;
}

/* Area a taken out of set, widened from game, unless game has it: what the
 * game draws itself is never taken away. */
static void set_leave_out(uint32_t set[DRAW_SET_WORDS], const uint32_t game[DRAW_SET_WORDS], int a)
{
    if (a >= 0 && a < DRAW_AREAS_MAX && !set_has(game, a)) set[a >> 5] &= ~(1u << (a & 31));
}

/* The areas the map's rules (k_rules) hide while game is the set one of
 * their areas makes, or from every set, for set_reach's wall: for Far
 * shadows' set (shadows nonzero) or Far buildings'. A rule for one scene
 * only while that scene plays. But none game has: what the game draws
 * itself is never hidden. */
static void rules_hidden(uint32_t hidden[DRAW_SET_WORDS], const uint32_t game[DRAW_SET_WORDS], int shadows)
{
    int r, k, a, w, from;
    memset(hidden, 0, DRAW_SET_WORDS * sizeof hidden[0]);
    for (r = 0; r < DRAW_RULES; r++) {
        if (!(s_map.rules >> r & 1u) || (k_rules[r].shadows_only && !shadows)) continue;
        if (k_rules[r].scene != RULE_ANY_SCENE && k_rules[r].scene != s_scene) continue;
        for (k = 0, from = !k_rules[r].nfrom; k < k_rules[r].nfrom && !from; k++)
            for (a = k_rules[r].from[k].first; a <= k_rules[r].from[k].last && !from; a++)
                from = a >= 0 && a < s_map.n && set_from_area(game, a);
        if (!from) continue;
        for (k = 0; k < k_rules[r].nnever; k++)
            for (a = k_rules[r].never[k].first; a <= k_rules[r].never[k].last; a++)
                if (a >= 0 && a < DRAW_AREAS_MAX) set_add(hidden, a);
    }
    for (w = 0; w < DRAW_SET_WORDS; w++) hidden[w] &= ~game[w];
}

/* The eye's area, for the log, from the game's set: the area that makes it
 * (set_from_area). The game's own number for it isn't kept, so where two
 * areas make the same set, as two areas side by side can, it is the first,
 * and the log says another fits too. A change of it has a line, up to
 * DRAW_EYE_LINES between two [DRAW] lines, with what Far buildings adds
 * there. */
static void eye_note(const uint32_t set[DRAW_SET_WORDS], double now)
{
    int i, w, eye = -1, many = 0;
    for (i = 0; i < s_map.n; i++) {
        if (!set_from_area(set, i)) continue;
        if (eye < 0) eye = i;
        else many = 1;
    }
    if (eye != s_eye) {
        char added[512];
        uint32_t adds[DRAW_SET_WORDS], hidden[DRAW_SET_WORDS];
        memcpy(adds, set, sizeof adds);
        rules_hidden(hidden, set, 0);
        set_reach(adds, s_far, hidden);
        for (w = 0; w < DRAW_SET_WORDS; w++) adds[w] &= ~set[w];
        set_print(added, sizeof added, adds);
        if (s_eye_lines < DRAW_EYE_LINES) {
            s_eye_lines++;
            if (eye >= 0)
                fprintf(stderr, "[DRAW] the eye at t=%.2f: area %d%s, from %d; Far buildings adds %s there\n",
                        now, eye, many ? " (another area's list is the set too)" : "", s_eye, added);
            else
                fprintf(stderr, "[DRAW] the eye at t=%.2f: no area's list is the set, from %d; "
                        "Far buildings adds %s\n", now, s_eye, added);
        } else s_eye_unsaid++;
    }
    s_eye = eye;
    s_eye_many = many;
    if (eye >= 0) set_add(s_eye_seen, eye);
}

/* Leave out area's press, on the game's thread, once s_added_now is what Far
 * buildings adds from the eye now: + the next of those after the one left
 * out, and none after the last; - the one before, and none before the first.
 * Logged with the eye's area and what Far buildings adds there. */
static void leave_step(void)
{
    char added[512];
    int press = s_leave_press, at = s_leave_out, to = -1, i;
    if (!press) return;
    s_leave_press = 0;
    if (press > 0) {
        for (i = at + 1; i < DRAW_AREAS_MAX; i++)
            if (set_has(s_added_now, i)) { to = i; break; }
    } else {
        for (i = at - 1; i >= 0; i--)
            if (set_has(s_added_now, i)) { to = i; break; }
    }
    s_leave_out = to;
    set_print(added, sizeof added, s_added_now);
    if (to >= 0) fprintf(stderr, "[DRAW] Far buildings test: leaving out area %d (the menu)", to);
    else fprintf(stderr, "[DRAW] Far buildings test: leaving out none (the menu)");
    if (s_eye >= 0) fprintf(stderr, "; the eye in area %d", s_eye);
    else fprintf(stderr, "; the eye's area unknown");
    fprintf(stderr, ", Far buildings %s adding %s\n", k_far_names[s_far], added);
}

/* A line as a scene one of the map's rules is for starts or ends, at the
 * first set after it (s_scene_said): the map's rules (s_map.rules) are known
 * only once its first set is made. */
static void scene_note(double now)
{
    int r;
    if (s_scene == s_scene_said) return;
    for (r = 0; r < DRAW_RULES; r++) {
        if (!(s_map.rules >> r & 1u) || k_rules[r].scene == RULE_ANY_SCENE) continue;
        if (k_rules[r].scene == s_scene)
            fprintf(stderr, "[DRAW] t=%.2f the game's scene %u plays: %s's rule for it holds\n", now,
                    (unsigned)s_scene, k_rules[r].map);
        else if (k_rules[r].scene == s_scene_said)
            fprintf(stderr, "[DRAW] t=%.2f the game's scene %u is over: %s's rule for it no longer holds\n",
                    now, (unsigned)s_scene_said, k_rules[r].map);
    }
    s_scene_said = s_scene;
}

void draw_buildings_widen(const DrawArea *areas, int n, uint32_t set[DRAW_SET_WORDS],
                          double now, unsigned frame)
{
    int game, reach = s_far, w;
    uint32_t key, was[DRAW_SET_WORDS], hidden[DRAW_SET_WORDS];
    s_widened = 0;
    if (!reach || !areas || n <= 0) {
        memset(s_added_now, 0, sizeof s_added_now);
        s_leave_press = 0;      /* nothing added to step through */
        return;
    }
    if (n > DRAW_AREAS_MAX) n = DRAW_AREAS_MAX;
    key = map_key(areas, n);
    if (!s_map.built || s_map.n != n || s_map.key != key) map_take(areas, n, key);
    s_misc_map = 1;
    game = set_count(set);
    memcpy(was, set, sizeof was);
    scene_note(now);
    eye_note(set, now);
    rules_hidden(hidden, was, 0);
    set_reach(set, reach, hidden);
    for (w = 0; w < DRAW_SET_WORDS; w++) s_added_now[w] = set[w] & ~was[w];
    leave_step();
    set_leave_out(set, was, s_leave_out);
    s_widened = reach;
    s_sets++;
    s_game += game;
    s_added += set_count(set) - game;
    draw_log(now, frame);
}

int draw_set_whole_map(const DrawArea *areas, int n, uint32_t set[DRAW_SET_WORDS])
{
    int i;
    unsigned e;
    if (n > DRAW_AREAS_MAX) n = DRAW_AREAS_MAX;
    for (i = 0; areas && i < n; i++)
        for (e = 0; areas[i].list && e < areas[i].nlist; e++)
            if (areas[i].list[e] < (uint32_t)n) set_add(set, (int)areas[i].list[e]);
    return set_count(set);
}

void draw_buildings_shadows(uint32_t set[DRAW_SET_WORDS], const uint32_t game[DRAW_SET_WORDS],
                            void (*run)(void))
{
    uint32_t wide[DRAW_SET_WORDS], hidden[DRAW_SET_WORDS], built[DRAW_SET_WORDS];
    int reach = s_widened, step = s_shadows_far;     /* the set's own, should a row change between */
    int w, same = 1;
    if (step > reach) step = reach;
    s_shadows++;
    s_shadows_added += set_count(set) - set_count(game);
    if (reach) {        /* what the map's rules hide from the shadows, and from the buildings */
        rules_hidden(hidden, game, 1);
        rules_hidden(built, game, 0);
        for (w = 0; w < DRAW_SET_WORDS; w++) same &= hidden[w] == built[w];
    }
    if (step == reach && same) {      /* the set Far buildings made */
        run();
        return;
    }
    memcpy(wide, set, sizeof wide);
    memcpy(set, game, sizeof wide);
    set_reach(set, step, hidden);
    set_leave_out(set, game, s_leave_out);
    s_shadows_added -= set_count(wide) - set_count(set);
    run();
    memcpy(set, wide, sizeof wide);
}

/* ---- the settings -------------------------------------------------------- */

/* Input playback's: a recording's steps for its play, and the player's back
 * after, as the menu's picks set them, but for the settings file. */
void draw_distance_set(int step)
{
    if (step >= 0 && step < DRAW_DISTANCE_STEPS) s_step = step;
}

void draw_far_buildings_set(int step)
{
    if (step < 0 || step >= DRAW_FAR_STEPS || step == s_far) return;
    s_misc_map = 0;     /* the map may change while it is off */
    s_far = step;
}

void draw_far_shadows_set(int step)
{
    if (step >= 0 && step < DRAW_FAR_STEPS) s_shadows_far = step;
}


#if defined(XBOXRECOMP_MENU)
static int  menu_step(void) { return s_step; }
static void menu_step_pick(int i)
{
    if (i < 0 || i >= DRAW_DISTANCE_STEPS) return;
    s_step = i;
    fprintf(stderr, "[DRAW] objects at %s (the menu)\n", k_names[i]);
    nv_window_pref_set_num("drawdistance", i);
}
static int  menu_far(void) { return s_far; }
static void menu_far_pick(int i)
{
    if (i < 0 || i >= DRAW_FAR_STEPS) return;
    s_misc_map = 0;     /* the map may change while it is off */
    s_far = i;
    fprintf(stderr, "[DRAW] Far buildings %s (the menu)\n", k_far_names[i]);
    nv_window_pref_set_num("farbuildingsahead", i);
}
static int  menu_shadows(void) { return s_shadows_far; }
static void menu_shadows_pick(int i)
{
    if (i < 0 || i >= DRAW_FAR_STEPS) return;
    s_shadows_far = i;
    fprintf(stderr, "[DRAW] Far shadows %s (the menu)\n", k_far_names[i]);
    nv_window_pref_set_num("farshadowsahead", i);
}
#endif

/* ---- Developer > Far buildings test ------------------------------------ */

int draw_far_test_backdrop(void)
{
    return s_backdrop_game;
}

#if defined(XBOXRECOMP_MENU)
static double test_leave_value(void)
{
    return s_leave_out;
}

/* A press of - or +: stepped on the game's thread (leave_step), where what
 * Far buildings adds from the eye is known. */
static void test_leave_set(double v)
{
    if (v != s_leave_out) s_leave_press = v > s_leave_out ? 1 : -1;
}

static int test_backdrop_state(void)
{
    return s_backdrop_game;
}

static void test_backdrop_flip(void)
{
    s_backdrop_game = !s_backdrop_game;
    fprintf(stderr, "[DRAW] Far buildings test: the backdrop's depth writes %s (the menu)\n",
            s_backdrop_game ? "as the game sets them" : "off, as Far buildings sets them");
}
#endif

/* A step from the settings, key= 0 to 4. With no key=, the toggle kept before
 * b2.1-hotfix, legacy=: Off for 0, and 3x (DRAW_FAR_ONCE_ON) for 1 rather
 * than All, which was what on drew (known bug 57). With neither, def. */
static int far_setting(const char *key, const char *legacy, int def)
{
    double v;
    int r = nv_window_pref_num(key, &v);
    if (r > 0 && v >= 0 && v < DRAW_FAR_STEPS && v == (int)v) return (int)v;
    if (r > 0)
        fprintf(stderr, "[DRAW] settings: %s=%g is not 0 to %d; using %d\n", key, v,
                DRAW_FAR_STEPS - 1, def);
    else if (r < 0)
        fprintf(stderr, "[DRAW] settings: %s is not a number; using %d\n", key, def);
    else if (nv_window_pref_num(legacy, &v) > 0)
        return v != 0 ? DRAW_FAR_ONCE_ON : 0;
    return def;
}

void draw_distance_start(void)
{
    double v;
    int r = nv_window_pref_num("drawdistance", &v);
    s_step = DRAW_DISTANCE_DEFAULT;
    if (r > 0 && v >= 0 && v < DRAW_DISTANCE_STEPS && v == (int)v)
        s_step = (int)v;
    else if (r > 0)
        fprintf(stderr, "[DRAW] settings: drawdistance=%g is not 0 to %d; using %d\n", v,
                DRAW_DISTANCE_STEPS - 1, DRAW_DISTANCE_DEFAULT);
    else if (r < 0)
        fprintf(stderr, "[DRAW] settings: drawdistance is not a number; using %d\n",
                DRAW_DISTANCE_DEFAULT);
    fprintf(stderr, "[DRAW] objects at %s (settings)\n", k_names[s_step]);
    s_far = far_setting("farbuildingsahead", "farbuildings", DRAW_FAR_DEFAULT);
    fprintf(stderr, "[DRAW] Far buildings %s (settings)\n", k_far_names[s_far]);
    s_shadows_far = far_setting("farshadowsahead", "farshadows", DRAW_SHADOWS_DEFAULT);
    fprintf(stderr, "[DRAW] Far shadows %s (settings)\n", k_far_names[s_shadows_far]);
#if defined(XBOXRECOMP_MENU)
    {
        /* Rows are drawn in the order added, and the window adds HUD
         * once it is up, after this: so Draw distance and the Building
         * distance submenu name the row they go under, and keep their order
         * there; Far buildings and Far shadows go in the submenu. */
        const XBOX_MENU_ITEM rows[] = {
            /* File > Presets: the game's own distances and areas in Xbox,
             * 3x of each in Modern, the defaults in Performance (the least
             * past the game's), 2x and 3x in Balanced (the defaults before
             * b2.2), and the most in Quality, All among them
             * (spriteice, 2026-11-23; docs/menu-format.md). */
            { .menu = XBOX_MENU_DISPLAY, .kind = XBOX_ITEM_CHOICE, .label = "Draw distance",
              .state = menu_step, .pick = menu_step_pick,
              .choices = k_names, .nchoices = DRAW_DISTANCE_STEPS, .under = DRAW_UNDER,
              .presets = XBOX_PRESETS_SET,
              .preset = { 0, 2, DRAW_DISTANCE_DEFAULT, DRAW_DISTANCE_BALANCED,
                          DRAW_DISTANCE_UNLIMITED },                      /* 1x, 3x, 1x, 2x, Unlimited */
              .hint = "Sets how far crowds, vehicles, props, and police are drawn. CPU heavy." },
            { .menu = XBOX_MENU_DISPLAY, .kind = XBOX_ITEM_GROUP, .label = DRAW_GROUP,
              .under = DRAW_UNDER, .hint = "Draws more of the map and its shadows to reduce pop-in." },
            /* The hints since known bug 57 (spriteice, 14 November 2026,
             * 15:32 UTC, on the card: "Use the proposed hints"). */
            { .menu = XBOX_MENU_DISPLAY, .kind = XBOX_ITEM_CHOICE, .label = "Far buildings",
              .state = menu_far, .pick = menu_far_pick, .group = DRAW_GROUP,
              .choices = k_far_names, .nchoices = DRAW_FAR_STEPS,
              .presets = XBOX_PRESETS_SET,
              .preset = { 0, 3, DRAW_FAR_DEFAULT, DRAW_FAR_BALANCED, DRAW_FAR_WHOLE },     /* Off, 3x, 1x, 3x, All */
              .hint = "Sets how many areas ahead buildings are drawn. CPU heavy." },
            /* On by default, at 1x: less pop-in is worth the glitches it can
             * cause (known bug 52). Never further than Far buildings. */
            { .menu = XBOX_MENU_DISPLAY, .kind = XBOX_ITEM_CHOICE, .label = "Far shadows",
              .state = menu_shadows, .pick = menu_shadows_pick, .group = DRAW_GROUP,
              .choices = k_far_names, .nchoices = DRAW_FAR_STEPS,
              .presets = XBOX_PRESETS_SET,
              .preset = { 0, 3, DRAW_SHADOWS_DEFAULT, DRAW_SHADOWS_BALANCED, DRAW_FAR_WHOLE }, /* Off, 3x, 1x, 3x, All */
              .hint = "Sets how many areas ahead shadows are drawn. Minor glitches." },
        };
        /* Developer > Far buildings test, a submenu of two rows that change at
         * once and aren't kept, to find which area Far buildings adds draws
         * what it shouldn't (spriteice, 16 November 2026, 06:01 UTC: "a
         * great developer option to leave in as well, to diagnose further
         * issues of the same kind"; known bug 66). */
        const XBOX_MENU_ITEM tests[] = {
            { .menu = XBOX_MENU_DEVELOPER, .kind = XBOX_ITEM_GROUP, .label = DRAW_TEST_GROUP,
              .hint = "Finds which area Far buildings adds draws something out of place." },
            { .menu = XBOX_MENU_DEVELOPER, .kind = XBOX_ITEM_NUMBER, .label = "Leave out area",
              .value = test_leave_value, .set = test_leave_set,
              .min = -1, .max = DRAW_AREAS_MAX - 1, .step = 1, .format = "%.0f", .group = DRAW_TEST_GROUP,
              .hint = "Leaves out one area Far buildings adds here. -1 is none." },
            { .menu = XBOX_MENU_DEVELOPER, .kind = XBOX_ITEM_TOGGLE, .label = "Game's backdrop depth",
              .state = test_backdrop_state, .run = test_backdrop_flip, .group = DRAW_TEST_GROUP,
              .hint = "Draws the backdrop with the depth writes the game sets." },
        };
        xbox_MenuAdd(rows, (int)(sizeof rows / sizeof rows[0]));
        xbox_MenuAdd(tests, (int)(sizeof tests / sizeof tests[0]));
    }
#endif
}

/* ---- where the draws come from ------------------------------------------ */

int draw_area_in_view(const float *volume, const float m[16], const float view[DRAW_VIEW_FLOATS])
{
    float lo[3], hi[3], c[3], r, x, y, z, d2 = 0.0f;
    int i, k;
    for (k = 0; k < 3; k++) lo[k] = hi[k] = volume[k];
    for (i = 1; i < 6; i++)
        for (k = 0; k < 3; k++) {
            float v = volume[i * 3 + k];
            if (v < lo[k]) lo[k] = v;
            if (v > hi[k]) hi[k] = v;
        }
    for (k = 0; k < 3; k++) {
        c[k] = 0.5f * (lo[k] + hi[k]);
        d2 += 0.25f * (hi[k] - lo[k]) * (hi[k] - lo[k]);
    }
    r = sqrtf(d2);
    x = c[0] * m[0] + c[1] * m[4] + c[2] * m[8] + m[12];
    y = c[0] * m[1] + c[1] * m[5] + c[2] * m[9] + m[13];
    z = c[0] * m[2] + c[1] * m[6] + c[2] * m[10] + m[14];
    /* As the game's: in front of the near distance and short of the far,
     * then inside each side plane and each vertical one. */
    if (!(z + r > view[0]) || !(z - r < view[1])) return 0;
    if (x <= 0.0f ? !(x + r * view[4] > z * view[5]) : !(x - r * view[6] < z * view[7])) return 0;
    if (y >= 0.0f ? !(y - r * view[2] < z * view[3]) : !(y + r * view[8] > z * view[9])) return 0;
    return 1;
}

static unsigned long s_from[DRAW_FROMS];        /* draws since the line, by where from */
static unsigned long s_from_areas[3], s_own_in; /* pass 0's areas: own, added in, added out */
static double s_from_since = -1;
static unsigned s_from_since_frame;
static int s_from_last = DRAW_FROM_OWN;         /* the last area's, for its later pieces */

int draw_from_piece(int pass0, int shadow, int area, int first, const uint32_t *game_set,
                    const float *volume, const float m[16], const float view[DRAW_VIEW_FLOATS])
{
    int own, in;
    if (shadow) return DRAW_FROM_SHADOWS;
    if (!first) return s_from_last;
    own = !game_set || area < 0 || area >= DRAW_AREAS_MAX
       || (game_set[area >> 5] >> (area & 31) & 1u);
    in = volume ? draw_area_in_view(volume, m, view) : 1;
    s_from_last = own ? DRAW_FROM_OWN : in ? DRAW_FROM_ADDED_IN : DRAW_FROM_ADDED_OUT;
    if (pass0) {
        s_from_areas[own ? 0 : in ? 1 : 2]++;
        if (own && in) s_own_in++;
    }
    return s_from_last;
}

void draw_from_drawn(int from)
{
    if (from < 0 || from >= DRAW_FROMS) from = DRAW_FROM_REST;
    __atomic_fetch_add(&s_from[from], 1, __ATOMIC_RELAXED);
}

void draw_from_frame(double now, unsigned frame, unsigned long long seethru_second)
{
    static unsigned long long seethru_since;
    unsigned long d[DRAW_FROMS];
    double f;
    int k;
    if (s_from_since < 0) {
        s_from_since = now; s_from_since_frame = frame; seethru_since = seethru_second;
        for (k = 0; k < DRAW_FROMS; k++) __atomic_exchange_n(&s_from[k], 0, __ATOMIC_RELAXED);
        s_from_areas[0] = s_from_areas[1] = s_from_areas[2] = s_own_in = 0;
        return;
    }
    if (now - s_from_since < DRAW_LOG_EVERY) return;
    f = frame != s_from_since_frame ? (double)(frame - s_from_since_frame) : 1.0;
    for (k = 0; k < DRAW_FROMS; k++) d[k] = __atomic_exchange_n(&s_from[k], 0, __ATOMIC_RELAXED);
    fprintf(stderr, "[DRAW] t=%.1f draws a frame by where they come from: the game's own areas "
                    "%.1f (%.1f areas, %.1f of them in view), the areas Far buildings adds %.1f "
                    "in view (%.1f areas) and %.1f out of it (%.1f areas), the shadow passes "
                    "%.1f, the rest %.1f; the see-through fix's second draws %.1f\n",
            now, d[DRAW_FROM_OWN] / f, s_from_areas[0] / f, s_own_in / f,
            d[DRAW_FROM_ADDED_IN] / f, s_from_areas[1] / f, d[DRAW_FROM_ADDED_OUT] / f,
            s_from_areas[2] / f, d[DRAW_FROM_SHADOWS] / f, d[DRAW_FROM_REST] / f,
            (double)(seethru_second - seethru_since) / f);
    s_from_areas[0] = s_from_areas[1] = s_from_areas[2] = s_own_in = 0;
    seethru_since = seethru_second;
    s_from_since = now;
    s_from_since_frame = frame;
}
