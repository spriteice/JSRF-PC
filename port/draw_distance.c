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
 * row: Display's order is Fullscreen, Render scale, Aspect ratio, HUD, then
 * Draw distance and Building distance (spriteice, 12 November 2026: "Render
 * scale up", on the card at 06:19 UTC, and HUD scaling "as it's own submenu
 * under aspect ratio", at 09:37 UTC, named HUD on the card at 09:43 UTC).
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
 * the stage asked for before it, its rule (k_rules; -1 none), the areas some
 * list names, and each area's list. */
static struct {
    int      built, n, rule;
    uint32_t key, stage;
    uint32_t listed[DRAW_SET_WORDS];
    uint32_t list[DRAW_AREAS_MAX][DRAW_SET_WORDS];
} s_map;

/* The stage the mission last asked for (draw_buildings_stage), on the game's
 * thread. */
static uint32_t s_stage = DRAW_STAGE_NONE;

/* The maps' own rules: an area Far buildings never adds while the game's set
 * is the one some areas make, where the game hides it on purpose and the
 * lists can't tell (draw_distance.h, known bug 66). The stage files don't say
 * which areas the game hides from which side, and no rule from the lists
 * tells these from the areas Far buildings is there to add
 * (far-buildings-1x/DIAGNOSIS.md in the shared folder): each one is kept by
 * hand, for a case seen and named, with spriteice's OK.
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
#define DRAW_RULE_FROM       2              /* the most areas a rule hides from */
static const struct {
    const char *map;
    uint32_t    stage;
    int         areas, never, from[DRAW_RULE_FROM];
} k_rules[] = {
    { "Dogenzaka Hill", STAGE_DOGENZAKA_HILL, DOGENZAKA_AREAS, DOGENZAKA_UPHILL,
      { DOGENZAKA_STREET, DOGENZAKA_CORNER } },
};
#define DRAW_RULES ((int)(sizeof k_rules / sizeof k_rules[0]))

void draw_buildings_stage(uint32_t stage)
{
    s_stage = stage;
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
    s_map.rule = -1;
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
        if (k_rules[i].stage != s_map.stage) continue;
        if (k_rules[i].areas != n) {
            fprintf(stderr, "[DRAW] Far buildings: stage %08X has %d areas, not %s's %d, so its rule is "
                    "left out\n", s_map.stage, n, k_rules[i].map, k_rules[i].areas);
            continue;
        }
        s_map.rule = i;
        fprintf(stderr, "[DRAW] Far buildings: %s (stage %08X), area %d never added from areas %d and %d, "
                "where the game hides it\n", k_rules[i].map, s_map.stage, k_rules[i].never,
                k_rules[i].from[0], k_rules[i].from[1]);
    }
}

/* Widens set to step: for 1 to 3, as many crossings ahead, each adding the
 * areas the lists of the areas the last one added name, from set itself;
 * for the whole map, every area some list names. s_map is the map's. */
