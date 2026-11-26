/* random_graffiti.c -- the designs the player sprays, drawn at random from
 * the unlocked ones, a design for each spot (random_graffiti.h). */
#include "random_graffiti.h"

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

enum { LAYER_NONE, LAYER_PLAYER, LAYER_OTHER };     /* whose tag a layer of a part is */
enum { PLAY_NONE, PLAY_ASKING, PLAY_RUNNING };      /* a soul's builds in play */

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

int random_graffiti_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("JSRF_RANDOM_GRAFFITI");
        on = v && *v && strcmp(v, "0") != 0;
    }
    return on;
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

/* The designs drawn, picks a size, for this many sets of sheets: for each
 * size its unlocked designs in a random order, each order as likely as the
 * others, the size's designs the first of them, starting over where they
 * run out. soul, a design just got (or -1), is one of its size's. The spots
 * are dealt again with them, unless keep: then a spot keeps its place among
 * the designs where there are no fewer, and shows the new design there. */
static void draw_picks(const uint8_t *mem, uint32_t picks, uint32_t sets, int soul, int keep, double t)
{
    char line[700] = "";
    uint32_t size, n, k;

    for (size = 0; size < RG_SIZES; size++) {
        uint32_t list[RG_DESIGNS], count = 0;

        for (n = 0; n < RG_DESIGNS; n++)
            if (unlocked(mem, size, n))
                list[count++] = size * RG_DESIGNS + n;
        for (n = count; n > 1; n--) {
            uint32_t pick = random_below(n), last = list[n - 1];
            list[n - 1] = list[pick];
            list[pick] = last;
        }
        for (n = picks; soul >= 0 && n < count; n++)
            if (list[n] == (uint32_t)soul) {    /* not among the first: it takes the place of one of them */
                uint32_t pick = random_below(picks);
                list[n] = list[pick];
                list[pick] = (uint32_t)soul;
                break;
            }
        for (k = 0; k < RG_PICKS; k++)
            s_pick[k][size] = count ? (int)list[k % count] : -1;
        if (!count) {
            say(line, sizeof line, "%s %s the garage's (none unlocked)", size ? "," : "", k_size[size]);
            continue;
        }
        say(line, sizeof line, "%s %s design%s", size ? "," : "", k_size[size], picks > 1 ? "s" : "");
        for (k = 0; k < picks && k < count; k++)
            say(line, sizeof line, " %d", s_pick[k][size]);
        say(line, sizeof line, " (of %u unlocked)", count);
    }
    if (!keep || picks < s_picks)
        s_deal++;
    s_picks = picks;
    s_sets = sets;
    s_drawn = 1;
    s_made = 0;
    if (picks > 1)
        fprintf(stderr, "[GRAFFITI] t=%.1f random graffiti's %u designs a size drawn, for %u sets of "
                "sheets:%s\n", t, picks, sets, line);
    else
        fprintf(stderr, "[GRAFFITI] t=%.1f random graffiti's set drawn:%s\n", t, line);
}

