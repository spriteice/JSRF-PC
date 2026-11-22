/* Run with: python tests/prompt_buttons/run.py
 *
 * The message boxes' A and B drawn flat (port/prompt_buttons.c), without the
 * game: a made-up display set's table of models, the matrix stack's top,
 * nodes and D3D's push buffer in a buffer that stands in for guest memory.
 * A walk opens only for the A or the B, in an art other than the game's;
 * its nodes' spheres are taken through the matrix as the game's own test
 * takes them, the largest kept, the same turned or not, and the pulse's
 * size in it; it is placed on the title's screen through the projection;
 * and its mark goes into the push buffer, after making space where it
 * wouldn't fit. An upright size's mark (the VS screen's) goes the same
 * way, and reads back as its size. run.py checks the log's lines.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "prompt_buttons.h"
#include "nv2a_prompts.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
#define CLOSE(a, b) (fabsf((float)(a) - (float)(b)) < 1e-3f * (1.0f + fabsf((float)(b))))

#define TOP     (4u << 20)   /* past the matrix stack's pointer, 0x264C04 */
#define MODELS  0x20000u     /* the display set's table of models, by id */
#define MODEL_A 0x30000u
#define MODEL_B 0x31000u
#define OTHER   0x32000u     /* a model of the set that is neither */
#define MODEL_BANDS 0x33000u   /* the set's 0x6B: the menus' bands, no button */
#define ESP     0x40000u     /* the guest's stack, as a node function finds it */
#define STACK   0x50000u     /* the stack's top entry */
#define MATRIX  0x51000u
#define NODES   0x60000u     /* three nodes, 0x6C apart */
#define NODE_SIZE 0x6Cu
#define DEVICE  0x70000u     /* D3D's device: its put and limit */
#define PUSH    0x80000u     /* its push buffer */
#define PUSH2   0x90000u     /* where make_space moves it */

static uint8_t *mem;
static int spaces;
static uint32_t space_gives;

static void put32(uint32_t va, uint32_t v) { memcpy(mem + va, &v, sizeof v); }
static uint32_t get32(uint32_t va) { uint32_t v; memcpy(&v, mem + va, sizeof v); return v; }
static void putf(uint32_t va, float f) { memcpy(mem + va, &f, sizeof f); }
static float bitsf(uint32_t u) { float f; memcpy(&f, &u, sizeof f); return f; }

static uint32_t make_space(void)
{
    spaces++;
    if (space_gives) {
        put32(DEVICE + WIDE_HUD_D3D_PUT, space_gives);
        put32(DEVICE + WIDE_HUD_D3D_LIMIT, space_gives + 0x100u);
    }
    return space_gives;
}

/* The matrix on top: turned by a about z, sized s, moved to (tx, ty, tz);
 * rows, D3D's way. */
static void matrix(float a, float s, float tx, float ty, float tz)
{
    const float c = cosf(a) * s, n = sinf(a) * s;
    const float m[16] = { c, n, 0, 0, -n, c, 0, 0, 0, 0, s, 0, tx, ty, tz, 1 };
    memcpy(mem + MATRIX, m, sizeof m);
}

/* Node i: its sphere's middle mid, its move and its radius. */
static void node(uint32_t i, float mx, float my, float mz, float vx, float vy, float vz, float r)
{
    const uint32_t n = NODES + i * NODE_SIZE;
    putf(n + PROMPT_BUTTONS_NODE_MID, mx);
    putf(n + PROMPT_BUTTONS_NODE_MID + 4u, my);
    putf(n + PROMPT_BUTTONS_NODE_MID + 8u, mz);
    putf(n + PROMPT_BUTTONS_NODE_MOVE, vx);
    putf(n + PROMPT_BUTTONS_NODE_MOVE + 4u, vy);
    putf(n + PROMPT_BUTTONS_NODE_MOVE + 8u, vz);
    putf(n + PROMPT_BUTTONS_NODE_R, r);
}

