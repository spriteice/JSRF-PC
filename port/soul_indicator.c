/* The graffiti soul indicator (soul_indicator.h): soulindicator=1 in the
 * settings file, or Gameplay > Graffiti souls > Visual indicator.
 *
 * Read once, at start (soul_indicator_start). The menu's row changes it at
 * once and keeps it in the settings file; it runs on the window's thread,
 * and the hooks read it on the game's. Plain C on guest memory, so a test
 * links it (tests/soul_indicator). */
#include <stdio.h>
#include <string.h>
#include "soul_indicator.h"
#include "platform/recomp_check.h"
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

/* Off with no soulindicator= in the settings: the HUD is the Xbox's. */
#define SOUL_INDICATOR_DEFAULT 0

static volatile int s_on;

#if defined(XBOXRECOMP_MENU)
static int  menu_on(void) { return s_on; }
static void menu_flip(void)
{
    s_on = !s_on;
    fprintf(stderr, "[SOUL] visual indicator %s (the menu)\n", s_on ? "on" : "off");
    nv_window_pref_set_num("soulindicator", s_on);
}
#endif

void soul_indicator_start(void)
{
    double v;
    s_on = nv_window_pref_num("soulindicator", &v) > 0 ? v != 0 : SOUL_INDICATOR_DEFAULT;
    fprintf(stderr, "[SOUL] visual indicator %s (settings)\n", s_on ? "on" : "off");
#if defined(XBOXRECOMP_MENU)
    {
        /* In Gameplay's Graffiti souls, which the window adds once it is up. */
        const XBOX_MENU_ITEM row = {
            .menu = XBOX_MENU_GAMEPLAY, .kind = XBOX_ITEM_TOGGLE, .label = "Visual indicator",
            .state = menu_on, .run = menu_flip, .group = XBOX_GROUP_SOULS,
            /* File > Presets: off in Xbox mode, as on the Xbox. */
            .presets = XBOX_PRESETS_SET,
            .preset = { 0, SOUL_INDICATOR_DEFAULT, SOUL_INDICATOR_DEFAULT },
            .hint = "Shows the graffiti soul icon while a soul challenge is on track." };
        xbox_MenuAdd(&row, 1);
    }
#endif
}

int soul_indicator_on(void)
{
    return s_on;
}

/* ---- the game's memory ------------------------------------------------------ */

static uint32_t guest32(const uint8_t *mem, uint32_t va)
{
    uint32_t v;
    memcpy(&v, mem + va, sizeof v);
    return v;
}

/* Whether size bytes at va are guest memory, below top and above the
 * first page the game's image starts at. */
static int in_guest(uint32_t va, uint32_t size, uint32_t top)
{
    return va >= 0x10000u && size <= top && va <= top - size;
}

/* The flag manager's banks, by bank number. */
static const uint32_t k_bank[SOUL_BANKS] = { 0x6A14u, 0x18u, 0x58u, 0x98u };

/* A flag's value now (0 or 1), or -1 when the word names no flag. */
static int flag_now(const uint8_t *mem, uint32_t top, uint32_t word)
{
    uint32_t bank = SOUL_FLAG_BANK(word), index = SOUL_FLAG_INDEX(word), at;
    if (bank >= SOUL_BANKS || index >= SOUL_BANK_FLAGS) return -1;
    at = SOUL_FLAGS + k_bank[bank] + (index >> 5) * 4u;
    if (!in_guest(at, 4, top)) return -1;
    return (int)((guest32(mem, at) >> (index & 31u)) & 1u);
}

/* Whether a condition holds: its flag has the value it tests for. */
static int holds(const uint8_t *mem, uint32_t top, uint32_t word)
{
    return flag_now(mem, top, word) == (int)SOUL_FLAG_VALUE(word);
}

/* A trigger's list of flag words at +list (a pointer, then how many): how
 * many, with the list's address in *at, or -1 when it isn't in memory. */
