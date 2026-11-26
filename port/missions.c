/* Developer > Missions: the game started at a story mission (missions.h).
 *
 * The game flow (sub_0007BDD0's object, its state at +0x48) goes from the
 * boot through the intro to the title (state 0xA), which makes the title's
 * scene task; the player's choice there leads to state 0xE, whose New Game
 * resets the progress and sets chapter 1, mission 1. State 0x11 asks for the
 * graffiti's resource (type 0x19), whose load reads the chapter from the
 * progress object; state 0x13 puts the entrances back to their start
 * (sub_0004EAF0); and state 0x1C makes the scene task, which starts the
 * mission of game variables 2 and 3. Mission-select/FINDINGS-4.md, in the
 * project's shared folder, has the reading of the game's code.
 *
 * A start does at the title's state what that state does but make the
 * title: its variables, and its own calls (missions_title_fn, made by the
 * hook), then New Game chosen and state 0xE, as the title's menu leaves it.
 * It writes the progress object as the flow first reaches 0x11, before the
 * graffiti's resource reads the chapter, and the chapter, the mission and the
 * entrance as it first reaches 0x1C, after the entrances' last reset. Each
 * once, in that order. The game's own states then load and play the mission.
 *
 * Mission flags aren't written: the mission flow clears them as it builds the
 * mission. Variable 0xD, the skater player 1 plays, is written with the
 * chapter and the mission when JSRF_START_SKATER gives one (Developer >
 * Missions > Character), and otherwise stays as New Game sets it (Yoyo); none
 * of its readers is reached from state 0xE to 0x1C. That skater is unlocked
 * in the progress written, as the game's own unlock does (sub_00039CB0: its
 * bit at +0x14), since where the story hasn't unlocked a skater yet the
 * mission's cast list gives the next unlocked one, as it does for a player.
 * The other skaters stay as the story has them.
 *
 * A start at Load Game (JSRF_START_LOAD) does the title's part the same
 * way, but chooses Load Game: game variable 0x1CC = 1, which state 0xE reads
 * to load the save menu's resource (0xA), and state 0xF opens the game's own
 * Load Game screen (sub_000681C0(1, 0)). Nothing else is written: what is
 * loaded is the save's. Its saves are the mission runs' (missions_paths).
 *
 * Without JSRF_START_STATE, the mission's line is looked for in
 * start-states.txt beside the settings file (MISSIONS_LINES), which a test
 * build puts there: the game's own story, walked from its mission files, for
 * the test only, never in the repository or a package.
 *
 * What the start writes is the story's progress and player 1's entrance. The
 * game also carries where each character stood from one mission to the next,
 * outside the progress, in the scene task's places (task slot 0xB), which a
 * start leaves empty: a stage entered with entrance -1, as after a rival's
 * preview, would put player 1 at the map's origin, in the air (known bug
 * 89). So in the first mission the start plays, player 1's empty place is
 * written as that mission's level start, and in a scene with no player in it
 * as where its script gives the rival its start (missions_script). */
#define _POSIX_C_SOURCE 200112L    /* setenv, unsetenv */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel.h"
#include "missions.h"

#define MSN_GUEST_LOW       0x10000u /* below it, no guest address the game uses */

/* ---- the game flow (sub_0007BDD0's object) ---- */
#define MSN_FLOW_STATE      0x48u   /* its state, one of 64 at 0x20D2B8 */
#define MSN_FLOW_TITLE      0x0Au   /* makes the title's scene task */
#define MSN_FLOW_NEW_GAME   0x0Eu   /* after the title's menu: New Game or Load */
#define MSN_FLOW_GRAFFITI   0x11u   /* asks for the graffiti's resource, which reads the chapter */
#define MSN_FLOW_SCENE      0x1Cu   /* makes the scene task, which starts the mission */

/* ---- the game manager's variables: [MSN_GAME] + MSN_GM_VARS + 4 * i ---- */
#define MSN_GAME            0x0022FCE0u /* -> the game manager */
#define MSN_GM_VARS         0x7868u
#define GV_CHAPTER          0x002u  /* the mission to start: its chapter ... */
#define GV_NUMBER           0x003u  /* ... and its number, the bare NN */
#define GV_TITLE_8          0x008u  /* 0 from the title's state (its use not read) */
#define GV_TITLE_MENU       0x009u  /* 0: the title's logo and menu */
#define GV_SCENE_MODE       0x00Bu  /* the scene task's mode: 0 mission, 1 title */
#define GV_SKATER           0x00Du  /* the skater player 1 plays: New Game sets 6 */
#define GV_ENTRANCE         0x04Du  /* player 1's entrance */
#define GV_SCENE_LOADED     0x19Au  /* 1: the scene's resources are freed at state 0xE */
#define GV_TITLE_CHOICE     0x1CCu  /* the title's choice: 0 New Game, 1 Load */
#define GV_LAST             GV_TITLE_CHOICE
#define SCENE_MODE_TITLE    1u
#define CHOICE_NEW_GAME     0u
#define CHOICE_LOAD         1u

