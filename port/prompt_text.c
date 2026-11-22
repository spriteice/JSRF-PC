/* The game's lines that name the A or B button, drawn with the button
 * prompts' picture in the letter's place (prompt_text.h). Plain C on guest
 * memory, so a test links it (tests/prompt_text). */
#include "prompt_text.h"

#include <stdio.h>
#include <string.h>

#define GUEST_LOW 0x10000u   /* below it, no guest address the game uses */
#define SJIS_A    0x60u      /* the full-width A's second byte, after 0x82; the B's is one more */

/* The lines, by their address in default.xbe (the US release), as the
 * game's files have them: each letter's byte and button. Found by reading
 * the game's strings and the code that draws them; tests/prompt_text
 * checks the table's shape, and the log says once if a line in the game's
 * memory isn't as it says. Where each is drawn:
 *
 * - go back: a table of five by language at 0x2221F8, read at 0x115490 and
 *   0x115D4C, drawn through the queuing call, its right end at 576;
 * - the test run's start: a table of two by language at 0x2250D0, read at
 *   0x11A6A2, the queuing call at (300, 404);
 * - the message box's (class 0x1CC660): five tables of fifteen by language,
 *   from 0x1FBCA8, filled in by sprintf and shown by sub_0006F450; the box's
 *   draw draws its text at once at 0x6EE3F, from (128, 140);
 * - the graffiti save screen's: one line a call, drawn at once from
 *   0x12FB4C to 0x134212, from x 65. */
const PromptTextLine prompt_text_lines[] = {
    { 0x001D2C8Cu, 1, 1, { { 0, 1, 1 } } },   /* go back, Japanese */
    { 0x001D2C6Cu, 1, 1, { { 10, 0, 1 } } },   /* go back, English */
    { 0x001D2C48u, 1, 1, { { 10, 0, 1 } } },   /* go back, its (Fr) copy */
    { 0x001D2C24u, 1, 1, { { 10, 0, 1 } } },   /* go back, its (Ger) copy */
    { 0x001D2C00u, 1, 1, { { 10, 0, 1 } } },   /* go back, its (Esp) copy */
    { 0x001D276Cu, 0, 1, { { 0, 1, 0 } } },   /* the test run's start, Japanese */
    { 0x001D7BF4u, 0, 1, { { 10, 0, 0 } } },   /* the test run's start, English */
    { 0x001CBAB0u, 0, 1, { { 41, 1, 0 } } },   /* box: the save can't be read, Japanese */
    { 0x001CBA30u, 0, 1, { { 99, 1, 0 } } },   /* box: a memory unit taken out, Japanese */
    { 0x001CB958u, 0, 2, { { 126, 1, 0 }, { 164, 1, 1 } } },   /* box: not enough memory, Japanese (A and B) */
    { 0x001CB884u, 0, 1, { { 24, 1, 0 } } },   /* box: done saving, Japanese */
    { 0x001CB718u, 0, 1, { { 58, 1, 0 } } },   /* box: graffiti can't be read, Japanese */
    { 0x001CB6C0u, 0, 1, { { 49, 0, 0 } } },   /* box: the save can't be read, English */
    { 0x001CB640u, 0, 1, { { 97, 1, 0 } } },   /* box: a memory unit taken out, Japanese (the English table's) */
    { 0x001CB570u, 0, 2, { { 97, 0, 0 }, { 149, 0, 1 } } },   /* box: not enough memory, English (A and B) */
    { 0x001CB4C0u, 0, 1, { { 24, 0, 0 } } },   /* box: done saving, English */
    { 0x001CB370u, 0, 1, { { 37, 0, 0 } } },   /* box: graffiti can't be read, English */
    { 0x001CB2DCu, 0, 1, { { 38, 0, 0 } } },   /* box: the saved data can't be read */
    { 0x001CB210u, 0, 2, { { 97, 0, 0 }, { 151, 0, 1 } } },   /* box: not enough memory, the other tables' (A and B) */
    { 0x001DDB80u, 0, 1, { { 37, 0, 1 } } },   /* graffiti save screen, Italian */
    { 0x001DDBACu, 0, 1, { { 19, 0, 1 } } },   /* graffiti save screen, Italian */
    { 0x001DDBDCu, 0, 1, { { 18, 0, 0 } } },   /* graffiti save screen, Italian */
    { 0x001DDC6Cu, 0, 1, { { 19, 0, 1 } } },   /* graffiti save screen, Spanish */
    { 0x001DDC9Cu, 0, 1, { { 18, 0, 0 } } },   /* graffiti save screen, Spanish */
    { 0x001DDD6Cu, 0, 1, { { 26, 0, 1 } } },   /* graffiti save screen, German */
    { 0x001DDD90u, 0, 1, { { 11, 0, 0 } } },   /* graffiti save screen, German */
    { 0x001DDE08u, 0, 1, { { 22, 0, 1 } } },   /* graffiti save screen, French */
    { 0x001DDE50u, 0, 1, { { 25, 0, 1 } } },   /* graffiti save screen, French */
    { 0x001DDE84u, 0, 1, { { 21, 0, 0 } } },   /* graffiti save screen, French */
    { 0x001DDF0Cu, 0, 1, { { 10, 0, 1 } } },   /* graffiti save screen, English */
    { 0x001DDF60u, 0, 1, { { 13, 0, 1 } } },   /* graffiti save screen, English */
    { 0x001DDF94u, 0, 1, { { 10, 0, 0 } } },   /* graffiti save screen, English */
    { 0x001DE028u, 0, 1, { { 4, 1, 1 } } },   /* graffiti save screen, Japanese */
    { 0x001DE080u, 0, 2, { { 0, 1, 0 }, { 36, 1, 1 } } },   /* graffiti save screen, Japanese (A and B) */
    { 0x001DE240u, 0, 1, { { 0, 1, 0 } } },   /* graffiti save screen, Japanese */
    { 0x001DE374u, 0, 1, { { 10, 0, 0 } } },   /* graffiti save screen, English */
    { 0x001DE430u, 0, 1, { { 12, 0, 0 } } },   /* graffiti save screen, French */
    { 0x001DE478u, 0, 1, { { 21, 0, 0 } } },   /* graffiti save screen, French */
    { 0x001DE504u, 0, 1, { { 11, 0, 0 } } },   /* graffiti save screen, German */
    { 0x001DE544u, 0, 1, { { 11, 0, 0 } } },   /* graffiti save screen, German */
    { 0x001DE5C0u, 0, 1, { { 9, 0, 0 } } },   /* graffiti save screen, Spanish */
    { 0x001DE5FCu, 0, 1, { { 18, 0, 0 } } },   /* graffiti save screen, Spanish */
    { 0x001DE678u, 0, 1, { { 8, 0, 0 } } },   /* graffiti save screen, Italian */
    { 0x001DE6B8u, 0, 1, { { 18, 0, 0 } } },   /* graffiti save screen, Italian */
};
const int prompt_text_nlines = (int)(sizeof prompt_text_lines / sizeof prompt_text_lines[0]);

