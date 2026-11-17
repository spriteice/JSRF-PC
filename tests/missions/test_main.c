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

    missions_start();
    check("JSRF_START_MISSION taken out", getenv("JSRF_START_MISSION") == NULL);
    check("JSRF_START_STATE taken out", getenv("JSRF_START_STATE") == NULL);
    check("JSRF_START_SKATER taken out", getenv("JSRF_START_SKATER") == NULL);
    check("JSRF_START_LOAD taken out", getenv("JSRF_START_LOAD") == NULL);
    missions_paths("save");

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
    }
    printf("%s\n", s_failed ? "FAILED" : "passed");
    return s_failed;
}
