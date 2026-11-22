/* Run with: python tests/prompt_text/run.py
 *
 * The game's lines naming the A or B button by its letter, drawn with the
 * button prompts' pictures in the letters' places (port/prompt_text.c),
 * without the game: made-up lines, in a buffer that stands in for guest
 * memory, and a made-up font, each character 10 wide at scale 1, that says
 * what it was asked to draw and where its pen ended.
 *
 * - table: the game's table's shape: one or two letters a line, in order,
 *   at addresses in the game's read-only data, none twice, and the go-back
 *   lines, the first five, the only ones placed by their right end;
 * - match: a line is known by its bytes, a format's %d and %s matching what
 *   was put there, with each letter's place in the text; a text that
 *   differs anywhere isn't, nor one outside guest memory, nor any text
 *   where the line's own letter isn't where its table says (said once);
 * - draw: the pieces and the pictures, on one line and over several, a
 *   letter first, two letters, a line placed by its right end, and a font
 *   that isn't there;
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
    int i, j, k;
    CHECK(prompt_text_nlines >= 5 && prompt_text_nlines <= 256);
    for (i = 0; i < prompt_text_nlines; i++) {
        const PromptTextLine *l = &prompt_text_lines[i];
        CHECK(l->va >= 0x1C3F60u && l->va < 0x1EB760u);   /* .rdata and .data */
        CHECK(l->n >= 1 && l->n <= PROMPT_TEXT_LETTERS);
        CHECK(l->right == (i < 5));
        for (k = 0; k < l->n; k++) {
            CHECK(l->letter[k].button <= 1 && l->letter[k].wide <= 1);
            if (k) CHECK(l->letter[k].at > l->letter[k - 1].at + 1);
        }
        for (j = 0; j < i; j++) CHECK(prompt_text_lines[j].va != l->va);
    }
    printf("prompt_text table: pass\n");
}

/* ---- matching ------------------------------------------------------------- */

/* Made-up lines: a plain one, B at 9; a full-width A first; a format with
 * both letters, A at 13 after a %d and B at 27 after a %s; and one whose
 * letter isn't where it says (its 4 is a space). */
static const PromptTextLine k_lines[] = {
    { LINES + 0x000u, 1, 1, { { 9, 0, 1 } } },
    { LINES + 0x100u, 0, 1, { { 0, 1, 0 } } },
    { LINES + 0x200u, 0, 2, { { 13, 0, 0 }, { 27, 0, 1 } } },
    { LINES + 0x300u, 0, 1, { { 4, 0, 0 } } },
};
#define NLINES ((int)(sizeof k_lines / sizeof k_lines[0]))

static void lines(void)
{
    memset(mem, 0, TOP);
    put_text(LINES + 0x000u, "Tap the  B key to leave.");
    put_text(LINES + 0x100u, "\x82\x60 starts it");
    put_text(LINES + 0x200u, "Need %d. Hit A$nor in <%s> B to stop.");
    put_text(LINES + 0x300u, "Hit the A key");
}

static void case_match(void)
{
    PromptTextMatch m;
    lines();
    /* The line itself, wherever its text is. */
    put_text(TEXT_AT, "Tap the  B key to leave.");
    CHECK(prompt_text_match(k_lines, NLINES, mem, TOP, TEXT_AT, &m));
    CHECK(m.line == 0 && m.right && m.n == 1 && m.len == 24);
    CHECK(m.letter[0].at == 9 && m.letter[0].bytes == 1 && m.letter[0].button == 1);
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
    CHECK(m.line == 1 && !m.right && m.letter[0].at == 0 && m.letter[0].bytes == 2 && m.letter[0].button == 0);
    /* The format, filled in: the letters where the text has them. */
    put_text(TEXT_AT, "Need 1234. Hit A$nor in <Unit 2> B to stop.");
    CHECK(prompt_text_match(k_lines, NLINES, mem, TOP, TEXT_AT, &m));
    CHECK(m.line == 2 && m.n == 2 && m.len == 43);
    CHECK(m.letter[0].at == 15 && m.letter[0].button == 0 && m.letter[1].at == 33 && m.letter[1].button == 1);
    put_text(TEXT_AT, "Need -7. Hit A$nor in <> B to stop.");
    CHECK(prompt_text_match(k_lines, NLINES, mem, TOP, TEXT_AT, &m) && m.letter[1].at == 25);
    put_text(TEXT_AT, "Need many. Hit A$nor in <Unit 2> B to stop.");   /* no number */
    CHECK(!prompt_text_match(k_lines, NLINES, mem, TOP, TEXT_AT, &m));
    put_text(TEXT_AT, "Need 3. Hit A$nor in <Unit 2 B to stop.");       /* no > */
    CHECK(!prompt_text_match(k_lines, NLINES, mem, TOP, TEXT_AT, &m));
    /* The line whose letter isn't there: never, said once. */
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

/* ---- drawing -------------------------------------------------------------- */

/* The made-up font: what it drew and the pictures marked. */
#define WIDE 10.0f                    /* a character's advance at scale 1 */
static struct { char text[64]; float x, y; } g_piece[8];
static struct { int button; float x, y, half; } g_mark[4];
static int g_npieces, g_nmarks, g_no_font;
static float g_xs, g_ys;

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
    return 1;
}

