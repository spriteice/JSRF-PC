/* The message boxes' A and B drawn flat in the button prompts' styles.
 *
 * The game's message box (class 0x1CC660, its draw sub_0006EC80) shows its
 * buttons as 3D models of the display set (SprNorm.bin, group 10): model
 * 0x69, the green A, and model 0x6A, the B, posed by animation 0x11, whose
 * size key pulses them at the game's frame. Each node's rest pose tilts
 * them, which the animation doesn't key (the test build prompts2's log,
 * button-prompts/logs/LOG-3 in the shared folder). Skip cutscenes' prompt
 * draws model 0x69 the same way (recomp_manual.c, "The prompt's A").
 * Another set's A and B, fetched from the display set by their ids, are
 * drawn flat too (recomp_manual.c, "The menus' 3D A and B").
 *
 * In the game's own art (src/input/input_prompts.h: the Default style) they
 * are left as the game draws them. In any other, Xbox and Auto included,
 * the models' draw (sub_00047690, which every 3D model goes through;
 * its override in recomp_manual.c) still poses the button, but none of its
 * nodes is drawn: each node function (the table at 0x1FA1B0, called with
 * the node's matrix on top of the game's matrix stack) hands its node here
 * instead (prompt_buttons_node). Each node's bounding sphere is taken as
 * the game's own test of it takes it (sub_0005BCE0): its middle, +0x54,
 * moved by +0x14, through that matrix, and its radius, +0x60, times the
 * matrix's scale. The largest is where the button is and how big, with the
 * pulse and without the turn. It is projected to the title's 640 x 480
 * pixels through the projection the game's device has then (its focal
 * length, the height's share of it, and the viewport), and a mark is
 * written into D3D's push buffer there (nv2a_prompts.h,
 * NV2A_PROMPT_MARK_METHOD), in the frame's order where the button would
 * have been drawn: over the box, under its text. The renderer draws the
 * style's picture of the control on that button at the mark, flat
 * (src/nv2a_gl/nv2a_prompts.c). The message box's draw's override says the
 * buttons it draws are the box's (the mark's box bit, for the log); no
 * picture is white, so each shows on the box's white as it is.
 *
 * The Start at a graffiti save point (spriteice, 02:35 and 02:41 UTC on 22
 * November 2026: drawn flat, 2D, with its pulse) is the talk bubble's
 * (class 0x1D1D00, its draw sub_00100D80) of type 2: the display set's
 * model 0xFE, posed by animations 0x2D and 0x2E, drawn through the same
 * models' draw. The class's other two bubbles aren't buttons and are left
 * as the game draws them: type 1's, model 0x7B, has a talking head, by a
 * character who can be spoken to (the test build editor1 took that one for
 * the Start: spriteice's test list, 22 November 2026, 10:48pm UTC+8, "it
 * has not changed" at the save point, and the talk prompt showing Start);
 * type 0's is model 0x39.
 *
 * The Start's ball is the menus' round button, 120 triangles with its
 * picture from the HUD's icon sheet (Disp/SprNorm1.dat, texels 204 to 232
 * by 454 to 482: editor1's log, at the save point), as the graffiti and VS
 * menus' buttons are. Those are cards a menu's own draw turns, which the
 * renderer can only stand upright; this one is a model's node, so it is
 * taken by the message box's path and drawn flat. The model is the whole
 * bubble, three nodes, of which only the ball is the button: the others
 * are drawn as the game draws them and the ball's alone is taken for the
 * picture (the walk's `only`). Which node the ball is isn't in the code:
 * the model's first draw in a style is walked without taking any (the game
 * draws it whole, at no size as the bubble opens), and the node with the
 * smallest sphere of its own above 0 is the ball from then on
 * (prompt_buttons_end; the log gives every node's sphere and says which).
 * The picture is of what presses Start now, and pulses as the ball does, by
 * the animation's scale on its node.
 *
 * The ball lies back in its bubble, under the word START, so it shows about
 * two thirds as tall as it is wide (80 by 55 pixels at 1080 lines in
 * spriteice's screenshots, its sphere as wide as it), in the 70 or so
 * between the word and the bubble's lower edge. A flat picture as wide as
 * the ball, let alone PROMPT_BUTTONS_GROW times it, would cover the word.
 * So the Start's picture is PROMPT_BUTTONS_START_SIDE of its ball's sphere
 * across, and a round button, three quarters of its picture's side, shows
 * nearly as tall as the game's ball: 51 pixels to the ball's 55. (The test
 * build editor2 had 0.65, which reckoned the picture's side as the button's
 * and showed one of 39: spriteice, 23 November 2026, 8:53am UTC+8, "make
 * the start glyph slightly larger", with a picture of the game's own ball
 * as the "size of the default for reference".)
 *
 * Nothing in the game's code is wrong: it is a feature's hook
 * (port/README.md), in the models' draw and the node functions, which
 * recomp_manual.c already overrides for the widescreen menus. The buttons
 * are told apart by their address in the display set's table of models, so
 * whatever draws them is covered. Plain C on guest memory, so a test links
 * it (tests/prompt_buttons). The game's thread. */
