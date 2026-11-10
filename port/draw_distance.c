/* Draw distance and Far buildings (draw_distance.h): drawdistance= and
 * farbuildings= in the settings file, or Display > Draw distance and Display >
 * Far buildings.
 *
 * Read once, at start (draw_distance_start). The menu's rows change them at
 * once and keep them in the settings file; they run on the window's thread,
 * and the gates and the areas read them on the game's. A step multiplies
 * every distance a gate cuts or fades at by the same number, so each kind of
 * object keeps its own distances in proportion, and a fade keeps its length
 * in proportion too. Far buildings adds areas to the set the camera makes,
 * from the map's own visible lists. */
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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

/* The rows go right under the window's Render scale (nv2a_gl.c). */
#define DRAW_UNDER     "Render scale (Restart)"
#define DRAW_LOG_EVERY 5.0      /* seconds between [DRAW] lines */

static const char *const k_names[DRAW_DISTANCE_STEPS] = { "1x", "2x", "3x", "4x", "Unlimited" };
static const char *const k_gates[DRAW_GATES] = {
    "crowds", "pickups", "checkpoints", "props and traffic", "enemies", "attached part" };

static volatile int s_step;
static volatile int s_far;      /* Far buildings */
static int s_copies;            /* JSRF_DRAW_COPIES: an area's copies drawn too */

int draw_distance_step(void)
{
    return s_step;
}

int draw_far_buildings(void)
{
    return s_far;
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
static double s_sets, s_game, s_added, s_copied;   /* Far buildings' sets since the line */
static double s_since = -1;     /* the time the counts start from; -1: not yet */
static unsigned s_since_frame;

/* Every 5 seconds: each gate's objects a frame, and how many of them past
 * 1x's distance, while a step is on; and, while Far buildings is, the areas
 * of a set the camera makes. */
static void draw_log(double now, unsigned frame)
{
    char line[640];
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
        snprintf(line + len, sizeof line - len,
                 "%s Far buildings, a set's areas: the game's %.1f, added %.1f, copies %s %.1f",
                 s_objects ? ";" : ",", s_game / s_sets, s_added / s_sets,
                 s_copies ? "drawn too" : "left out", s_copied / s_sets);
    fprintf(stderr, "%s\n", line);
    s_objects = 0;
    s_sets = s_game = s_added = s_copied = 0;
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

/* ---- Far buildings ------------------------------------------------------- */

#define COPY_NORMAL 1e-3f       /* a normal's x, y or z: the same within this */
#define COPY_SLACK  0.5         /* a plane's place: the same within this, plus... */
#define COPY_SLACK_PER 1e-4     /* ...this much of the offset's length */
#define COPY_MIN    1.0         /* an offset shorter than this is the same place */

static int set_has(const uint32_t *set, int i) { return (set[i >> 5] >> (i & 31)) & 1u; }
static void set_add(uint32_t *set, int i) { set[i >> 5] |= 1u << (i & 31); }
static int set_count(const uint32_t *set)
{
    int i, n = 0;
    for (i = 0; i < DRAW_AREAS_MAX; i++) n += set_has(set, i);
    return n;
}

/* Plane i of a volume: its point and its normal. */
static const float *plane_at(const float *v, int i) { return v + 3 * i; }
static const float *plane_normal(const float *v, int i) { return v + 18 + 3 * i; }

/* Nonzero when each of b's 6 planes is a's moved by d. */
static int planes_moved(const float *a, const float *b, const double d[3])
{
    double slack = COPY_SLACK + COPY_SLACK_PER * sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    int i, k;
    for (i = 0; i < 6; i++) {
        const float *pa = plane_at(a, i), *pb = plane_at(b, i);
        const float *na = plane_normal(a, i), *nb = plane_normal(b, i);
        double moved = 0, along = 0;
        for (k = 0; k < 3; k++) {
            if (fabsf(na[k] - nb[k]) > COPY_NORMAL) return 0;
            moved += (double)na[k] * ((double)pb[k] - pa[k]);
            along += (double)na[k] * d[k];
        }
        if (fabs(moved - along) > slack) return 0;
    }
    return 1;
}

int draw_area_copy(const DrawArea *a, const DrawArea *b, float d[3])
{
    double m[3][3] = { { 0 } }, r[3] = { 0 }, x[3], det;
    unsigned c;
    int i, j, k;
    if (!a->volume || !b->volume || a->ncells != b->ncells) return 0;
    if (a->ncells && (!a->cells || !b->cells)) return 0;
    /* The offset that moves a's planes onto b's, the nearest by least
     * squares: each plane says how far it moved along its normal. */
    for (i = 0; i < 6; i++) {
        const float *n = plane_normal(a->volume, i), *pa = plane_at(a->volume, i);
        const float *pb = plane_at(b->volume, i);
        double moved = 0;
        for (k = 0; k < 3; k++) moved += (double)n[k] * ((double)pb[k] - pa[k]);
        for (j = 0; j < 3; j++) {
            for (k = 0; k < 3; k++) m[j][k] += (double)n[j] * n[k];
            r[j] += (double)n[j] * moved;
        }
    }
    det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
        - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
        + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (fabs(det) < 1e-6) return 0;     /* the normals don't fix a place */
    for (j = 0; j < 3; j++) {
        double t[3][3];
        memcpy(t, m, sizeof t);
        for (k = 0; k < 3; k++) t[k][j] = r[k];
        x[j] = (t[0][0] * (t[1][1] * t[2][2] - t[1][2] * t[2][1])
              - t[0][1] * (t[1][0] * t[2][2] - t[1][2] * t[2][0])
              + t[0][2] * (t[1][0] * t[2][1] - t[1][1] * t[2][0])) / det;
    }
    if (sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]) < COPY_MIN) return 0;
    if (!planes_moved(a->volume, b->volume, x)) return 0;
    for (c = 0; c < a->ncells; c++)
        if (!planes_moved(a->cells + 36 * c, b->cells + 36 * c, x)) return 0;
    for (k = 0; k < 3; k++) d[k] = (float)x[k];
    return 1;
}

