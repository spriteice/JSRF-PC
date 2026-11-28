/* The message boxes' A and B drawn flat in the button prompts' styles
 * (prompt_buttons.h). Plain C on guest memory, so a test links it
 * (tests/prompt_buttons). */
#include "prompt_buttons.h"
#include "nv2a_prompts.h"          /* the mark */
#include "platform/recomp_check.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define GUEST_LOW   0x10000u   /* below it, no guest address the game uses */
#define MATRIX_SIZE 64u        /* 4 x 4 floats */
#define NODE_SIZE   (PROMPT_BUTTONS_NODE_R + 4u)
#define TOLD_KEYS   2          /* in the box and out of it */

/* A method header as D3D writes one: the mark's values, subchannel 0. */
#define MARK_HEADER ((NV2A_PROMPT_MARK_WORDS << 18) | NV2A_PROMPT_MARK_METHOD)

static uint32_t guest32(const uint8_t *mem, uint32_t va)
{
    uint32_t v;
    memcpy(&v, mem + va, sizeof v);
    return v;
}

static void put32(uint8_t *mem, uint32_t va, uint32_t v)
{
    memcpy(mem + va, &v, sizeof v);
}

static uint32_t float_bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}

/* Whether va to va + bytes is in the guest's memory, past its lowest page. */
static int in_guest(uint32_t top, uint32_t va, uint32_t bytes)
{
    return va >= GUEST_LOW && va < top && bytes <= top - va;
}

/* The buttons, by their number: each one's model in the display set and
 * the control it is. */
#define BUTTON_START 2
static const struct { uint32_t id; int control; const char *name; } k_buttons[PROMPT_BUTTONS_KINDS] = {
    { PROMPT_BUTTONS_A, INPUT_CTL_A, "A" },
    { PROMPT_BUTTONS_B, INPUT_CTL_B, "B" },
    { PROMPT_BUTTONS_START, INPUT_CTL_START, "Start" },
};

/* The Start's ball's node, once a walk found it, the walks that haven't,
 * and for the log's second line its flat draws so far, with its sphere's
 * least and most over the later half of them (its pulse). */
static int s_start_node = PROMPT_BUTTONS_LEARN;
static int s_start_tries;
static uint32_t s_start_draws;
static float s_start_least, s_start_most;

int prompt_buttons_id(uint32_t id)
{
    int b;
    for (b = 0; b < PROMPT_BUTTONS_KINDS; b++)
        if (k_buttons[b].id == id) return b;
    return -1;
}

int prompt_buttons_control(int button)
{
    return button >= 0 && button < PROMPT_BUTTONS_KINDS ? k_buttons[button].control : INPUT_CTL_A;
}

int prompt_buttons_start_node(void)
{
    return s_start_node;
}

void prompt_buttons_forget(void)
{
    s_start_node = PROMPT_BUTTONS_LEARN;
    s_start_tries = 0;
    s_start_draws = 0;
}

int prompt_buttons_open(PromptButtonsWalk *w, uint32_t model, uint32_t id, int art, int box)
{
    const int b = prompt_buttons_id(id);
    w->button = -1;
    if (art == PROMPT_ART_GAME || !model || b < 0) return 0;
    if (b == BUTTON_START && s_start_node == PROMPT_BUTTONS_LEARN && s_start_tries >= PROMPT_BUTTONS_TRIES)
        return 0;
    memset(w, 0, sizeof *w);
    w->button = b;
    w->box = box;
    w->art = art;
    w->only = b == BUTTON_START ? s_start_node : PROMPT_BUTTONS_ALL;
    w->model = model;
    w->id = id;
    return 1;
}

int prompt_buttons_begin(PromptButtonsWalk *w, const uint8_t *mem, uint32_t top, uint32_t models,
                         uint32_t model, int art, int box)
{
    int b;
    w->button = -1;
    if (art == PROMPT_ART_GAME || !model) return 0;
    for (b = 0; b < PROMPT_BUTTONS_KINDS; b++)
        if (in_guest(top, models, (k_buttons[b].id + 1u) * 4u)
         && model == guest32(mem, models + k_buttons[b].id * 4u))
            return prompt_buttons_open(w, model, k_buttons[b].id, art, box);
    return 0;
}

/* A node's sphere's own radius, before any matrix; 0 for none. */
static float own_radius(const uint8_t *mem, uint32_t top, uint32_t node)
{
    float r;
    if (!in_guest(top, node, NODE_SIZE)) return 0.0f;
    memcpy(&r, mem + node + PROMPT_BUTTONS_NODE_R, sizeof r);
    return r == r ? fabsf(r) : 0.0f;
}

