/* Run with: python tests/prompt_text/run.py
 *
 * The game's lines naming a control, drawn with the button prompts'
 * pictures in the names' places (port/prompt_text.c), without the game:
 * made-up lines, in a buffer that stands in for guest memory, and a
 * made-up font, each character 10 wide at scale 1, that says what it was
 * asked to draw and where its pen ended.
 *
 * - table: the game's table's shape: one to four names a line, in order,
 *   none over the next, at addresses in the game's read-only data, none
 *   twice, the go-back lines, the first five, the only ones placed by
 *   their right end, and the graffiti editor's 72 help lines there;
 * - check: a name's bytes' check, the letters' as the table has them;
 * - match: a line is known by its bytes, a format's %d and %s matching what
 *   was put there, with each name's place in the text; a text that differs
 *   anywhere isn't, nor one outside guest memory, nor any text where the
 *   line's own name isn't where its table says (said once);
 * - pictures: what stands in a name's place in each art: a button's or a
 *   trigger's own picture; a pad's whole D-pad or stick, each way's own
 *   where a D-pad's way named is on something else; the keys on each way
 *   named, in the order shown, or the Xbox pad's picture with none;
 * - draw: the pieces and the pictures, on one line and over several, a
 *   name first, two names, a name in words, several pictures in one place,
 *   a line placed by its right end, and a font that isn't there;
 * - width: a line's width with its pictures, the draw's own end;
 * - mark: the mark's fade, read back.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "prompt_text.h"
#include "nv2a_prompts.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
#define CLOSE(a, b) (fabsf((float)(a) - (float)(b)) < 1e-3f * (1.0f + fabsf((float)(b))))

#define TOP     (1u << 20)
#define LINES   0x20000u     /* the made-up lines, 0x100 apart */
#define TEXT_AT 0x40000u     /* the text a call is given (not TEXT: the Windows
                              * headers' own macro, which prompt_text.h's
                              * includes bring in there) */
#define SCRATCH 0x50000u     /* the pieces' text */

static uint8_t *mem;

static uint32_t put_text(uint32_t va, const char *s)
{
    memcpy(mem + va, s, strlen(s) + 1);
    return va;
}

/* ---- the table ------------------------------------------------------------ */

static void case_table(void)
{
    int i, j, k, help = 0, count[PROMPT_TEXT_WHATS] = { 0 };
    CHECK(prompt_text_nlines >= 5 && prompt_text_nlines <= 256);
    for (i = 0; i < prompt_text_nlines; i++) {
        const PromptTextLine *l = &prompt_text_lines[i];
        int words = 0;
        CHECK(l->va >= 0x1C3F60u && l->va < 0x1EB760u);   /* .rdata and .data */
        CHECK(l->n >= 1 && l->n <= PROMPT_TEXT_NAMES);
        CHECK(l->right == (i < 5));
        for (k = 0; k < l->n; k++) {
            CHECK(l->name[k].what < PROMPT_TEXT_WHATS && l->name[k].bytes >= 1);
            /* A letter is one byte, or two full-width; a name in words more. */
            if (l->name[k].what > PROMPT_TEXT_B) words = 1;
            else if (l->name[k].bytes > 2) words = 1;
            if (k) CHECK(l->name[k].at > l->name[k - 1].at + l->name[k - 1].bytes);
            count[l->name[k].what]++;
        }
        help += words;
        for (j = 0; j < i; j++) CHECK(prompt_text_lines[j].va != l->va);
    }
    /* The graffiti editor's help lines: twelve a language, six languages,
     * each naming the D-pad, the left stick or a trigger in words, or the A
     * as "the A button"; and every kind of name the pictures have a use
     * for is in them. */
    CHECK(help == 72);
    CHECK(count[PROMPT_TEXT_LTRIGGER] == 12 && count[PROMPT_TEXT_RTRIGGER] == 18);
    CHECK(count[PROMPT_TEXT_DPAD] && count[PROMPT_TEXT_DPAD_ACROSS] && count[PROMPT_TEXT_LSTICK]
          && count[PROMPT_TEXT_LSTICK_UPDOWN]);
    printf("prompt_text table: pass\n");
}

/* ---- a name's check ------------------------------------------------------- */

static void case_check(void)
{
    /* The letters, as every line of the table that names one has them. */
    CHECK(prompt_text_check((const uint8_t *)"A", 1) == 0x32C7u);
    CHECK(prompt_text_check((const uint8_t *)"B", 1) == 0x3C8Eu);
    CHECK(prompt_text_check((const uint8_t *)"\x82\x60", 2) == 0x1981u);
    CHECK(prompt_text_check((const uint8_t *)"\x82\x61", 2) == 0x1F12u);
    /* Other bytes, another check; and the count is part of it. */
    CHECK(prompt_text_check((const uint8_t *)"the D-pad", 9) != prompt_text_check((const uint8_t *)"the D-pat", 9));
    CHECK(prompt_text_check((const uint8_t *)"the D-pad", 9) != prompt_text_check((const uint8_t *)"the D-pad", 8));
    printf("prompt_text check: pass\n");
}

