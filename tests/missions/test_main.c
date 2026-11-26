/* Run with: python tests/missions/run.py
 *
 * Made-up guest memory for port/missions.c: the game manager's pointer at
 * 0x22FCE0 and its variables at +0x7868, the progress object at 0x1EFFB0,
 * and a game flow object with its state at +0x48, as the game lays them out.
 * Each case reads the environment run.py gives it (missions_start), and
 * start-states.txt beside its settings file through the engine's own
 * settings code (run.py puts it in front of this file), asks the path step
 * (missions_paths) with a made-up path layer standing in for the kernel's,
 * and walks the game flow through its states as the game would, New Game's
 * reset included (Load Game's state 0xE resets nothing), calling
 * missions_flow at each as the hook on sub_0007BDD0 does. It prints a
 * "check" line for each check; run.py reads those and the module's own log
 * lines. The argument picks the case.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel.h"
#include "missions.h"

#define RAM          0x240000u   /* the made-up guest memory */
#define GAME_PTR     0x22FCE0u   /* -> the game manager */
#define GM           0x230000u   /* the game manager */
#define VARS         (GM + 0x7868u)
#define FLOW         0x20000u    /* the game flow object */
#define STATE        0x48u
#define PROGRESS     0x1EFFB0u
#define P_CHAPTER    0x0004u
#define P_SKATERS    0x0014u
#define P_CHAPTER_F  0x0018u
#define P_GLOBAL_F   0x0058u
#define P_BANK3_F    0x0098u
#define P_APPEARED   0x00D8u
#define P_GOT        0x00F8u
#define P_HELD       0x0118u
#define P_TANKS      0x2A1Cu
#define V_CHAPTER    0x002u
#define V_NUMBER     0x003u
#define V_8          0x008u
#define V_9          0x009u
#define V_B          0x00Bu
#define V_SKATER     0x00Du
#define V_ENTRANCE   0x04Du
#define V_19A        0x19Au
#define V_1CC        0x1CCu
#define T            12.5
/* The stage's script, for player 1's place (missions_script): task slot
 * 0xB's object, with 32 places of 0x34 bytes at +0x544 (x, y, z and the turn
 * at +0x0C); a scene's header (a rival's preview, which doesn't spawn player
 * 1) and the stage's that follows it, each with its entrances at +0x3C (0x2C
 * bytes, the spot at +8), its start table at +0xE4 and its script at +0x174
 * (0x54-byte records: the op at +0x10, the slot at +0x14, the start at
 * +0x18); and the script's runner, its header at +0x1040. */
#define PLACES_OBJ   0x100000u
#define PLACE(slot)  (PLACES_OBJ + 0x544u + 0x34u * (slot))
#define SPOT         0x0Cu
#define SCENE        0x110000u
#define STAGE        0x120000u
#define RUNNER       0x130000u
#define H_ENTRANCES  0x3Cu
#define H_STARTS     0xE4u
#define H_SCRIPT     0x174u
#define ENTRANCES_AT 0x1000u     /* the header's tables, after it */
#define STARTS_AT    0x2000u
#define START_PLACES 0x3000u
#define SCRIPT_AT    0x4000u
#define LEVEL_X      100.5f      /* entrance 0's x; its y, z and turn below */
#define START_X      700.5f      /* start k's x: + 100 k */
/* New Game, as its reset leaves the progress: chapter 1, skaters 1, 2 and 6
 * (Yoyo), and its 20 souls. */
#define NEW_GAME_SKATERS ((1u << 1) | (1u << 2) | (1u << 6))
static const unsigned k_new_game_souls[] = {
    0, 1, 2, 3, 28, 29, 30, 31, 56, 57, 58, 59, 84, 85, 86, 87, 112, 113, 114, 115
};

static _Alignas(16) uint8_t s_ram[RAM];   /* as guest memory is, for its words */
static int s_failed;
static const char *s_case;
static int s_title_calls;
static uint32_t s_after_title;  /* the flow's state once the title's frame is done */
static char s_user_dir[512];
static char s_asked[128];

static void check(const char *what, int ok)
{
    printf("check: %s %s\n", what, ok ? "ok" : "FAILED");
    s_failed |= !ok;
}