/* What Far buildings keeps of a map, worked out on the first call on it:
 * the areas some list names, each area's list, each one's copies, and its
 * middle. */
static struct {
    int      built, n;
    uint32_t key;
    uint32_t listed[DRAW_SET_WORDS];
    uint32_t list[DRAW_AREAS_MAX][DRAW_SET_WORDS];
    uint32_t copy[DRAW_AREAS_MAX][DRAW_SET_WORDS];
    float    middle[DRAW_AREAS_MAX][3];
} s_map;

/* Nonzero when some area's list names both i and j: the game draws them
 * together. */
static int named_together(int i, int j)
{
    int a;
    for (a = 0; a < s_map.n; a++)
        if (set_has(s_map.list[a], i) && set_has(s_map.list[a], j)) return 1;
    return 0;
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
        MIX(a[i].ncells);
        MIX(a[i].nlist);
        for (k = 0; a[i].list && k < a[i].nlist; k++) MIX(a[i].list[k]);
    }
#undef MIX
    return h;
}

static void map_take(const DrawArea *a, int n, uint32_t key)
{
    char line[2048];
    int i, j, k, len, listed = 0, copies = 0, first[DRAW_AREAS_MAX];
    float off[DRAW_AREAS_MAX][3];
    unsigned e;
    memset(&s_map, 0, sizeof s_map);
    s_map.built = 1;
    s_map.n = n;
    s_map.key = key;
    for (i = 0; i < n; i++) {
        first[i] = -1;
        for (e = 0; a[i].list && e < a[i].nlist; e++)
            if (a[i].list[e] < (uint32_t)n) {
                set_add(s_map.listed, (int)a[i].list[e]);
                set_add(s_map.list[i], (int)a[i].list[e]);
            }
        for (k = 0; k < 3 && a[i].volume; k++) {
            for (j = 0; j < 6; j++) s_map.middle[i][k] += plane_at(a[i].volume, j)[k];
            s_map.middle[i][k] /= 6.0f;
        }
    }
    /* Only the areas a list names can be in a set; and two areas the game
     * draws together aren't copies to leave out, whatever their shapes (a
     * street of areas the same size). */
    for (i = 0; i < n; i++) {
        if (!set_has(s_map.listed, i)) continue;
        listed++;
        for (j = i + 1; j < n; j++) {
            float d[3];
            if (!set_has(s_map.listed, j) || !draw_area_copy(&a[i], &a[j], d) || named_together(i, j))
                continue;
            set_add(s_map.copy[i], j);
            set_add(s_map.copy[j], i);
            if (first[j] < 0) { first[j] = i; memcpy(off[j], d, sizeof d); }
        }
    }
    for (i = 0; i < n; i++) copies += first[i] >= 0;
    if (copies == 1)
        len = snprintf(line, sizeof line, "[DRAW] Far buildings: the map has %d areas; lists name %d of "
                       "them, 1 a copy of another", n, listed);
    else
        len = snprintf(line, sizeof line, "[DRAW] Far buildings: the map has %d areas; lists name %d of "
                       "them, %d copies of others", n, listed, copies);
    /* An area no list names is never drawn; the line says which of them
     * are copies of an area that is, so a log tells what that rule keeps
     * hidden (a street built twice, its far copy in no list). */
    for (i = 0, k = 0; i < n && len > 0 && len < (int)sizeof line; i++) {
        float d[3];
        if (set_has(s_map.listed, i)) continue;
        len += snprintf(line + len, sizeof line - len, "%s %d", k++ ? "," : "; no list names", i);
        for (j = 0; j < n && len > 0 && len < (int)sizeof line; j++)
            if (set_has(s_map.listed, j) && draw_area_copy(&a[j], &a[i], d)) {
                len += snprintf(line + len, sizeof line - len, " (a copy of area %d, (%.0f, %.0f, %.0f) away)",
                                j, d[0], d[1], d[2]);
                break;
            }
    }
    fprintf(stderr, "%s\n", line);
    for (i = 0; i < n; i++) {
        if (!set_has(s_map.listed, i)) continue;
        if (first[i] >= 0)
            fprintf(stderr, "[DRAW] area %d: fog %g to %g, density %g, a copy of area %d, (%.0f, %.0f, %.0f) away\n",
                    i, a[i].fog_start, a[i].fog_end, a[i].fog_density, first[i],
                    off[i][0], off[i][1], off[i][2]);
        else
            fprintf(stderr, "[DRAW] area %d: fog %g to %g, density %g\n",
                    i, a[i].fog_start, a[i].fog_end, a[i].fog_density);
    }
}