#ifndef JSRF_PROMPT_BUTTONS_H
#define JSRF_PROMPT_BUTTONS_H

#include <stdint.h>
#include "widescreen_hud.h"   /* widescreen_hud_space_fn, D3D's put and limit */

#define PROMPT_BUTTONS_A          0x69u      /* the display set's models, by id: the A */
#define PROMPT_BUTTONS_B          0x6Au      /* ... the B */
#define PROMPT_BUTTONS_START      0xFEu      /* ... and the save point's bubble with the Start's ball */
#define PROMPT_BUTTONS_KINDS      3          /* the A, the B and the Start */
#define PROMPT_BUTTONS_ALL        (-1)       /* a walk's `only`: every node is the button's */
#define PROMPT_BUTTONS_LEARN      (-2)       /* ... none is taken: the walk finds which node is */
#define PROMPT_BUTTONS_TRIES      8          /* walks that find no node before the model is left as it is */
#define PROMPT_BUTTONS_MATRIX_TOP 0x264C04u  /* -> the matrix stack's top, -> its matrix */
#define PROMPT_BUTTONS_NODE_MOVE  0x14u      /* a node's: its sphere's move (3 floats) */
#define PROMPT_BUTTONS_NODE_MID   0x54u      /*   its bounding sphere's middle (3 floats) */
#define PROMPT_BUTTONS_NODE_R     0x60u      /*   and radius */
#define PROMPT_BUTTONS_TOLD       8u         /* the nodes the first walk's line gives */
#define PROMPT_BUTTONS_GROW       1.25f      /* the picture a quarter wider than the button's sphere, to read at a glance */
#define PROMPT_BUTTONS_START_SIDE 0.85f      /* the Start's picture's side, of its ball's sphere's: under the word */
#define PROMPT_BUTTONS_START_AGAIN 120u      /* the Start's flat draw the log gives again, its bubble open */

/* One button's draw, walked: the largest of its nodes' spheres. */
typedef struct {
    int      button;                  /* 0 the A, 1 the B, 2 the Start; -1 while no walk is open */
    int      box;                     /* the message box's (its draw is running) */
    int      art;                     /* the prompts' art (PROMPT_ART_...) */
    int      only;                    /* the one node that is the button, PROMPT_BUTTONS_ALL or _LEARN */
    uint32_t model;
    uint32_t id;                      /* its id in the display set: PROMPT_BUTTONS_A, _B or _START */
    uint32_t nodes;                   /* nodes handed in, taken or drawn */
    uint32_t node;                    /* the largest taken's index among them */
    float    mid[3], r, scale;        /* its middle in view space, its radius there, the matrix's scale */
    float    told[PROMPT_BUTTONS_TOLD][4];   /* the first nodes' middles and radii, for the log */
    float    own[PROMPT_BUTTONS_TOLD];       /* ... and their spheres' own radii, before the matrix */
} PromptButtonsWalk;

/* The button a display set's model id is: 0 the A (PROMPT_BUTTONS_A), 1 the
 * B (PROMPT_BUTTONS_B), 2 the Start (PROMPT_BUTTONS_START), -1 none. */
int prompt_buttons_id(uint32_t id);

/* The Xbox pad's control (INPUT_CTL_...) a button is: what its mark shows.
 * INPUT_CTL_A for one that is none. */
int prompt_buttons_control(int button);

/* Which node of the Start's model is its ball: PROMPT_BUTTONS_LEARN until a
 * walk has found it, then its index, or PROMPT_BUTTONS_ALL for a model of
 * one node. prompt_buttons_forget puts it back to not found (a test's). */
int prompt_buttons_start_node(void);
void prompt_buttons_forget(void);

/* A walk of model opened in *w, as the button its display set id names
 * (prompt_buttons_id), the message box's if box: 1 when opened, as
 * prompt_buttons_begin. The Start's takes only its ball's node, or none
 * while that isn't known (above). 0, *w left closed, for the game's own
 * art (PROMPT_ART_GAME), no model, an id that is no button, or the Start's
 * model once PROMPT_BUTTONS_TRIES walks have found no ball in it. */
int prompt_buttons_open(PromptButtonsWalk *w, uint32_t model, uint32_t id, int art, int box);

/* Before the models' draw of model, in guest memory mem of top bytes: when
 * art isn't the game's own (PROMPT_ART_GAME) and model is the A, the B or
 * the Start of models (the display set's table of models by id; 0 while
 * none is loaded), a walk of it is opened in *w, the message box's if box.
 * 1 when opened: the draw then poses it and hands each node in, and draws
 * only the nodes that aren't the button's (prompt_buttons_takes). 0, *w
 * left closed, for anything else. */