static void set_reach(uint32_t set[DRAW_SET_WORDS], int step)
{
    uint32_t from[DRAW_SET_WORDS], next[DRAW_SET_WORDS];
    int i, w, cross;
    if (step >= DRAW_FAR_WHOLE) {
        for (w = 0; w < DRAW_SET_WORDS; w++) set[w] |= s_map.listed[w];
        return;
    }
    memcpy(from, set, sizeof from);
    for (cross = 0; cross < step; cross++) {
        memcpy(next, set, sizeof next);
        for (i = 0; i < s_map.n; i++)
            if (set_has(from, i))
                for (w = 0; w < DRAW_SET_WORDS; w++) next[w] |= s_map.list[i][w];
        for (w = 0; w < DRAW_SET_WORDS; w++) {
            from[w] = next[w] & ~set[w];
            set[w] = next[w];
        }
    }
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

/* The map's rule (k_rules) on set, widened from game: its area taken out
 * while game is the set one of its areas makes. */
static void map_rule(uint32_t set[DRAW_SET_WORDS], const uint32_t game[DRAW_SET_WORDS])
{
    int k, r = s_map.rule;
    if (r < 0) return;
    for (k = 0; k < DRAW_RULE_FROM; k++)
        if (set_from_area(game, k_rules[r].from[k])) set_leave_out(set, game, k_rules[r].never);
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
        uint32_t adds[DRAW_SET_WORDS];
        memcpy(adds, set, sizeof adds);
        set_reach(adds, s_far);
        map_rule(adds, set);
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

void draw_buildings_widen(const DrawArea *areas, int n, uint32_t set[DRAW_SET_WORDS],
                          double now, unsigned frame)
{
    int game, reach = s_far, w;
    uint32_t key, was[DRAW_SET_WORDS];
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
    eye_note(set, now);
    set_reach(set, reach);
    map_rule(set, was);
    for (w = 0; w < DRAW_SET_WORDS; w++) s_added_now[w] = set[w] & ~was[w];
    leave_step();
    set_leave_out(set, was, s_leave_out);
    s_widened = reach;
    s_sets++;
    s_game += game;
    s_added += set_count(set) - game;
    draw_log(now, frame);
}

void draw_buildings_shadows(uint32_t set[DRAW_SET_WORDS], const uint32_t game[DRAW_SET_WORDS],
                            void (*run)(void))
{
    uint32_t wide[DRAW_SET_WORDS];
    int reach = s_widened, step = s_shadows_far;     /* the set's own, should a row change between */
    if (step > reach) step = reach;
    s_shadows++;
    s_shadows_added += set_count(set) - set_count(game);
    if (step == reach) {      /* the set Far buildings made */
        run();
        return;
    }
    memcpy(wide, set, sizeof wide);
    memcpy(set, game, sizeof wide);
    set_reach(set, step);
    map_rule(set, game);
    set_leave_out(set, game, s_leave_out);
    s_shadows_added -= set_count(wide) - set_count(set);
    run();
    memcpy(set, wide, sizeof wide);
}

/* ---- the settings -------------------------------------------------------- */

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
 * b2.1-hotfix, legacy=: Off for 0, and def for 1 rather than All, which was
 * what on drew (known bug 57). */
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
        return v != 0 ? def : 0;
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
            /* File > Presets: the game's own distances and areas in Xbox
             * mode, the defaults, and the most in Max quality, but for Far
             * buildings and Far shadows, 3x there (docs/menu-format.md). */
            { .menu = XBOX_MENU_DISPLAY, .kind = XBOX_ITEM_CHOICE, .label = "Draw distance",
              .state = menu_step, .pick = menu_step_pick,
              .choices = k_names, .nchoices = DRAW_DISTANCE_STEPS, .under = DRAW_UNDER,
              .presets = XBOX_PRESETS_SET,
              .preset = { 0, DRAW_DISTANCE_DEFAULT, DRAW_DISTANCE_UNLIMITED },     /* 1x, 2x, Unlimited */
              .hint = "Sets how far crowds, vehicles, props, and police are drawn. CPU heavy." },
            { .menu = XBOX_MENU_DISPLAY, .kind = XBOX_ITEM_GROUP, .label = DRAW_GROUP,
              .under = DRAW_UNDER, .hint = "Draws more of the map and its shadows to reduce pop-in." },
            /* The hints since known bug 57 (spriteice, 14 November 2026,
             * 15:32 UTC, on the card: "Use the proposed hints"). */
            { .menu = XBOX_MENU_DISPLAY, .kind = XBOX_ITEM_CHOICE, .label = "Far buildings",
              .state = menu_far, .pick = menu_far_pick, .group = DRAW_GROUP,
              .choices = k_far_names, .nchoices = DRAW_FAR_STEPS,
              .presets = XBOX_PRESETS_SET,
              .preset = { 0, DRAW_FAR_DEFAULT, DRAW_FAR_MAX_QUALITY },     /* Off, 3x, 3x */
              .hint = "Sets how many areas ahead buildings are drawn. CPU heavy." },
            /* 3x by default: less pop-in is worth the glitches it can cause
             * (known bug 52). Never further than Far buildings. */
            { .menu = XBOX_MENU_DISPLAY, .kind = XBOX_ITEM_CHOICE, .label = "Far shadows",
              .state = menu_shadows, .pick = menu_shadows_pick, .group = DRAW_GROUP,
              .choices = k_far_names, .nchoices = DRAW_FAR_STEPS,
              .presets = XBOX_PRESETS_SET,
              .preset = { 0, DRAW_SHADOWS_DEFAULT, DRAW_FAR_MAX_QUALITY },     /* Off, 3x, 3x */
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