void random_graffiti_build(uint32_t mode)
{
    if (s_chain) {              /* one an area's load or a soul asked for here */
        s_chain = 0;
        s_answer = s_next;
        s_play_build = s_play != PLAY_NONE;
        return;
    }
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
    uint32_t size, n, sets;

    *most = 0;
    for (size = 0; size < RG_SIZES; size++) {
        uint32_t count = 0;
        for (n = 0; n < RG_DESIGNS; n++)
            count += (uint32_t)unlocked(mem, size, n);
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

int random_graffiti_area(const RANDOM_GRAFFITI_GAME *game, const uint8_t *mem, int textures,
                         int stage, double t)
{
    if (!s_asking) {
        uint32_t sets, picks, most, unused, mine, places;

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
        sets = sets_wanted(game, mem, &picks, &most, &unused, &mine, &places);
        if (!textures && sets < 2) {    /* the game has just built its one sheet */
            s_chained = 1;
            return 1;
        }
        s_asking = 1;
        s_todraw = sets;
        s_topick = picks;
        fprintf(stderr, "[GRAFFITI] t=%.1f an area loads, %s: the sheet %s, to be built again for "
                "%u set%s, %u design%s a size (%u unlocked in the fullest size, %u KB of texture memory "
                "never used and %u KB the other sets' before, %u texture places free)\n", t,
                stage ? "a stage still loaded" : "no stage loaded",
                textures ? "is let go" : "was just built", sets, sets > 1 ? "s" : "", picks,
                picks > 1 ? "s" : "", most, unused >> 10, mine >> 10, places);
    }
    if (game->busy())
        return 0;
    if (s_todraw) {
        draw_picks(mem, s_topick, s_todraw, -1, 0, t);
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
            draw_picks(mem, 1, 1, -1, 0, t);
        return s_pick[0][size];
    }
    design = s_pick[1u + (s_answer - 1u) * RG_ROWS + row][size];
    return design < 0 ? RG_AS_ROW_0 : design;
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
                "%u design%s a size (%u unlocked in the fullest size, %u KB of texture memory never used "
                "and %u KB the other sets' before, %u texture places free)\n", t, sets,
                sets > 1 ? "s" : "", picks, picks > 1 ? "s" : "", most, unused >> 10, mine >> 10, places);
        draw_picks(mem, picks, sets, s_soul_design, 1, t);
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

/* The design a spot of this kind has: at its first draw of a deal, where
 * first is set, the next of its kind's in turn, from a place that is the
 * kind's and the deal's own; the same until the next deal. More spots than
 * are kept each have a design by the spot alone. */
static uint32_t dealt(uint32_t spot, uint32_t kind, int *first, double t)
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
        if (s_spot[k] == spot)
            return s_spot_pick[k] % s_picks;
    if (s_spots == RG_SPOTS_DEALT)
        return mix(spot, s_deal) % s_picks;
    pick = (mix(kind, s_deal) + s_turn[kind]++) % s_picks;
    s_spot[s_spots] = spot;
    s_spot_pick[s_spots++] = (uint8_t)pick;
    *first = 1;
    return pick;
}

uint32_t random_graffiti_spot(const uint8_t *mem, uint32_t spot, uint32_t id, uint32_t mesh, double t,
                              RANDOM_GRAFFITI_DRAW *draw)
{
    static const uint32_t k_row[RG_ROWS] = { 0x00000000u, 0x3E800000u, 0x3F000000u, 0x3F400000u };
    uint32_t sheet = id - RG_TEXTURE, parts, pick, set, row, own, b, j, k, game;
    uint32_t players = 0, others = 0, over = 0, under = 0;
    uint8_t whose[RG_PARTS][2];
    int first;

    draw->passes = 0;
    if (sheet >= RG_SHEETS || s_picks < 2 || !random_graffiti_on())
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
    if (!players)               /* nothing of the player's: the game's own draw, and no design dealt yet */
        return 0;
    pick = dealt(spot, sheet * (RG_PARTS + 1u) + parts, &first, t);
    set = pick ? 1u + (pick - 1u) / RG_ROWS : 0;
    row = pick ? (pick - 1u) % RG_ROWS : 0;
    own = pick ? RG_OWN_TEXTURE + set * RG_SHEETS + sheet : id;
    if (pick && random_graffiti_texture(mem, own) < 0) {    /* not made: the game's own sheet's design */
        pick = set = row = 0;
        own = id;
    }
    if (first) {
        s_of_set[set]++;
        if (s_spots <= RG_SPOTS_SAID)
            fprintf(stderr, "[GRAFFITI] t=%.1f spot %08X is dealt design %u of %u: set %u's sheet, row %u, "
                    "texture %08X\n", t, spot, pick, s_picks, set, row, own);
    }
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