/* ---- matching ------------------------------------------------------------- */

/* Made-up lines: a plain one, B at 9; a full-width A first; a format with
 * both letters, A at 13 after a %d and B at 27 after a %s; one whose name
 * isn't where it says (its 4 is a space); and a help line, the D-pad's
 * left and right in words at 6 and the left trigger at 30. Each name's
 * check is worked out from the line (lines, below), but the fourth's. */
static PromptTextLine k_lines[] = {
    { LINES + 0x000u, 1, 1, { { 9, 1, PROMPT_TEXT_B, 0 } } },
    { LINES + 0x100u, 0, 1, { { 0, 2, PROMPT_TEXT_A, 0 } } },
    { LINES + 0x200u, 0, 2, { { 13, 1, PROMPT_TEXT_A, 0 }, { 27, 1, PROMPT_TEXT_B, 0 } } },
    { LINES + 0x300u, 0, 1, { { 4, 1, PROMPT_TEXT_A, 0 } } },
    { LINES + 0x400u, 0, 2, { { 6, 9, PROMPT_TEXT_DPAD_ACROSS, 0 }, { 30, 6, PROMPT_TEXT_LTRIGGER, 0 } } },
};
#define NLINES ((int)(sizeof k_lines / sizeof k_lines[0]))
#define LINE_ASTRAY 3

static void lines(void)
{
    int i, k;
    memset(mem, 0, TOP);
    put_text(LINES + 0x000u, "Tap the  B key to leave.");
    put_text(LINES + 0x100u, "\x82\x60 starts it");
    put_text(LINES + 0x200u, "Need %d. Hit A$nor in <%s> B to stop.");
    put_text(LINES + 0x300u, "Hit the A key");
    put_text(LINES + 0x400u, "Press the D-pad to pick, hold L-trig to go.  ");
    for (i = 0; i < NLINES; i++)
        for (k = 0; k < k_lines[i].n; k++)
            k_lines[i].name[k].check = i == LINE_ASTRAY ? prompt_text_check((const uint8_t *)"A", 1)
                : prompt_text_check(mem + k_lines[i].va + k_lines[i].name[k].at, k_lines[i].name[k].bytes);
}

static void case_match(void)
{
    PromptTextMatch m;
    lines();
    /* The line itself, wherever its text is. */
    put_text(TEXT_AT, "Tap the  B key to leave.");
    CHECK(prompt_text_match(k_lines, NLINES, mem, TOP, TEXT_AT, &m));
    CHECK(m.line == 0 && m.right && m.n == 1 && m.len == 24);
    CHECK(m.name[0].at == 9 && m.name[0].bytes == 1 && m.name[0].what == PROMPT_TEXT_B && m.name[0].pics == 0);
    CHECK(prompt_text_match(k_lines, NLINES, mem, TOP, LINES, &m) && m.line == 0);
    /* Not it: a byte changed, longer, shorter. */
    put_text(TEXT_AT, "Tap the  B key to leave!");
    CHECK(!prompt_text_match(k_lines, NLINES, mem, TOP, TEXT_AT, &m));
    put_text(TEXT_AT, "Tap the  B key to leave..");
    CHECK(!prompt_text_match(k_lines, NLINES, mem, TOP, TEXT_AT, &m));
    put_text(TEXT_AT, "Tap the  B key");
    CHECK(!prompt_text_match(k_lines, NLINES, mem, TOP, TEXT_AT, &m));
    /* The full-width letter: two bytes. */
    put_text(TEXT_AT, "\x82\x60 starts it");
    CHECK(prompt_text_match(k_lines, NLINES, mem, TOP, TEXT_AT, &m));
    CHECK(m.line == 1 && !m.right && m.name[0].at == 0 && m.name[0].bytes == 2
          && m.name[0].what == PROMPT_TEXT_A);
    /* The format, filled in: the letters where the text has them. */
    put_text(TEXT_AT, "Need 1234. Hit A$nor in <Unit 2> B to stop.");
    CHECK(prompt_text_match(k_lines, NLINES, mem, TOP, TEXT_AT, &m));
    CHECK(m.line == 2 && m.n == 2 && m.len == 43);
    CHECK(m.name[0].at == 15 && m.name[0].what == PROMPT_TEXT_A && m.name[1].at == 33
          && m.name[1].what == PROMPT_TEXT_B);
    put_text(TEXT_AT, "Need -7. Hit A$nor in <> B to stop.");
    CHECK(prompt_text_match(k_lines, NLINES, mem, TOP, TEXT_AT, &m) && m.name[1].at == 25);
    put_text(TEXT_AT, "Need many. Hit A$nor in <Unit 2> B to stop.");   /* no number */
    CHECK(!prompt_text_match(k_lines, NLINES, mem, TOP, TEXT_AT, &m));
    put_text(TEXT_AT, "Need 3. Hit A$nor in <Unit 2 B to stop.");       /* no > */
    CHECK(!prompt_text_match(k_lines, NLINES, mem, TOP, TEXT_AT, &m));
    /* The help line: its names in words, each with its bytes. */
    CHECK(prompt_text_match(k_lines, NLINES, mem, TOP, LINES + 0x400u, &m));
    CHECK(m.line == 4 && m.n == 2 && m.len == 45);
    CHECK(m.name[0].at == 6 && m.name[0].bytes == 9 && m.name[0].what == PROMPT_TEXT_DPAD_ACROSS);
    CHECK(m.name[1].at == 30 && m.name[1].bytes == 6 && m.name[1].what == PROMPT_TEXT_LTRIGGER);
    /* The line whose name isn't there: never, said once. */
    put_text(TEXT_AT, "Hit the A key");
    CHECK(!prompt_text_match(k_lines, NLINES, mem, TOP, TEXT_AT, &m));
    CHECK(!prompt_text_match(k_lines, NLINES, mem, TOP, TEXT_AT, &m));
    /* Outside guest memory, empty, or past its end. */
    CHECK(!prompt_text_match(k_lines, NLINES, mem, TOP, 0x100u, &m));
    CHECK(!prompt_text_match(k_lines, NLINES, mem, TOP, TOP, &m));
    put_text(TEXT_AT, "");
    CHECK(!prompt_text_match(k_lines, NLINES, mem, TOP, TEXT_AT, &m));
    printf("prompt_text match: pass\n");
}