static float font_measure(void *ctx, uint32_t va, float scale)
{
    (void)ctx;
    return (float)strlen((const char *)mem + va) * WIDE * scale;
}

static void font_mark(void *ctx, int button, float x, float y, float half)
{
    (void)ctx;
    CHECK(g_nmarks < 4);
    g_mark[g_nmarks].button = button;
    g_mark[g_nmarks].x = x;
    g_mark[g_nmarks].y = y;
    g_mark[g_nmarks].half = half;
    g_nmarks++;
}

static const PromptTextCalls k_font = { font_draw, font_measure, font_mark, NULL };

/* text, drawn from (x, y) at scale s, as one of k_lines: what it returns. */
static int draw(const char *text, float x, float y, float s)
{
    PromptTextMatch m;
    lines();
    put_text(TEXT_AT, text);
    CHECK(prompt_text_match(k_lines, NLINES, mem, TOP, TEXT_AT, &m));
    g_npieces = g_nmarks = 0;
    g_xs = g_ys = s;
    return prompt_text_draw(&m, mem, TEXT_AT, SCRATCH, x, y, s, s, &k_font);
}

static int piece_is(int i, const char *text, float x, float y)
{
    return i < g_npieces && !strcmp(g_piece[i].text, text) && CLOSE(g_piece[i].x, x) && CLOSE(g_piece[i].y, y);
}

static int mark_is(int i, int button, float left, float top, float s)
{
    const float side = PROMPT_TEXT_SIDE * s;
    return i < g_nmarks && g_mark[i].button == button && CLOSE(g_mark[i].half, 0.5f * side)
        && CLOSE(g_mark[i].x, left + PROMPT_TEXT_PAD * s + 0.5f * side)
        && CLOSE(g_mark[i].y, top + 0.5f * PROMPT_TEXT_CELL * s);
}

static void case_draw(void)
{
    const float s = 0.5f, side = PROMPT_TEXT_SIDE * s, pad = PROMPT_TEXT_PAD * s, cell = PROMPT_TEXT_CELL * s;
    float x;

    /* Placed by its right end: left by half the picture over the letter,
     * so its middle is the game's line's; the words before it, the picture,
     * the rest from the picture's right. */
    CHECK(draw("Tap the  B key to leave.", 300.0f, 100.0f, s) == 1);
    x = 300.0f - 0.5f * (side + 2.0f * pad - WIDE * s);
    CHECK(g_npieces == 2 && g_nmarks == 1);
    CHECK(piece_is(0, "Tap the  ", x, 100.0f));
    CHECK(mark_is(0, 1, x + 9.0f * WIDE * s, 100.0f, s));
    CHECK(piece_is(1, " key to leave.", x + 9.0f * WIDE * s + side + 2.0f * pad, 100.0f));

    /* The letter first: no piece before it. */
    CHECK(draw("\x82\x60 starts it", 50.0f, 60.0f, 1.0f) == 1);
    CHECK(g_npieces == 1 && g_nmarks == 1);
    CHECK(mark_is(0, 0, 50.0f, 60.0f, 1.0f));
    CHECK(piece_is(0, " starts it", 50.0f + PROMPT_TEXT_SIDE + 2.0f * PROMPT_TEXT_PAD, 60.0f));

    /* Over two lines, a letter on each: the first's line to its end from
     * the picture, the next line from the line's start, the second
     * picture where its words end, the rest from it. */
    CHECK(draw("Need 12. Hit A$nor in <Unit> B to stop.", 128.0f, 140.0f, s) == 2);
    CHECK(g_npieces == 3 && g_nmarks == 2);
    CHECK(piece_is(0, "Need 12. Hit ", 128.0f, 140.0f));
    CHECK(mark_is(0, 0, 128.0f + 13.0f * WIDE * s, 140.0f, s));
    CHECK(piece_is(1, "or in <Unit> ", 128.0f, 140.0f + cell));
    CHECK(mark_is(1, 1, 128.0f + 13.0f * WIDE * s, 140.0f + cell, s));
    CHECK(piece_is(2, " to stop.", 128.0f + 13.0f * WIDE * s + side + 2.0f * pad, 140.0f + cell));

    /* No font: nothing more after the first piece it couldn't draw. */
    g_no_font = 1;
    CHECK(draw("Need 12. Hit A$nor in <Unit> B to stop.", 128.0f, 140.0f, s) == 0);
    CHECK(g_npieces == 0 && g_nmarks == 0);
    g_no_font = 0;
    printf("prompt_text draw: pass\n");
}