/* ---- the progress object, the block a save keeps ---- */
#define MSN_PROGRESS        0x001EFFB0u
#define PRG_CHAPTER         0x0004u
#define PRG_SKATERS         0x0014u /* a bit for each skater unlocked */
#define PRG_FLAGS_CHAPTER   0x0018u /* the flag banks: flag n at dword n >> 5, bit n & 31 */
#define PRG_FLAGS_GLOBAL    0x0058u
#define PRG_FLAGS_BANK3     0x0098u
#define PRG_SOULS_APPEARED  0x00D8u /* the graffiti souls: a bit each, three ways */
#define PRG_SOULS_GOT       0x00F8u
#define PRG_SOULS_HELD      0x0118u
#define PRG_TANKS           0x2A1Cu /* the Noise Tanks' count, a bit each */
#define PRG_END             (PRG_TANKS + 4u * TANK_DWORDS)

/* ---- where each character stands, kept from one mission to the next ----
 * Task slot 0xB's object, which the scene task makes at state 0x1C
 * (sub_000A82D0), zeroed: a place for each of 32 characters, slot 0 player
 * 1's. A spawn (the stage's script record 0x48, 0x58930) puts a character at
 * the mission's entrance that game variable 0x4D + its slot names, or with
 * -1 where its place is kept (0x58B30); a start (record 0x4C, 0x59656) puts
 * one at an entry of the mission's start table. The stage's script runner
 * (sub_0005B3F0) has the mission's header at +0x1040. */
#define MSN_GM_TASKS        0x98u   /* the game manager's task slots, 4 bytes each */
#define TASK_PLACES         0x0Bu   /* the object holding each character's place */
#define PLACES_AT           0x544u  /* its places, PLACE_SIZE each */
#define PLACE_SIZE          0x34u
#define PLACES              32u
#define PLACE_SPOT          0x0Cu   /* x, y and z (floats), then the turn about each (ints) */
#define SPOT_WORDS          6u      /* what a spawn at -1 keeps of the place */
#define RUN_HEADER          0x1040u /* the runner's mission header */
#define HDR_ENTRANCES       0x3Cu   /* the entrances: a pointer, then the count */
#define HDR_STARTS          0xE4u   /* the start table: pointers to places */
#define HDR_SCRIPT          0x174u  /* the script's records */
#define ENTRANCE_SIZE       0x2Cu
#define ENTRANCE_SPOT       0x08u   /* as a place's PLACE_SPOT, for the two kinds below */
#define ENTRANCE_KIND_SPOT  0u      /* its kind, its first word: a spawn (0x58930) */
#define ENTRANCE_KIND_SPOT2 2u      /* takes the spot of these two kinds */
#define RECORD_SIZE         0x54u
#define RECORD_OP           0x10u
#define RECORD_SLOT         0x14u
#define RECORD_ARG          0x18u   /* a start's entry in the start table */
#define OP_SPAWN            0x48u
#define OP_START            0x4Cu
#define SCRIPT_MOST         4096u   /* more records than any mission's */
#define FLAG_DWORDS         16u     /* a bank's 512 flags */
#define SOUL_DWORDS         8u      /* 256 souls */
#define TANK_DWORDS         32u     /* 1024 */
#define SKATERS             32u

/* ---- what can be asked for ---- */
#define CHAPTER_FIRST       1       /* the story's chapters, the rows the graffiti's */
#define CHAPTER_LAST        9       /* resource has for them */
#define MISSION_FILE        "D:\\Media\\Mission\\mssn%04d.bin"  /* the game's own name (sub_00030490) */
#define STATE_MAX           2048    /* JSRF_START_STATE's longest line */
#define LINES_MAX           (1 << 20) /* start-states.txt's most bytes */

enum { BANK_CHAPTER, BANK_GLOBAL, BANK_3, BANKS };
static const struct { char key; const char *name; uint32_t at; } k_banks[BANKS] = {
    { 'C', "Chapter", PRG_FLAGS_CHAPTER },
    { 'G', "Global",  PRG_FLAGS_GLOBAL },
    { 'R', "bank 3",  PRG_FLAGS_BANK3 },
};

/* Where the start is: waiting for each state in turn, then done. */
enum { STEP_NONE, STEP_TITLE, STEP_PROGRESS, STEP_MISSION, STEP_DONE };

/* A start line's fields (JSRF_START_STATE's, or start-states.txt's). */
typedef struct {
    int have_entrance;
    uint32_t entrance;
    int have_bank[BANKS];
    uint32_t bank[BANKS][FLAG_DWORDS];
    int have_skaters;
    uint32_t skaters;
    unsigned souls, tanks;              /* counts to meet; 0 none */
    int off_story;                      /* story=0: no exit leads to it */
} StartLine;

static struct {
    int step;
    int mission, chapter, number;       /* CCNN, CC, NN */
    int skater;                         /* JSRF_START_SKATER's; -1 none */
    int load;                           /* JSRF_START_LOAD's: Load Game, not a mission */
    StartLine line;
    uint32_t progress;                  /* the line's fields hashed (progress_hash); 0 without one */
} s;

/* Player 1's place in the first mission a start plays (missions_script). */
static struct {
    int armed;                          /* from the mission's start until done with it */
    uint32_t header;                    /* the first mission's header; 0 before its script runs */
    uint32_t slot;                      /* the character whose start to take; 0 none */
    uint32_t wrote[SPOT_WORDS];         /* the level start, as written to player 1's place */
} s_place;

/* Taking a variable out of the environment, as developer.c's set_env does
 * (Windows' _putenv_s also takes it out of what a restart's new process
 * gets); a function of its own, as developer.c's is static to it. */