static void put(uint32_t at, uint32_t v) { memcpy(s_ram + at, &v, 4); }
static uint32_t get(uint32_t at) { uint32_t v; memcpy(&v, s_ram + at, 4); return v; }
static uint32_t var(uint32_t i) { return get(VARS + 4u * i); }
static int bit(uint32_t base, unsigned n) { return get(base + 4u * (n >> 5)) >> (n & 31u) & 1u; }
static unsigned count(uint32_t base, unsigned words)
{
    unsigned i, n = 0;
    for (i = 0; i < 32u * words; i++) n += (unsigned)bit(base, i);
    return n;
}

/* The kernel's path layer, made up: every mission's file is there but in
 * the case "no_file", and the user dir given is kept. */
NTSTATUS __stdcall xbox_NtQueryFullAttributesFile(PXBOX_OBJECT_ATTRIBUTES oa,
                                                  PXBOX_FILE_NETWORK_OPEN_INFORMATION info)
{
    (void)info;
    snprintf(s_asked, sizeof s_asked, "%.*s", (int)oa->ObjectName->Length, oa->ObjectName->Buffer);
    return strcmp(s_case, "no_file") ? STATUS_SUCCESS : STATUS_OBJECT_NAME_NOT_FOUND;
}

void xbox_path_set_user_dir(const char *dir)
{
    snprintf(s_user_dir, sizeof s_user_dir, "%s", dir);
}

/* State 0xA's own calls, which the hook makes: by then the title's variables
 * are written, and the choice and the state not yet. */
static void title(void)
{
    s_title_calls++;
    check("the title's variables written before its calls",
          var(V_8) == 0 && var(V_9) == 0 && var(V_B) == 1 && var(V_19A) == 1);
    check("the choice and the state not yet", var(V_1CC) == 0xFFu && get(FLOW + STATE) == 0xA);
}

/* ---- the stage's script (missions_script) ---- */

static void putf(uint32_t at, float f) { memcpy(s_ram + at, &f, 4); }
static float getf(uint32_t at) { float f; memcpy(&f, s_ram + at, 4); return f; }

/* A mission's header at h: two entrances, the first the level start; three
 * starts; and the script's records, each {op, slot, start}. */
static void header(uint32_t h, const uint32_t (*ops)[3], unsigned n)
{
    unsigned i;
    put(h + H_ENTRANCES, h + ENTRANCES_AT);
    put(h + H_ENTRANCES + 4u, 2);
    for (i = 0; i < 2; i++) {
        const uint32_t e = h + ENTRANCES_AT + 0x2Cu * i;
        put(e, 0);                          /* a kind that gives a position */
        putf(e + 8u, LEVEL_X + 10.0f * (float)i);
        putf(e + 0xCu, 20.0f);
        putf(e + 0x10u, 300.5f);
        put(e + 0x14u, 0);
        put(e + 0x18u, 0xFFFFC000u);        /* the turn about y */
        put(e + 0x1Cu, 0);
    }
    put(h + H_STARTS, h + STARTS_AT);
    put(h + H_STARTS + 4u, 3);
    for (i = 0; i < 3; i++) {
        const uint32_t place = h + START_PLACES + 0x40u * i;
        put(h + STARTS_AT + 4u * i, place);
        put(place, 1);
        putf(place + SPOT, START_X + 100.0f * (float)i);
        putf(place + SPOT + 4u, -40.0f);
        putf(place + SPOT + 8u, 50.5f);
        put(place + SPOT + 0x10u, 0x2000u);
        put(place + 0x2Cu, 0x34u);
    }
    put(h + H_SCRIPT, h + SCRIPT_AT);
    put(h + H_SCRIPT + 4u, n);
    for (i = 0; i < n; i++) {
        const uint32_t r = h + SCRIPT_AT + 0x54u * i;
        put(r + 0x10u, ops[i][0]);
        put(r + 0x14u, ops[i][1]);
        put(r + 0x18u, ops[i][2]);
    }
}

/* A rival's preview: a record handed back to the caller (0xCE), the rival
 * placed (0x4F), and its start by the story's flags (0x4C, one for each);
 * the stage after it spawns player 1 (0x48). */
static const uint32_t k_scene[][3] = {
    { 0xCE, 0, 1 }, { 0x4F, 1, 0 }, { 0x4C, 1, 0 }, { 0x4C, 1, 1 }, { 0x4C, 1, 2 }, { 0xA6, 5, 0 },
};
static const uint32_t k_stage[][3] = { { 0xCE, 0, 1 }, { 0x48, 0, 0 }, { 0x4C, 1, 0 } };