/* Two letters on one line, and lines after the last letter's. */
static const PromptTextLine k_more[] = {
    { LINES + 0x400u, 0, 2, { { 0, 0, 0 }, { 6, 0, 1 } } },
    { LINES + 0x500u, 0, 1, { { 6, 0, 0 } } },
};

static void case_more(void)
{
    const float side = PROMPT_TEXT_SIDE, pad = PROMPT_TEXT_PAD, cell = PROMPT_TEXT_CELL;
    PromptTextMatch m;
    lines();
    put_text(LINES + 0x400u, "A and B both");
    put_text(LINES + 0x500u, "Press A$n$nthen go$non");
    g_xs = g_ys = 1.0f;

    g_npieces = g_nmarks = 0;
    CHECK(prompt_text_match(k_more, 2, mem, TOP, LINES + 0x400u, &m) && m.n == 2);
    CHECK(prompt_text_draw(&m, mem, LINES + 0x400u, SCRATCH, 10.0f, 20.0f, 1.0f, 1.0f, &k_font) == 2);
    CHECK(g_npieces == 2 && g_nmarks == 2);
    CHECK(mark_is(0, 0, 10.0f, 20.0f, 1.0f));
    CHECK(piece_is(0, " and ", 10.0f + side + 2.0f * pad, 20.0f));
    CHECK(mark_is(1, 1, 10.0f + side + 2.0f * pad + 5.0f * WIDE, 20.0f, 1.0f));
    CHECK(piece_is(1, " both", 10.0f + 2.0f * (side + 2.0f * pad) + 5.0f * WIDE, 20.0f));

    /* The letter ends its line: the lines after it, blank ones too, from
     * the line's start. */
    g_npieces = g_nmarks = 0;
    CHECK(prompt_text_match(k_more, 2, mem, TOP, LINES + 0x500u, &m) && m.n == 1);
    CHECK(prompt_text_draw(&m, mem, LINES + 0x500u, SCRATCH, 10.0f, 20.0f, 1.0f, 1.0f, &k_font) == 1);
    CHECK(g_npieces == 2 && g_nmarks == 1);
    CHECK(piece_is(0, "Press ", 10.0f, 20.0f));
    CHECK(mark_is(0, 0, 10.0f + 6.0f * WIDE, 20.0f, 1.0f));
    CHECK(piece_is(1, "$nthen go$non", 10.0f, 20.0f + cell));
    printf("prompt_text more: pass\n");
}

/* ---- the mark's fade ------------------------------------------------------ */

static void case_mark(void)
{
    int button, box, art;
    const uint32_t w = nv2a_prompts_mark(1, 0, 3);
    CHECK(nv2a_prompts_mark_alpha(w, 255) == w);
    CHECK(CLOSE(nv2a_prompts_mark_opacity(w), 1.0f));
    CHECK(nv2a_prompts_mark_read(nv2a_prompts_mark_alpha(w, 0), &button, &box, &art));
    CHECK(button == 1 && !box && art == 3);
    CHECK(nv2a_prompts_mark_opacity(nv2a_prompts_mark_alpha(w, 0)) < 0.01f);
    CHECK(fabsf(nv2a_prompts_mark_opacity(nv2a_prompts_mark_alpha(w, 128)) - 128.0f / 255.0f) < 0.01f);
    CHECK(nv2a_prompts_mark_alpha(w, 1000) == w);
    printf("prompt_text mark: pass\n");
}

int main(void)
{
    mem = (uint8_t *)calloc(1, TOP);
    CHECK(mem != NULL);
    case_table();
    case_match();
    case_draw();
    case_more();
    case_mark();
    free(mem);
    printf("prompt_text: all cases pass\n");
    return 0;
}