static int in_guest(uint32_t top, uint32_t va, uint32_t bytes)
{
    return va >= GUEST_LOW && va < top && bytes <= top - va;
}

/* Whether line l's letters are in the game's bytes where it says. */
static int letters_there(const PromptTextLine *l, const uint8_t *mem, uint32_t top)
{
    int k;
    for (k = 0; k < l->n; k++) {
        const uint32_t at = l->va + l->letter[k].at;
        if (!in_guest(top, at, 2u)) return 0;
        if (l->letter[k].wide ? mem[at] != 0x82u || mem[at + 1] != SJIS_A + l->letter[k].button
                              : mem[at] != (uint8_t)('A' + l->letter[k].button))
            return 0;
    }
    return 1;
}

static int digit(uint8_t c) { return c >= '0' && c <= '9'; }

/* The text at text against line l's bytes, a format's %d and %s matching
 * what sprintf put in their place, the letters' places in the text noted. */
static int same(const PromptTextLine *l, const uint8_t *mem, uint32_t top, uint32_t text, PromptTextMatch *m)
{
    uint32_t i = 0, j = 0;
    int k = 0;
    for (;;) {
        uint8_t f, t;
        if (i >= PROMPT_TEXT_LONGEST || j >= PROMPT_TEXT_LONGEST || !in_guest(top, l->va + i, 3u)
         || !in_guest(top, text + j, 1u))
            return 0;
        f = mem[l->va + i];
        t = mem[text + j];
        if (k < l->n && i == l->letter[k].at) {
            m->letter[k].at = j;
            m->letter[k].bytes = l->letter[k].wide ? 2u : 1u;
            m->letter[k].button = l->letter[k].button;
            k++;
        }
        if (f == '%') {
            const uint8_t spec = mem[l->va + i + 1], next = mem[l->va + i + 2];
            if (spec == 'd') {
                if (t == '-') j++;
                if (!in_guest(top, text + j, 1u) || !digit(mem[text + j])) return 0;
                while (j < PROMPT_TEXT_LONGEST && in_guest(top, text + j, 1u) && digit(mem[text + j])) j++;
            } else if (spec == 's') {
                while (j < PROMPT_TEXT_LONGEST && in_guest(top, text + j, 1u) && mem[text + j]
                       && mem[text + j] != next)
                    j++;
            } else if (spec == '%') {
                if (t != '%') return 0;
                j++;
            } else {
                return 0;
            }
            i += 2;
            continue;
        }
        if (f != t) return 0;
        if (!f) break;
        i++;
        j++;
    }
    m->len = j;
    m->n = k;
    return k == l->n;
}