/* One run of the script at header h, as the hook on sub_0005B3F0 makes it:
 * before it, the game's (which gives `slot` start k when slot >= 0), and
 * after it. */
static void run_script(uint32_t h, int slot, unsigned k)
{
    put(RUNNER + 0x1040u, h);
    missions_script(s_ram, RAM, RUNNER, MISSIONS_BEFORE, T);
    if (slot >= 0)
        memcpy(s_ram + PLACE((unsigned)slot), s_ram + get(h + STARTS_AT + 4u * k), 0x34u);
    missions_script(s_ram, RAM, RUNNER, MISSIONS_AFTER, T);
}

/* Whether player 1's place is at x (then entrance 0's or a start's y, z and
 * turn), only the spot written. */
static int p1_at(float x, int level)
{
    return getf(PLACE(0) + SPOT) == x && get(PLACE(0)) == 0 && get(PLACE(0) + 0x2Cu) == 0
           && (level ? getf(PLACE(0) + SPOT + 4u) == 20.0f && getf(PLACE(0) + SPOT + 8u) == 300.5f
                       && get(PLACE(0) + SPOT + 0x10u) == 0xFFFFC000u
                     : getf(PLACE(0) + SPOT + 4u) == -40.0f && getf(PLACE(0) + SPOT + 8u) == 50.5f
                       && get(PLACE(0) + SPOT + 0x10u) == 0x2000u);
}

static int p1_empty(void)
{
    static const uint8_t zero[0x34];
    return !memcmp(s_ram + PLACE(0), zero, sizeof zero);
}

/* The scene, then the stage after it: as a start plays them, with the
 * story's flags giving the rival its start 1 in the scene's second run
 * (unless `none`), the rival moving off in the third, and the stage giving
 * it its own start. */
static void scene_then_stage(int none)
{
    header(SCENE, k_scene, sizeof k_scene / sizeof k_scene[0]);
    header(STAGE, k_stage, sizeof k_stage / sizeof k_stage[0]);
    run_script(SCENE, -1, 0);
    run_script(SCENE, none ? -1 : 1, 1);
    putf(PLACE(1) + SPOT, 9999.0f);
    run_script(SCENE, -1, 0);
    run_script(STAGE, 1, 0);
    run_script(STAGE, -1, 0);
}

/* The script's runs where nothing may write player 1's place. */
static void script_left_alone(void)
{
    scene_then_stage(0);
    check("player 1's place left as the game has it", p1_empty());
}

/* One frame of the game flow at `state`: the hook, then the game's own state
 * as far as the start sees it. */
static void frame(uint32_t state)
{
    put(FLOW + STATE, state);
    missions_flow(s_ram, RAM, FLOW, title, T);
}

/* New Game's reset at state 0xE: the progress as a new game has it. */
static void new_game(void)
{
    size_t i;
    memset(s_ram + PROGRESS, 0, 0x3000);
    put(PROGRESS + P_CHAPTER, 1);
    put(PROGRESS + P_SKATERS, NEW_GAME_SKATERS);
    for (i = 0; i < sizeof k_new_game_souls / sizeof k_new_game_souls[0]; i++) {
        unsigned n = k_new_game_souls[i];
        uint32_t m = 1u << (n & 31u), w = 4u * (n >> 5);
        put(PROGRESS + P_APPEARED + w, get(PROGRESS + P_APPEARED + w) | m);
        put(PROGRESS + P_GOT + w, get(PROGRESS + P_GOT + w) | m);
        put(PROGRESS + P_HELD + w, get(PROGRESS + P_HELD + w) | m);
    }
    put(VARS + 4u * V_CHAPTER, 1);
    put(VARS + 4u * V_NUMBER, 1);
    put(VARS + 4u * V_SKATER, 6);        /* Yoyo */
}

/* The boot to the mission, as the game goes: the intro's states, the title,
 * New Game, the graffiti's resource, the entrances' reset, the scene. */