static int flag_list(const uint8_t *mem, uint32_t top, uint32_t trigger, uint32_t list,
                     uint32_t *at)
{
    uint32_t n = guest32(mem, trigger + list + 4);
    *at = guest32(mem, trigger + list);
    if (!n) return 0;
    if (!RECOMP_CHECK(n <= SOUL_LIST_MOST && in_guest(*at, 4 * n, top),
                      "trigger %08X's flags at %08X, %u of them", trigger, *at, n))
        return -1;
    return (int)n;
}

/* The notebook flag a trigger sets, or 0 for none. */
static uint32_t award_of(const uint8_t *mem, uint32_t top, uint32_t trigger)
{
    uint32_t at, w;
    int n = flag_list(mem, top, trigger, SOUL_TRIGGER_SETS, &at), k;
    for (k = 0; k < n; k++) {
        w = guest32(mem, at + 4u * (uint32_t)k);
        if (SOUL_FLAG_BANK(w) == SOUL_BANK_RT && SOUL_FLAG_VALUE(w)
         && SOUL_FLAG_INDEX(w) >= SOUL_AWARD_FIRST && SOUL_FLAG_INDEX(w) <= SOUL_AWARD_LAST)
            return w;
    }
    return 0;
}

/* Whether a touch (op 1) among the n triggers at base sets mission flag
 * word (to 1). */
static int touch_sets(const uint8_t *mem, uint32_t top, uint32_t base, uint32_t n, uint32_t word)
{
    uint32_t i, at;
    for (i = 0; i < n; i++) {
        uint32_t t = base + i * SOUL_TRIGGER_SIZE;
        int k, m;
        if (guest32(mem, t + SOUL_TRIGGER_OP) != SOUL_OP_TOUCH) continue;
        m = flag_list(mem, top, t, SOUL_TRIGGER_SETS, &at);
        for (k = 0; k < m; k++)
            if (guest32(mem, at + 4u * (uint32_t)k) == word) return 1;
    }
    return 0;
}

int soul_indicator_read(const uint8_t *mem, uint32_t top, uint32_t flow, SoulChallenge *c)
{
    uint32_t base, n, i, at;
    memset(c, 0, sizeof *c);
    c->kind = SOUL_NONE;
    c->flow = flow;
    if (!RECOMP_CHECK(in_guest(flow, SOUL_FLOW_SIZE, top), "the mission flow at %08X", flow))
        return 0;
    c->mission = guest32(mem, flow + SOUL_FLOW_MISSION);
    c->script = guest32(mem, flow + SOUL_FLOW_SCRIPT);
    if (!RECOMP_CHECK(in_guest(c->script, SOUL_SCRIPT_SIZE, top),
                      "mission %08X's script at %08X", c->mission, c->script))
        return 0;
    base = guest32(mem, c->script + SOUL_SCRIPT_TRIGGERS);
    n = guest32(mem, c->script + SOUL_SCRIPT_TRIGGERS + 4);
    if (!n) return 0;
    if (!RECOMP_CHECK(n <= SOUL_TRIGGERS_MOST && in_guest(base, n * SOUL_TRIGGER_SIZE, top),
                      "mission %08X's triggers at %08X, %u of them", c->mission, base, n))
        return 0;
    for (i = 0; i < n; i++) {
        uint32_t t = base + i * SOUL_TRIGGER_SIZE, op = guest32(mem, t + SOUL_TRIGGER_OP), award;
        SoulChallenge found = *c;
        int k, m;
        if (op != SOUL_OP_COURSE && op != SOUL_OP_SHEETS) continue;
        /* Keyed on the award, not the op: other records with these ops
         * award nothing in the notebook. */
        award = award_of(mem, top, t);
        if (!award) continue;
        m = flag_list(mem, top, t, SOUL_TRIGGER_IF, &at);
        if (m < 0 || !RECOMP_CHECK(m <= SOUL_CONDITIONS, "mission %08X's award %u has %d "
                                   "conditions", c->mission, i, m))
            continue;
        found.trigger = i;
        found.award = award;
        for (k = 0; k < m; k++) {
            uint32_t w = guest32(mem, at + 4u * (uint32_t)k);
            /* The sheets: the mission flags it waits on that a touch sets. */
            if (op == SOUL_OP_SHEETS && SOUL_FLAG_BANK(w) == SOUL_BANK_MF && SOUL_FLAG_VALUE(w)
             && touch_sets(mem, top, base, n, w))
                found.sheet[found.nsheets++] = w;
            else
                found.cond[found.nif++] = w;
        }
        if (op == SOUL_OP_COURSE) {
            found.kind = SOUL_COURSE;
            found.player = guest32(mem, t + SOUL_TRIGGER_ARGS);
            found.course = guest32(mem, t + SOUL_TRIGGER_ARGS + 4);
        } else if (found.nsheets) {
            found.kind = SOUL_SHEETS;
        } else {
            continue;       /* no progress to show before the award */
        }
        if (c->kind == SOUL_NONE) *c = found;
        else c->others++;
    }
    return c->kind != SOUL_NONE;
}