/* ---- what stands in a name's place ---------------------------------------- */

/* A pad's layout with its D-pad where it is, and the keys' with W, A, S and
 * D on the left stick and the arrows on the D-pad, as the defaults have
 * them (src/input/input_layout.c). */
static void layouts(InputLayout *pad, InputLayout *keys)
{
    int c, b;
    for (c = 0; c < INPUT_CTLS; c++)
        for (b = 0; b < INPUT_BINDS; b++) pad->bind[c][b] = keys->bind[c][b] = INPUT_NONE;
    pad->bind[INPUT_CTL_A][0] = INPUT_PAD_A;
    pad->bind[INPUT_CTL_LTRIGGER][0] = INPUT_PAD_LTRIGGER;
    pad->bind[INPUT_CTL_DPAD_UP][0] = INPUT_PAD_DPAD_UP;
    pad->bind[INPUT_CTL_DPAD_DOWN][0] = INPUT_PAD_DPAD_DOWN;
    pad->bind[INPUT_CTL_DPAD_LEFT][0] = INPUT_PAD_DPAD_LEFT;
    pad->bind[INPUT_CTL_DPAD_RIGHT][0] = INPUT_PAD_DPAD_RIGHT;
    keys->bind[INPUT_CTL_LSTICK_UP][0] = INPUT_KEY_W;
    keys->bind[INPUT_CTL_LSTICK_LEFT][0] = INPUT_KEY_A;
    keys->bind[INPUT_CTL_LSTICK_DOWN][0] = INPUT_KEY_S;
    keys->bind[INPUT_CTL_LSTICK_RIGHT][0] = INPUT_KEY_D;
    keys->bind[INPUT_CTL_DPAD_UP][0] = INPUT_KEY_UP;
    keys->bind[INPUT_CTL_DPAD_LEFT][0] = INPUT_KEY_LEFT;
    keys->bind[INPUT_CTL_DPAD_DOWN][0] = INPUT_KEY_DOWN;
    keys->bind[INPUT_CTL_DPAD_RIGHT][0] = INPUT_KEY_RIGHT;
}

static int pic_is(PromptTextPicture p, int show, int art)
{
    return p.show == show && p.art == art;
}

