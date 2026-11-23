/* The game's lines that name a control, drawn with the button prompts'
 * picture of it in the name's place, in any style but Default (spriteice,
 * 12:14 to 12:33 UTC on 21 November 2026, the A and the B; 02:35 UTC on 22
 * November, the graffiti editor's help lines): the line telling a player
 * to press B to go back (the pause menu's notebook and graffiti picker),
 * Roboy's test run's line to press A to start, the message box's save,
 * load and memory messages, the graffiti save screen's lines, and the help
 * line that scrolls along the top of the graffiti editor for the tool in
 * hand, in each language the game has them in. The A and the B are named
 * by their letter; the help lines name the D-pad, the left thumbstick and
 * the two triggers as well, in words ("the D-pad", "bouton
 * multidirectionnel", "Steuerkreuz"), and the words are what the picture
 * takes the place of, an article before them kept ("use the [picture] to
 * change the color"). START is left as a word.
 *
 * Every line goes through one of the game's two font calls: sub_0003D5D0,
 * which queues it for the font's draw late in the frame, or sub_0003D610,
 * which draws it at once (x, y, depth, colour, x scale, y scale, text;
 * cdecl). Their overrides (recomp_manual.c) hand each text here first
 * (prompt_text_match). A line is known by its content, compared with the
 * game's own bytes at the line's address in the table below: the message
 * box's are formats (sprintf's, with %d and %s), so its text is a copy
 * filled in, and a %d or %s in the line matches what was put there. Only
 * a line whose names are still at the places the table says, in the
 * game's bytes (each name's bytes by their check, prompt_text_check), is
 * matched; one that isn't is said once in the log and left as it is.
 *
 * What stands in a name's place (prompt_text_show) is what presses the
 * control now, as the icons show it (src/input/input_prompts.h): for the A,
 * the B and a trigger, the picture of the first input on it. The D-pad and
 * the left stick are named whole, or by two of their ways ("press right or
 * left", "up and down"): in a pad's style, one picture of that pad's D-pad
 * or stick with those ways lit (src/input/prompt_art.h, PROMPT_GROUP_...),
 * or, where a layout has put one of the D-pad's ways named on something
 * else, each of those ways' own picture side by side; in Keyboard, the key
 * on each way named, side by side (spriteice's card, 03:09 UTC on 22
 * November 2026: "keys side by side"), up, left, down and right, or the
 * Xbox pad's picture where no key is on any of them. German's eraser line
 * names the right stick where every other language's, and the game, use
 * the left: it gets the left stick's picture.
 *
 * A matched line is drawn at once (prompt_text_draw), in pieces through
 * the call that draws at once, each name's place left clear for its
 * pictures: the words before it; the pictures, each a mark in D3D's push
 * buffer (nv2a_prompts.h, NV2A_PROMPT_MARK_METHOD) the renderer draws the
 * picture on, as the message box's flat A and B are (prompt_buttons.h);
 * then the rest. The font leaves its pen where a piece ends (the font
 * object's +0x4B118 and +0x4B11C), so each picture is where the name
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
 * The editor's help line scrolls: the game measures it (sub_0003D750) to
 * know where its next copy starts and when to start again, so that
 * measure's override gives the line's width with its pictures
 * (prompt_text_width, the pieces measured as they are drawn), and each
 * copy carries its own pictures along at the text's pace.
 *
 * Nothing in the game's code is wrong: it is a feature's hook
 * (port/README.md), in the font calls, which recomp_manual.c already
 * overrides for widescreen. Plain C on guest memory, so a test links it
 * (tests/prompt_text). The game's thread. */
#ifndef JSRF_PROMPT_TEXT_H
#define JSRF_PROMPT_TEXT_H

#include <stdint.h>
#include "nv2a_prompts.h"          /* NV2A_PROMPT_LARGER, the marks */
#include "input_layout.h"

#define PROMPT_TEXT_NAMES    4       /* the most controls a line names */
#define PROMPT_TEXT_PICTURES 4       /* the most pictures in one name's place: a way each */
#define PROMPT_TEXT_LONGEST 512u     /* the longest text compared, in bytes */
#define PROMPT_TEXT_CELL    26.0f    /* the font's line at scale 1: its new line's advance */
#define PROMPT_TEXT_SIDE    (30.0f * NV2A_PROMPT_LARGER) /* the picture's side at scale 1: the talk box's flat button's, a quarter larger, then NV2A_PROMPT_LARGER */
#define PROMPT_TEXT_PAD     2.0f     /* clear on each side of a name's pictures, at scale 1 */
#define PROMPT_TEXT_RIGHT_MOVE 0.5f  /* a line placed by its right end: moved left by this share of what its pictures add */
#define PROMPT_TEXT_PEN_X   0x4B118u /* the font object's pen after a draw at once: x */
#define PROMPT_TEXT_PEN_Y   0x4B11Cu /*   and y, the top of its line */