/* The course's record for its player, with the course's object in
 * *course, or 0 while the mission's setup hasn't made the course yet. */
static uint32_t course_record(const uint8_t *mem, uint32_t top, const SoulChallenge *c,
                              uint32_t *course_at)
{
    uint32_t courses = guest32(mem, c->flow + SOUL_FLOW_COURSES), count, course, record;
    count = guest32(mem, c->script + SOUL_SCRIPT_COURSES + 4);
    if (!RECOMP_CHECK(c->course < count && c->player < SOUL_PLAYERS,
                      "course %u of %u, player %u", c->course, count, c->player))
        return 0;
    if (!courses) return 0;
    if (!RECOMP_CHECK(in_guest(courses, 4 * count, top), "the courses at %08X", courses))
        return 0;
    course = guest32(mem, courses + 4u * c->course);
    if (!course) return 0;
    if (!RECOMP_CHECK(in_guest(course, SOUL_COURSE_PLAYERS + 4 * SOUL_PLAYERS, top),
                      "course %u at %08X", c->course, course))
        return 0;
    record = guest32(mem, course + SOUL_COURSE_PLAYERS + 4u * c->player);
    if (!record) return 0;
    if (!RECOMP_CHECK(in_guest(record, SOUL_RECORD_SIZE, top),
                      "course %u's record at %08X", c->course, record))
        return 0;
    *course_at = course;
    return record;
}

/* Whether the game can fail the course's first step: 1 when it has fail
 * boxes, 0 when it hasn't, -1 when its steps can't be read. */
static int first_step_fails(const uint8_t *mem, uint32_t top, uint32_t course)
{
    uint32_t entry = guest32(mem, course + SOUL_COURSE_STEPS), steps, count;
    if (!RECOMP_CHECK(in_guest(entry, 8, top), "course %08X's steps at %08X", course, entry))
        return -1;
    steps = guest32(mem, entry);
    count = guest32(mem, entry + 4);
    if (!RECOMP_CHECK(count && in_guest(steps, SOUL_STEP_SIZE, top),
                      "course %08X's first step at %08X, of %u", course, steps, count))
        return -1;
    return guest32(mem, steps + SOUL_STEP_FAILS) != 0;
}

int soul_indicator_sample(const uint8_t *mem, uint32_t top, const SoulChallenge *c,
                          SoulSample *s)
{
    unsigned k;
    memset(s, 0, sizeof *s);
    if (c->kind == SOUL_NONE || !in_guest(c->flow, SOUL_FLOW_SIZE, top)
     || !in_guest(c->script, SOUL_SCRIPT_SIZE, top))
        return 0;
    s->awarded = flag_now(mem, top, c->award) == 1;
    s->under_way = 1;
    for (k = 0; k < c->nif; k++)
        if (!holds(mem, top, c->cond[k])) s->under_way = 0;
    if (c->kind == SOUL_SHEETS) {
        for (k = 0; k < c->nsheets; k++)
            if (holds(mem, top, c->sheet[k])) s->progress++;
        s->done = s->progress == (int)c->nsheets;
    } else {
        uint32_t course = 0, record = course_record(mem, top, c, &course);
        if (record) {
            s->done = guest32(mem, record + SOUL_RECORD_DONE) != 0;
            s->failed = guest32(mem, record + SOUL_RECORD_FAILED) != 0;
            s->step = (int)guest32(mem, record + SOUL_RECORD_STEP);
            /* The time and amount are floats: 0.0 is all zero bits. */
            s->moving = s->step || guest32(mem, record + SOUL_RECORD_TIMER)
                     || guest32(mem, record + SOUL_RECORD_AMOUNT);
            /* From where the game can fail it: past its first step, or in a
             * first step with fail boxes. Unread, any progress. */
            s->progress = s->step || (s->moving && first_step_fails(mem, top, course) != 0);
        }
    }
    return 1;
}