static void case_pictures(void)
{
    static const int pads[] = { PROMPT_ART_XBOX, PROMPT_ART_PLAYSTATION, PROMPT_ART_NINTENDO };
    static const struct { int what, group; } wholes[] = {
        { PROMPT_TEXT_DPAD, PROMPT_GROUP_DPAD }, { PROMPT_TEXT_DPAD_ACROSS, PROMPT_GROUP_DPAD_ACROSS },
        { PROMPT_TEXT_DPAD_UPDOWN, PROMPT_GROUP_DPAD_UPDOWN }, { PROMPT_TEXT_LSTICK, PROMPT_GROUP_LSTICK },
        { PROMPT_TEXT_LSTICK_ACROSS, PROMPT_GROUP_LSTICK_ACROSS },
        { PROMPT_TEXT_LSTICK_UPDOWN, PROMPT_GROUP_LSTICK_UPDOWN },
    };
    static const struct { int what, control; } ones[] = {
        { PROMPT_TEXT_A, INPUT_CTL_A }, { PROMPT_TEXT_B, INPUT_CTL_B },
        { PROMPT_TEXT_LTRIGGER, INPUT_CTL_LTRIGGER }, { PROMPT_TEXT_RTRIGGER, INPUT_CTL_RTRIGGER },
    };
    InputLayout pad, keys;
    PromptTextPicture p[PROMPT_TEXT_PICTURES];
    PromptTextMatch m;
    size_t a, i;
    layouts(&pad, &keys);
    /* A button or a trigger: its own picture, in every art, the keys' too. */
    for (i = 0; i < sizeof ones / sizeof ones[0]; i++) {
        for (a = 0; a < sizeof pads / sizeof pads[0]; a++)
            CHECK(prompt_text_pictures(ones[i].what, pads[a], &pad, &keys, p) == 1
                  && pic_is(p[0], ones[i].control, pads[a]));
        CHECK(prompt_text_pictures(ones[i].what, PROMPT_ART_KEYS, &pad, &keys, p) == 1
              && pic_is(p[0], ones[i].control, PROMPT_ART_KEYS));
    }
    /* A pad's D-pad or stick, whole or by two of its ways: one picture. */
    for (i = 0; i < sizeof wholes / sizeof wholes[0]; i++)
        for (a = 0; a < sizeof pads / sizeof pads[0]; a++)
            CHECK(prompt_text_pictures(wholes[i].what, pads[a], &pad, &keys, p) == 1
                  && pic_is(p[0], NV2A_PROMPT_MARK_GROUP + wholes[i].group, pads[a]));
    /* The D-pad's right on another button: the ways named that have it
     * shown each on its own, up, left, down, right; up and down, which
     * haven't, still one picture; and the stick's, which a pad's layout
     * doesn't move, one picture. */
    pad.bind[INPUT_CTL_DPAD_RIGHT][0] = INPUT_PAD_Y;
    CHECK(prompt_text_pictures(PROMPT_TEXT_DPAD_ACROSS, PROMPT_ART_PLAYSTATION, &pad, &keys, p) == 2);
    CHECK(pic_is(p[0], INPUT_CTL_DPAD_LEFT, PROMPT_ART_PLAYSTATION)
          && pic_is(p[1], INPUT_CTL_DPAD_RIGHT, PROMPT_ART_PLAYSTATION));
    CHECK(prompt_text_pictures(PROMPT_TEXT_DPAD, PROMPT_ART_XBOX, &pad, &keys, p) == 4);
    CHECK(pic_is(p[0], INPUT_CTL_DPAD_UP, PROMPT_ART_XBOX) && pic_is(p[1], INPUT_CTL_DPAD_LEFT, PROMPT_ART_XBOX)
          && pic_is(p[2], INPUT_CTL_DPAD_DOWN, PROMPT_ART_XBOX)
          && pic_is(p[3], INPUT_CTL_DPAD_RIGHT, PROMPT_ART_XBOX));
    CHECK(prompt_text_pictures(PROMPT_TEXT_DPAD_UPDOWN, PROMPT_ART_XBOX, &pad, &keys, p) == 1
          && pic_is(p[0], NV2A_PROMPT_MARK_GROUP + PROMPT_GROUP_DPAD_UPDOWN, PROMPT_ART_XBOX));
    CHECK(prompt_text_pictures(PROMPT_TEXT_LSTICK, PROMPT_ART_XBOX, &pad, &keys, p) == 1
          && pic_is(p[0], NV2A_PROMPT_MARK_GROUP + PROMPT_GROUP_LSTICK, PROMPT_ART_XBOX));
    /* A way with nothing on it is on something else too. */
    pad.bind[INPUT_CTL_DPAD_RIGHT][0] = INPUT_NONE;
    CHECK(prompt_text_pictures(PROMPT_TEXT_DPAD_ACROSS, PROMPT_ART_NINTENDO, &pad, &keys, p) == 2);
    /* Its second input alone is the way's first. */
    pad.bind[INPUT_CTL_DPAD_RIGHT][1] = INPUT_PAD_DPAD_RIGHT;
    CHECK(prompt_text_pictures(PROMPT_TEXT_DPAD_ACROSS, PROMPT_ART_NINTENDO, &pad, &keys, p) == 1);
    layouts(&pad, &keys);

    /* The keys: the key on each way named, side by side, in the order
     * shown. */
    CHECK(prompt_text_pictures(PROMPT_TEXT_LSTICK, PROMPT_ART_KEYS, &pad, &keys, p) == 4);
    CHECK(pic_is(p[0], INPUT_CTL_LSTICK_UP, PROMPT_ART_KEYS) && pic_is(p[1], INPUT_CTL_LSTICK_LEFT, PROMPT_ART_KEYS)
          && pic_is(p[2], INPUT_CTL_LSTICK_DOWN, PROMPT_ART_KEYS)
          && pic_is(p[3], INPUT_CTL_LSTICK_RIGHT, PROMPT_ART_KEYS));
    CHECK(prompt_text_pictures(PROMPT_TEXT_LSTICK_UPDOWN, PROMPT_ART_KEYS, &pad, &keys, p) == 2);
    CHECK(pic_is(p[0], INPUT_CTL_LSTICK_UP, PROMPT_ART_KEYS) && pic_is(p[1], INPUT_CTL_LSTICK_DOWN, PROMPT_ART_KEYS));
    CHECK(prompt_text_pictures(PROMPT_TEXT_DPAD_ACROSS, PROMPT_ART_KEYS, &pad, &keys, p) == 2);
    CHECK(pic_is(p[0], INPUT_CTL_DPAD_LEFT, PROMPT_ART_KEYS) && pic_is(p[1], INPUT_CTL_DPAD_RIGHT, PROMPT_ART_KEYS));
    /* A way with no key isn't shown; with none on any, the Xbox pad's
     * picture of them. */
    keys.bind[INPUT_CTL_DPAD_LEFT][0] = INPUT_NONE;
    CHECK(prompt_text_pictures(PROMPT_TEXT_DPAD_ACROSS, PROMPT_ART_KEYS, &pad, &keys, p) == 1
          && pic_is(p[0], INPUT_CTL_DPAD_RIGHT, PROMPT_ART_KEYS));
    keys.bind[INPUT_CTL_DPAD_RIGHT][0] = INPUT_NONE;
    CHECK(prompt_text_pictures(PROMPT_TEXT_DPAD_ACROSS, PROMPT_ART_KEYS, &pad, &keys, p) == 1
          && pic_is(p[0], NV2A_PROMPT_MARK_GROUP + PROMPT_GROUP_DPAD_ACROSS, PROMPT_ART_XBOX));
    CHECK(prompt_text_pictures(PROMPT_TEXT_DPAD, PROMPT_ART_KEYS, &pad, NULL, p) == 1
          && pic_is(p[0], NV2A_PROMPT_MARK_GROUP + PROMPT_GROUP_DPAD, PROMPT_ART_XBOX));
    /* A what there isn't: the A's. */
    CHECK(prompt_text_pictures(PROMPT_TEXT_WHATS, PROMPT_ART_XBOX, &pad, &keys, p) == 1
          && pic_is(p[0], INPUT_CTL_A, PROMPT_ART_XBOX));
    CHECK(prompt_text_pictures(-1, PROMPT_ART_XBOX, &pad, &keys, p) == 1 && pic_is(p[0], INPUT_CTL_A, PROMPT_ART_XBOX));

    /* A whole line's names given theirs. */
    layouts(&pad, &keys);
    lines();
    CHECK(prompt_text_match(k_lines, NLINES, mem, TOP, LINES + 0x400u, &m));
    prompt_text_show(&m, PROMPT_ART_KEYS, &pad, &keys);
    CHECK(m.name[0].pics == 2 && pic_is(m.name[0].pic[0], INPUT_CTL_DPAD_LEFT, PROMPT_ART_KEYS)
          && pic_is(m.name[0].pic[1], INPUT_CTL_DPAD_RIGHT, PROMPT_ART_KEYS));
    CHECK(m.name[1].pics == 1 && pic_is(m.name[1].pic[0], INPUT_CTL_LTRIGGER, PROMPT_ART_KEYS));
    printf("prompt_text pictures: pass\n");
}