void draw_buildings_widen(const DrawArea *areas, int n, const float eye[3],
                          uint32_t set[DRAW_SET_WORDS], double now, unsigned frame)
{
    int order[DRAW_AREAS_MAX];
    float dist[DRAW_AREAS_MAX];
    int i, j, k = 0, game, added = 0, copied = 0;
    uint32_t key;
    if (!s_far || !areas || n <= 0) return;
    if (n > DRAW_AREAS_MAX) n = DRAW_AREAS_MAX;
    key = map_key(areas, n);
    if (!s_map.built || s_map.n != n || s_map.key != key) map_take(areas, n, key);
    game = set_count(set);
    /* The areas to add, nearest the eye first, so that of two copies the
     * nearer is drawn. */
    for (i = 0; i < n; i++) {
        float d2 = 0;
        if (!set_has(s_map.listed, i) || set_has(set, i)) continue;
        for (j = 0; eye && j < 3; j++) {
            float t = eye[j] - s_map.middle[i][j];
            d2 += t * t;
        }
        for (j = k++; j > 0 && dist[j - 1] > d2; j--) { dist[j] = dist[j - 1]; order[j] = order[j - 1]; }
        dist[j] = d2;
        order[j] = i;
    }
    for (j = 0; j < k; j++) {
        int w, copy = 0;
        i = order[j];
        for (w = 0; w < DRAW_SET_WORDS; w++) copy |= (s_map.copy[i][w] & set[w]) != 0;
        if (copy) {
            copied++;
            if (!s_copies) continue;
        }
        set_add(set, i);
        added++;
    }
    s_sets++;
    s_game += game;
    s_added += added;
    s_copied += copied;
    draw_log(now, frame);
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
static void menu_far_flip(void)
{
    s_far = !s_far;
    fprintf(stderr, "[DRAW] Far buildings %s (the menu)\n", s_far ? "on" : "off");
    nv_window_pref_set_num("farbuildings", s_far);
}
#endif

void draw_distance_start(void)
{
    const char *copies = getenv("JSRF_DRAW_COPIES");
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
    s_far = nv_window_pref_num("farbuildings", &v) > 0 ? v != 0 : DRAW_FAR_DEFAULT;
    fprintf(stderr, "[DRAW] Far buildings %s (settings)\n", s_far ? "on" : "off");
    s_copies = copies && strcmp(copies, "0") != 0;
    if (s_copies) fprintf(stderr, "[DRAW] JSRF_DRAW_COPIES: an area's copies drawn too\n");
#if defined(XBOXRECOMP_MENU)
    {
        /* Rows are drawn in the order added, and the window adds Render scale
         * once it is up, after this: so the rows name the one they go under,
         * and keep their order there. */
        const XBOX_MENU_ITEM rows[] = {
            { .menu = XBOX_MENU_DISPLAY, .kind = XBOX_ITEM_CHOICE, .label = "Draw distance",
              .state = menu_step, .pick = menu_step_pick,
              .choices = k_names, .nchoices = DRAW_DISTANCE_STEPS, .under = DRAW_UNDER,
              .hint = "Sets how far crowds, vehicles, props, and police are drawn. CPU heavy." },
            { .menu = XBOX_MENU_DISPLAY, .kind = XBOX_ITEM_TOGGLE, .label = "Far buildings",
              .state = menu_far, .run = menu_far_flip, .under = DRAW_UNDER,
              .hint = "Draws the whole map, eliminating building pop-in. CPU heavy." },
        };
        xbox_MenuAdd(rows, (int)(sizeof rows / sizeof rows[0]));
    }
#endif
}