/* ---- the badge from frame to frame ------------------------------------------ */

void soul_indicator_reset(SoulShow *show)
{
    memset(show, 0, sizeof *show);
    show->phase = SOUL_HIDDEN;
    show->pulse_at = -1.0;
}

static void begin(SoulShow *show, int phase, double now)
{
    show->phase = phase;
    show->at = now;
    if (phase != SOUL_ON) show->pulse_at = -1.0;
    if (phase == SOUL_IN) show->red = 0;
}

int soul_indicator_step(SoulShow *show, int kind, const SoulSample *s, double now)
{
    int showing, event = SOUL_SAME, progress = s->failed ? 0 : s->progress;
    /* The phases that end with time. */
    if (show->phase == SOUL_IN && now - show->at >= SOUL_IN_TIME)
        begin(show, SOUL_ON, show->at + SOUL_IN_TIME);
    if (show->phase == SOUL_FAILING && now - show->at >= SOUL_RED_TIME + SOUL_RED_HOLD) {
        begin(show, SOUL_OUT, show->at + SOUL_RED_TIME + SOUL_RED_HOLD);
        show->red = 1;
    }
    if (show->phase == SOUL_OUT && now - show->at >= SOUL_OUT_TIME)
        begin(show, SOUL_HIDDEN, show->at + SOUL_OUT_TIME);
    if (show->pulse_at >= 0.0 && now - show->pulse_at >= SOUL_PULSE_TIME) show->pulse_at = -1.0;
    showing = show->phase == SOUL_IN || show->phase == SOUL_ON;
    if (s->awarded || !s->under_way) {
        /* The soul awarded, or the challenge over some other way (the
         * mission left): out, without the red. */
        if (showing) {
            begin(show, SOUL_OUT, now);
            event = s->awarded ? SOUL_DONE : SOUL_ENDED;
        }
        show->last = 0;
        return event;
    }
    if (progress > 0) {
        if (!showing) {
            begin(show, SOUL_IN, now);
            event = SOUL_SHOWN;
        } else if (kind == SOUL_SHEETS && progress > show->last) {
            show->pulse_at = now;
            event = SOUL_PULSED;
        }
    } else if (showing && show->last > 0 && !s->done) {
        /* Back to nothing without the award: failed. */
        begin(show, SOUL_FAILING, now);
        event = SOUL_FAILED;
    }
    show->last = progress;
    return event;
}

static float clamp01(double v)
{
    return v <= 0.0 ? 0.0f : v >= 1.0 ? 1.0f : (float)v;
}

/* A colour's RGB with alpha a (0 to 1) on top. */
static uint32_t with_alpha(uint32_t rgb, float a)
{
    return ((uint32_t)(a * 255.0f + 0.5f) << 24) | (rgb & 0x00FFFFFFu);
}