/* ---- drawing -------------------------------------------------------------- */

/* The made-up font: what it drew and the pictures marked. */
#define WIDE 10.0f                    /* a character's advance at scale 1 */
static struct { char text[64]; float x, y; } g_piece[8];
static struct { PromptTextPicture pic; float x, y, half; } g_mark[8];
static int g_npieces, g_nmarks, g_no_font;
static float g_xs, g_ys, g_end;

static int font_draw(void *ctx, uint32_t va, float x, float y, float *px, float *py)
{
    const char *s = (const char *)mem + va;
    float pen = x, top = y;
    (void)ctx;
    if (g_no_font) return 0;
    CHECK(va == SCRATCH && g_npieces < 8 && strlen(s) < sizeof g_piece[0].text);
    strcpy(g_piece[g_npieces].text, s);
    g_piece[g_npieces].x = x;
    g_piece[g_npieces].y = y;
    g_npieces++;
    for (; *s; s++) {
        if (s[0] == '$' && s[1] == 'n') { pen = x; top += PROMPT_TEXT_CELL * g_ys; s++; continue; }
        pen += WIDE * g_xs;
    }
    *px = pen;
    *py = top;
    g_end = pen;
    return 1;
}

static float font_measure(void *ctx, uint32_t va, float scale)
{
    (void)ctx;
    return (float)strlen((const char *)mem + va) * WIDE * scale;
}

static void font_mark(void *ctx, PromptTextPicture pic, float x, float y, float half)
{
    (void)ctx;
    CHECK(g_nmarks < 8);
    g_mark[g_nmarks].pic = pic;
    g_mark[g_nmarks].x = x;
    g_mark[g_nmarks].y = y;
    g_mark[g_nmarks].half = half;
    g_nmarks++;
}

