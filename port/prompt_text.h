/* The game's lines that name the A or B button by its letter, drawn with
 * the button prompts' picture of the control in the letter's place, in any
 * style but Default (spriteice, 12:14 to 12:33 UTC on 21 November 2026):
 * the line telling a player to press B to go back (the pause menu's
 * notebook and graffiti picker), Roboy's test run's line to press A to
 * start, the message box's save, load and memory messages, and the
 * graffiti save screen's lines, in each language the game has them in.
 * START, and the graffiti editor's help lines, are left as words.
 *
 * Every line goes through one of the game's two font calls: sub_0003D5D0,
 * which queues it for the font's draw late in the frame, or sub_0003D610,
 * which draws it at once (x, y, depth, colour, x scale, y scale, text;
 * cdecl). Their overrides (recomp_manual.c) hand each text here first
 * (prompt_text_match). A line is known by its content, compared with the
 * game's own bytes at the line's address in the table below: the message
 * box's are formats (sprintf's, with %d and %s), so its text is a copy
 * filled in, and a %d or %s in the line matches what was put there. Only
 * a line whose letter is still at the place the table says, in the game's
 * bytes, is matched; one that isn't is said once in the log and left as
 * it is.
 *
 * A matched line is drawn at once (prompt_text_draw), in pieces through
 * the call that draws at once, each letter's place left clear for its
 * picture: the words before it; the picture, a mark in D3D's push buffer
 * (nv2a_prompts.h, NV2A_PROMPT_MARK_METHOD) the renderer draws the
 * picture on, as the message box's flat A and B are (prompt_buttons.h);
 * then the rest. The font leaves its pen where a piece ends (the font
 * object's +0x4B118 and +0x4B11C), so each picture is where the letter
 * was; a new line in the game's text ($n) takes the pen back to the line's
 * start and one line (PROMPT_TEXT_CELL) down, so the words after a picture
 * are drawn from the picture to the line's end, and the lines after it
 * from the line's start again. A line its caller placed by its right end
 * (the go-back line: the game measures it, sub_0003D750) is moved left by
 * half of what the pictures add (PROMPT_TEXT_RIGHT_MOVE), so its middle
 * stays where the game's was, on the banner the game draws round its own
 * line: moved by all of it, it ended where the game's did but came off the
 * banner's left end (spriteice, 7:06pm on 22 November 2026, UTC+8). The
 * picture is a quarter larger than the talk box's at the font's scale 1
 * (PROMPT_TEXT_SIDE; spriteice, 23:44 UTC on 21 November 2026, as the talk
 * box's own were made a quarter larger), and NV2A_PROMPT_LARGER times that
 * (spriteice, 1:34pm on 22 November 2026, UTC+8), the words round it making
 * room; centred on the line, and fades with the text's colour.
 *
 * Nothing in the game's code is wrong: it is a feature's hook
 * (port/README.md), in the font calls, which recomp_manual.c already
 * overrides for widescreen. Plain C on guest memory, so a test links it
 * (tests/prompt_text). The game's thread. */
#ifndef JSRF_PROMPT_TEXT_H
#define JSRF_PROMPT_TEXT_H

#include <stdint.h>
#include "nv2a_prompts.h"          /* NV2A_PROMPT_LARGER */

#define PROMPT_TEXT_LETTERS 2        /* the most letters a line has */
#define PROMPT_TEXT_LONGEST 512u     /* the longest text compared, in bytes */
#define PROMPT_TEXT_CELL    26.0f    /* the font's line at scale 1: its new line's advance */
#define PROMPT_TEXT_SIDE    (30.0f * NV2A_PROMPT_LARGER) /* the picture's side at scale 1: the talk box's flat button's, a quarter larger, then NV2A_PROMPT_LARGER */
#define PROMPT_TEXT_PAD     2.0f     /* clear on each side of it, at scale 1 */
#define PROMPT_TEXT_RIGHT_MOVE 0.5f  /* a line placed by its right end: moved left by this share of what its pictures add */
#define PROMPT_TEXT_PEN_X   0x4B118u /* the font object's pen after a draw at once: x */
#define PROMPT_TEXT_PEN_Y   0x4B11Cu /*   and y, the top of its line */

/* A line of the game's, at va: whether its caller places it by its right
 * end, and each letter's byte in it, whether it is the full-width letter
 * (two bytes, Shift-JIS) and the button (0 the A, 1 the B). */
typedef struct {
    uint32_t va;
    uint8_t  right, n;
    struct { uint16_t at; uint8_t wide, button; } letter[PROMPT_TEXT_LETTERS];
} PromptTextLine;

/* The table (prompt_text.c). */
extern const PromptTextLine prompt_text_lines[];
extern const int prompt_text_nlines;

/* A text that is one of the lines: which, its length in bytes, whether it
 * is right-aligned, and each letter's byte in the text and its button. */
typedef struct {
    int      line, right, n;
    uint32_t len;
    struct { uint32_t at, bytes; int button; } letter[PROMPT_TEXT_LETTERS];
} PromptTextMatch;

/* Whether the text at text, in guest memory mem of top bytes, is one of
 * table's n lines: 1 with *m filled in, 0 for any other text. A line whose
 * letter isn't in the game's bytes where the table says is never matched,
 * and said once in the log. */
int prompt_text_match(const PromptTextLine *table, int n, const uint8_t *mem, uint32_t top, uint32_t text,
                      PromptTextMatch *m);

/* What a draw of a matched line asks of the game, all with ctx. */
typedef struct {
    /* The text at va drawn at once at (x, y), with the call's own depth,
     * colour and scales: 1 with the pen where it ended, 0 when the font
     * isn't there to draw it. */
    int (*draw)(void *ctx, uint32_t va, float x, float y, float *pen_x, float *pen_y);
    /* The game's measure of the text at va at scale (sub_0003D750). */
    float (*measure)(void *ctx, uint32_t va, float scale);
    /* The picture of button (0 the A, 1 the B), its middle at (x, y) and
     * half its side half, in the title's pixels: a mark. */
    void (*mark)(void *ctx, int button, float x, float y, float half);
    void *ctx;
} PromptTextCalls;

/* The matched line m, its text at text, drawn from (x, y) at the scales xs
 * and ys, as described above, with each piece's text copied to scratch, a
 * guest buffer of m->len + 1 bytes or more. Returns the pictures marked;
 * 0 when the font isn't there, which leaves the rest undrawn as the game's
 * own call would. */
int prompt_text_draw(const PromptTextMatch *m, uint8_t *mem, uint32_t text, uint32_t scratch, float x,
                     float y, float xs, float ys, const PromptTextCalls *c);

#endif