static void make(void)
{
    memset(mem, 0, TOP);
    put32(MODELS + PROMPT_BUTTONS_A * 4u, MODEL_A);
    put32(MODELS + PROMPT_BUTTONS_B * 4u, MODEL_B);
    put32(MODELS + 0x68u * 4u, OTHER);
    put32(MODELS + 0x6Bu * 4u, MODEL_BANDS);
    put32(PROMPT_BUTTONS_MATRIX_TOP, STACK);
    put32(STACK, MATRIX);
    /* A rim, the coin (the largest, its middle off the node's origin and
     * moved), and the letter. */
    node(0, 0, 0, 0, 0, 0, 0, 3.0f);
    node(1, 1.0f, 2.0f, 0, 0, 0, 0.5f, 10.0f);
    node(2, 0, 0, 0, 0, 0, 0, 6.0f);
    put32(DEVICE + WIDE_HUD_D3D_PUT, PUSH);
    put32(DEVICE + WIDE_HUD_D3D_LIMIT, PUSH + 0x100u);
}

/* The three nodes walked under the matrix on top. */
static void walk(PromptButtonsWalk *w)
{
    uint32_t i;
    for (i = 0; i < 3; i++) prompt_buttons_node(w, mem, TOP, NODES + i * NODE_SIZE);
}