static const PromptTextCalls k_font = { font_draw, font_measure, font_mark, NULL };

/* text, as one of k_lines with its pictures in art, drawn from (x, y) at
 * scale s: what the draw returns, the match left in *m. */
static int draw_in(const char *text, int art, float x, float y, float s, PromptTextMatch *m)
{
    InputLayout pad, keys;
    layouts(&pad, &keys);
    lines();
    put_text(TEXT_AT, text);
    CHECK(prompt_text_match(k_lines, NLINES, mem, TOP, TEXT_AT, m));
    prompt_text_show(m, art, &pad, &keys);
    g_npieces = g_nmarks = 0;
    g_xs = g_ys = s;
    g_end = x;
    return prompt_text_draw(m, mem, TEXT_AT, SCRATCH, x, y, s, s, &k_font);
}

static int draw(const char *text, float x, float y, float s)
{
    PromptTextMatch m;
    return draw_in(text, PROMPT_ART_XBOX, x, y, s, &m);
}

static int piece_is(int i, const char *text, float x, float y)
{
    return i < g_npieces && !strcmp(g_piece[i].text, text) && CLOSE(g_piece[i].x, x) && CLOSE(g_piece[i].y, y);
}

/* Mark i: show in art, the p-th picture of a name whose place starts at
 * left, on the line whose top is top, at scale s. */
static int mark_at(int i, int show, int art, int p, float left, float top, float s)
{
    const float side = PROMPT_TEXT_SIDE * s;
    return i < g_nmarks && pic_is(g_mark[i].pic, show, art) && CLOSE(g_mark[i].half, 0.5f * side)
        && CLOSE(g_mark[i].x, left + PROMPT_TEXT_PAD * s + ((float)p + 0.5f) * side)
        && CLOSE(g_mark[i].y, top + 0.5f * PROMPT_TEXT_CELL * s);
}

static int mark_is(int i, int control, float left, float top, float s)
{
    return mark_at(i, control, PROMPT_ART_XBOX, 0, left, top, s);
}

static void case_draw(void)
{
    const float s = 0.5f, side = PROMPT_TEXT_SIDE * s, pad = PROMPT_TEXT_PAD * s, cell = PROMPT_TEXT_CELL * s;
    PromptTextMatch m;
    float x;

    /* Placed by its right end: left by half the picture over the letter,
     * so its middle is the game's line's; the words before it, the picture,
     * the rest from the picture's right. */
    CHECK(draw("Tap the  B key to leave.", 300.0f, 100.0f, s) == 1);
    x = 300.0f - 0.5f * (side + 2.0f * pad - WIDE * s);
    CHECK(g_npieces == 2 && g_nmarks == 1);
    CHECK(piece_is(0, "Tap the  ", x, 100.0f));
    CHECK(mark_is(0, INPUT_CTL_B, x + 9.0f * WIDE * s, 100.0f, s));
    CHECK(piece_is(1, " key to leave.", x + 9.0f * WIDE * s + side + 2.0f * pad, 100.0f));

    /* The letter first: no piece before it. */
    CHECK(draw("\x82\x60 starts it", 50.0f, 60.0f, 1.0f) == 1);
    CHECK(g_npieces == 1 && g_nmarks == 1);
    CHECK(mark_is(0, INPUT_CTL_A, 50.0f, 60.0f, 1.0f));
    CHECK(piece_is(0, " starts it", 50.0f + PROMPT_TEXT_SIDE + 2.0f * PROMPT_TEXT_PAD, 60.0f));

    /* Over two lines, a letter on each: the first's line to its end from
     * the picture, the next line from the line's start, the second
     * picture where its words end, the rest from it. */
    CHECK(draw("Need 12. Hit A$nor in <Unit> B to stop.", 128.0f, 140.0f, s) == 2);
    CHECK(g_npieces == 3 && g_nmarks == 2);
    CHECK(piece_is(0, "Need 12. Hit ", 128.0f, 140.0f));
    CHECK(mark_is(0, INPUT_CTL_A, 128.0f + 13.0f * WIDE * s, 140.0f, s));
    CHECK(piece_is(1, "or in <Unit> ", 128.0f, 140.0f + cell));
    CHECK(mark_is(1, INPUT_CTL_B, 128.0f + 13.0f * WIDE * s, 140.0f + cell, s));
    CHECK(piece_is(2, " to stop.", 128.0f + 13.0f * WIDE * s + side + 2.0f * pad, 140.0f + cell));

    /* A help line in a pad's art: each name's words gone, one picture in
     * their place, the D-pad's with its left and right and the trigger's. */
    CHECK(draw_in("Press the D-pad to pick, hold L-trig to go.  ", PROMPT_ART_NINTENDO, 78.0f, 42.0f, 1.0f, &m)
          == 2);
    CHECK(g_npieces == 3 && g_nmarks == 2);
    CHECK(piece_is(0, "Press ", 78.0f, 42.0f));
    x = 78.0f + 6.0f * WIDE;
    CHECK(mark_at(0, NV2A_PROMPT_MARK_GROUP + PROMPT_GROUP_DPAD_ACROSS, PROMPT_ART_NINTENDO, 0, x, 42.0f, 1.0f));
    x += PROMPT_TEXT_SIDE + 2.0f * PROMPT_TEXT_PAD;
    CHECK(piece_is(1, " to pick, hold ", x, 42.0f));
    x += 15.0f * WIDE;
    CHECK(mark_at(1, INPUT_CTL_LTRIGGER, PROMPT_ART_NINTENDO, 0, x, 42.0f, 1.0f));
    x += PROMPT_TEXT_SIDE + 2.0f * PROMPT_TEXT_PAD;
    CHECK(piece_is(2, " to go.  ", x, 42.0f));

    /* In the keys': a key for each way named, side by side in the one
     * place, the words after them from the last one's right. */
    CHECK(draw_in("Press the D-pad to pick, hold L-trig to go.  ", PROMPT_ART_KEYS, 78.0f, 42.0f, 1.0f, &m) == 3);
    CHECK(g_npieces == 3 && g_nmarks == 3);
    x = 78.0f + 6.0f * WIDE;
    CHECK(mark_at(0, INPUT_CTL_DPAD_LEFT, PROMPT_ART_KEYS, 0, x, 42.0f, 1.0f));
    CHECK(mark_at(1, INPUT_CTL_DPAD_RIGHT, PROMPT_ART_KEYS, 1, x, 42.0f, 1.0f));
    x += 2.0f * PROMPT_TEXT_SIDE + 2.0f * PROMPT_TEXT_PAD;
    CHECK(piece_is(1, " to pick, hold ", x, 42.0f));
    x += 15.0f * WIDE;
    CHECK(mark_at(2, INPUT_CTL_LTRIGGER, PROMPT_ART_KEYS, 0, x, 42.0f, 1.0f));

    /* No font: nothing more after the first piece it couldn't draw. */
    g_no_font = 1;
    CHECK(draw("Need 12. Hit A$nor in <Unit> B to stop.", 128.0f, 140.0f, s) == 0);
    CHECK(g_npieces == 0 && g_nmarks == 0);
    g_no_font = 0;
    printf("prompt_text draw: pass\n");
}