int prompt_buttons_takes(const PromptButtonsWalk *w)
{
    if (w->button < 0) return 0;
    return w->only == PROMPT_BUTTONS_ALL || (w->only >= 0 && w->nodes == (uint32_t)w->only);
}

/* A node's sphere through the matrix on top of the game's stack: its
 * middle in view space v, its radius there *r and the matrix's scale
 * *scale. The sphere's middle, moved by the node's +0x14, through the
 * matrix (rows, D3D's way: a point is a row times it), as sub_0005BCE0
 * takes it; and its radius times the matrix's scale, the longest of its
 * first three rows, which a turn leaves as they are. 0 when the node or the
 * matrix isn't in the guest's memory. */
static int node_sphere(const uint8_t *mem, uint32_t top, uint32_t node, float v[3], float *r, float *scale)
{
    const uint32_t stack = in_guest(top, PROMPT_BUTTONS_MATRIX_TOP, 4u)
                         ? guest32(mem, PROMPT_BUTTONS_MATRIX_TOP) : 0;
    const uint32_t at = in_guest(top, stack, 4u) ? guest32(mem, stack) : 0;
    float m[16], c[3];
    int i, k;
    if (!in_guest(top, at, MATRIX_SIZE) || !in_guest(top, node, NODE_SIZE)) return 0;
    memcpy(m, mem + at, sizeof m);
    for (k = 0; k < 3; k++) {
        float mid, move;
        memcpy(&mid, mem + node + PROMPT_BUTTONS_NODE_MID + 4u * k, sizeof mid);
        memcpy(&move, mem + node + PROMPT_BUTTONS_NODE_MOVE + 4u * k, sizeof move);
        c[k] = mid + move;
    }
    for (k = 0; k < 3; k++) v[k] = c[0] * m[k] + c[1] * m[4 + k] + c[2] * m[8 + k] + m[12 + k];
    *scale = 0.0f;
    for (i = 0; i < 3; i++) {
        const float len = sqrtf(m[4 * i] * m[4 * i] + m[4 * i + 1] * m[4 * i + 1] + m[4 * i + 2] * m[4 * i + 2]);
        if (len > *scale) *scale = len;
    }
    *r = own_radius(mem, top, node) * *scale;
    return 1;
}

/* A node's sphere kept for the log, among the first PROMPT_BUTTONS_TOLD. */
static void note(PromptButtonsWalk *w, const float v[3], float r)
{
    if (w->nodes >= PROMPT_BUTTONS_TOLD) return;
    w->told[w->nodes][0] = v[0];
    w->told[w->nodes][1] = v[1];
    w->told[w->nodes][2] = v[2];
    w->told[w->nodes][3] = r;
}

void prompt_buttons_pass(PromptButtonsWalk *w, const uint8_t *mem, uint32_t top, uint32_t node)
{
    float v[3], r, scale;
    if (w->button < 0) return;
    if (w->nodes < PROMPT_BUTTONS_TOLD) w->own[w->nodes] = own_radius(mem, top, node);
    if (node_sphere(mem, top, node, v, &r, &scale)) note(w, v, r);
    w->nodes++;
}

void prompt_buttons_node(PromptButtonsWalk *w, const uint8_t *mem, uint32_t top, uint32_t node)
{
    float v[3], r, scale;
    if (w->button < 0 || !node_sphere(mem, top, node, v, &r, &scale)) return;
    if (w->nodes < PROMPT_BUTTONS_TOLD) w->own[w->nodes] = own_radius(mem, top, node);
    note(w, v, r);
    if (r == r && r > w->r) {
        w->r = r;
        w->scale = scale;
        w->node = w->nodes;
        memcpy(w->mid, v, sizeof v);
    }
    w->nodes++;
}

uint32_t prompt_buttons_node_ret(PromptButtonsWalk *w, const uint8_t *mem, uint32_t top, uint32_t esp)
{
    prompt_buttons_node(w, mem, top, in_guest(top, esp + 8u, 4u) ? guest32(mem, esp + 8u) : 0);
    return esp + 4u;
}