int main(void)
{
    static const uint32_t view[4] = { 0, 0, 640, 480 };
    PromptButtonsWalk w, turned, by_ret;
    float x, y, half;
    int button, box, art;
    uint32_t put, i;
    mem = (uint8_t *)calloc(TOP, 1);
    CHECK(mem != NULL);
    make();

    /* Opened only for the A or the B, in an art other than the game's. */
    CHECK(prompt_buttons_begin(&w, mem, TOP, MODELS, MODEL_A, PROMPT_ART_PLAYSTATION, 1));
    CHECK(w.button == 0 && w.box == 1 && w.art == PROMPT_ART_PLAYSTATION && w.nodes == 0);
    CHECK(prompt_buttons_begin(&w, mem, TOP, MODELS, MODEL_B, PROMPT_ART_KEYS, 0));
    CHECK(w.button == 1 && w.box == 0 && w.art == PROMPT_ART_KEYS);
    CHECK(!prompt_buttons_begin(&w, mem, TOP, MODELS, MODEL_A, PROMPT_ART_GAME, 1) && w.button == -1);
    CHECK(!prompt_buttons_begin(&w, mem, TOP, MODELS, OTHER, PROMPT_ART_NINTENDO, 1) && w.button == -1);
    CHECK(!prompt_buttons_begin(&w, mem, TOP, MODELS, 0, PROMPT_ART_NINTENDO, 1));
    CHECK(!prompt_buttons_begin(&w, mem, TOP, 0, MODEL_A, PROMPT_ART_NINTENDO, 1));
    CHECK(!prompt_buttons_begin(&w, mem, TOP, TOP - 8u, MODEL_A, PROMPT_ART_NINTENDO, 1));
    /* The set's 0x6B is the menus' bands, no button (prompts7 drew it as a
     * B across the whole menu): never begun. The A and the B by their ids
     * from any set (the getter's note, recomp_manual.c). */
    CHECK(!prompt_buttons_begin(&w, mem, TOP, MODELS, MODEL_BANDS, PROMPT_ART_XBOX, 0) && w.button == -1);
    CHECK(prompt_buttons_open(&w, OTHER, PROMPT_BUTTONS_A, PROMPT_ART_NINTENDO, 0));
    CHECK(w.button == 0 && w.model == OTHER && w.id == PROMPT_BUTTONS_A && w.nodes == 0);
    CHECK(prompt_buttons_open(&w, OTHER, PROMPT_BUTTONS_B, PROMPT_ART_KEYS, 1) && w.button == 1 && w.box == 1);
    CHECK(!prompt_buttons_open(&w, OTHER, PROMPT_BUTTONS_B, PROMPT_ART_GAME, 0) && w.button == -1);
    CHECK(!prompt_buttons_open(&w, MODEL_BANDS, 0x6Bu, PROMPT_ART_NINTENDO, 0) && w.button == -1);
    CHECK(!prompt_buttons_open(&w, 0, PROMPT_BUTTONS_A, PROMPT_ART_NINTENDO, 0) && w.button == -1);
    CHECK(prompt_buttons_id(PROMPT_BUTTONS_A) == 0 && prompt_buttons_id(PROMPT_BUTTONS_B) == 1
          && prompt_buttons_id(0x6Bu) == -1 && prompt_buttons_id(0x68u) == -1 && prompt_buttons_id(0x11u) == -1);
    /* A closed walk takes no node. */
    walk(&w);
    CHECK(w.button == -1 && w.nodes == 0);

    /* The largest sphere, through the matrix: unturned at size 1, the
     * coin's middle (1, 2, 0.5) moved to (10, -5, 1000). */
    matrix(0.0f, 1.0f, 10.0f, -5.0f, 1000.0f);
    CHECK(prompt_buttons_begin(&w, mem, TOP, MODELS, MODEL_A, PROMPT_ART_NINTENDO, 1));
    walk(&w);
    CHECK(w.nodes == 3 && w.node == 1);
    CHECK(CLOSE(w.r, 10.0f) && CLOSE(w.scale, 1.0f));
    CHECK(CLOSE(w.mid[0], 11.0f) && CLOSE(w.mid[1], -3.0f) && CLOSE(w.mid[2], 1000.5f));

    /* A node function stood in for: its node taken from esp + 8 (its
     * return address at esp, the first argument at esp + 4), and esp + 4
     * given back, as its own `ret` would leave it. Without that the guest
     * stack slips 4 bytes for each node skipped, and the game crashes
     * after the walk. Closed, or with esp + 8 out of guest memory, no node
     * is taken, and the ret is the same. */
    CHECK(prompt_buttons_begin(&by_ret, mem, TOP, MODELS, MODEL_A, PROMPT_ART_NINTENDO, 1));
    for (i = 0; i < 3; i++) {
        put32(ESP, 0x000476B5u);
        put32(ESP + 4u, NODES + 2u * NODE_SIZE);
        put32(ESP + 8u, NODES + i * NODE_SIZE);
        CHECK(prompt_buttons_node_ret(&by_ret, mem, TOP, ESP) == ESP + 4u);
    }
    CHECK(by_ret.nodes == 3 && by_ret.node == 1 && CLOSE(by_ret.r, 10.0f));
    CHECK(CLOSE(by_ret.mid[0], 11.0f) && CLOSE(by_ret.mid[1], -3.0f) && CLOSE(by_ret.mid[2], 1000.5f));
    CHECK(prompt_buttons_node_ret(&by_ret, mem, TOP, TOP - 8u) == TOP - 4u && by_ret.nodes == 3);
    by_ret.button = -1;
    CHECK(prompt_buttons_node_ret(&by_ret, mem, TOP, ESP) == ESP + 4u && by_ret.nodes == 3);

    /* Turned, it is the same size, and its middle is where the turn puts
     * it: no turn is kept, only the place. Sized by the pulse, its radius
     * with it. */
    matrix(0.7f, 1.0f, 10.0f, -5.0f, 1000.0f);
    CHECK(prompt_buttons_begin(&turned, mem, TOP, MODELS, MODEL_A, PROMPT_ART_NINTENDO, 1));
    walk(&turned);
    CHECK(turned.node == 1 && CLOSE(turned.r, 10.0f) && CLOSE(turned.scale, 1.0f));
    CHECK(CLOSE(turned.mid[0], 10.0f + cosf(0.7f) - 2.0f * sinf(0.7f)));
    CHECK(CLOSE(turned.mid[1], -5.0f + sinf(0.7f) + 2.0f * cosf(0.7f)));
    matrix(0.7f, 1.25f, 10.0f, -5.0f, 1000.0f);
    CHECK(prompt_buttons_begin(&turned, mem, TOP, MODELS, MODEL_A, PROMPT_ART_NINTENDO, 1));
    walk(&turned);
    CHECK(CLOSE(turned.r, 12.5f) && CLOSE(turned.scale, 1.25f));
    turned.button = -1;

    /* On the screen: from the viewport's middle, focal pixels at a depth of
     * 1; the height's share of it down; the picture a quarter wider than the
     * sphere (10 at that depth), then 12.5% larger (NV2A_PROMPT_LARGER). */
    CHECK(prompt_buttons_place(&w, 1000.5f, 1.0f, view, &x, &y, &half));
    CHECK(CLOSE(x, 331.0f) && CLOSE(y, 243.0f) && CLOSE(half, 12.5f * 1.125f));
    CHECK(prompt_buttons_place(&w, 1000.5f, 2.0f, view, &x, &y, &half));
    CHECK(CLOSE(y, 246.0f) && CLOSE(half, 12.5f * 1.125f));
    {
        static const uint32_t part[4] = { 100, 50, 200, 100 };
        CHECK(prompt_buttons_place(&w, 1000.5f, 1.0f, part, &x, &y, &half));
        CHECK(CLOSE(x, 211.0f) && CLOSE(y, 103.0f));
    }
    CHECK(!prompt_buttons_place(&w, 0.0f, 1.0f, view, &x, &y, &half));

    /* The mark, at the put pointer, and the pointer past it. */
    put = prompt_buttons_end(&w, mem, DEVICE, 1000.5f, 1.0f, view, make_space);
    CHECK(put == PUSH && spaces == 0 && w.button == -1);
    CHECK(get32(PUSH) == ((NV2A_PROMPT_MARK_WORDS << 18) | NV2A_PROMPT_MARK_METHOD));
    CHECK(nv2a_prompts_mark_read(get32(PUSH + 4u), &button, &box, &art));
    CHECK(button == 0 && box == 1 && art == PROMPT_ART_NINTENDO);
    CHECK(CLOSE(bitsf(get32(PUSH + 8u)), 331.0f) && CLOSE(bitsf(get32(PUSH + 12u)), 243.0f)
          && CLOSE(bitsf(get32(PUSH + 16u)), 12.5f * 1.125f));
    CHECK(get32(DEVICE + WIDE_HUD_D3D_PUT) == PUSH + 20u);
    /* Anything else isn't read as one. */
    CHECK(!nv2a_prompts_mark_read(0x48550000u, &button, &box, &art));
    CHECK(!nv2a_prompts_mark_read(NV2A_PROMPT_MARK_TAG | 0x2u, &button, &box, &art));
    CHECK(nv2a_prompts_mark_read(NV2A_PROMPT_MARK_TAG | 0x200u, &button, &box, &art));   /* a fade */

    /* An upright size's mark, as the VS screen's objects' draws write it:
     * read as its size, and not as a flat button's; a flat button's, a size
     * of 0, none, past the most, or a stray word, not read as one; none with
     * no device. */
    {
        uint32_t words[NV2A_PROMPT_MARK_WORDS];
        float grow = 0.0f;
        unsigned k;
        put32(DEVICE + WIDE_HUD_D3D_PUT, PUSH);
        CHECK(prompt_buttons_upright(mem, DEVICE, PROMPT_BUTTONS_VS_UPRIGHT, make_space) == PUSH);
        CHECK(get32(PUSH) == ((NV2A_PROMPT_MARK_WORDS << 18) | NV2A_PROMPT_MARK_METHOD) && spaces == 0);
        CHECK(get32(DEVICE + WIDE_HUD_D3D_PUT) == PUSH + 20u);
        for (k = 0; k < NV2A_PROMPT_MARK_WORDS; k++) words[k] = get32(PUSH + 4u + 4u * k);
        CHECK(nv2a_prompts_upright_read(words, &grow) && CLOSE(grow, 0.956f));
        CHECK(!nv2a_prompts_mark_read(words[0], &button, &box, &art));
        words[1] = 0x7FC00000u;                 /* no number */
        CHECK(!nv2a_prompts_upright_read(words, &grow));
        words[1] = 0u;
        CHECK(!nv2a_prompts_upright_read(words, &grow));
        memcpy(&words[1], &(float){ NV2A_PROMPT_UPRIGHT_MOST * 2.0f }, sizeof words[1]);
        CHECK(!nv2a_prompts_upright_read(words, &grow));
        memcpy(&words[1], &(float){ NV2A_PROMPT_UPRIGHT_GROW }, sizeof words[1]);
        CHECK(nv2a_prompts_upright_read(words, &grow) && grow == NV2A_PROMPT_UPRIGHT_GROW);
        words[3] = 1u;
        CHECK(!nv2a_prompts_upright_read(words, &grow));
        words[3] = 0u;
        words[0] = nv2a_prompts_mark(0, 1, PROMPT_ART_XBOX);
        CHECK(!nv2a_prompts_upright_read(words, &grow));
        CHECK(prompt_buttons_upright(mem, 0, PROMPT_BUTTONS_VS_UPRIGHT, make_space) == 0);
    }

    /* The B out of the box, near the limit: space made first. */
    put32(DEVICE + WIDE_HUD_D3D_PUT, PUSH + 0x100u - 16u);
    space_gives = PUSH2;
    matrix(0.0f, 1.0f, -20.0f, 0.0f, 500.0f);
    CHECK(prompt_buttons_begin(&w, mem, TOP, MODELS, MODEL_B, PROMPT_ART_XBOX, 0));
    walk(&w);
    put = prompt_buttons_end(&w, mem, DEVICE, 500.0f, 1.0f, view, make_space);
    CHECK(put == PUSH2 && spaces == 1);
    CHECK(nv2a_prompts_mark_read(get32(PUSH2 + 4u), &button, &box, &art));
    CHECK(button == 1 && box == 0 && art == PROMPT_ART_XBOX);
    CHECK(get32(DEVICE + WIDE_HUD_D3D_PUT) == PUSH2 + 20u);

    /* No room after making space: no mark, said once. */
    put32(DEVICE + WIDE_HUD_D3D_PUT, PUSH2 + 0x100u);
    space_gives = 0;
    CHECK(prompt_buttons_begin(&w, mem, TOP, MODELS, MODEL_B, PROMPT_ART_XBOX, 0));
    walk(&w);
    CHECK(prompt_buttons_end(&w, mem, DEVICE, 500.0f, 1.0f, view, make_space) == 0 && spaces == 2);
    CHECK(prompt_buttons_begin(&w, mem, TOP, MODELS, MODEL_B, PROMPT_ART_XBOX, 0));
    walk(&w);
    CHECK(prompt_buttons_end(&w, mem, DEVICE, 500.0f, 1.0f, view, make_space) == 0 && spaces == 3);

    /* Behind the eye: not placed, nothing written, said once. */
    put32(DEVICE + WIDE_HUD_D3D_PUT, PUSH);
    matrix(0.0f, 1.0f, 0.0f, 0.0f, -100.0f);
    CHECK(prompt_buttons_begin(&w, mem, TOP, MODELS, MODEL_A, PROMPT_ART_KEYS, 0));
    walk(&w);
    CHECK(prompt_buttons_end(&w, mem, DEVICE, 500.0f, 1.0f, view, make_space) == 0);
    CHECK(get32(DEVICE + WIDE_HUD_D3D_PUT) == PUSH && w.button == -1);

    printf("prompt_buttons: all cases pass\n");
    free(mem);
    return 0;
}