int prompt_text_match(const PromptTextLine *table, int n, const uint8_t *mem, uint32_t top, uint32_t text,
                      PromptTextMatch *m)
{
    static unsigned char told[256];
    int i;
    if (!in_guest(top, text, 1u) || !mem[text]) return 0;
    for (i = 0; i < n; i++) {
        const PromptTextLine *l = &table[i];
        /* The first byte first: nearly every text stops there. */
        if (!in_guest(top, l->va, 1u) || mem[l->va] != mem[text]) continue;
        if (!letters_there(l, mem, top)) {
            if (i < (int)sizeof told && !told[i]) {
                told[i] = 1;
                fprintf(stderr, "[INPUT] button prompts: the game's line at %08X hasn't the letter the "
                        "table has: left as it is\n", (unsigned)l->va);
            }
            continue;
        }
        if (same(l, mem, top, text, m)) {
            m->line = i;
            m->right = l->right;
            return 1;
        }
    }
    return 0;
}

/* Bytes from to to of the text drawn at (x, y) through scratch; the pen
 * where it ended. */
static int piece(const PromptTextCalls *c, uint8_t *mem, uint32_t text, uint32_t scratch, uint32_t from,
                 uint32_t to, float x, float y, float *px, float *py)
{
    memcpy(mem + scratch, mem + text + from, to - from);
    mem[scratch + (to - from)] = 0;
    return c->draw(c->ctx, scratch, x, y, px, py);
}

/* Where the text's next new line ($n) is, from from to before to; to if none. */
static uint32_t new_line(const uint8_t *mem, uint32_t text, uint32_t from, uint32_t to)
{
    uint32_t q;
    for (q = from; q + 1 < to; q++)
        if (mem[text + q] == '$' && mem[text + q + 1] == 'n') return q;
    return to;
}

int prompt_text_draw(const PromptTextMatch *m, uint8_t *mem, uint32_t text, uint32_t scratch, float x,
                     float y, float xs, float ys, const PromptTextCalls *c)
{
    const float side = PROMPT_TEXT_SIDE * ys, pad = PROMPT_TEXT_PAD * xs, line = PROMPT_TEXT_CELL * ys;
    float cx, cy, px, py;
    uint32_t cur = 0;
    int k;
    /* Placed by its right end: left by half of what each picture adds over
     * its letter, as the game measures the letter, so the line's middle
     * stays where the game's was (prompt_text.h). */
    if (m->right)
        for (k = 0; k < m->n; k++) {
            memcpy(mem + scratch, mem + text + m->letter[k].at, m->letter[k].bytes);
            mem[scratch + m->letter[k].bytes] = 0;
            x -= PROMPT_TEXT_RIGHT_MOVE * (side + 2.0f * pad - c->measure(c->ctx, scratch, xs));
        }
    cx = x;
    cy = y;
    for (k = 0; k < m->n; k++) {
        const uint32_t at = m->letter[k].at, next = k + 1 < m->n ? m->letter[k + 1].at : m->len;
        uint32_t q;
        float gx;
        if (at > cur) {
            if (!piece(c, mem, text, scratch, cur, at, cx, cy, &px, &py)) return 0;
        } else {
            px = cx;
            py = cy;
        }
        c->mark(c->ctx, m->letter[k].button, px + pad + 0.5f * side, py + 0.5f * line, 0.5f * side);
        gx = px + side + 2.0f * pad;
        cur = at + m->letter[k].bytes;
        /* The words after it to the end of its line, from the picture; the
         * lines after that from the line's start. */
        q = new_line(mem, text, cur, next);
        if (q < next) {
            if (q > cur && !piece(c, mem, text, scratch, cur, q, gx, py, &px, &py)) return 0;
            cur = q + 2u;
            cx = x;
            cy = py + line;
        } else {
            cx = gx;
            cy = py;
        }
    }
    if (cur < m->len && !piece(c, mem, text, scratch, cur, m->len, cx, cy, &px, &py)) return 0;
    return m->n;
}