/* Two letters on one line, and lines after the last letter's. */
static PromptTextLine k_more[] = {
    { LINES + 0x500u, 0, 2, { { 0, 1, PROMPT_TEXT_A, 0 }, { 6, 1, PROMPT_TEXT_B, 0 } } },
    { LINES + 0x600u, 0, 1, { { 6, 1, PROMPT_TEXT_A, 0 } } },
};

static void case_more(void)
{
    const float side = PROMPT_TEXT_SIDE, pad = PROMPT_TEXT_PAD, cell = PROMPT_TEXT_CELL;
    InputLayout lpad, lkeys;
    PromptTextMatch m;
    layouts(&lpad, &lkeys);
    lines();
    put_text(LINES + 0x500u, "A and B both");
    put_text(LINES + 0x600u, "Press A$n$nthen go$non");
    k_more[0].name[0].check = k_more[1].name[0].check = prompt_text_check((const uint8_t *)"A", 1);
    k_more[0].name[1].check = prompt_text_check((const uint8_t *)"B", 1);
    g_xs = g_ys = 1.0f;

    g_npieces = g_nmarks = 0;
    CHECK(prompt_text_match(k_more, 2, mem, TOP, LINES + 0x500u, &m) && m.n == 2);
    prompt_text_show(&m, PROMPT_ART_XBOX, &lpad, &lkeys);
    CHECK(prompt_text_draw(&m, mem, LINES + 0x500u, SCRATCH, 10.0f, 20.0f, 1.0f, 1.0f, &k_font) == 2);
    CHECK(g_npieces == 2 && g_nmarks == 2);
    CHECK(mark_is(0, INPUT_CTL_A, 10.0f, 20.0f, 1.0f));
    CHECK(piece_is(0, " and ", 10.0f + side + 2.0f * pad, 20.0f));
    CHECK(mark_is(1, INPUT_CTL_B, 10.0f + side + 2.0f * pad + 5.0f * WIDE, 20.0f, 1.0f));
    CHECK(piece_is(1, " both", 10.0f + 2.0f * (side + 2.0f * pad) + 5.0f * WIDE, 20.0f));

    /* The letter ends its line: the lines after it, blank ones too, from
     * the line's start. */
    g_npieces = g_nmarks = 0;
    CHECK(prompt_text_match(k_more, 2, mem, TOP, LINES + 0x600u, &m) && m.n == 1);
    prompt_text_show(&m, PROMPT_ART_XBOX, &lpad, &lkeys);
    CHECK(prompt_text_draw(&m, mem, LINES + 0x600u, SCRATCH, 10.0f, 20.0f, 1.0f, 1.0f, &k_font) == 1);
    CHECK(g_npieces == 2 && g_nmarks == 1);
    CHECK(piece_is(0, "Press ", 10.0f, 20.0f));
    CHECK(mark_is(0, INPUT_CTL_A, 10.0f + 6.0f * WIDE, 20.0f, 1.0f));
    CHECK(piece_is(1, "$nthen go$non", 10.0f, 20.0f + cell));
    printf("prompt_text more: pass\n");
}