int soul_indicator_quad(const SoulShow *show, double now, SoulQuad *q)
{
    double t = now - show->at, scale = 1.0, half;
    memset(q, 0, sizeof *q);
    switch (show->phase) {
    case SOUL_IN:
        q->alpha = clamp01(t / SOUL_IN_TIME);
        scale = SOUL_IN_FROM + (1.0 - SOUL_IN_FROM) * q->alpha;
        break;
    case SOUL_ON:
        q->alpha = 1.0f;
        if (show->pulse_at >= 0.0) {
            /* Up in the pulse's first half, back in its second. */
            double p = clamp01((now - show->pulse_at) / SOUL_PULSE_TIME);
            scale = 1.0 + (SOUL_PULSE_TO - 1.0) * (p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p);
        }
        break;
    case SOUL_FAILING:
        q->alpha = 1.0f;
        q->red = clamp01(t / SOUL_RED_TIME);
        break;
    case SOUL_OUT:
        q->alpha = 1.0f - clamp01(t / SOUL_OUT_TIME);
        scale = 1.0 + (SOUL_OUT_TO - 1.0) * clamp01(t / SOUL_OUT_TIME);
        q->red = show->red ? 1.0f : 0.0f;
        break;
    default:
        return 0;
    }
    if (q->alpha <= 0.0f) return 0;
    half = SOUL_SIZE * 0.5 * scale;
    q->x0 = (float)(SOUL_LEFT + SOUL_SIZE * 0.5 - half);
    q->x1 = (float)(SOUL_LEFT + SOUL_SIZE * 0.5 + half);
    q->y0 = (float)(SOUL_MIDDLE_Y - half);
    q->y1 = (float)(SOUL_MIDDLE_Y + half);
    q->colour = with_alpha(SOUL_WHITE_DOT, q->alpha);
    q->red_colour = with_alpha(SOUL_RED, q->alpha * q->red * SOUL_RED_MOST);
    return 1;
}

void soul_indicator_vertices(uint8_t *mem, uint32_t at, const SoulQuad *q, uint32_t colour)
{
    const float x[2] = { q->x0, q->x1 }, y[2] = { q->y0, q->y1 }, corner[2] = { 0.0f, 1.0f };
    const uint32_t specular = SOUL_SPECULAR;
    unsigned k;
    for (k = 0; k < 4; k++) {
        uint8_t *v = mem + at + k * SOUL_VERTEX;
        memcpy(v + SOUL_VERTEX_X, &x[k & 1u], 4);
        memcpy(v + SOUL_VERTEX_Y, &y[k >> 1], 4);
        memcpy(v + SOUL_VERTEX_DIFFUSE, &colour, 4);
        memcpy(v + SOUL_VERTEX_SPECULAR, &specular, 4);
        memcpy(v + SOUL_VERTEX_U, &corner[k & 1u], 4);
        memcpy(v + SOUL_VERTEX_V, &corner[k >> 1], 4);
    }
}

/* ---- the hooks' side --------------------------------------------------------- */

static SoulChallenge s_challenge;
static SoulShow s_show = { SOUL_HIDDEN, 0.0, -1.0, 0, 0 };
static SoulSample s_course;     /* a course's last frame, for the log */
static uint32_t s_setup_flow;   /* the flow whose setup was read, while it lasts */

/* A mission's name, as its file's: mssn0910 for mission 910. */
static void mission_name(char *buf, size_t n, uint32_t mission)
{
    snprintf(buf, n, "mssn%04u", mission & 0xFFFFu);
}

static void log_found(const SoulChallenge *c, double now)
{
    char name[32], flags[SOUL_CONDITIONS * 10] = "";
    unsigned k;
    mission_name(name, sizeof name, c->mission);
    if (c->kind == SOUL_COURSE) {
        fprintf(stderr, "[SOUL] t=%.1f %s: a course (trigger %u: course %u, player %u), its "
                "award RT[%X]", now, name, c->trigger, c->course, c->player,
                SOUL_FLAG_INDEX(c->award));
    } else if (c->kind == SOUL_SHEETS) {
        for (k = 0; k < c->nsheets; k++)
            snprintf(flags + strlen(flags), sizeof flags - strlen(flags), " %X",
                     SOUL_FLAG_INDEX(c->sheet[k]));
        fprintf(stderr, "[SOUL] t=%.1f %s: %u sheets (trigger %u: MF%s), its award RT[%X]",
                now, name, c->nsheets, c->trigger, flags, SOUL_FLAG_INDEX(c->award));
    } else {
        fprintf(stderr, "[SOUL] t=%.1f %s: no soul challenge to show\n", now, name);
        return;
    }
    if (c->others) fprintf(stderr, "; %u more, not shown", c->others);
    fprintf(stderr, "\n");
}