static void play(void)
{
    frame(7);
    frame(9);
    frame(0xA);
    s_after_title = get(FLOW + STATE);
    if (s_after_title == 0xE && var(V_1CC) == 0) new_game();   /* Load Game's 0xE resets nothing */
    frame(0x11);
    if (!strcmp(s_case, "changed"))      /* something writes a Global flag in between */
        put(PROGRESS + P_GLOBAL_F, get(PROGRESS + P_GLOBAL_F) | 1u << 5);
    frame(0x12);
    put(VARS + 4u * V_ENTRANCE, 0);      /* state 0x13's sub_0004EAF0 */
    frame(0x13);
    frame(0x1C);
    frame(0x1D);
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    s_case = argv[1];
    put(GAME_PTR, GM);
    put(VARS + 4u * V_8, 0xFFu);
    put(VARS + 4u * V_9, 0xFFu);
    put(VARS + 4u * V_B, 0xFFu);
    put(VARS + 4u * V_19A, 0xFFu);
    put(VARS + 4u * V_1CC, 0xFFu);
    put(VARS + 4u * V_ENTRANCE, 0x55u);
    put(VARS + 4u * V_SKATER, 0x55u);
    if (!strcmp(s_case, "no_game_manager")) put(GAME_PTR, 0);
    if (strcmp(s_case, "place_none")) put(GM + 0x98u + 4u * 0xBu, PLACES_OBJ);

    const char *load_var = getenv("JSRF_START_LOAD");
    int asked_load = load_var && !strcmp(load_var, "1");
    missions_start();
    check("JSRF_START_MISSION taken out", getenv("JSRF_START_MISSION") == NULL);
    check("JSRF_START_STATE taken out", getenv("JSRF_START_STATE") == NULL);
    check("JSRF_START_SKATER taken out", getenv("JSRF_START_SKATER") == NULL);
    check("JSRF_START_LOAD taken out", getenv("JSRF_START_LOAD") == NULL);
    missions_paths("save");
    /* What a recording made now would note (port/playback.c): run.py
     * compares each case's progress with the others'. */
    {
        MISSIONS_START st;
        int at = missions_started(&st);
        if (!strcmp(s_case, "refused") || !strcmp(s_case, "no_file") || !strcmp(s_case, "nothing"))
            check("not started at anything", !at);
        else if (asked_load)
            check("started at Load Game", at && st.mission == -1 && st.skater == -1 && st.progress == 0);
        else {
            check("started at 0110", at && st.mission == 110);
            printf("started: skater %d, progress %08X\n", at ? st.skater : -2, at ? (unsigned)st.progress : 0u);
        }
    }

    if (!strcmp(s_case, "order")) {
        /* The later states before the title: nothing is written. */
        frame(0x11);
        frame(0x1C);
        check("nothing written before the title",
              get(PROGRESS + P_CHAPTER) == 0 && var(V_NUMBER) == 0 && var(V_ENTRANCE) == 0x55u);
    }
    play();

    if (!strcmp(s_case, "refused") || !strcmp(s_case, "no_file") || !strcmp(s_case, "nothing")
        || !strcmp(s_case, "no_game_manager")) {
        check("not starting", !missions_starting());
        check("the title left to the game", s_title_calls == 0 && var(V_1CC) == 0xFFu);
        check("the skater left to the game", var(V_SKATER) != 2u);
        /* Once the path step has put them apart, they stay apart. */
        if (!strcmp(s_case, "no_game_manager"))
            check("the saves still apart", !strcmp(s_user_dir, "save/" MISSIONS_SAVES));
        else
            check("the saves where they were", s_user_dir[0] == '\0');
        if (!strcmp(s_case, "no_file"))
            check("the mission's file asked for", !strcmp(s_asked, "D:\\Media\\Mission\\mssn0110.bin"));
        script_left_alone();
        printf("%s\n", s_failed ? "FAILED" : "passed");
        return s_failed;
    }

    if (!strcmp(s_case, "load")) {
        /* Load Game: the title's part, Load Game chosen, and nothing written. */
        check("no mission's file asked for", s_asked[0] == '\0');
        check("the saves apart", !strcmp(s_user_dir, "save/" MISSIONS_SAVES));
        check("the title's calls made once", s_title_calls == 1);
        check("Load Game chosen, on to state 0xE", var(V_1CC) == 1 && s_after_title == 0xE);
        check("done", !missions_starting());
        check("nothing written", get(PROGRESS + P_CHAPTER) == 0 && get(PROGRESS + P_SKATERS) == 0
              && var(V_CHAPTER) == 0 && var(V_NUMBER) == 0 && var(V_SKATER) == 0x55u);
        script_left_alone();
        printf("%s\n", s_failed ? "FAILED" : "passed");
        return s_failed;
    }

    check("the mission's file asked for", !strcmp(s_asked, "D:\\Media\\Mission\\mssn0110.bin"));
    check("the saves apart", !strcmp(s_user_dir, "save/" MISSIONS_SAVES));
    check("the title's calls made once", s_title_calls == 1);
    check("New Game chosen", var(V_1CC) == 0);
    check("the mission started", !missions_starting());
    check("the chapter in the progress", get(PROGRESS + P_CHAPTER) == 1);
    check("the chapter and the mission", var(V_CHAPTER) == 1 && var(V_NUMBER) == 10);

    if (!strcmp(s_case, "full") || !strcmp(s_case, "changed") || !strcmp(s_case, "own_line")) {
        unsigned i;
        int lowest = 1;
        check("the entrance", var(V_ENTRANCE) == 7);
        check("the skaters", get(PROGRESS + P_SKATERS) == NEW_GAME_SKATERS);
        check("the Chapter flags", count(PROGRESS + P_CHAPTER_F, 16) == 2
              && bit(PROGRESS + P_CHAPTER_F, 0) && bit(PROGRESS + P_CHAPTER_F, 2));
        check("the Global flags", bit(PROGRESS + P_GLOBAL_F, 1)
              && count(PROGRESS + P_GLOBAL_F, 16) == (strcmp(s_case, "changed") ? 1u : 2u));
        check("the bank 3 flags", count(PROGRESS + P_BANK3_F, 16) == 3 && bit(PROGRESS + P_BANK3_F, 384)
              && bit(PROGRESS + P_BANK3_F, 450) && bit(PROGRESS + P_BANK3_F, 511));
        check("25 souls held", count(PROGRESS + P_HELD, 8) == 25);
        for (i = 4; i <= 8; i++)
            lowest &= bit(PROGRESS + P_APPEARED, i) && bit(PROGRESS + P_GOT, i) && bit(PROGRESS + P_HELD, i);
        check("the five added the lowest not held, appeared, got and held", lowest
              && count(PROGRESS + P_APPEARED, 8) == 25 && count(PROGRESS + P_GOT, 8) == 25);
        check("200 Noise Tanks, the lowest", count(PROGRESS + P_TANKS, 32) == 200
              && bit(PROGRESS + P_TANKS, 199) && !bit(PROGRESS + P_TANKS, 200));
    } else if (!strcmp(s_case, "no_state") || !strcmp(s_case, "order") || !strcmp(s_case, "skater")) {
        check("the entrance as the game left it", var(V_ENTRANCE) == 0);
        check("the progress New Game's", get(PROGRESS + P_SKATERS) == NEW_GAME_SKATERS
              && count(PROGRESS + P_HELD, 8) == 20 && count(PROGRESS + P_TANKS, 32) == 0
              && count(PROGRESS + P_GLOBAL_F, 16) == 0);
        check(strcmp(s_case, "skater") ? "the skater New Game's" : "the skater given",
              var(V_SKATER) == (strcmp(s_case, "skater") ? 6u : 2u));
    } else if (!strcmp(s_case, "skater_locked") || !strcmp(s_case, "skater_last")) {
        /* A skater New Game hasn't unlocked: unlocked, as the game's own
         * unlock does, and the rest as New Game has them. */
        unsigned want = strcmp(s_case, "skater_last") ? 3u : 23u;
        check("the skater given", var(V_SKATER) == want);
        check("the skater given unlocked, with New Game's",
              get(PROGRESS + P_SKATERS) == (NEW_GAME_SKATERS | 1u << want));
    } else if (!strcmp(s_case, "skater_line")) {
        /* The line's skaters, Yoyo locked, and the skater given, Yoyo. */
        check("the skater given", var(V_SKATER) == 6u);
        check("the line's skaters and the skater given",
              get(PROGRESS + P_SKATERS) == ((1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 6)));
    } else if (!strcmp(s_case, "no_entrance")) {
        check("the entrance as the game left it", var(V_ENTRANCE) == 0);
        check("no Chapter flags given: New Game's", count(PROGRESS + P_CHAPTER_F, 16) == 0);
        check("souls already held: none added", count(PROGRESS + P_HELD, 8) == 20);
        check("Yoyo locked", get(PROGRESS + P_SKATERS) == ((1u << 0) | (1u << 1) | (1u << 2) | (1u << 3)));
    } else if (!strcmp(s_case, "exit_names_none")) {
        check("the entrance -1", var(V_ENTRANCE) == 0xFFFFFFFFu);
    } else if (!strcmp(s_case, "place_scene")) {
        /* A rival's preview, then the stage it hands back to with entrance
         * -1: player 1 at the level start before the scene's script runs,
         * then at the rival's start once the script gives it, and the place
         * left to the game from there. */
        header(SCENE, k_scene, sizeof k_scene / sizeof k_scene[0]);
        header(STAGE, k_stage, sizeof k_stage / sizeof k_stage[0]);
        put(RUNNER + 0x1040u, SCENE);
        missions_script(s_ram, RAM, RUNNER, MISSIONS_BEFORE, T);
        check("player 1 at the level start before the scene's script runs", p1_at(LEVEL_X, 1));
        missions_script(s_ram, RAM, RUNNER, MISSIONS_AFTER, T);
        check("still there while the rival has no start", p1_at(LEVEL_X, 1));
        run_script(SCENE, 1, 1);
        check("player 1 at the rival's start, where the scene starts", p1_at(START_X + 100.0f, 0));
        putf(PLACE(1) + SPOT, 9999.0f);
        run_script(SCENE, -1, 0);
        check("not where the rival goes", p1_at(START_X + 100.0f, 0));
        run_script(STAGE, 1, 0);
        putf(PLACE(0) + SPOT, 5.0f);         /* where the game has player 1 go */
        run_script(STAGE, -1, 0);
        check("the place the game's from the stage on", getf(PLACE(0) + SPOT) == 5.0f);
    } else if (!strcmp(s_case, "place_stage")) {
        /* A mission that spawns player 1 (a race after a preview, started
         * at e=-1): the level start, and not a rival's. */
        header(STAGE, k_stage, sizeof k_stage / sizeof k_stage[0]);
        header(SCENE, k_scene, sizeof k_scene / sizeof k_scene[0]);
        run_script(STAGE, -1, 0);
        check("player 1 at the level start", p1_at(LEVEL_X, 1));
        run_script(STAGE, 1, 0);
        run_script(SCENE, 1, 1);
        check("still there, the rival's starts given", p1_at(LEVEL_X, 1));
    } else if (!strcmp(s_case, "place_no_start")) {
        /* No start given (its flags not set): the level start stays. */
        scene_then_stage(1);
        check("player 1 at the level start", p1_at(LEVEL_X, 1));
    } else if (!strcmp(s_case, "place_kept")) {
        putf(PLACE(0) + SPOT, 1.0f);
        putf(PLACE(0) + SPOT + 4u, 2.0f);
        putf(PLACE(0) + SPOT + 8u, 3.0f);
        scene_then_stage(0);
        check("player 1's place as it was", getf(PLACE(0) + SPOT) == 1.0f && getf(PLACE(0) + SPOT + 4u) == 2.0f);
    } else if (!strcmp(s_case, "place_changed")) {
        /* The game writes player 1's place in the scene: left to it. */
        header(SCENE, k_scene, sizeof k_scene / sizeof k_scene[0]);
        run_script(SCENE, -1, 0);
        putf(PLACE(0) + SPOT, 5.0f);
        run_script(SCENE, 1, 0);
        run_script(SCENE, 1, 2);
        check("player 1's place the game's", getf(PLACE(0) + SPOT) == 5.0f && getf(PLACE(0) + SPOT + 4u) == 20.0f);
    } else if (!strcmp(s_case, "place_none")) {
        scene_then_stage(0);
        check("no places to write", get(GM + 0x98u + 4u * 0xBu) == 0 && p1_empty());
    } else if (!strcmp(s_case, "place_no_entrance")) {
        /* An entrance 0 of a kind that gives no position. */
        header(SCENE, k_scene, sizeof k_scene / sizeof k_scene[0]);
        put(SCENE + ENTRANCES_AT, 1);
        run_script(SCENE, -1, 0);
        run_script(SCENE, 1, 0);
        check("player 1's place left empty", p1_empty());
    }
    printf("%s\n", s_failed ? "FAILED" : "passed");
    return s_failed;
}
