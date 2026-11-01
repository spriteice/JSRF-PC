/* Run with: python tests/resource_load/run.py
 *
 * Made-up guest memory for port/resource_load.c: a resource manager with its
 * file buffer at +0x268, and a game flow with its state at +0x5C, its
 * mission's file at +0x1040 and the stage copied from it at +0x104C, as the
 * game lays them out. A made-up heap says which addresses were released.
 * Each case runs one step's check, as the hooks on sub_00032C70 and
 * sub_0004AC80 do, and prints a "check" line for each check; run.py reads
 * those and the module's own log lines. The argument picks the case.
 */
#include <stdio.h>
#include <string.h>
#include "resource_load.h"

#define RAM        0x10000u   /* the made-up guest memory */
#define MGR        0x1000u    /* the resource manager */
#define FILE_BUF   0x268u
#define FLOW       0x3000u    /* the game flow */
#define STATE      0x5Cu
#define MISSION    0x58u
#define FILE_AT    0x1040u
#define STAGE      0x104Cu
#define MISSION_ID 0x000401A9u  /* mssn0425 */
#define STG20      0x00020000u  /* its stage */
#define MULT       0x544C554Du  /* the first word of a MULT file */
#define BUF        0x8000u    /* a file buffer */
#define ASKING     9u         /* the flow's state that asks for the stage */
#define ASKED      10u        /* the one after */
#define T          229.5

static uint8_t s_ram[RAM];
static int s_failed;
static int s_released;        /* what the made-up heap says of BUF */
static unsigned s_asked;      /* how often it was asked */

static void check(const char *what, int ok)
{
    printf("check: %s %s\n", what, ok ? "ok" : "FAILED");
    s_failed |= !ok;
}

static void put(uint32_t at, uint32_t v) { memcpy(s_ram + at, &v, 4); }
static uint32_t get(uint32_t at) { uint32_t v; memcpy(&v, s_ram + at, 4); return v; }

static int released(uint32_t va)
{
    s_asked++;
    return va == BUF && s_released;
}

static void flow(uint32_t state, uint32_t first_word, uint32_t copied)
{
    put(FLOW + MISSION, MISSION_ID);
    put(FLOW + STATE, state);
    put(FLOW + FILE_AT, BUF);
    put(BUF, first_word);
    put(FLOW + STAGE, copied);
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";

    if (!strcmp(c, "kept")) {            /* released, and the pointer kept */
        put(MGR + FILE_BUF, BUF);
        s_released = 1;
        resource_load_char_step(s_ram, MGR, released, T);
        check("a released buffer forgotten", get(MGR + FILE_BUF) == 0);
    } else if (!strcmp(c, "live")) {     /* a read in progress holds it */
        put(MGR + FILE_BUF, BUF);
        resource_load_char_step(s_ram, MGR, released, T);
        check("a live buffer kept", get(MGR + FILE_BUF) == BUF);
    } else if (!strcmp(c, "none")) {     /* nothing in the buffer */
        resource_load_char_step(s_ram, MGR, released, T);
        check("an empty buffer left alone", get(MGR + FILE_BUF) == 0 && s_asked == 0);
    } else if (!strcmp(c, "stage")) {    /* asked for, its file as it loaded */
        flow(ASKING, STG20, STG20);
        check("the state read", resource_load_flow_state(s_ram, FLOW) == ASKING);
        put(FLOW + STATE, ASKED);
        resource_load_stage_step(s_ram, FLOW, ASKING, T);
        check("the flow untouched", get(FLOW + STATE) == ASKED && get(BUF) == STG20);
    } else if (!strcmp(c, "written_over")) {   /* asked for, its file changed */
        flow(ASKED, MULT, STG20);
        resource_load_stage_step(s_ram, FLOW, ASKING, T);
        check("the flow untouched", get(FLOW + STATE) == ASKED && get(BUF) == MULT);
    } else if (!strcmp(c, "waiting")) {  /* the manager was busy: not asked yet */
        flow(ASKING, STG20, STG20);
        resource_load_stage_step(s_ram, FLOW, ASKING, T);
        check("still asking", get(FLOW + STATE) == ASKING);
    } else {
        printf("unknown case %s FAILED\n", c);
        return 2;
    }
    return s_failed;
}