void soul_indicator_flow(const uint8_t *mem, uint32_t top, uint32_t flow, double now)
{
    uint32_t state;
    if (!in_guest(flow, SOUL_FLOW_SIZE, top)) return;
    state = guest32(mem, flow + SOUL_FLOW_STATE);
    if (state == SOUL_STATE_SETUP) {
        /* The first frame of its setup: the mission's script is in memory,
         * its setup not yet run. */
        if (flow != s_setup_flow) {
            s_setup_flow = flow;
            soul_indicator_read(mem, top, flow, &s_challenge);
            soul_indicator_reset(&s_show);
            memset(&s_course, 0, sizeof s_course);
            log_found(&s_challenge, now);
        }
        return;
    }
    if (flow == s_setup_flow) s_setup_flow = 0;
    if (s_challenge.kind != SOUL_NONE && flow == s_challenge.flow
     && (state == SOUL_STATE_NEXT || state == SOUL_STATE_HANDOVER)) {
        s_challenge.kind = SOUL_NONE;
        soul_indicator_reset(&s_show);
    }
}

int soul_indicator_once(unsigned loop, uint32_t pass)
{
    static unsigned drawn_in;       /* the loop pass the badge last drew in, */
    static uint32_t drawn_on;       /* on the gauge's pass, */
    static int drawn;               /* once it has drawn at all */
    static int said_first, said_again;
    if (drawn && loop == drawn_in) {
        if (s_on && !said_again) {
            said_again = 1;
            fprintf(stderr, "[SOUL] the gauge drew again in one pass of the game's loop, on its "
                    "pass %u after %u: the badge drawn with the first only\n", pass, drawn_on);
        }
        return 0;
    }
    if (s_on && !said_first) {
        said_first = 1;
        fprintf(stderr, "[SOUL] the gauge drew, on its pass %u: the badge goes with its first "
                "draw in each pass of the game's loop\n", pass);
    }
    drawn = 1;
    drawn_in = loop;
    drawn_on = pass;
    return 1;
}

/* A course's moves, for the log, a few lines a try: when it starts to move,
 * each step it goes on to, when it is done, and when it goes back to
 * nothing without being done, which is the game failing it. */
static void log_course(const SoulSample *s, double now)
{
    if (s->moving && !s_course.moving)
        fprintf(stderr, "[SOUL] t=%.1f the course under way, at step %d\n", now, s->step);
    else if (s->moving && s->step > s_course.step)
        fprintf(stderr, "[SOUL] t=%.1f the course on to step %d\n", now, s->step);
    else if (!s->moving && s_course.moving && !s->done && !s_course.done)
        fprintf(stderr, "[SOUL] t=%.1f the game failed the course, at step %d\n", now,
                s_course.step);
    if (s->done && !s_course.done)
        fprintf(stderr, "[SOUL] t=%.1f the course done, at step %d\n", now, s->step);
    s_course = *s;
}

int soul_indicator_frame(const uint8_t *mem, uint32_t top, double now, SoulQuad *q)
{
    static const char *const said[] = {
        [SOUL_SHOWN] = "shown: under way and on track",
        [SOUL_FAILED] = "failed: red, then out",
        [SOUL_DONE] = "done: the soul awarded, out",
        [SOUL_ENDED] = "the challenge ended, out",
    };
    SoulSample s;
    int event;
    if (!s_on || !soul_indicator_sample(mem, top, &s_challenge, &s)) {
        soul_indicator_reset(&s_show);
        return 0;
    }
    if (s_challenge.kind == SOUL_COURSE) log_course(&s, now);
    event = soul_indicator_step(&s_show, s_challenge.kind, &s, now);
    if (event == SOUL_PULSED)
        fprintf(stderr, "[SOUL] t=%.1f a pulse: %d of %u sheets\n", now, s.progress,
                s_challenge.nsheets);
    else if (event != SOUL_SAME)
        fprintf(stderr, "[SOUL] t=%.1f %s\n", now, said[event]);
    return soul_indicator_quad(&s_show, now, q);
}