int prompt_buttons_place(const PromptButtonsWalk *w, float focal, float tall, const uint32_t view[4],
                         float *x, float *y, float *half)
{
    const float z = w->mid[2];
    if (!(w->r > 0.0f) || !(z > 0.0f) || !(focal > 0.0f)) return 0;
    if (!(tall > 0.0f)) tall = 1.0f;
    /* As prompt_a_draw (recomp_manual.c) puts the skip prompt's A, the
     * other way: from the viewport's middle, focal pixels at a depth of 1. */
    *x = (float)view[0] + 0.5f * (float)view[2] + w->mid[0] * focal / z;
    *y = (float)view[1] + 0.5f * (float)view[3] - w->mid[1] * focal * tall / z;
    /* The Start's picture is fitted under its bubble's word, not grown. */
    *half = (w->button == BUTTON_START ? PROMPT_BUTTONS_START_SIDE
                                       : PROMPT_BUTTONS_GROW * NV2A_PROMPT_LARGER) * w->r * focal / z;
    return *x == *x && *y == *y && *half == *half;
}

uint32_t prompt_buttons_push(uint8_t *mem, uint32_t device, const uint32_t *words,
                             widescreen_hud_space_fn make_space)
{
    const uint32_t bytes = (1u + NV2A_PROMPT_MARK_WORDS) * 4u;
    uint32_t put = guest32(mem, device + WIDE_HUD_D3D_PUT), limit, i;
    limit = guest32(mem, device + WIDE_HUD_D3D_LIMIT);
    if (put > limit || limit - put < bytes) {
        put = make_space();
        limit = guest32(mem, device + WIDE_HUD_D3D_LIMIT);
        if (!RECOMP_CHECK(put && put <= limit && limit - put >= bytes && !(put & 3u),
                          "D3D's make-space gave %08X with the limit at %08X: no flat button", put, limit))
            return 0;
    }
    put32(mem, put, MARK_HEADER);
    for (i = 0; i < NV2A_PROMPT_MARK_WORDS; i++) put32(mem, put + 4u + 4u * i, words[i]);
    put32(mem, device + WIDE_HUD_D3D_PUT, put + bytes);
    return put;
}

uint32_t prompt_buttons_upright(uint8_t *mem, uint32_t device, float grow,
                                widescreen_hud_space_fn make_space)
{
    const uint32_t words[NV2A_PROMPT_MARK_WORDS] = { NV2A_PROMPT_UPRIGHT_TAG, float_bits(grow), 0u, 0u };
    return device ? prompt_buttons_push(mem, device, words, make_space) : 0u;
}

uint32_t prompt_buttons_line(uint8_t *mem, uint32_t device, int open, widescreen_hud_space_fn make_space)
{
    const uint32_t words[NV2A_PROMPT_MARK_WORDS] = {
        NV2A_PROMPT_LINE_TAG, open ? NV2A_PROMPT_LINE_OPEN : NV2A_PROMPT_LINE_SHUT, 0u, 0u
    };
    return device ? prompt_buttons_push(mem, device, words, make_space) : 0u;
}

/* Once for each button, in the box and out of it: where it went, from
 * which node, with each node's sphere (its size is to be checked against
 * the game's own in the test build). The Start's first flat draw is at no
 * size, as its bubble opens, so its line is given again at its
 * PROMPT_BUTTONS_START_AGAIN-th, with how far its sphere's size went over
 * the later half of those draws: its pulse, if the game's is a size. */
