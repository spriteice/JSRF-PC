/* Run with: python tests/voice_follow/run.py
 *
 * Made-up guest memory for port/voice_follow.c: characters, each with its
 * voice bank at +0xC74 and where it is at +0xCA4, as the game lays them
 * out. Each case plays sounds as the hook on sub_00119540 reports them, and
 * runs frames as the hook on sub_001185B0 does, with made-up answers for
 * whether a sound still plays, and counts the moves asked for. It prints a
 * "check" line for each check, and the module's own log lines with
 * RECOMP_APU_STATS set, which run.py reads. The argument picks the case.
 */
#include <stdio.h>
#include <string.h>
#include "voice_follow.h"

#define RAM        0x100000u  /* the made-up guest memory */
#define CHR        0x10000u   /* the first character, and the next ones after it */
#define CHR_STRIDE 0x2000u
#define VOICE_BANK 0xC74u
#define POS        0xCA4u
#define BANK       13u        /* the first character's voice bank, and the next ones' after it */
#define OTHER_BANK 8u         /* a bank of the game's own sounds */
#define ESP        0x80000u   /* the caller's stack */
#define NEAR_ESP   0x100u     /* a local of the caller's */
#define NO_SOUND   0xFFFFFFFFu
#define LINE       2u         /* the line played */
#define FRAMES     90u        /* the frames a line plays: 1.5 s */
#define STEP       2.0f       /* how far the character skates each frame */
#define KEPT       16u        /* the lines the module keeps at once */

static uint8_t s_ram[RAM];
static int s_failed;
static unsigned s_frame;       /* frames run */
static unsigned s_plays_for;   /* frames a sound plays for */
static unsigned s_starts_at;   /* frames after it is played that it starts */
static unsigned s_played_at[KEPT + 2];   /* the frame each character's line was played */
static unsigned s_moves[KEPT + 2];   /* moves asked for, by character */
static unsigned s_asked;       /* playing asked */
static uint32_t s_wrong;       /* a move to a handle or place not played */
static uint32_t s_second;      /* a second line's handle, at the first character */

static void check(const char *what, int ok)
{
    printf("check: %s %s\n", what, ok ? "ok" : "FAILED");
    s_failed |= !ok;
}

static uint32_t chr(unsigned n) { return CHR + n * CHR_STRIDE; }
static uint32_t where(unsigned n) { return chr(n) + POS; }
static uint32_t handle(unsigned n) { return (BANK + n) << 16 | LINE; }

static void put(uint32_t at, uint32_t v) { memcpy(s_ram + at, &v, 4); }
static void putf(uint32_t at, float v) { memcpy(s_ram + at, &v, 4); }
static float getf(uint32_t at) { float v; memcpy(&v, s_ram + at, 4); return v; }

static void make_chr(unsigned n)
{
    put(chr(n) + VOICE_BANK, BANK + n);
    putf(where(n), 0.0f);
    putf(where(n) + 4, 0.0f);
    putf(where(n) + 8, 0.0f);
}

static int playing(uint32_t h)
{
    unsigned n = (h >> 16) - BANK, at = s_played_at[n < KEPT + 2 ? n : 0] + s_starts_at;
    s_asked++;
    return s_frame >= at && s_frame < at + s_plays_for;
}

static void move(uint32_t h, uint32_t at)
{
    unsigned n = (at - where(0)) / CHR_STRIDE;
    if (at < where(0) || (at - where(0)) % CHR_STRIDE || n >= KEPT + 2
        || (h != handle(n) && !(n == 0 && s_second && h == s_second)))
        s_wrong = at;
    else
        s_moves[n]++;
}

static void play(unsigned n)
{
    s_played_at[n] = s_frame;
    voice_follow_played(s_ram, RAM, handle(n), BANK + n, where(n), ESP);
}