/* What a line names: the A or the B button, a trigger, or the D-pad or the
 * left stick, whole, by its left and right, or by its up and down. */
enum {
    PROMPT_TEXT_A, PROMPT_TEXT_B, PROMPT_TEXT_LTRIGGER, PROMPT_TEXT_RTRIGGER,
    PROMPT_TEXT_DPAD, PROMPT_TEXT_DPAD_ACROSS, PROMPT_TEXT_DPAD_UPDOWN,
    PROMPT_TEXT_LSTICK, PROMPT_TEXT_LSTICK_ACROSS, PROMPT_TEXT_LSTICK_UPDOWN,
    PROMPT_TEXT_WHATS
};

/* A line of the game's, at va: whether its caller places it by its right
 * end, and each name in it, in the line's order: its first byte, how many
 * bytes it is (the letter A is one, the full-width one two, Shift-JIS; a
 * control's name in words more), what it names (PROMPT_TEXT_...) and its
 * bytes' check (prompt_text_check). */
typedef struct {
    uint32_t va;
    uint8_t  right, n;
    struct { uint16_t at; uint8_t bytes, what; uint16_t check; } name[PROMPT_TEXT_NAMES];
} PromptTextLine;

/* The table (prompt_text.c). */
extern const PromptTextLine prompt_text_lines[];
extern const int prompt_text_nlines;

/* A name's bytes' check: n bytes at bytes, folded to 16 bits (FNV-1a). */
uint16_t prompt_text_check(const uint8_t *bytes, uint32_t n);

/* A picture in a name's place: what its mark shows (nv2a_prompts.h: a
 * control, or NV2A_PROMPT_MARK_GROUP and a group) and the art it is in. */
typedef struct { uint8_t show, art; } PromptTextPicture;

/* A text that is one of the lines: which, its length in bytes, whether it
 * is right-aligned, and each name's byte in the text, its length and what
 * it names; and, once prompt_text_show has been asked, the pictures in its
 * place, pics of them. */
typedef struct {
    int      line, right, n;
    uint32_t len;
    struct {
        uint32_t at, bytes;
        int what, pics;
        PromptTextPicture pic[PROMPT_TEXT_PICTURES];
    } name[PROMPT_TEXT_NAMES];
} PromptTextMatch;

/* Whether the text at text, in guest memory mem of top bytes, is one of
 * table's n lines: 1 with *m filled in (its pictures the control's own in
 * no art yet: prompt_text_show), 0 for any other text. A line whose names
 * aren't in the game's bytes where the table says is never matched, and
 * said once in the log. */
int prompt_text_match(const PromptTextLine *table, int n, const uint8_t *mem, uint32_t top, uint32_t text,
                      PromptTextMatch *m);

/* The pictures that stand for what (PROMPT_TEXT_...) in art (any but the
 * game's own) with the layouts pad and keys, as described above, into out:
 * returns how many, 1 to PROMPT_TEXT_PICTURES. */
int prompt_text_pictures(int what, int art, const InputLayout *pad, const InputLayout *keys,
                         PromptTextPicture out[PROMPT_TEXT_PICTURES]);

/* Every name of *m given its pictures (prompt_text_pictures). */
void prompt_text_show(PromptTextMatch *m, int art, const InputLayout *pad, const InputLayout *keys);

/* What a draw of a matched line asks of the game, all with ctx. */
typedef struct {
    /* The text at va drawn at once at (x, y), with the call's own depth,
     * colour and scales: 1 with the pen where it ended, 0 when the font
     * isn't there to draw it. */
    int (*draw)(void *ctx, uint32_t va, float x, float y, float *pen_x, float *pen_y);
    /* The game's measure of the text at va at scale (sub_0003D750). */
    float (*measure)(void *ctx, uint32_t va, float scale);
    /* A picture, its middle at (x, y) and half its side half, in the
     * title's pixels: a mark. */
    void (*mark)(void *ctx, PromptTextPicture pic, float x, float y, float half);
    void *ctx;
} PromptTextCalls;

/* The matched line m, its text at text, drawn from (x, y) at the scales xs
 * and ys, as described above, with each piece's text copied to scratch, a
 * guest buffer of m->len + 1 bytes or more. Returns the pictures marked;
 * 0 when the font isn't there, which leaves the rest undrawn as the game's
 * own call would. */
int prompt_text_draw(const PromptTextMatch *m, uint8_t *mem, uint32_t text, uint32_t scratch, float x,
                     float y, float xs, float ys, const PromptTextCalls *c);

/* How wide the matched line m is drawn so, at the scales xs and ys: its
 * pieces as the game measures them (c->measure, each through scratch, as
 * above) and its pictures' room. For a line of one row (the help lines
 * have no new line). */
float prompt_text_width(const PromptTextMatch *m, uint8_t *mem, uint32_t text, uint32_t scratch, float xs,
                        float ys, const PromptTextCalls *c);

#endif