static void tell(const PromptButtonsWalk *w, int placed, float x, float y, float half, float focal)
{
    static unsigned char told[PROMPT_BUTTONS_KINDS][TOLD_KEYS];
    const char *name = k_buttons[w->button].name;
    const int start = w->button == BUTTON_START && !w->box;
    char line[1024];
    size_t at;
    uint32_t i;
    int again = 0;
    if (start && placed && s_start_draws < PROMPT_BUTTONS_START_AGAIN) {
        /* Its sphere's range, over the draws after the first half. */
        if (++s_start_draws <= PROMPT_BUTTONS_START_AGAIN / 2u + 1u) s_start_least = s_start_most = w->r;
        if (w->r < s_start_least) s_start_least = w->r;
        if (w->r > s_start_most) s_start_most = w->r;
        again = s_start_draws == PROMPT_BUTTONS_START_AGAIN;
    }
    if (told[w->button][w->box != 0] && !again) return;
    told[w->button][w->box != 0] = 1;
    at = (size_t)snprintf(line, sizeof line, "[INPUT] button prompts: %s %s (model %X)%s ",
                          w->box ? "the message box's" : w->button == BUTTON_START ? "the save point's"
                          : w->button ? "a" : "an", name, (unsigned)w->id,
                          w->box ? "" : ", not a message box's,");
    if (again)
        at += (size_t)snprintf(line + at, sizeof line - at, "at its flat draw %u, its sphere %.2f to %.2f "
                               "over its last %u, ", (unsigned)PROMPT_BUTTONS_START_AGAIN,
                               (double)s_start_least, (double)s_start_most,
                               (unsigned)(PROMPT_BUTTONS_START_AGAIN - PROMPT_BUTTONS_START_AGAIN / 2u));
    if (placed)
        at += (size_t)snprintf(line + at, sizeof line - at,
                               "drawn as a flat picture: node %u of %u, sphere %.2f at %.1f, %.1f, %.1f, "
                               "scale %.3f, on screen at %.1f, %.1f, %.1f pixels across (focal %.1f);",
                               w->node, w->nodes, (double)w->r, (double)w->mid[0], (double)w->mid[1],
                               (double)w->mid[2], (double)w->scale, (double)x, (double)y,
                               (double)(2.0f * half), (double)focal);
    else
        at += (size_t)snprintf(line + at, sizeof line - at,
                               "not drawn: %u nodes and no sphere in front (focal %.1f);", w->nodes,
                               (double)focal);
    for (i = 0; i < w->nodes && i < PROMPT_BUTTONS_TOLD && at < sizeof line; i++)
        at += (size_t)snprintf(line + at, sizeof line - at, " node %u%s %.2f at %.1f, %.1f, %.1f%s", i,
                               w->only >= 0 && i != (uint32_t)w->only ? " (the game's own)" : "",
                               (double)w->told[i][3], (double)w->told[i][0], (double)w->told[i][1],
                               (double)w->told[i][2], i + 1 < w->nodes ? "," : "");
    fprintf(stderr, "%s\n", line);
}

/* A walk that took no node, of the Start's model: the node with the
 * smallest sphere of its own above 0 is the ball. */
static void learn(const PromptButtonsWalk *w)
{
    uint32_t i, n = w->nodes < PROMPT_BUTTONS_TOLD ? w->nodes : PROMPT_BUTTONS_TOLD;
    int ball = -1;
    for (i = 0; i < n; i++)
        if (w->own[i] > 0.0f && (ball < 0 || w->own[i] < w->own[ball])) ball = (int)i;
    if (w->nodes == 1u && ball == 0) {
        s_start_node = PROMPT_BUTTONS_ALL;
        fprintf(stderr, "[INPUT] button prompts: the save point's Start (model %X) is one node, sphere "
                "%.2f: all of it drawn flat from now on\n", (unsigned)w->id, (double)w->own[0]);
    } else if (ball >= 0 && w->nodes <= PROMPT_BUTTONS_TOLD) {
        char line[512];
        size_t at = 0;
        s_start_node = ball;
        for (i = 0; i < n && at < sizeof line; i++)
            at += (size_t)snprintf(line + at, sizeof line - at, "%s%.2f", i ? ", " : "", (double)w->own[i]);
        fprintf(stderr, "[INPUT] button prompts: the save point's Start (model %X) has %u nodes, their "
                "spheres' own radii %s: node %d, the smallest, is the ball, drawn flat from now on, the "
                "rest as the game draws them\n", (unsigned)w->id, w->nodes, line, ball);
    } else if (++s_start_tries == PROMPT_BUTTONS_TRIES) {
        fprintf(stderr, "[INPUT] button prompts: the save point's Start (model %X): no ball found in %d "
                "draws (%u nodes): left as the game draws it\n", (unsigned)w->id, PROMPT_BUTTONS_TRIES,
                w->nodes);
    }
}

uint32_t prompt_buttons_end(PromptButtonsWalk *w, uint8_t *mem, uint32_t device, float focal, float tall,
                            const uint32_t view[4], widescreen_hud_space_fn make_space)
{
    float x = 0.0f, y = 0.0f, half = 0.0f;
    uint32_t words[NV2A_PROMPT_MARK_WORDS], put = 0;
    int placed;
    if (w->button < 0) return 0;
    if (w->only == PROMPT_BUTTONS_LEARN) {
        learn(w);
        w->button = -1;
        return 0;
    }
    placed = prompt_buttons_place(w, focal, tall, view, &x, &y, &half);
    if (placed && device) {
        words[0] = nv2a_prompts_mark(prompt_buttons_control(w->button), w->box, w->art);
        words[1] = float_bits(x);
        words[2] = float_bits(y);
        words[3] = float_bits(half);
        put = prompt_buttons_push(mem, device, words, make_space);
    }
    tell(w, placed, x, y, half, focal);
    w->button = -1;
    return put;
}
