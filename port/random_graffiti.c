/* random_graffiti.c -- the designs the player sprays, drawn at random from
 * the unlocked ones, a design for each spot (random_graffiti.h). */
#include "random_graffiti.h"
#include "random_graffiti_kept.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Where the builder's calls return to: sub_0003A820's two (the first sheet's
 * designs, then the second's), then sub_0003A870's five (the copy into the
 * sheet, sub_0002DA70, and the step's four). The garage's graffiti menu
 * calls both functions too, from 0x001245C5 on, and is not answered. */
static const uint32_t k_site[] = {
    0x0002DD05u, 0x0002DEFCu,
    0x0002DAD6u, 0x0002DD26u, 0x0002DE21u, 0x0002DF1Du, 0x0002E04Fu,
};
static const char *const k_size[RG_SIZES] = { "SS", "S", "M", "L", "XL" };

#define RG_OWN_SHEETS   ((RG_SETS - 1u) * RG_SHEETS)    /* the other sets' sheets, at most */
#define RG_SPOTS_SAID   6u      /* a deal's first spots, each a line in the log */
#define RG_TWICE_SAID   3u      /* ... and its first spots drawn twice */
#define RG_SPOTS_DEALT  256u    /* the spots a deal keeps the designs of */
#define RG_KINDS        (RG_SHEETS * (RG_PARTS + 1u))   /* a spot's kind: its sheet and how many parts */
#define RG_RECORDS      4096u   /* the records of a list walked before it is given up on */
#define RG_RS_ZFUNC     0x0019E1C4u     /* D3D's depth test and write as the game's device has them */
#define RG_RS_ZWRITE    0x0019E1E0u     /* ... for the log's line of a spot drawn twice */
#define RG_KEPT_SAID    6u      /* an area's first tags kept, ended or shown kept, each a line in the log */
#define RG_POOL         (RG_DESIGNS + RG_OWN_SLOTS)    /* a size's designs to draw from, at most */

enum { LAYER_NONE, LAYER_PLAYER, LAYER_OTHER };     /* whose tag a layer of a part is */
enum { PLAY_NONE, PLAY_ASKING, PLAY_RUNNING };      /* a soul's builds in play */
enum { DRAW_LOAD, DRAW_SOUL, DRAW_HOLD };           /* a draw at an area's load or a game's build, a soul's, or a hold */

static int s_pick[RG_PICKS][RG_SIZES];  /* each design of each size, or -1: none unlocked, the garage's */
static uint32_t s_picks = 1;    /* the designs a size of the last draw */
static uint32_t s_sets = 1;     /* ... and the sets of sheets they are on */
static uint32_t s_deal;         /* the deals so far: a spot's design changes with it */
static int s_drawn;             /* the designs are drawn, for this build */
static int s_story;             /* the last build of the game's own was of the story's sheet */
static int s_asking;            /* an area's load is asking for its builds */
static uint32_t s_todraw;       /* ... and has designs to draw first, for this many sets, once the holder is free */
static uint32_t s_topick;       /* ... this many a size */
static uint32_t s_left;         /* the other sets' builds still to ask for */
static int s_chain;             /* the next build to start is one asked for here */
static uint32_t s_next;         /* ... to answer from this set */
static uint32_t s_answer;       /* the set the last build started answers from */
static int s_chained;           /* the builds are asked for, for the textures the game next loads */
static int s_mine;              /* the textures' letting go now is an area's load's, here */
static uint32_t s_made;         /* the other sets' sheets made textures since the draw */
static uint32_t s_most;         /* ... and the most there have been at once */
static int s_place[RG_OWN_SHEETS];      /* ... and the places they were made in */
static uint32_t s_random;       /* the draw's own generator; 0: not started */
static int s_soul;              /* a graffiti soul was got in play: the sheets are to be built again */
static int s_soul_design;       /* ... its design, or -1 */
static int s_play;              /* ... and are being: PLAY_RUNNING for a build left to the holder's pace */
static int s_play_build;        /* the last build started is one of those: its sheets are made textures in their places */
static uint32_t s_play_made;    /* the game's own sheets made textures again in play */
static uint32_t s_play_steps;   /* the holder's steps the builds in play took */

/* Gameplay > Graffiti's options: as the menu set them, and as taken
 * at the last area's load or game's build. b2.2-hotfix's until set. */
static volatile int s_opt_set[RG_OPTS] = { RG_DESIGNS_UNLOCKED, 0, 1, 1 };
static int s_opt[RG_OPTS] = { RG_DESIGNS_UNLOCKED, 0, 1, 1 };
static int s_fresh = 1;         /* the game built its own sheet since the last draw, or Designs changed: draw anew */
static uint8_t s_own_size[RG_OWN_SLOTS];    /* each slot's own design's size plus 1, or 0 for none */
static uint8_t s_own_bad[RG_OWN_SLOTS];     /* ... and the ones the builder couldn't read, left out */
static int s_own_listed;        /* ... listed, since the start or the garage's last build */
static uint32_t s_stage;        /* the save's number for the area's stage, 0 for none */
static uint32_t s_checked;      /* a bit a stage: its kept tags checked against the save since New Game or Load Game */
static uint32_t s_pinned[RG_SIZES];         /* a bit a place: a kept design, put there at the draw */
static uint32_t s_area_n;       /* the areas' loads so far */
static uint32_t s_said_area = 0xFFFFFFFFu;  /* ... the last one whose spots' stage was said */
static uint32_t s_kept_said, s_kept_said_area = 0xFFFFFFFFu;
static uint32_t s_size_said;
static int s_switch = -1;       /* Gameplay > Graffiti > Random graffiti, for this run; -1: not set */
static int s_read_failed;       /* a read of a design of the game's own choice failed in this build: its notice stays */
static int s_unread_held;       /* a stand-in's write was held back, its own flag's to follow */
static uint32_t s_unread_row, s_unread_size;

int random_graffiti_on(void)
{
    if (s_switch < 0) {
        const char *v = getenv("JSRF_RANDOM_GRAFFITI");
        s_switch = v && *v && strcmp(v, "0") != 0;
    }
    return s_switch;
}

void random_graffiti_switch(int on)
{
    s_switch = on != 0;
}

void random_graffiti_seed(uint32_t seed) { s_random = seed; }

/* The next of the draw's own random numbers (xorshift32). */
static uint32_t next_random(void)
{
    if (!s_random) s_random = (uint32_t)time(NULL) ^ ((uint32_t)clock() << 16);
    if (!s_random) s_random = 0x9E3779B9u;
    s_random ^= s_random << 13;
    s_random ^= s_random >> 17;
    s_random ^= s_random << 5;
    return s_random;
}

/* One of 0 to n - 1, each as likely as the others. */
static uint32_t random_below(uint32_t n)
{
    return (uint32_t)(((uint64_t)next_random() * n) >> 32);
}

static uint32_t word(const uint8_t *mem, uint32_t at)
{
    uint32_t w;
    memcpy(&w, mem + at, 4);
    return w;
}

/* More of a line for the log. */
static void say(char *line, size_t room, const char *format, ...)
{
    size_t at = strlen(line);
    va_list args;

    va_start(args, format);
    vsnprintf(line + at, room - at, format, args);
    va_end(args);
}

/* Whether the game's design n of a size is unlocked, as the garage's menu
 * asks (sub_0003A2B0). */
static int unlocked(const uint8_t *mem, uint32_t size, uint32_t n)
{
    uint32_t design = size * RG_DESIGNS + n;
    return (word(mem, RG_PROGRESS + RG_SOULS_GOT + (design >> 5) * 4u) >> (design & 31u)) & 1u;
}

void random_graffiti_option(int which, int value)
{
    if (which >= 0 && which < RG_OPTS) s_opt_set[which] = value;
}

int random_graffiti_option_now(int which)
{
    return which >= 0 && which < RG_OPTS ? s_opt_set[which] : 0;
}

/* The options as the menu has them now, for this load or build. */
static void take_options(void)
{
    int k, designs = s_opt[RG_OPT_DESIGNS];

    for (k = 0; k < RG_OPTS; k++)
        s_opt[k] = s_opt_set[k];
    if (s_opt[RG_OPT_DESIGNS] < RG_DESIGNS_UNLOCKED || s_opt[RG_OPT_DESIGNS] > RG_DESIGNS_BOTH)
        s_opt[RG_OPT_DESIGNS] = RG_DESIGNS_UNLOCKED;
    if (s_opt[RG_OPT_DESIGNS] != designs)
        s_fresh = 1;
}

uint32_t random_graffiti_stage(const uint8_t *mem, uint32_t code)
{
    uint32_t stage = word(mem, RG_STAGE_TABLE + (((code >> 16) % 10u) * 10u + (code & 0xFFFFu) % 10u) * 4u);
    return stage < RG_KEPT_STAGES ? stage : 0;
}

/* Whether the player's own design in a slot is one of this size the builder
 * can read. */
static int own_ok(uint32_t slot, uint32_t size)
{
    return slot < RG_OWN_SLOTS && s_own_size[slot] == size + 1u && !s_own_bad[slot];
}

/* Whether a design can be drawn for a size: one of the game's of it, or one
 * of the player's own of it. */
static int design_ok(uint32_t size, int design)
{
    if (RG_IS_OWN(design)) return own_ok((uint32_t)design - RG_OWN, size);
    return design >= (int)(size * RG_DESIGNS) && design < (int)((size + 1u) * RG_DESIGNS);
}

/* How long an own design's file of a size is, as the reader wants it. */
static uint32_t own_length(const uint8_t *mem, uint32_t size)
{
    uint32_t k, length = RG_OWN_EXTRA;
    for (k = 0; k < 4u; k++)
        length += word(mem, RG_OWN_LENGTHS + size * 16u + k * 4u);
    return length;
}

static double now_ms(void)
{
    struct timespec ts;
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) return 0;
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

/* The player's own designs found, a slot at a time, as the builder's reader
 * opens them: one whose header names a size and whose file is that size's
 * length is that size's. */
static void list_own(const RANDOM_GRAFFITI_GAME *game, const uint8_t *mem, double t)
{
    uint32_t slot, count[RG_SIZES] = { 0 }, found = 0, wrong = 0, bad = 0;
    double began = now_ms();

    for (slot = 0; slot < RG_OWN_SLOTS; slot++) {
        uint32_t length = 0;
        int size = game->own_header ? game->own_header(slot, &length) : -1;

        s_own_size[slot] = 0;
        s_own_listed = 1;
        if (size < 0)
            continue;
        if ((uint32_t)size >= RG_SIZES || length != own_length(mem, (uint32_t)size)) {
            wrong++;
            continue;
        }
        s_own_size[slot] = (uint8_t)(size + 1);
        count[size]++;
        found++;
        bad += s_own_bad[slot];
    }
    fprintf(stderr, "[GRAFFITI] t=%.1f the player's own designs: %u found (SS %u, S %u, M %u, L %u, XL %u), "
            "%u left out as the builder couldn't read them, %u not a design's file, in %.1f ms\n", t, found,
            count[0], count[1], count[2], count[3], count[4], bad, wrong, now_ms() - began);
}

/* A size's designs to draw from, by Designs: the unlocked ones, the player's
 * own of the size, or both; with Own and none of the size, the unlocked
 * ones. *own is how many are the player's own. */
static uint32_t pool(const uint8_t *mem, uint32_t size, int *list, uint32_t *own)
{
    uint32_t n, count = 0;

    if (s_opt[RG_OPT_DESIGNS] != RG_DESIGNS_UNLOCKED)
        for (n = 0; n < RG_OWN_SLOTS; n++)
            if (own_ok(n, size))
                list[count++] = (int)(RG_OWN + n);
    *own = count;
    if (s_opt[RG_OPT_DESIGNS] != RG_DESIGNS_OWN || !count)
        for (n = 0; n < RG_DESIGNS; n++)
            if (unlocked(mem, size, n))
                list[count++] = (int)(size * RG_DESIGNS + n);
    return count;
}

/* The area's kept designs of a size, each once, the most kept first, at
 * most most of them: those that can still be drawn. */
static uint32_t kept_designs(uint32_t size, int *out, uint32_t most)
{
    int design[RG_KEPT_SPOTS];
    uint32_t uses[RG_KEPT_SPOTS], n = 0, spot, k;

    if (!s_stage) return 0;
    for (spot = 0; spot < RG_KEPT_SPOTS; spot++) {
        const RG_KEPT *tag = rg_kept_get(s_stage, spot);
        if (!tag || tag->size != size || !design_ok(size, tag->design)) continue;
        for (k = 0; k < n && design[k] != tag->design; k++)
            ;
        if (k < n) {
            uses[k]++;
            continue;
        }
        design[n] = tag->design;
        uses[n++] = 1;
    }
    for (k = 1; k < n; k++) {           /* the most kept first; of as many, the first found */
        uint32_t j = k, u = uses[k];
        int d = design[k];
        for (; j > 0 && uses[j - 1] < u; j--) {
            uses[j] = uses[j - 1];
            design[j] = design[j - 1];
        }
        uses[j] = u;
        design[j] = d;
    }
    for (k = 0; k < n && k < most; k++)
        out[k] = design[k];
    return k;
}

/* How many designs a size has to draw from: its pool's, and the area's kept
 * ones that aren't in it. */
static uint32_t size_count(const uint8_t *mem, uint32_t size)
{
    int list[RG_POOL], pin[RG_PICKS];
    uint32_t own, count = pool(mem, size, list, &own), pins = kept_designs(size, pin, RG_PICKS), k, n, more = 0;

    for (k = 0; k < pins; k++) {
        for (n = 0; n < count && list[n] != pin[k]; n++)
            ;
        more += n == count;
    }
    return count + more;
}

/* The place holding a design of a size, or -1. */
static int place_of(uint32_t size, int design)
{
    uint32_t k;
    for (k = 0; k < s_picks && k < RG_PICKS; k++)
        if (s_pick[k][size] == design) return (int)k;
    return -1;
}

/* A stage's kept tags checked against the save: a tag the progress object
 * has no part of the player's on, as the tag or the one fading out, is gone,
 * and ended. How many were, with *kept how many there were. */
static uint32_t check_tags(const uint8_t *mem, uint32_t stage, uint32_t *kept)
{
    uint32_t spot, gone = 0;

    *kept = 0;
    for (spot = 0; spot < RG_KEPT_SPOTS; spot++) {
        const RG_KEPT *tag = rg_kept_get(stage, spot);
        uint32_t which, part, mine = 0;

        if (!tag) continue;
        (*kept)++;
        for (which = 0; which < 2u; which++) {
            uint32_t tags = word(mem, RG_PROGRESS + RG_SPOT_TAGS + ((stage * RG_KEPT_SPOTS + spot) * 2u + which) * 4u);
            for (part = 0; part < tag->parts && part < RG_PARTS; part++)
                mine |= ((tags >> (3u * part)) & 7u) == 0;
        }
        if (!mine) {
            rg_kept_end(stage, spot);
            gone++;
        }
    }
    return gone;
}

/* The area's kept tags checked against the save, at the stage's first load
 * since a New Game or Load Game. */
static void check_stage(const uint8_t *mem, uint32_t stage, double t)
{
    uint32_t kept, gone;

    if (!stage || (s_checked >> stage & 1u)) return;
    s_checked |= 1u << stage;
    gone = check_tags(mem, stage, &kept);
    if (kept)
        fprintf(stderr, "[GRAFFITI] t=%.1f stage %u's kept tags checked against the save: %u of %u gone%s\n",
                t, stage, gone, kept, gone ? ", and ended" : "");
}

/* Every stage's kept tags checked against the save: how many were gone,
 * with *kept how many there were. */
static uint32_t check_all(const uint8_t *mem, uint32_t *kept)
{
    uint32_t stage, gone = 0, n;

    *kept = 0;
    for (stage = 1; stage < RG_KEPT_STAGES; stage++) {
        gone += check_tags(mem, stage, &n);
        *kept += n;
    }
    s_checked = (1u << RG_KEPT_STAGES) - 2u;
    return gone;
}

void random_graffiti_slot(const uint8_t *mem, uint32_t slot, int saved, const char *file, double t)
{
    uint32_t kept, gone;
    double began;
    int bad = 0, done;

    if (!random_graffiti_on() || slot >= RG_KEPT_SLOTS)
        return;
    began = now_ms();
    rg_kept_file(file);
    if (!saved) {
        done = rg_kept_load(slot, &bad);
        gone = check_all(mem, &kept);
        fprintf(stderr, "[GRAFFITI] t=%.1f the save's slot %u is loaded, and its kept tags: %s%s, %u tags, "
                "%u of them gone from the save and ended, in %.1f ms\n", t, slot,
                done >= 0 ? "read from the save's folder"
                          : file && file[0] ? "none in the save's folder" : "no save's folder found",
                bad ? " (lines that couldn't be read left out)" : "", kept, gone, now_ms() - began);
        return;
    }
    gone = check_all(mem, &kept);
    done = rg_kept_save(slot, &bad);
    if (done < 0)
        fprintf(stderr, "[GRAFFITI] t=%.1f the game is saved to slot %u, and its kept tags couldn't be written "
                "to the save's folder%s: they stay in play, for the next save\n", t, slot,
                file && file[0] ? "" : " (no save's folder found)");
    else
        fprintf(stderr, "[GRAFFITI] t=%.1f the game is saved to slot %u, and its kept tags with it: %u tags%s, "
                "%u gone from the save ended first, written to the save's folder in %.1f ms\n", t, slot,
                kept - gone, bad ? " (the file's lines that couldn't be read left out)" : "", gone,
                now_ms() - began);
}

/* The designs drawn, picks a size, for this many sets of sheets: for each
 * size the area's kept designs first, most kept first, and then its pool's
 * others in a random order, each order as likely as the others, the size's
 * designs the first of them, starting over where they run out. soul, a
 * design just got (or -1), is one of its size's. The spots are dealt again
 * with them at a load's or a game's build's draw, and at a soul's a spot
 * keeps its place among the designs where there are no fewer, and shows the
 * new design there. A hold draws only where a design can't stay: the kept
 * ones go first, and every other place keeps its design if it still can.
 * Without Keep, a shuffle ends every kept tag first. */
static void draw_picks(const uint8_t *mem, uint32_t picks, uint32_t sets, int soul, int mode, double t)
{
    char line[900] = "";
    uint32_t size, n, k;
    int hold = mode == DRAW_HOLD && !s_fresh;

    if (!hold && !s_opt[RG_OPT_KEEP])
        rg_kept_clear();
    for (size = 0; size < RG_SIZES; size++) {
        int list[RG_POOL], pin[RG_PICKS], was[RG_PICKS], fresh[RG_POOL];
        uint32_t own, count = pool(mem, size, list, &own), pins, rest = 0, room, held = 0, nfresh = 0, j = 0;

        for (n = count; n > 1; n--) {
            uint32_t pick = random_below(n);
            int last = list[n - 1];
            list[n - 1] = list[pick];
            list[pick] = last;
        }
        pins = kept_designs(size, pin, picks);
        for (n = 0; n < count; n++) {           /* the pool's others */
            for (k = 0; k < pins && pin[k] != list[n]; k++)
                ;
            if (k == pins) list[rest++] = list[n];
        }
        room = picks - pins;
        for (n = room; soul >= 0 && room && n < rest; n++)
            if (list[n] == soul) {              /* not among the first: it takes the place of one of them */
                uint32_t pick = random_below(room);
                list[n] = list[pick];
                list[pick] = soul;
                break;
            }
        for (k = 0; k < RG_PICKS; k++)
            was[k] = s_pick[k][size];
        s_pinned[size] = 0;
        for (k = 0; k < pins; k++) {
            s_pick[k][size] = pin[k];
            s_pinned[size] |= 1u << k;
        }
        for (k = pins; hold && k < RG_PICKS; k++) {     /* a hold: what can stay, stays */
            for (n = 0; n < rest && list[n] != was[k]; n++)
                ;
            for (j = pins; n < rest && j < k; j++)
                if ((held >> j & 1u) && s_pick[j][size] == was[k]) n = rest;
            if (n < rest) {
                s_pick[k][size] = was[k];
                held |= 1u << k;
            }
        }
        for (n = 0; n < rest; n++) {            /* ... and the others go where nothing stayed */
            for (k = pins; k < RG_PICKS && !((held >> k & 1u) && s_pick[k][size] == list[n]); k++)
                ;
            if (k == RG_PICKS) fresh[nfresh++] = list[n];
        }
        for (k = pins, j = 0; k < RG_PICKS; k++)
            if (!(held >> k & 1u))
                s_pick[k][size] = nfresh ? fresh[j++ % nfresh] : rest ? list[j++ % rest]
                                : pins ? pin[j++ % pins] : -1;
        if (!count && !pins) {
            say(line, sizeof line, "%s %s the garage's (none unlocked)", size ? "," : "", k_size[size]);
            continue;
        }
        say(line, sizeof line, "%s %s design%s", size ? "," : "", k_size[size], picks > 1 ? "s" : "");
        for (k = 0; k < picks && k < rest + pins; k++)
            say(line, sizeof line, " %d", s_pick[k][size]);
        if (!own && !pins)
            say(line, sizeof line, " (of %u unlocked)", count);
        else
            say(line, sizeof line, " (%u kept; of %u unlocked and %u own)", pins, count - own, own);
    }
    if (mode != DRAW_SOUL || picks < s_picks)
        s_deal++;
    s_picks = picks;
    s_sets = sets;
    s_drawn = 1;
    s_made = 0;
    s_fresh = 0;
    if (picks > 1)
        fprintf(stderr, "[GRAFFITI] t=%.1f random graffiti's %u designs a size %s, for %u sets of "
                "sheets:%s\n", t, picks, hold ? "held" : "drawn", sets, line);
    else
        fprintf(stderr, "[GRAFFITI] t=%.1f random graffiti's set %s:%s\n", t, hold ? "held" : "drawn", line);
}

void random_graffiti_build(uint32_t mode)
{
    s_read_failed = 0;
    s_unread_held = 0;
    if (s_chain) {              /* one an area's load or a soul asked for here */
        s_chain = 0;
        s_answer = s_next;
        s_play_build = s_play != PLAY_NONE;
        return;
    }
    take_options();
    s_fresh = 1;
    if (mode == RG_BUILD_NEW_GAME || mode == RG_BUILD_LOAD_GAME)
        s_checked = 0;          /* each stage's kept tags are checked against this save at its first load */
    if (mode == RG_BUILD_NEW_GAME)
        rg_kept_clear();        /* a new game has no tags of the player's, nor a slot yet */
    if (mode == RG_BUILD_GARAGE || mode == RG_BUILD_MENU)
        s_own_listed = 0;       /* the garage's editor may have made, changed or let go of one: found again */
    s_drawn = 0;                /* the game's own: it draws its own one a size */
    s_answer = 0;
    s_play_build = 0;
    s_sets = 1;
    s_picks = 1;
    s_asking = 0;               /* whoever asked, the builds waited to be asked for here are forgotten */
    s_todraw = 0;
    s_left = 0;
    s_chained = 0;
    s_soul = 0;
    s_play = PLAY_NONE;
    s_story = mode == RG_BUILD_NEW_GAME || mode == RG_BUILD_LOAD_GAME || mode == RG_BUILD_GARAGE
           || mode == RG_BUILD_MENU;
}

int random_graffiti_texture(const uint8_t *mem, uint32_t id)
{
    uint32_t node = word(mem, RG_TEXTURES + RG_TM_BUCKETS + (id & 0xFFu) * 4u), steps;

    for (steps = 0; steps < RG_TM_NODES && node >= RG_GUEST_LOW && node <= RG_GUEST_RAM - RG_NODE_BYTES; steps++) {
        if (word(mem, node + RG_NODE_ID) == id)
            return (int)word(mem, node + RG_NODE_PLACE);
        node = word(mem, node + RG_NODE_NEXT);
    }
    return -1;
}

uint32_t random_graffiti_places(const uint8_t *mem)
{
    uint32_t n, places = 0;

    if (!word(mem, RG_TEXTURES + RG_TM_FREE))
        return 0;
    for (n = 0; n < RG_TM_PLACE_COUNT; n++)
        places += !word(mem, RG_TEXTURES + RG_TM_PLACES + n * 4u);
    return places;
}

/* How many sets of sheets are to be built, and for how many designs a size:
 * no more designs than the save has in some size, and no sets beyond the
 * game's own without the room. At an area's load it is asked with none of
 * the sheets a texture, the game's own two neither. The memory never used
 * is less by what the other sets' sheets took of it before, which is theirs
 * to take again: mine is the most that was. */
static uint32_t sets_wanted(const RANDOM_GRAFFITI_GAME *game, const uint8_t *mem, uint32_t *picks,
                            uint32_t *most, uint32_t *unused, uint32_t *mine, uint32_t *places)
{
    uint32_t size, sets;

    *most = 0;
    for (size = 0; size < RG_SIZES; size++) {
        uint32_t count = size_count(mem, size);
        if (count > *most) *most = count;
    }
    *unused = game->unused();
    *mine = s_most * RG_SHEET_BYTES;
    *places = random_graffiti_places(mem);
    *picks = *most < RG_PICKS ? *most : RG_PICKS;
    if (!*picks) *picks = 1;
    sets = 1u + (*picks - 1u + RG_ROWS - 1u) / RG_ROWS;
    for (; sets > 1; sets--)
        if ((uint64_t)(sets - 1u) * RG_SHEETS * RG_SHEET_BYTES + RG_SPARE_BYTES <= (uint64_t)*unused + *mine
         && sets * RG_SHEETS + RG_SPARE_PLACES <= *places)
            break;
    if (*picks > 1u + (sets - 1u) * RG_ROWS)
        *picks = 1u + (sets - 1u) * RG_ROWS;
    return sets;
}

/* The next of the builds asked for: 1 when the holder took it. */
static int ask_next(const RANDOM_GRAFFITI_GAME *game)
{
    int taken;

    s_chain = 1;
    s_next = s_left;
    taken = game->ask(RG_SHEET, RG_BUILD_GARAGE);
    s_chain = 0;
    return taken;
}

/* Whether the area's stage has kept tags of the player's own designs. */
static int kept_own(void)
{
    uint32_t spot;
    for (spot = 0; s_stage && spot < RG_KEPT_SPOTS; spot++) {
        const RG_KEPT *tag = rg_kept_get(s_stage, spot);
        if (tag && RG_IS_OWN(tag->design)) return 1;
    }
    return 0;
}

int random_graffiti_area(const RANDOM_GRAFFITI_GAME *game, const uint8_t *mem, int textures,
                         int stage, uint32_t code, double t)
{
    if (!s_asking) {
        uint32_t sets, picks, most, unused, mine, places, kept;

        if (!random_graffiti_on() || !s_story)
            return 1;
        s_soul = 0;             /* a soul's builds not asked for yet are this load's own */
        s_play = PLAY_NONE;
        if (textures) {
            s_mine = 1;
            game->let_go(RG_SHEET, 0);
            game->let_go(RG_SHEET_TEXTURES, 0);
            s_mine = 0;
        } else if (s_chained) {
            return 1;
        }
        /* This load's options, the area's kept tags, and the player's own
         * designs, before what is to be built is worked out. */
        take_options();
        s_area_n++;
        s_stage = random_graffiti_stage(mem, code);
        check_stage(mem, s_stage, t);
        if (!s_own_listed && (s_opt[RG_OPT_DESIGNS] != RG_DESIGNS_UNLOCKED || kept_own()))
            list_own(game, mem, t);
        kept = s_stage ? rg_kept_count(s_stage) : 0;
        if (kept)
            fprintf(stderr, "[GRAFFITI] t=%.1f the area is the save's stage %u (code %08X): %u tags kept "
                    "there\n", t, s_stage, code, kept);
        sets = sets_wanted(game, mem, &picks, &most, &unused, &mine, &places);
        if (!textures && sets < 2 && !kept) {   /* the game has just built its one sheet */
            s_chained = 1;
            return 1;
        }
        s_asking = 1;
        s_todraw = sets;
        s_topick = picks;
        fprintf(stderr, "[GRAFFITI] t=%.1f an area loads, %s: the sheet %s, to be built again for "
                "%u set%s, %u design%s a size (%u %s in the fullest size, %u KB of texture memory "
                "never used and %u KB the other sets' before, %u texture places free)\n", t,
                stage ? "a stage still loaded" : "no stage loaded",
                textures ? "is let go" : "was just built", sets, sets > 1 ? "s" : "", picks,
                picks > 1 ? "s" : "", most,
                s_opt[RG_OPT_DESIGNS] == RG_DESIGNS_UNLOCKED && !kept ? "unlocked" : "to draw from",
                unused >> 10, mine >> 10, places);
    }
    if (game->busy())
        return 0;
    if (s_todraw) {
        draw_picks(mem, s_topick, s_todraw, -1, s_opt[RG_OPT_AREA] ? DRAW_LOAD : DRAW_HOLD, t);
        s_left = s_todraw - 1u;
        s_todraw = 0;
    }
    if (!ask_next(game))
        return 0;
    if (s_left) {               /* an other set's: the game's own comes after it */
        s_left--;
        return 0;
    }
    if (s_sets > 1) {
        char line[120] = "";
        uint32_t k;

        for (k = 0; k < s_made && k < RG_OWN_SHEETS; k++)
            say(line, sizeof line, " %d", s_place[k]);
        fprintf(stderr, "[GRAFFITI] t=%.1f the other sets' sheets: %u of %u made textures%s%s; the "
                "game's own sheet's build is asked for\n", t, s_made, (s_sets - 1u) * RG_SHEETS,
                s_made ? ", in places" : "", line);
    }
    s_asking = 0;
    s_chained = 1;
    return 1;
}

int random_graffiti_design(const uint8_t *mem, uint32_t from, uint32_t row, uint32_t size,
                           uint32_t set, double t)
{
    size_t k;
    int design;

    if (set || row >= RG_ROWS || size >= RG_SIZES || !random_graffiti_on())
        return -1;
    for (k = 0; k < sizeof k_site / sizeof k_site[0] && k_site[k] != from; k++)
        ;
    if (k == sizeof k_site / sizeof k_site[0])
        return -1;
    if (!s_answer || s_answer >= RG_SETS) {     /* the game's own sheet: the player's row */
        if (row)
            return -1;
        if (!s_drawn)
            draw_picks(mem, 1, 1, -1, DRAW_LOAD, t);
        return s_pick[0][size];
    }
    design = s_pick[1u + (s_answer - 1u) * RG_ROWS + row][size];
    return design < 0 ? RG_AS_ROW_0 : design;
}

int random_graffiti_unread(uint32_t from, uint32_t row, uint32_t size, uint32_t set, uint32_t value,
                           double t)
{
    uint32_t place;
    int design;

    if (!random_graffiti_on() || set || row >= RG_ROWS || size >= RG_SIZES)
        return RG_UNREAD_GAME;
    if (from == RG_UNREAD_OWN || from == RG_UNREAD_OWN_XL) {    /* the own flag's write, after the stand-in's */
        if (!s_unread_held || row != s_unread_row || size != s_unread_size)
            return RG_UNREAD_GAME;
        s_unread_held = 0;
        return RG_UNREAD_HELD;
    }
    if (from != RG_UNREAD_DESIGN && from != RG_UNREAD_DESIGN_XL)
        return RG_UNREAD_GAME;
    s_unread_held = 0;
    if (!s_answer || s_answer >= RG_SETS)
        place = row ? RG_PICKS : 0;     /* the game's own sheet: another tagger's row is the game's */
    else
        place = 1u + (s_answer - 1u) * RG_ROWS + row;
    design = s_drawn && place < RG_PICKS ? s_pick[place][size] : -1;
    if (!RG_IS_OWN(design) || value < size * RG_DESIGNS || value >= (size + 1u) * RG_DESIGNS) {
        s_read_failed = 1;      /* the garage's own choice: the game's write, and its notice */
        return RG_UNREAD_GAME;
    }
    s_own_bad[design - (int)RG_OWN] = 1;
    s_pick[place][size] = (int)value;
    s_unread_held = 1;
    s_unread_row = row;
    s_unread_size = size;
    fprintf(stderr, "[GRAFFITI] t=%.1f the player's own design in slot %d (%s) couldn't be read: the place "
            "shows the game's stand-in, design %u, the progress object keeps the garage's choice%s, and the "
            "slot is left out until the game is closed\n", t, design - (int)RG_OWN, k_size[size], value,
            s_read_failed ? " (the notice stays, for a design of the garage's that failed too)"
                          : ", with no notice");
    return s_read_failed ? RG_UNREAD_NOTICE : RG_UNREAD_HELD;
}

uint32_t random_graffiti_sheet(const uint8_t *mem, uint32_t sheet)
{
    if (!random_graffiti_on() || s_answer >= RG_SETS || sheet >= RG_SHEETS)
        return 0;
    if (s_play_build && !s_chained)     /* a build in play, and the load's textures let go since */
        return 0;
    if (s_answer)
        return RG_OWN_TEXTURE + s_answer * RG_SHEETS + sheet;
    return s_play_build && random_graffiti_texture(mem, RG_TEXTURE + sheet) >= 0 ? RG_TEXTURE + sheet : 0;
}

void random_graffiti_made(uint32_t id, int place, double t)
{
    static const char *const k_why[] = { "the builder had no sheet", "the texture manager had no place free",
                                         "the device made no texture" };
    const char *why = k_why[place == RG_NO_SHEET ? 0 : place == RG_NO_PLACE ? 1 : 2];
    uint32_t n = id - RG_OWN_TEXTURE;

    if (id - RG_TEXTURE < RG_SHEETS) {  /* the game's own, again in play */
        if (place >= 0)
            s_play_made++;
        else
            fprintf(stderr, "[GRAFFITI] t=%.1f the game's own sheet %u was not made a texture again in "
                    "play (%s)\n", t, id - RG_TEXTURE, why);
        return;
    }
    if (place >= 0) {
        if (s_made < RG_OWN_SHEETS) s_place[s_made++] = place;
        if (s_made > s_most) s_most = s_made;
        return;
    }
    fprintf(stderr, "[GRAFFITI] t=%.1f set %u's sheet %u was not made a texture (%s): its spots draw "
            "the game's own\n", t, n / RG_SHEETS, n % RG_SHEETS, why);
}

void random_graffiti_let_go(void)
{
    if (!s_mine) s_chained = 0;
}

void random_graffiti_soul(uint32_t design, double t)
{
    if (!random_graffiti_on())
        return;
    if (!s_story || !s_chained) {
        fprintf(stderr, "[GRAFFITI] t=%.1f a graffiti soul's design %u is got: the next load's draw has "
                "it\n", t, design);
        return;
    }
    if (!s_opt_set[RG_OPT_SOUL]) {
        fprintf(stderr, "[GRAFFITI] t=%.1f a graffiti soul's design %u is got: Shuffle at Graffiti soul is "
                "off, so the designs stay until the next load's draw\n", t, design);
        return;
    }
    s_soul = 1;
    s_soul_design = design < RG_SIZES * RG_DESIGNS ? (int)design : -1;
    fprintf(stderr, "[GRAFFITI] t=%.1f a graffiti soul's design %u is got: the sheets are to be built "
            "again in play\n", t, design);
}

/* A build in play has ended: the next is asked for at the next pass, or it
 * was the last, the game's own sheet's. */
static void play_built(double t)
{
    if (s_left) {
        s_left--;
        s_play = PLAY_ASKING;
        return;
    }
    s_play = PLAY_NONE;
    fprintf(stderr, "[GRAFFITI] t=%.1f built again in play: %u of %u of the other sets' sheets and %u of "
            "%u of the game's own made textures again, in %u steps of the holder's\n", t, s_made,
            (s_sets - 1u) * RG_SHEETS, s_play_made, RG_SHEETS, s_play_steps);
}

void random_graffiti_play(const RANDOM_GRAFFITI_GAME *game, const uint8_t *mem, double t)
{
    uint32_t n;

    if (!s_soul && s_play == PLAY_NONE)
        return;
    if (!random_graffiti_on() || !s_story || !s_chained || s_asking) {  /* not this load's sheets any more */
        s_soul = 0;
        s_play = PLAY_NONE;
        return;
    }
    if (game->busy())
        return;
    if (s_play == PLAY_RUNNING) {       /* the build left to the holder's pace has ended */
        play_built(t);
        if (s_play == PLAY_NONE && !s_soul)
            return;
    }
    if (s_soul) {               /* the designs are drawn again, and the builds start over */
        uint32_t sets, picks, most, unused, mine, places;

        if (random_graffiti_texture(mem, RG_TEXTURE) < 0)
            return;             /* the game's own sheet isn't a texture yet */
        sets = sets_wanted(game, mem, &picks, &most, &unused, &mine, &places);
        fprintf(stderr, "[GRAFFITI] t=%.1f a graffiti soul: the sheets are built again in play, %u set%s, "
                "%u design%s a size (%u %s in the fullest size, %u KB of texture memory never used "
                "and %u KB the other sets' before, %u texture places free)\n", t, sets,
                sets > 1 ? "s" : "", picks, picks > 1 ? "s" : "", most,
                s_opt[RG_OPT_DESIGNS] == RG_DESIGNS_UNLOCKED && !(s_stage && rg_kept_count(s_stage))
                ? "unlocked" : "to draw from", unused >> 10, mine >> 10, places);
        draw_picks(mem, picks, sets, s_soul_design, DRAW_SOUL, t);
        s_soul = 0;
        s_play = PLAY_ASKING;
        s_left = sets - 1u;
        s_play_made = 0;
        s_play_steps = 0;
    }
    if (!ask_next(game))
        return;
    for (n = 0; n < RG_PLAY_STEPS && game->busy(); n++)
        game->step();
    s_play_steps += n;
    if (game->busy()) {
        s_play = PLAY_RUNNING;
        fprintf(stderr, "[GRAFFITI] t=%.1f a build in play is not done in %u steps of the holder's: left "
                "to its own pace\n", t, n);
        return;
    }
    play_built(t);
}

/* A number for a spot or a kind of spot at a deal: neighbours have nothing
 * in common, and it changes with the deal. */
static uint32_t mix(uint32_t spot, uint32_t deal)
{
    uint32_t h = (spot ^ (deal * 0x9E3779B1u)) + 0x7F4A7C15u;
    h ^= h >> 16; h *= 0x7FEB352Du;
    h ^= h >> 15; h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

/* A spot's parts, from its record as the spot's own draw finds it
 * (sub_0003F9E0): 0 when it has none. */
static uint32_t spot_parts(const uint8_t *mem, uint32_t spot)
{
    uint32_t record = word(mem, RG_SPOT_LISTS + (spot & 0xFFu) * 4u), steps;

    for (steps = 0; steps < RG_RECORDS && record >= RG_GUEST_LOW && record <= RG_GUEST_RAM - RG_RECORD_BYTES; steps++) {
        if (word(mem, record + RG_RECORD_ID) == spot)
            return word(mem, record + RG_RECORD_PARTS);
        record = word(mem, record + RG_RECORD_NEXT);
    }
    return 0;
}

/* The spots dealt a design in this deal, and how the deal went, for the log. */
static uint32_t s_spot[RG_SPOTS_DEALT], s_spots, s_spots_deal = 0xFFFFFFFFu, s_spots_sets;
static uint8_t s_spot_pick[RG_SPOTS_DEALT];
static uint32_t s_turn[RG_KINDS], s_of_set[RG_SETS];
static uint32_t s_twice[RG_TWICE_SAID], s_twices;

/* The n-th of the places a size's spots are dealt from: those not holding
 * a kept design, or all of them where every one does. */
static uint32_t free_place(uint32_t size, uint32_t n)
{
    uint32_t pinned = size < RG_SIZES ? s_pinned[size] & ((1u << s_picks) - 1u) : 0, k, free = 0;

    for (k = 0; k < s_picks; k++)
        free += !(pinned >> k & 1u);
    if (!free || free == s_picks)
        return n % s_picks;
    n %= free;
    for (k = 0; k < s_picks; k++)
        if (!(pinned >> k & 1u) && !n--)
            return k;
    return 0;
}

/* The place a spot of this kind and size has: at its first draw of a deal,
 * where first is set, the next of its kind's in turn, from a place that is
 * the kind's and the deal's own, and not one holding a kept design while
 * others are free; the same until the next deal. want, where not -1, is
 * the place of the spot's kept design, which it has instead. More spots
 * than are kept each have a design by the spot alone. */
static uint32_t dealt(uint32_t spot, uint32_t kind, uint32_t size, int want, int *first, double t)
{
    uint32_t k, pick;

    *first = 0;
    if (s_spots_deal != s_deal) {
        if (s_spots) {
            char line[80] = "";
            for (k = 0; k < s_spots_sets && k < RG_SETS; k++)
                say(line, sizeof line, "%s set %u %u", k ? "," : "", k, s_of_set[k]);
            fprintf(stderr, "[GRAFFITI] t=%.1f the deal before this: %u%s spots dealt a design,%s\n", t,
                    s_spots, s_spots == RG_SPOTS_DEALT ? " or more" : "", line);
        }
        s_spots_deal = s_deal;
        s_spots = 0;
        s_twices = 0;
        memset(s_turn, 0, sizeof s_turn);
        memset(s_of_set, 0, sizeof s_of_set);
    }
    s_spots_sets = s_sets;
    for (k = 0; k < s_spots; k++)
        if (s_spot[k] == spot) {
            if (want >= 0 && s_spot_pick[k] != (uint8_t)want) {     /* kept since: its kept design's place */
                s_spot_pick[k] = (uint8_t)want;
                *first = 1;
            }
            return s_spot_pick[k] % s_picks;
        }
    if (s_spots == RG_SPOTS_DEALT)
        return want >= 0 ? (uint32_t)want : free_place(size, mix(spot, s_deal));
    pick = want >= 0 ? (uint32_t)want : free_place(size, mix(kind, s_deal) + s_turn[kind]++);
    s_spot[s_spots] = spot;
    s_spot_pick[s_spots++] = (uint8_t)pick;
    *first = 1;
    return pick;
}

/* The save's stage of a spot, for its key, or 0 for none: a spot numbered
 * under RG_KEPT_SPOTS on a stage the save numbers, whose size is one its
 * sheet holds. A stage that isn't the area's becomes the area's. */
static uint32_t spot_stage(const uint8_t *mem, uint32_t spot, uint32_t sheet, uint32_t code, uint32_t number,
                           uint32_t size, double t)
{
    uint32_t stage;

    if (code == RG_NO_KEY || number >= RG_KEPT_SPOTS || size >= RG_SIZES)
        return 0;
    if ((size == RG_SIZES - 1u) != (sheet == RG_SHEETS - 1u)) {
        if (s_size_said < RG_TWICE_SAID) {
            s_size_said++;
            fprintf(stderr, "[GRAFFITI] t=%.1f spot %08X's size, %u, isn't one of its sheet's (%u): its tag "
                    "isn't kept\n", t, spot, size, sheet);
        }
        return 0;
    }
    stage = random_graffiti_stage(mem, code);
    if (stage && stage != s_stage) {
        if (s_said_area != s_area_n) {
            s_said_area = s_area_n;
            fprintf(stderr, "[GRAFFITI] t=%.1f spot %08X is on the save's stage %u (code %08X), not the "
                    "area's %u: its tags are kept there\n", t, spot, stage, code, s_stage);
        }
        s_stage = stage;
        check_stage(mem, stage, t);
    }
    return stage;
}

/* Whether a line about a kept tag goes in the log: an area's first few. */
static int kept_said(void)
{
    if (s_kept_said_area != s_area_n) {
        s_kept_said_area = s_area_n;
        s_kept_said = 0;
    }
    return s_kept_said++ < RG_KEPT_SAID;
}

/* A spot's tag kept with the design it is shown with, where it has none
 * kept, or one of another size, or one of the player's own whose slot has
 * no design of the size any more. One whose design just can't be shown
 * stays, for a draw that can. */
static void keep_tag(uint32_t spot, uint32_t stage, uint32_t number, uint32_t size, uint32_t parts, int design,
                     double t)
{
    const RG_KEPT *tag = rg_kept_get(stage, number);

    if (design < 0 || !design_ok(size, design))
        return;
    if (tag && tag->size == size) {
        int gone = RG_IS_OWN(tag->design) && s_own_listed
                && s_own_size[tag->design - (int)RG_OWN] != size + 1u;
        if (!gone) {
            if (tag->parts != parts)
                rg_kept_set(stage, number, size, parts, tag->design);
            return;
        }
    }
    rg_kept_set(stage, number, size, parts, design);
    if (kept_said())
        fprintf(stderr, "[GRAFFITI] t=%.1f spot %08X's tag is kept: stage %u, spot %u, %s, %u parts, %s %d\n",
                t, spot, stage, number, k_size[size], parts, RG_IS_OWN(design) ? "own slot" : "design",
                RG_IS_OWN(design) ? design - (int)RG_OWN : design);
}

uint32_t random_graffiti_spot(const uint8_t *mem, uint32_t spot, uint32_t id, uint32_t mesh, uint32_t code,
                              uint32_t number, uint32_t size, double t, RANDOM_GRAFFITI_DRAW *draw)
{
    static const uint32_t k_row[RG_ROWS] = { 0x00000000u, 0x3E800000u, 0x3F000000u, 0x3F400000u };
    uint32_t sheet = id - RG_TEXTURE, parts, pick, set, row, own, b, j, k, game, stage;
    uint32_t players = 0, others = 0, over = 0, under = 0;
    uint8_t whose[RG_PARTS][2];
    const RG_KEPT *tag;
    int first, want = -1, shown;

    draw->passes = 0;
    if (sheet >= RG_SHEETS || !random_graffiti_on())
        return 0;
    if (s_picks < 2 && number >= RG_KEPT_SPOTS)     /* one design a size, and no tag to keep */
        return 0;
    if (mesh < RG_GUEST_LOW || mesh > RG_GUEST_RAM - (RG_MESH_ROW + 8u * RG_PARTS))
        return 0;
    parts = spot_parts(mem, spot);
    if (!parts || parts > RG_PARTS)
        return 0;
    for (b = 0; b < parts; b++) {
        uint32_t *layer = draw->layer[0][b];

        layer[0] = word(mem, mesh + RG_MESH_SHOWN + 8u * b);
        layer[1] = word(mem, mesh + RG_MESH_SHOWN + 8u * b + 4u);
        layer[2] = word(mem, mesh + RG_MESH_ROW + 8u * b);
        layer[3] = word(mem, mesh + RG_MESH_ROW + 8u * b + 4u);
        for (j = 0; j < 2; j++) {
            whose[b][j] = !layer[j] || layer[2 + j] == RG_ROW_NONE ? LAYER_NONE
                        : layer[2 + j] ? LAYER_OTHER : LAYER_PLAYER;
            players += whose[b][j] == LAYER_PLAYER;
            others += whose[b][j] == LAYER_OTHER;
        }
        over += whose[b][0] == LAYER_PLAYER && whose[b][1] == LAYER_OTHER;
        under += whose[b][0] == LAYER_OTHER && whose[b][1] == LAYER_PLAYER;
    }
    stage = spot_stage(mem, spot, sheet, code, number, size, t);
    tag = stage ? rg_kept_get(stage, number) : NULL;
    if (!players) {             /* nothing of the player's: the game's own draw, and no design dealt yet */
        if (tag && others) {    /* another tagger's tag has taken the spot: the player's is gone */
            rg_kept_end(stage, number);
            if (kept_said())
                fprintf(stderr, "[GRAFFITI] t=%.1f spot %08X's kept tag is ended: another tagger's took "
                        "the spot (stage %u, spot %u)\n", t, spot, stage, number);
        }
        return 0;
    }
    if (s_picks < 2) {          /* the game's own draw: its sheet has the one design a size */
        if (stage && s_drawn)
            keep_tag(spot, stage, number, size, parts, s_pick[0][size], t);
        return 0;
    }
    if (tag && tag->size == size && design_ok(size, tag->design)) {    /* its kept design's place */
        for (k = 0; k < s_spots && s_spot[k] != spot; k++)
            ;
        want = k < s_spots && s_spots_deal == s_deal && s_spot_pick[k] < s_picks
            && s_pick[s_spot_pick[k]][size] == tag->design ? (int)s_spot_pick[k] : place_of(size, tag->design);
    }
    pick = dealt(spot, sheet * (RG_PARTS + 1u) + parts, size < RG_SIZES ? size : RG_SIZES, want, &first, t);
    set = pick ? 1u + (pick - 1u) / RG_ROWS : 0;
    row = pick ? (pick - 1u) % RG_ROWS : 0;
    own = pick ? RG_OWN_TEXTURE + set * RG_SHEETS + sheet : id;
    if (pick && random_graffiti_texture(mem, own) < 0) {    /* not made: the game's own sheet's design */
        pick = set = row = 0;
        own = id;
    }
    shown = stage ? s_pick[pick][size] : -1;
    if (first) {
        s_of_set[set]++;
        if (s_spots <= RG_SPOTS_SAID) {
            if (want >= 0)
                fprintf(stderr, "[GRAFFITI] t=%.1f spot %08X is dealt design %u of %u: set %u's sheet, row %u, "
                        "texture %08X, its kept design's%s\n", t, spot, pick, s_picks, set, row, own,
                        shown == tag->design ? "" : " (not made a texture: the game's own sheet's instead)");
            else
                fprintf(stderr, "[GRAFFITI] t=%.1f spot %08X is dealt design %u of %u: set %u's sheet, row %u, "
                        "texture %08X\n", t, spot, pick, s_picks, set, row, own);
        }
    }
    if (stage)
        keep_tag(spot, stage, number, size, parts, shown, t);
    if (!pick)
        return 0;
    /* One draw from the design's sheet, the player's layers in its row; or
     * two, the other taggers' layers from the game's own sheet as well. */
    draw->parts = parts;
    game = others ? (over && !under ? 1u : 0u) : 2u;    /* which draw is the game's own sheet's: neither of one */
    draw->passes = others ? 2 : 1;
    for (b = 0; b < parts; b++) {
        uint32_t was[4];

        memcpy(was, draw->layer[0][b], sizeof was);
        for (k = 0; k < draw->passes; k++) {
            uint32_t *layer = draw->layer[k][b];

            memcpy(layer, was, sizeof was);
            for (j = 0; j < 2; j++) {
                if (whose[b][j] == LAYER_PLAYER && k != game) {
                    layer[2 + j] = k_row[row];
                } else if (whose[b][j] == (k == game ? LAYER_PLAYER : LAYER_OTHER)) {
                    layer[j] = 0;       /* not this draw's: hidden, as the game hides a part with no tag */
                    layer[2 + j] = RG_ROW_NONE;
                }
            }
        }
    }
    for (k = 0; k < draw->passes; k++) {
        draw->texture[k] = k == game ? id : own;
        draw->depth_test[k] = k + 1u < draw->passes ? RG_Z_LESS : 0;    /* the one underneath: strict */
    }
    if (others) {
        for (k = 0; k < s_twices && s_twice[k] != spot; k++)
            ;
        if (k == s_twices && s_twices < RG_TWICE_SAID) {
            s_twice[s_twices++] = spot;
            fprintf(stderr, "[GRAFFITI] t=%.1f spot %08X is drawn twice: another tagger's layers from the "
                    "game's own sheet %s, the player's from texture %08X; the first with depth test %X, the "
                    "second with the game's (depth write %u, test %X)\n", t, spot, game ? "last" : "first",
                    own, RG_Z_LESS, word(mem, RG_RS_ZWRITE), word(mem, RG_RS_ZFUNC));
        }
    }
    return draw->passes;
}