int prompt_buttons_begin(PromptButtonsWalk *w, const uint8_t *mem, uint32_t top, uint32_t models,
                         uint32_t model, int art, int box);

/* A node function's node at node, while a walk is open, with its matrix
 * on top of the game's stack: its sphere taken, the largest kept. */
void prompt_buttons_node(PromptButtonsWalk *w, const uint8_t *mem, uint32_t top, uint32_t node);

/* Whether the node about to be handed to an open walk is the button's, and
 * so not drawn (prompt_buttons_node_ret): every node of the A's and the
 * B's, the ball's of the Start's. 0 for a node the game draws as it is,
 * which is handed to prompt_buttons_pass. */
int prompt_buttons_takes(const PromptButtonsWalk *w);

/* A node of an open walk's model that isn't the button's, at node, with its
 * matrix on top of the game's stack: counted, its sphere noted for the log
 * (its own radius, and where it is), and the game draws it. */
void prompt_buttons_pass(PromptButtonsWalk *w, const uint8_t *mem, uint32_t top, uint32_t node);

/* A node function stood in for while a walk is open (recomp_manual.c's
 * model_node): called with the guest's esp as the node function finds it
 * (its return address at esp, its node at esp + 8), it hands the node to
 * prompt_buttons_node and gives the esp to leave, esp + 4, as the node
 * function's own `ret` would (it pops its return address, the caller its
 * arguments). */
uint32_t prompt_buttons_node_ret(PromptButtonsWalk *w, const uint8_t *mem, uint32_t top, uint32_t esp);

/* Where the walk's largest sphere is on the title's screen: its middle *x,
 * *y and half the picture's width *half (the sphere's, PROMPT_BUTTONS_GROW
 * times, then NV2A_PROMPT_LARGER: nv2a_prompts.h; the Start's,
 * PROMPT_BUTTONS_START_SIDE of its ball's), in the title's pixels,
 * through a projection of focal length focal (the viewport's half width
 * over the half angle's tangent), the height's share of it tall, and the
 * viewport view (x, y, width, height). 1 when placed; 0 for no sphere, or
 * one not in front. */
int prompt_buttons_place(const PromptButtonsWalk *w, float focal, float tall, const uint32_t view[4],
                         float *x, float *y, float *half);

/* The mark's NV2A_PROMPT_MARK_WORDS values written into D3D's push buffer,
 * as D3D writes a method (widescreen_hud_push): one header, then the
 * values, at the put pointer of the device at device, after make_space
 * when they wouldn't fit below its limit; then the put pointer moved past
 * them. Returns where they went, or 0 when make_space left no room, which
 * is written nowhere and said once in the log (RECOMP_CHECK). */
uint32_t prompt_buttons_push(uint8_t *mem, uint32_t device, const uint32_t *words,
                             widescreen_hud_space_fn make_space);

/* An upright size's mark (nv2a_prompts.h, NV2A_PROMPT_UPRIGHT_TAG): the
 * renderer draws the menus' upright faces after it at grow times their
 * faces' size. Written as prompt_buttons_push writes one; returns where it
 * went, or 0 for no device or no room. No screen writes one now: the VS
 * setup screen's, round the draws of its three kinds of object
 * (prompts18), never ran (spriteice's prompts18 log, 22 November 2026: the
 * Back kept every menu's size) and was taken out; what draws that screen's
 * Back, and its size, are still to be found (b3.1 row 9). */
uint32_t prompt_buttons_upright(uint8_t *mem, uint32_t device, float grow,
                                widescreen_hud_space_fn make_space);

/* A line's mark (nv2a_prompts.h, NV2A_PROMPT_LINE_TAG): open before a line
 * of the game's text with pictures in it is drawn, shut after its
 * pictures, so the renderer draws them at its letters' depth. Written as
 * prompt_buttons_push writes one; returns where it went, or 0 for no
 * device or no room. */
uint32_t prompt_buttons_line(uint8_t *mem, uint32_t device, int open, widescreen_hud_space_fn make_space);

/* After the draw: the walk closed, and its sphere placed (as
 * prompt_buttons_place) and marked (as prompt_buttons_push) for the
 * renderer. The first time each button is drawn this way, in the box and
 * out of it, the log says where, with each node's sphere; the Start's again
 * at its PROMPT_BUTTONS_START_AGAIN-th flat draw, as its first is at no size
 * while the bubble opens, with its sphere's least and most over the later
 * half of those draws (its pulse). A walk that took
 * no node to find the Start's ball (PROMPT_BUTTONS_LEARN) marks nothing:
 * the node whose sphere's own radius is the smallest above 0 is the ball
 * from then on (a model of one node is all button), and the log says so.
 * Returns where the mark went, or 0 for none. */
uint32_t prompt_buttons_end(PromptButtonsWalk *w, uint8_t *mem, uint32_t device, float focal, float tall,
                            const uint32_t view[4], widescreen_hud_space_fn make_space);

#endif