/* Frames, the first character skating along x each one. */
static void run(unsigned frames)
{
    unsigned i;
    for (i = 0; i < frames; i++) {
        putf(where(0), getf(where(0)) + STEP);
        voice_follow_frame(s_ram, playing, move);
        s_frame++;
    }
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    unsigned n;

    for (n = 0; n < KEPT + 2; n++)
        make_chr(n);
    s_plays_for = FRAMES;
    if (!strcmp(c, "follow")) {          /* a line moved with its speaker until it ends */
        play(0);
        run(FRAMES / 9);
        play(1);                       /* and another speaker's, ending after it */
        run(FRAMES + 10);
        check("moved each frame it played", s_moves[0] == FRAMES);
        check("the other one too, after the first was let go", s_moves[1] == FRAMES);
        check("only where it was played, by its handle", !s_wrong);
        check("let go once it ended", s_asked == 2 * (FRAMES + 1));
    } else if (!strcmp(c, "not_voice")) {  /* a sound of another bank, at a character */
        voice_follow_played(s_ram, RAM, handle(0), OTHER_BANK, where(0), ESP);
        voice_follow_played(s_ram, RAM, NO_SOUND, BANK, where(0), ESP);
        voice_follow_played(s_ram, RAM, handle(0), BANK, 0, ESP);
        put(VOICE_BANK - 4, BANK);     /* a bank where one before memory's start would have it */
        voice_follow_played(s_ram, RAM, handle(0), BANK, POS - 4, ESP);
        put(RAM - 8 - POS + VOICE_BANK, BANK);   /* and one at memory's end */
        voice_follow_played(s_ram, RAM, handle(0), BANK, RAM - 8, ESP);
        voice_follow_played(s_ram, RAM, handle(0), BANK, where(0), where(0) + NEAR_ESP);
        voice_follow_played(s_ram, RAM, handle(0), BANK, where(0), where(0) - NEAR_ESP);
        run(FRAMES);
        check("none kept: another bank, nothing played, no place, out of memory, on the stack",
              !s_moves[0] && !s_asked && !s_wrong);
    } else if (!strcmp(c, "gone")) {     /* its speaker destroyed while it plays */
        play(0);
        play(1);                       /* another speaker's line, still playing after */
        s_second = handle(0) + 1;      /* and a second line of the first's */
        voice_follow_played(s_ram, RAM, s_second, BANK, where(0), ESP);
        run(FRAMES / 3);
        putf(where(0), 0.0f);          /* back where it began */
        run(1);
        voice_follow_gone(chr(0));     /* its memory left as it was, as freed memory is */
        run(FRAMES / 3);
        check("both its lines moved until it was destroyed", s_moves[0] == 2 * (FRAMES / 3 + 1));
        check("then let go, not asked again",
              s_asked == 2 * (FRAMES / 3 + 1) + 2 * (FRAMES / 3) + 1);
        check("the other speaker's line still moved", s_moves[1] == 2 * (FRAMES / 3) + 1);
        check("only where it was played, by its handle", !s_wrong);
    } else if (!strcmp(c, "late")) {     /* not playing yet on its first frame */
        s_starts_at = 1;
        play(0);
        run(FRAMES + 10);
        check("kept a frame more, then moved while it played", s_moves[0] == FRAMES);
        s_starts_at = 2;               /* and one that doesn't start in time */
        play(1);
        run(FRAMES);
        check("one not playing on its first two frames let go",
              !s_moves[1] && s_asked == FRAMES + 2 + 2);
    } else if (!strcmp(c, "again")) {    /* the same line again while it plays */
        play(0);
        run(FRAMES / 3);
        play(0);
        run(FRAMES + 1);
        check("kept once, until the second ended", s_moves[0] == FRAMES / 3 + FRAMES
              && s_asked == FRAMES / 3 + FRAMES + 1);
    } else if (!strcmp(c, "full")) {     /* more lines at once than are kept */
        s_plays_for = 2 * FRAMES;
        for (n = 0; n < KEPT; n++) {
            play(n);
            run(1);
        }
        play(0);                       /* the first played again, so kept for less */
        run(1);
        play(KEPT);
        run(1);
        check("the newest kept in place of the one kept longest", s_moves[KEPT] == 1
              && s_moves[1] == KEPT);
        check("the others still kept", s_moves[0] == KEPT + 2 && s_moves[KEPT - 1] == 3);
    } else if (!strcmp(c, "off")) {      /* nothing, with the switch unset */
        play(0);
        run(FRAMES);
        check("nothing kept with the switch off", !s_moves[0] && !s_asked);
    } else {
        printf("voice_follow: no case %s\n", c);
        return 1;
    }
    return s_failed;
}