/* ---- a line's width ------------------------------------------------------- */

/* The width the editor's ticker is told: a line's pieces as the game
 * measures them and its pictures' room, which is where a draw of it ends;
 * wider by a key with two ways' keys than with a pad's one picture, and
 * the game's own measure of the text with no name's pictures at all. */
static void case_width(void)
{
    static const char text[] = "Press the D-pad to pick, hold L-trig to go.  ";
    static const float scales[] = { 1.0f, 0.5f, 2.0f };
    PromptTextMatch m;
    float pad_w = 0.0f, keys_w = 0.0f;
    size_t i;
    for (i = 0; i < sizeof scales / sizeof scales[0]; i++) {
        const float s = scales[i], x = 78.0f - 31.0f;
        CHECK(draw_in(text, PROMPT_ART_PLAYSTATION, x, 42.0f, s, &m) == 2);
        pad_w = prompt_text_width(&m, mem, TEXT_AT, SCRATCH, s, s, &k_font);
        CHECK(CLOSE(pad_w, g_end - x));
        /* 45 characters less the names' 15, and a picture and its pads for
         * each of the two names. */
        CHECK(CLOSE(pad_w, 30.0f * WIDE * s + 2.0f * (PROMPT_TEXT_SIDE * s + 2.0f * PROMPT_TEXT_PAD * s)));
        CHECK(draw_in(text, PROMPT_ART_KEYS, x, 42.0f, s, &m) == 3);
        keys_w = prompt_text_width(&m, mem, TEXT_AT, SCRATCH, s, s, &k_font);
        CHECK(CLOSE(keys_w, g_end - x) && CLOSE(keys_w - pad_w, PROMPT_TEXT_SIDE * s));
    }
    /* A line that names a letter only: the same rule. */
    CHECK(draw_in("Tap the  B key to leave.", PROMPT_ART_XBOX, 0.0f, 0.0f, 1.0f, &m) == 1);
    CHECK(CLOSE(prompt_text_width(&m, mem, TEXT_AT, SCRATCH, 1.0f, 1.0f, &k_font),
                23.0f * WIDE + PROMPT_TEXT_SIDE + 2.0f * PROMPT_TEXT_PAD));
    printf("prompt_text width: pass\n");
}

/* ---- the mark's fade ------------------------------------------------------ */

static void case_mark(void)
{
    int what, box, art;
    const uint32_t w = nv2a_prompts_mark(INPUT_CTL_B, 0, PROMPT_ART_NINTENDO);
    const uint32_t g = nv2a_prompts_mark(NV2A_PROMPT_MARK_GROUP + PROMPT_GROUP_LSTICK_UPDOWN, 0, PROMPT_ART_KEYS);
    CHECK(nv2a_prompts_mark_alpha(w, 255) == w);
    CHECK(CLOSE(nv2a_prompts_mark_opacity(w), 1.0f));
    CHECK(nv2a_prompts_mark_read(nv2a_prompts_mark_alpha(w, 0), &what, &box, &art));
    CHECK(what == INPUT_CTL_B && !box && art == PROMPT_ART_NINTENDO);
    CHECK(nv2a_prompts_mark_opacity(nv2a_prompts_mark_alpha(w, 0)) < 0.01f);
    CHECK(fabsf(nv2a_prompts_mark_opacity(nv2a_prompts_mark_alpha(w, 128)) - 128.0f / 255.0f) < 0.01f);
    CHECK(nv2a_prompts_mark_alpha(w, 1000) == w);
    /* A group's, and its fade, read back the same way. */
    CHECK(nv2a_prompts_mark_read(nv2a_prompts_mark_alpha(g, 64), &what, &box, &art));
    CHECK(what == NV2A_PROMPT_MARK_GROUP + PROMPT_GROUP_LSTICK_UPDOWN && !box && art == PROMPT_ART_KEYS);
    printf("prompt_text mark: pass\n");
}

int main(void)
{
    mem = (uint8_t *)calloc(1, TOP);
    CHECK(mem != NULL);
    case_table();
    case_check();
    case_match();
    case_pictures();
    case_draw();
    case_more();
    case_width();
    case_mark();
    free(mem);
    printf("prompt_text: all cases pass\n");
    return 0;
}
