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
 * pictures: the words before it, the rest, and then the pictures, each a
 * mark in D3D's push buffer (nv2a_prompts.h, NV2A_PROMPT_MARK_METHOD) the
 * renderer draws the picture on, as the message box's flat A and B are
 * (prompt_buttons.h). They come after the line's last piece so that each
 * is drawn after its letters, and the font call's override marks the line
 * around them (NV2A_PROMPT_LINE_TAG) so the renderer draws them at the
 * letters' depth: a screen the game draws after the text, behind it, can't
 * cover them, as it can't the letters (the loading screen held under the
 * graffiti error box, spriteice, 12:21pm on 27 November 2026, UTC+8: "We
 * need to make the button show here"), and what covers the text, a fade,
 * covers them too. The font leaves its pen where a piece ends (the font
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
 * The game's loaded text is the other half (spriteice, 8:30pm on 24
 * November 2026, UTC+8: known bug 81, the tutorial). The talks' lines and
 * the missions' messages are in the game's files (Media/Event/TalkEvent's
 * TE<n>.bin and Media/Mission's mssn<n>.bin, in Japanese and English),
 * which the game loads into memory it allocates, so none of them has an
 * address for the table above. They name the A, B, X and Y buttons, the
 * triggers and the left thumbstick in words, mostly in quote marks and a
 * colour, so they are found by the names themselves: any text that isn't
 * one of default.xbe's own strings (PROMPT_TEXT_OWN_LOW to _HIGH, the
 * table's) is searched for the names in prompt_text_names
 * (prompt_text_find), a character at a time, as the font reads it. The
 * picture takes a button's letter, the word for button staying, as in the
 * table's lines, and a trigger's or the stick's words; the quote marks and
 * the colour stay the game's, and START stays a word. A name in a colour
 * keeps it: each piece after a picture starts with the colour code in
 * force there, as each font call starts in its own colour. The talk window
 * types its page out a character at a time, so the rest of the page not
 * yet typed can be given (more): a name the text's end cuts is found
 * through it and drawn whole, its picture coming in as the typing reaches
 * it. A text whose names are found is measured with its pictures too
 * (prompt_text_width), so a line the game centres by its measure stays
 * centred.
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
#define PROMPT_TEXT_COLOUR  3u       /* a colour code's bytes: $c and its digit */
#define PROMPT_TEXT_CELL    26.0f    /* the font's line at scale 1: its new line's advance */
#define PROMPT_TEXT_SIDE    (30.0f * NV2A_PROMPT_LARGER) /* the picture's side at scale 1: the talk box's flat button's, a quarter larger, then NV2A_PROMPT_LARGER */
#define PROMPT_TEXT_PAD     2.0f     /* clear on each side of a name's pictures, at scale 1 */
#define PROMPT_TEXT_RIGHT_MOVE 0.5f  /* a line placed by its right end: moved left by this share of what its pictures add */
#define PROMPT_TEXT_PEN_X   0x4B118u /* the font object's pen after a draw at once: x */
#define PROMPT_TEXT_PEN_Y   0x4B11Cu /*   and y, the top of its line */

/* What a line names: the A, B, X or Y button, a trigger, or the D-pad or
 * the left stick, whole, by its left and right, or by its up and down. */
enum {
    PROMPT_TEXT_A, PROMPT_TEXT_B, PROMPT_TEXT_X, PROMPT_TEXT_Y, PROMPT_TEXT_LTRIGGER, PROMPT_TEXT_RTRIGGER,
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

/* A text that is one of the lines: which (PROMPT_TEXT_FOUND for names
 * found in the game's loaded text), its length in bytes, whether it is
 * right-aligned, and each name's byte in the text, how many of the text's
 * bytes its picture takes (fewer than the name's where the text's end cuts
 * it) and what it names; and, once prompt_text_show has been asked, the
 * pictures in its place, pics of them. */
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

/* ---- the game's loaded text (above) ---- */

#define PROMPT_TEXT_FOUND    (-1)    /* a match's line: its names found by prompt_text_find, no table line's */
#define PROMPT_TEXT_OWN_LOW  0x001C3F60u /* default.xbe's read-only data, its own strings (the US release): */
#define PROMPT_TEXT_OWN_HIGH 0x001EB760u /*   the table's, never searched for names */
#define PROMPT_TEXT_LEAD_LOW  0x80u  /* a byte from here ... */
#define PROMPT_TEXT_LEAD_HIGH 0x9Fu  /* ... to here starts a two-byte character, as the font reads one */

/* A name the game's loaded text uses: its bytes, how many, how many of
 * them from its first the picture takes (a letter, or the words), what it
 * names (PROMPT_TEXT_...), and whether it is in English, found only as a
 * word of its own (no letter or digit just before it or just after). */
typedef struct {
    const char *bytes;
    uint8_t len, take, what, word;
} PromptTextName;

/* The names (prompt_text.c). */
extern const PromptTextName prompt_text_names[];
extern const int prompt_text_nnames;

/* Whether the text at text in guest memory mem of top bytes, not one of
 * default.xbe's own strings, names a control with one of names' n names:
 * 1 with *m filled in (its line PROMPT_TEXT_FOUND, its first
 * PROMPT_TEXT_NAMES names in the text's order, their pictures the
 * control's own in no art yet: prompt_text_show), 0 for no name. A name
 * after those is left as words, and the first text with one is said once
 * in the log, by its address. more, if
 * not 0, is where the text goes on (the talk window's page not yet typed):
 * a name the text's end cuts is found through it, its bytes in the text
 * the picture's. */
int prompt_text_find(const PromptTextName *names, int n, const uint8_t *mem, uint32_t top, uint32_t text,
                     uint32_t more, PromptTextMatch *m);

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
 * guest buffer of m->len + PROMPT_TEXT_COLOUR + 1 bytes or more: a piece
 * after a picture starts with the colour code in force there. The pictures
 * are marked after the last piece, in the line's order. Returns the
 * pictures marked;
 * 0 when the font isn't there, which leaves the rest undrawn as the game's
 * own call would, and marks none. */
int prompt_text_draw(const PromptTextMatch *m, uint8_t *mem, uint32_t text, uint32_t scratch, float x,
                     float y, float xs, float ys, const PromptTextCalls *c);

/* How wide the matched line m is drawn so, at the scales xs and ys: its
 * pieces as the game measures them (c->measure, each through scratch, as
 * above) and its pictures' room. For a line of one row (the help lines
 * have no new line). */
float prompt_text_width(const PromptTextMatch *m, uint8_t *mem, uint32_t text, uint32_t scratch, float xs,
                        float ys, const PromptTextCalls *c);

#endif