static void env_remove(const char *name)
{
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

/* A comma-separated list of numbers below `below`, as bits in `bits`
 * (`words` dwords). Returns 0 for anything else. */
static int read_bits(const char *v, uint32_t *bits, unsigned words, unsigned below)
{
    memset(bits, 0, words * 4u);
    for (;;) {
        char *end;
        unsigned long n;
        if (*v < '0' || *v > '9') return 0;
        n = strtoul(v, &end, 10);
        if (n >= below) return 0;
        bits[n >> 5] |= 1u << (n & 31u);
        if (*end == '\0') return 1;
        if (*end != ',') return 0;
        v = end + 1;
    }
}

/* A number from 0 to `most`. */
static int read_count(const char *v, unsigned most, unsigned *out)
{
    char *end;
    unsigned long n;
    if (*v < '0' || *v > '9') return 0;
    n = strtoul(v, &end, 10);
    if (*end || n > most) return 0;
    *out = (unsigned)n;
    return 1;
}

/* Four digits, CCNN, as a number; -1 for anything else. */
static int read_mission(const char *v)
{
    int i;
    for (i = 0; i < 4; i++)
        if (v[i] < '0' || v[i] > '9') return -1;
    return v[4] ? -1 : atoi(v);
}

/* A start line's fields into `st`, each once: e=, C=, G=, R=, S=, souls=,
 * tanks=, story=, after mission's own number if the line starts with it.
 * Says which field it can't read, in `why`, and returns 0. */
static int read_state(const char *line, int mission, StartLine *st, char *why, size_t n)
{
    char buf[STATE_MAX], *next = buf;
    int first = 1, seen_souls = 0, seen_tanks = 0, seen_story = 0, b;
    if (strlen(line) >= sizeof buf) {
        snprintf(why, n, "longer than %d characters", STATE_MAX - 1);
        return 0;
    }
    strcpy(buf, line);
    for (;; first = 0) {
        char *field, *v;
        int ok = 0, again = 0;
        next += strspn(next, " \t");
        if (!*next) break;
        field = next;
        next += strcspn(next, " \t");
        if (*next) *next++ = '\0';
        v = strchr(field, '=');
        if (first && !v && read_mission(field) >= 0) {
            if (read_mission(field) == mission) continue;
            snprintf(why, n, "it is %s's line, not %04d's", field, mission);
            return 0;
        }
        if (!v) {
            snprintf(why, n, "\"%s\" has no value", field);
            return 0;
        }
        *v++ = '\0';
        if (!strcmp(field, "e")) {
            char *end;
            long e = strtol(v, &end, 10);
            again = st->have_entrance;
            ok = *v && !*end && e >= -1 && e < 0x7FFFFFFFL;
            st->have_entrance = 1;
            st->entrance = (uint32_t)e;        /* -1: an exit that names none */
        } else if (!strcmp(field, "S")) {
            again = st->have_skaters;
            ok = read_bits(v, &st->skaters, 1, SKATERS);
            st->have_skaters = 1;
        } else if (!strcmp(field, "souls")) {
            again = seen_souls;
            ok = read_count(v, 32u * SOUL_DWORDS, &st->souls);
            seen_souls = 1;
        } else if (!strcmp(field, "tanks")) {
            again = seen_tanks;
            ok = read_count(v, 32u * TANK_DWORDS, &st->tanks);
            seen_tanks = 1;
        } else if (!strcmp(field, "story")) {
            unsigned on = 1;
            again = seen_story;
            ok = read_count(v, 1, &on);
            st->off_story = !on;
            seen_story = 1;
        } else {
            for (b = 0; b < BANKS; b++) {
                if (field[0] != k_banks[b].key || field[1]) continue;
                again = st->have_bank[b];
                ok = read_bits(v, st->bank[b], FLAG_DWORDS, 32u * FLAG_DWORDS);
                st->have_bank[b] = 1;
                break;
            }
            if (b == BANKS) {
                snprintf(why, n, "\"%s\" is not one of e, C, G, R, S, souls, tanks and story", field);
                return 0;
            }
        }
        if (again) {
            snprintf(why, n, "%s= is given twice", field);
            return 0;
        }
        if (!ok) {
            snprintf(why, n, "%s=%s is not what %s= takes", field, v, field);
            return 0;
        }
    }
    return 1;
}

/* A start line's progress as one number, for a recording to note
 * (missions_started): FNV-1a over each field as read, in one order, a
 * little-endian word each, with whether it was given. 0 is New Game's
 * progress: no line, or one that gives nothing (its mission alone, souls=0,
 * tanks=0, story=1); any other is never 0. */
static uint32_t hash_word(uint32_t h, uint32_t w)
{
    int i;
    for (i = 0; i < 4; i++) {
        h ^= (w >> (8 * i)) & 0xFFu;
        h *= 16777619u;
    }
    return h;
}

static uint32_t progress_hash(const StartLine *st)
{
    uint32_t h = 2166136261u;
    unsigned b, i;
    if (!st->have_entrance && !st->have_bank[BANK_CHAPTER] && !st->have_bank[BANK_GLOBAL]
        && !st->have_bank[BANK_3] && !st->have_skaters && !st->souls && !st->tanks && !st->off_story)
        return 0u;
    h = hash_word(h, (uint32_t)st->have_entrance);
    h = hash_word(h, st->have_entrance ? st->entrance : 0u);
    for (b = 0; b < BANKS; b++) {
        h = hash_word(h, (uint32_t)st->have_bank[b]);
        for (i = 0; i < FLAG_DWORDS; i++) h = hash_word(h, st->have_bank[b] ? st->bank[b][i] : 0u);
    }
    h = hash_word(h, (uint32_t)st->have_skaters);
    h = hash_word(h, st->have_skaters ? st->skaters : 0u);
    h = hash_word(h, st->souls);
    h = hash_word(h, st->tanks);
    h = hash_word(h, (uint32_t)st->off_story);
    return h ? h : 1u;
}

static unsigned count_bits(const uint32_t *w, unsigned words)
{
    unsigned i, n = 0;
    for (i = 0; i < words; i++) {
        uint32_t x = w[i];
        for (; x; x &= x - 1) n++;
    }
    return n;
}

/* The bits a field sets, as a list for the log ("1,2,6"), or "-". */
static void list_bits(char *out, size_t n, const uint32_t *w, unsigned words)
{
    unsigned i;
    size_t at = 0;
    out[0] = '\0';
    for (i = 0; i < 32u * words && at + 1 < n; i++)
        if (w[i >> 5] >> (i & 31u) & 1u)
            at += (size_t)snprintf(out + at, n - at, at ? ",%u" : "%u", i);
    if (!at) snprintf(out, n, "-");
}

#if defined(NV2A_GL_USE_SDL)
/* nv2a_gl.c: a file beside the settings file. */
extern void nv_window_file_path(const char *file, char *buf, size_t n);
#endif

/* start-states.txt (MISSIONS_LINES) beside the settings file, read once: its
 * text, or NULL when there is none. */
static const char *lines_text(void)
{
#if defined(NV2A_GL_USE_SDL)
    static char *text;
    static int done;
    char path[1024];
    FILE *f;
    long size;
    if (done) return text;
    done = 1;
    nv_window_file_path(MISSIONS_LINES, path, sizeof path);
    if (!path[0] || !(f = fopen(path, "rb"))) return NULL;
    if (fseek(f, 0, SEEK_END) || (size = ftell(f)) < 0 || size > LINES_MAX || fseek(f, 0, SEEK_SET)
        || !(text = malloc((size_t)size + 1)))
        fprintf(stderr, "[MISSION] %s left out: unreadable, or over %d bytes\n", path, LINES_MAX);
    else
        text[fread(text, 1, (size_t)size, f)] = '\0';
    fclose(f);
    return text;
#else
    return NULL;                        /* no settings file to be beside */
#endif
}

/* The mission's line in start-states.txt, CCNN and all, into `out` (cut at
 * n - 1 characters, so a line too long is seen to be): 1 when it has one. */
static int find_line(int mission, char *out, size_t n)
{
    const char *p = lines_text();
    char id[8];
    if (!p) return 0;
    snprintf(id, sizeof id, "%04d", mission);
    while (*p) {
        size_t len = strcspn(p, "\r\n");
        if (len >= 4 && !strncmp(p, id, 4) && (len == 4 || p[4] == ' ' || p[4] == '\t')) {
            if (len > n - 1) len = n - 1;
            memcpy(out, p, len);
            out[len] = '\0';
            return 1;
        }
        p += len;
        p += strspn(p, "\r\n");
    }
    return 0;
}

int missions_off_story(int mission)
{
    static char line[STATE_MAX + 1];
    StartLine st;
    char why[160];
    memset(&st, 0, sizeof st);
    return find_line(mission, line, sizeof line) && read_state(line, mission, &st, why, sizeof why)
        && st.off_story;
}

/* JSRF_START_SKATER's skater, one of the game's characters; -1 for anything
 * else. */
static int read_skater(const char *v)
{
    unsigned n;
    return read_count(v, MISSIONS_CHARACTERS - 1, &n) ? (int)n : -1;
}

void missions_start(void)
{
    static const char *const k_env[] = { "JSRF_START_STATE", "JSRF_START_SKATER" };
    const char *mission = getenv("JSRF_START_MISSION"), *state = getenv("JSRF_START_STATE"),
               *skater = getenv("JSRF_START_SKATER"), *load = getenv("JSRF_START_LOAD");
    static char line[STATE_MAX + 1];   /* one longer, so a line too long is seen to be */
    char mbuf[32], kbuf[32], why[160], skater_said[32] = "";
    const char *from = "JSRF_START_STATE";
    int have_line = state != NULL;
    size_t i;
    memset(&s, 0, sizeof s);
    s.skater = -1;
    if (load) {
        int at_load = !strcmp(load, "1");
        if (!at_load)
            fprintf(stderr, "[MISSION] JSRF_START_LOAD=%.16s refused: not 1; left alone\n", load);
        env_remove("JSRF_START_LOAD");
        if (at_load) {
            if (mission) env_remove("JSRF_START_MISSION");
            for (i = 0; i < sizeof k_env / sizeof k_env[0]; i++) env_remove(k_env[i]);
            s.load = 1;
            s.step = STEP_TITLE;
            fprintf(stderr, "[MISSION] starting at Load Game, with the mission runs' saves%s\n",
                    mission ? " (JSRF_START_MISSION left alone)" : "");
            return;
        }
    }
    if (!mission) {
        for (i = 0; i < sizeof k_env / sizeof k_env[0]; i++) {
            if (!getenv(k_env[i])) continue;
            fprintf(stderr, "[MISSION] %s without JSRF_START_MISSION: left alone\n", k_env[i]);
            env_remove(k_env[i]);
        }
        return;
    }
    snprintf(mbuf, sizeof mbuf, "%s", mission);
    snprintf(kbuf, sizeof kbuf, "%s", skater ? skater : "");
    snprintf(line, sizeof line, "%s", have_line ? state : "");
    /* Taken out at once, so the restart that follows a run starts as normal. */
    env_remove("JSRF_START_MISSION");
    for (i = 0; i < sizeof k_env / sizeof k_env[0]; i++) env_remove(k_env[i]);
    s.mission = read_mission(mbuf);
    s.chapter = s.mission / 100;
    s.number = s.mission % 100;
    if (s.mission >= 0 && !have_line && find_line(s.mission, line, sizeof line)) {
        have_line = 1;
        from = MISSIONS_LINES;
    }
    if (s.mission < 0) {
        fprintf(stderr, "[MISSION] JSRF_START_MISSION=%s refused: not four digits, CCNN; starting normally\n",
                mbuf);
    } else if (s.chapter < CHAPTER_FIRST || s.chapter > CHAPTER_LAST) {
        fprintf(stderr, "[MISSION] JSRF_START_MISSION=%s refused: chapter %d, where only the story's,"
                " %d to %d, start this way; starting normally\n", mbuf, s.chapter, CHAPTER_FIRST,
                CHAPTER_LAST);
    } else if (skater && (s.skater = read_skater(kbuf)) < 0) {
        fprintf(stderr, "[MISSION] JSRF_START_SKATER=%s refused: not 0 to %d, the game's characters;"
                " starting normally\n", kbuf, MISSIONS_CHARACTERS - 1);
    } else if (have_line && !read_state(line, s.mission, &s.line, why, sizeof why)) {
        fprintf(stderr, "[MISSION] %s refused for %04d: %s; starting normally\n", from, s.mission, why);
    } else {
        s.step = STEP_TITLE;
        s.progress = have_line ? progress_hash(&s.line) : 0u;
        if (skater) snprintf(skater_said, sizeof skater_said, ", skater %d", s.skater);
        if (!have_line)
            fprintf(stderr, "[MISSION] starting at %04d: chapter %d, mission %d%s; no JSRF_START_STATE or line"
                    " in %s, so the progress is New Game's\n", s.mission, s.chapter, s.number, skater_said,
                    MISSIONS_LINES);
        else
            fprintf(stderr, "[MISSION] starting at %04d: chapter %d, mission %d%s; %s: %s\n",
                    s.mission, s.chapter, s.number, skater_said, from, line);
    }
}

void missions_paths(const char *save_dir)
{
    char path[64], dir[1024];
    XBOX_ANSI_STRING name;
    XBOX_OBJECT_ATTRIBUTES oa;
    XBOX_FILE_NETWORK_OPEN_INFORMATION info;
    if (s.step != STEP_TITLE) return;
    /* Load Game has no mission's file to look for. */
    if (!s.load) {
        snprintf(path, sizeof path, MISSION_FILE, s.mission);
        name.Buffer = path;
        name.Length = (USHORT)strlen(path);
        name.MaximumLength = (USHORT)(name.Length + 1);
        oa.RootDirectory = NULL;
        oa.ObjectName = &name;
        oa.Attributes = 0;
        if (!NT_SUCCESS(xbox_NtQueryFullAttributesFile(&oa, &info))) {
            fprintf(stderr, "[MISSION] %04d refused: the game's files have no %s; starting normally\n",
                    s.mission, path);
            s.step = STEP_NONE;
            return;
        }
    }
    snprintf(dir, sizeof dir, "%s/%s", save_dir, MISSIONS_SAVES);
    xbox_path_set_user_dir(dir);
    fprintf(stderr, "[MISSION] saves in %s (E:\\UDATA and E:\\TDATA), apart from the player's own\n", dir);
}

int missions_starting(void)
{
    return s.step != STEP_NONE && s.step != STEP_DONE;
}

int missions_started(MISSIONS_START *out)
{
    if (s.step == STEP_NONE) return 0;
    out->mission = s.load ? -1 : s.mission;
    out->skater = s.load ? -1 : s.skater;
    out->progress = s.load ? 0u : s.progress;
    return 1;
}

static uint32_t *at(uint8_t *mem, uint32_t va)
{
    return (uint32_t *)(void *)(mem + va);
}

/* The game manager's variables, or NULL while it isn't there. */
static uint32_t *game_vars(uint8_t *mem, uint32_t size)
{
    uint32_t gm;
    if (MSN_GAME + 4u > size) return NULL;
    gm = *at(mem, MSN_GAME);
    if (gm < MSN_GUEST_LOW || gm >= size || size - gm < MSN_GM_VARS + 4u * (GV_LAST + 1u)) return NULL;
    return at(mem, gm + MSN_GM_VARS);
}

/* Souls held up to `want`: the lowest-numbered not yet held, each set
 * appeared, got and held, as the game's own pick-up does. Returns how many
 * it added. */
static unsigned fill_souls(uint32_t *p, unsigned want)
{
    uint32_t *held = p + PRG_SOULS_HELD / 4u;
    unsigned have = count_bits(held, SOUL_DWORDS), i, added = 0;
    for (i = 0; i < 32u * SOUL_DWORDS && have < want; i++) {
        uint32_t bit = 1u << (i & 31u);
        if (held[i >> 5] & bit) continue;
        p[PRG_SOULS_APPEARED / 4u + (i >> 5)] |= bit;
        p[PRG_SOULS_GOT / 4u + (i >> 5)] |= bit;
        held[i >> 5] |= bit;
        have++;
        added++;
    }
    return added;
}

/* The Noise Tanks' count up to `want`, the lowest-numbered bits first. */
static unsigned fill_tanks(uint32_t *p, unsigned want)
{
    uint32_t *t = p + PRG_TANKS / 4u;
    unsigned have = count_bits(t, TANK_DWORDS), i, added = 0;
    for (i = 0; i < 32u * TANK_DWORDS && have < want; i++) {
        uint32_t bit = 1u << (i & 31u);
        if (t[i >> 5] & bit) continue;
        t[i >> 5] |= bit;
        have++;
        added++;
    }
    return added;
}

/* What the progress object holds now that the start wrote, for the check at
 * state 0x1C. */
static uint32_t s_wrote[PRG_END / 4u];

static void write_progress(uint8_t *mem, double t)
{
    uint32_t *p = at(mem, MSN_PROGRESS);
    unsigned b, souls = 0, tanks = 0;
    int unlock;
    char skaters[160], said[64] = "";
    p[PRG_CHAPTER / 4u] = (uint32_t)s.chapter;
    if (s.line.have_skaters) p[PRG_SKATERS / 4u] = s.line.skaters;
    unlock = s.skater >= 0 && !(p[PRG_SKATERS / 4u] >> s.skater & 1u);
    if (unlock) p[PRG_SKATERS / 4u] |= 1u << s.skater;     /* as sub_00039CB0 unlocks one */
    if (unlock)
        snprintf(said, sizeof said, s.line.have_skaters ? " (%d unlocked, the skater given)"
                                                        : " (New Game's; %d unlocked, the skater given)", s.skater);
    else if (!s.line.have_skaters)
        snprintf(said, sizeof said, " (New Game's)");
    for (b = 0; b < BANKS; b++)
        if (s.line.have_bank[b]) memcpy(p + k_banks[b].at / 4u, s.line.bank[b], 4u * FLAG_DWORDS);
    if (s.line.souls) souls = fill_souls(p, s.line.souls);
    if (s.line.tanks) tanks = fill_tanks(p, s.line.tanks);
    memcpy(s_wrote, p, sizeof s_wrote);
    list_bits(skaters, sizeof skaters, p + PRG_SKATERS / 4u, 1);
    fprintf(stderr, "[MISSION] t=%.2f before the graffiti's resource (state 0x%X): chapter %d, skaters %s%s,"
            " Chapter flags %u%s, Global flags %u%s, bank 3 flags %u%s, souls held %u (%u added),"
            " Noise Tanks %u (%u added)\n",
            t, MSN_FLOW_GRAFFITI, s.chapter, skaters, said,
            count_bits(p + PRG_FLAGS_CHAPTER / 4u, FLAG_DWORDS), s.line.have_bank[BANK_CHAPTER] ? "" : " (New Game's)",
            count_bits(p + PRG_FLAGS_GLOBAL / 4u, FLAG_DWORDS), s.line.have_bank[BANK_GLOBAL] ? "" : " (New Game's)",
            count_bits(p + PRG_FLAGS_BANK3 / 4u, FLAG_DWORDS), s.line.have_bank[BANK_3] ? "" : " (New Game's)",
            count_bits(p + PRG_SOULS_HELD / 4u, SOUL_DWORDS), souls,
            count_bits(p + PRG_TANKS / 4u, TANK_DWORDS), tanks);
}

/* Which of what the start wrote at state 0x11 the game has changed since, for
 * the log: "" when nothing has. */
static void progress_changes(uint8_t *mem, char *out, size_t n)
{
    static const struct { const char *what; uint32_t at, words; } k_parts[] = {
        { "the chapter", PRG_CHAPTER, 1 },
        { "the skaters", PRG_SKATERS, 1 },
        { "the Chapter flags", PRG_FLAGS_CHAPTER, FLAG_DWORDS },
        { "the Global flags", PRG_FLAGS_GLOBAL, FLAG_DWORDS },
        { "the bank 3 flags", PRG_FLAGS_BANK3, FLAG_DWORDS },
        { "the souls", PRG_SOULS_APPEARED, 3 * SOUL_DWORDS },
        { "the Noise Tanks", PRG_TANKS, TANK_DWORDS },
    };
    const uint32_t *p = at(mem, MSN_PROGRESS);
    size_t i, used = 0;
    out[0] = '\0';
    for (i = 0; i < sizeof k_parts / sizeof k_parts[0] && used + 1 < n; i++)
        if (memcmp(p + k_parts[i].at / 4u, s_wrote + k_parts[i].at / 4u, 4u * k_parts[i].words))
            used += (size_t)snprintf(out + used, n - used, used ? ", %s" : "%s", k_parts[i].what);
}

void missions_flow(uint8_t *mem, uint32_t size, uint32_t flow, missions_title_fn title, double t)
{
    uint32_t *v, *state;
    if (!missions_starting() || flow < MSN_GUEST_LOW || flow >= size || size - flow < MSN_FLOW_STATE + 4u
        || size < MSN_PROGRESS + PRG_END)
        return;
    state = at(mem, flow + MSN_FLOW_STATE);
    switch (s.step) {
    case STEP_TITLE:
        if (*state != MSN_FLOW_TITLE) return;
        if (!(v = game_vars(mem, size))) {
            if (s.load)
                fprintf(stderr, "[MISSION] t=%.2f Load Game: the game manager isn't there at the title;"
                        " starting normally, with the saves still apart\n", t);
            else
                fprintf(stderr, "[MISSION] t=%.2f %04d: the game manager isn't there at the title;"
                        " starting normally, with the saves still apart\n", t, s.mission);
            s.step = STEP_NONE;
            return;
        }
        /* The title's state (0x7C160) but its scene task: its variables, its
         * own calls, and the title's menu's New Game, or its Load Game. */
        v[GV_TITLE_8] = 0;
        v[GV_SCENE_MODE] = SCENE_MODE_TITLE;
        v[GV_TITLE_MENU] = 0;
        v[GV_SCENE_LOADED] = 1;
        title();
        v[GV_TITLE_CHOICE] = s.load ? CHOICE_LOAD : CHOICE_NEW_GAME;
        *state = MSN_FLOW_NEW_GAME;
        s.step = s.load ? STEP_DONE : STEP_PROGRESS;
        fprintf(stderr, "[MISSION] t=%.2f the title (game flow state 0x%X) left out: %s chosen,"
                " on to state 0x%X\n", t, MSN_FLOW_TITLE, s.load ? "Load Game" : "New Game",
                MSN_FLOW_NEW_GAME);
        return;
    case STEP_PROGRESS:
        if (*state != MSN_FLOW_GRAFFITI) return;
        write_progress(mem, t);
        s.step = STEP_MISSION;
        return;
    case STEP_MISSION: {
        char changed[200], entrance[48], skater[48];
        if (*state != MSN_FLOW_SCENE) return;
        if (!(v = game_vars(mem, size))) {
            fprintf(stderr, "[MISSION] t=%.2f %04d: the game manager isn't there at state 0x%X;"
                    " the mission is New Game's\n", t, s.mission, MSN_FLOW_SCENE);
            s.step = STEP_DONE;
            return;
        }
        v[GV_CHAPTER] = (uint32_t)s.chapter;
        v[GV_NUMBER] = (uint32_t)s.number;
        if (s.line.have_entrance) v[GV_ENTRANCE] = s.line.entrance;
        if (s.line.have_entrance) snprintf(entrance, sizeof entrance, "%d", (int)s.line.entrance);
        else snprintf(entrance, sizeof entrance, "%d (as the game left it)", (int)v[GV_ENTRANCE]);
        if (s.skater >= 0) v[GV_SKATER] = (uint32_t)s.skater;
        snprintf(skater, sizeof skater, s.skater >= 0 ? "%d" : "%d (as the game left it)", (int)v[GV_SKATER]);
        progress_changes(mem, changed, sizeof changed);
        fprintf(stderr, "[MISSION] t=%.2f %04d started at game flow state 0x%X: chapter %d, mission %d,"
                " entrance %s, skater %s; the progress %s%s\n", t, s.mission, MSN_FLOW_SCENE, s.chapter,
                s.number, entrance, skater, changed[0] ? "changed since state 0x11: " : "as written at state 0x11",
                changed);
        s.step = STEP_DONE;
        s_place.armed = 1;
        return;
    }
    default:
        return;
    }
}

/* A word of guest memory, 0 outside it. */
static uint32_t word(const uint8_t *mem, uint32_t size, uint32_t va)
{
    uint32_t v;
    if (va < MSN_GUEST_LOW || va >= size || size - va < 4u) return 0;
    memcpy(&v, mem + va, 4u);
    return v;
}

/* A spot (SPOT_WORDS words) at `va` in guest memory, or NULL outside it. */
static uint32_t *spot_at(uint8_t *mem, uint32_t size, uint32_t va)
{
    if (va < MSN_GUEST_LOW || va >= size || size - va < 4u * SPOT_WORDS) return NULL;
    return at(mem, va);
}

/* Player 1's place, or another slot's, in the scene task's object: NULL
 * while it isn't there. */
static uint32_t *place_spot(uint8_t *mem, uint32_t size, uint32_t slot)
{
    const uint32_t gm = word(mem, size, MSN_GAME);
    const uint32_t places = gm ? word(mem, size, gm + MSN_GM_TASKS + 4u * TASK_PLACES) : 0;
    return places ? spot_at(mem, size, places + PLACES_AT + slot * PLACE_SIZE + PLACE_SPOT) : NULL;
}

static void say_spot(const char *what, const uint32_t *spot, const char *then, double t)
{
    float xyz[3];
    memcpy(xyz, spot, sizeof xyz);
    fprintf(stderr, "[MISSION] t=%.2f %04d: %s at (%.1f, %.1f, %.1f)%s\n", t, s.mission, what,
            xyz[0], xyz[1], xyz[2], then);
}

/* The slot a record 0x4C of the script gives a start, the lowest but player
 * 1's; 0 for none. */
static uint32_t start_slot(const uint8_t *mem, uint32_t size, uint32_t header)
{
    const uint32_t records = word(mem, size, header + HDR_SCRIPT);
    const uint32_t n = word(mem, size, header + HDR_SCRIPT + 4u);
    uint32_t i, best = 0;
    for (i = 0; i < n && i < SCRIPT_MOST; i++) {
        const uint32_t r = records + i * RECORD_SIZE, slot = word(mem, size, r + RECORD_SLOT);
        if (word(mem, size, r + RECORD_OP) == OP_START && slot > 0 && slot < PLACES && (!best || slot < best))
            best = slot;
    }
    return best;
}

/* Whether the script spawns player 1 (a record 0x48 for slot 0). */
static int spawns_player(const uint8_t *mem, uint32_t size, uint32_t header)
{
    const uint32_t records = word(mem, size, header + HDR_SCRIPT);
    const uint32_t n = word(mem, size, header + HDR_SCRIPT + 4u);
    uint32_t i;
    for (i = 0; i < n && i < SCRIPT_MOST; i++) {
        const uint32_t r = records + i * RECORD_SIZE;
        if (word(mem, size, r + RECORD_OP) == OP_SPAWN && word(mem, size, r + RECORD_SLOT) == 0)
            return 1;
    }
    return 0;
}

/* The start a record 0x4C has given `slot`: its entry of the start table
 * whose spot the slot's place now holds, or NULL. */
static const uint32_t *start_given(uint8_t *mem, uint32_t size, uint32_t header, uint32_t slot)
{
    const uint32_t records = word(mem, size, header + HDR_SCRIPT);
    const uint32_t n = word(mem, size, header + HDR_SCRIPT + 4u);
    const uint32_t table = word(mem, size, header + HDR_STARTS);
    const uint32_t entries = word(mem, size, header + HDR_STARTS + 4u);
    const uint32_t *place = place_spot(mem, size, slot), *start;
    uint32_t i, k;
    if (!place) return NULL;
    for (i = 0; i < n && i < SCRIPT_MOST; i++) {
        const uint32_t r = records + i * RECORD_SIZE;
        if (word(mem, size, r + RECORD_OP) != OP_START || word(mem, size, r + RECORD_SLOT) != slot) continue;
        k = word(mem, size, r + RECORD_ARG);
        if (k >= entries) continue;
        start = spot_at(mem, size, word(mem, size, table + 4u * k) + PLACE_SPOT);
        /* A start at the map's origin would be no better than none. */
        if (start && (start[0] | start[1] | start[2]) && !memcmp(start, place, 4u * SPOT_WORDS))
            return start;
    }
    return NULL;
}

void missions_script(uint8_t *mem, uint32_t size, uint32_t runner, int when, double t)
{
    uint32_t header, *p1;
    if (!s_place.armed) return;
    header = word(mem, size, runner + RUN_HEADER);
    if (!header) return;
    if (s_place.header && header != s_place.header) {
        /* The first mission is over: from here the game keeps the place. */
        if (s_place.slot)
            fprintf(stderr, "[MISSION] t=%.2f %04d: its script gave slot %u no start;"
                    " player 1 stays at the level start\n", t, s.mission, s_place.slot);
        s_place.armed = 0;
        return;
    }
    p1 = place_spot(mem, size, 0);
    if (!s_place.header) {
        const uint32_t entrance = word(mem, size, header + HDR_ENTRANCES);
        const uint32_t *spot = word(mem, size, header + HDR_ENTRANCES + 4u)
                             ? spot_at(mem, size, entrance + ENTRANCE_SPOT) : NULL;
        const uint32_t kind = word(mem, size, entrance);
        if (when != MISSIONS_BEFORE) return;
        s_place.header = header;
        s_place.armed = 0;
        if (!p1) {
            fprintf(stderr, "[MISSION] t=%.2f %04d: the scene task keeps no places yet;"
                    " player 1's left to the game\n", t, s.mission);
            return;
        }
        if (p1[0] | p1[1] | p1[2]) {
            say_spot("player 1's place already kept", p1, ": left alone", t);
            return;
        }
        if (!spot || (kind != ENTRANCE_KIND_SPOT && kind != ENTRANCE_KIND_SPOT2)) {
            fprintf(stderr, "[MISSION] t=%.2f %04d: its entrance 0 gives no position (%u entrances,"
                    " kind %u); player 1's place left empty\n", t, s.mission,
                    word(mem, size, header + HDR_ENTRANCES + 4u), kind);
            return;
        }
        /* A start keeps no place for player 1, where the game's own play
         * keeps where they stood: the level start in its stead. */
        memcpy(p1, spot, 4u * SPOT_WORDS);
        memcpy(s_place.wrote, spot, sizeof s_place.wrote);
        say_spot("player 1 has no place kept (a start keeps none): put at its level start,"
                 " entrance 0,", p1, "", t);
        if (!spawns_player(mem, size, header) && (s_place.slot = start_slot(mem, size, header)) != 0)
            s_place.armed = 1;      /* a scene with no player in it: where it starts, once its script says */
        return;
    }
    if (when != MISSIONS_AFTER) return;
    if (!p1 || memcmp(p1, s_place.wrote, sizeof s_place.wrote)) {
        fprintf(stderr, "[MISSION] t=%.2f %04d: player 1's place changed by the game; left to it\n",
                t, s.mission);
        s_place.armed = 0;
        return;
    }
    {
        const uint32_t *start = start_given(mem, size, header, s_place.slot);
        char what[128];
        if (!start) return;
        memcpy(p1, start, 4u * SPOT_WORDS);
        snprintf(what, sizeof what, "its script gave slot %u its start: player 1 put there, where the"
                 " scene starts,", s_place.slot);
        say_spot(what, p1, "", t);
        s_place.armed = 0;
    }
}
