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
#define PROMPT_BUTTONS_B          0x6Au      /* ... and the B */
#define PROMPT_BUTTONS_MATRIX_TOP 0x264C04u  /* -> the matrix stack's top, -> its matrix */
#define PROMPT_BUTTONS_NODE_MOVE  0x14u      /* a node's: its sphere's move (3 floats) */
#define PROMPT_BUTTONS_NODE_MID   0x54u      /*   its bounding sphere's middle (3 floats) */
#define PROMPT_BUTTONS_NODE_R     0x60u      /*   and radius */
#define PROMPT_BUTTONS_TOLD       8u         /* the nodes the first walk's line gives */
#define PROMPT_BUTTONS_GROW       1.25f      /* the picture a quarter wider than the button's sphere, to read at a glance */

/* One button's draw, walked: the largest of its nodes' spheres. */
typedef struct {
    int      button;                  /* 0 the A, 1 the B; -1 while no walk is open */
    int      box;                     /* the message box's (its draw is running) */
    int      art;                     /* the prompts' art (PROMPT_ART_...) */
    uint32_t model;
    uint32_t id;                      /* its id in the display set: PROMPT_BUTTONS_A or _B */
    uint32_t nodes;                   /* nodes handed in */
    uint32_t node;                    /* the largest's index among them */
    float    mid[3], r, scale;        /* its middle in view space, its radius there, the matrix's scale */
    float    told[PROMPT_BUTTONS_TOLD][4];   /* the first nodes' middles and radii, for the log */
} PromptButtonsWalk;

/* The button a display set's model id is: 0 the A (PROMPT_BUTTONS_A), 1 the
 * B (PROMPT_BUTTONS_B), -1 none. */
int prompt_buttons_id(uint32_t id);

/* A walk of model opened in *w, as the button its display set id names
 * (prompt_buttons_id), the message box's if box: 1 when opened, as
 * prompt_buttons_begin. 0, *w left closed, for the game's own art
 * (PROMPT_ART_GAME), no model, or an id that is no button. */
int prompt_buttons_open(PromptButtonsWalk *w, uint32_t model, uint32_t id, int art, int box);

/* Before the models' draw of model, in guest memory mem of top bytes: when
 * art isn't the game's own (PROMPT_ART_GAME) and model is the A or the B of
 * models (the display set's table of models by id; 0 while none is
 * loaded), a walk of it is opened in *w, the message box's if box. 1 when
 * opened: the draw then poses it and hands each node in, and draws none.
 * 0, *w left closed, for anything else. */
int prompt_buttons_begin(PromptButtonsWalk *w, const uint8_t *mem, uint32_t top, uint32_t models,
                         uint32_t model, int art, int box);

/* A node function's node at node, while a walk is open, with its matrix
 * on top of the game's stack: its sphere taken, the largest kept. */
void prompt_buttons_node(PromptButtonsWalk *w, const uint8_t *mem, uint32_t top, uint32_t node);

/* A node function stood in for while a walk is open (recomp_manual.c's
 * model_node): called with the guest's esp as the node function finds it
 * (its return address at esp, its node at esp + 8), it hands the node to
 * prompt_buttons_node and gives the esp to leave, esp + 4, as the node
 * function's own `ret` would (it pops its return address, the caller its
 * arguments). */
uint32_t prompt_buttons_node_ret(PromptButtonsWalk *w, const uint8_t *mem, uint32_t top, uint32_t esp);

/* Where the walk's largest sphere is on the title's screen: its middle *x,
 * *y and half the picture's width *half (the sphere's, PROMPT_BUTTONS_GROW
 * times, then NV2A_PROMPT_LARGER: nv2a_prompts.h), in the title's pixels,
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

/* The VS setup screen's buttons, which the renderer draws upright (its
 * menus' faces, nv2a_prompts.h), at this much of their faces' size, not
 * NV2A_PROMPT_UPRIGHT_GROW's: 110 against the 125 they looked at the
 * faces' own size (spriteice, 03:40 UTC on 22 November 2026, "110%, rather
 * than 125%"; 03:55 UTC, the VS menu's only). Its B, turned about 50
 * degrees, covers 1/1.25 of the area it has upright at its face's size
 * (prompts11's log, button-prompts/p13/VS-GLYPH-FINDING.md in the shared
 * folder). Until prompts18 that size never reached the Back: its mark was
 * put round the screen's own draw, and the Back is its objects'
 * (recomp_manual.c, "The VS setup screen's 3D models"), so it had every
 * menu's size, 1.0, then NV2A_PROMPT_UPRIGHT_GROW once the other pictures
 * were made NV2A_PROMPT_LARGER times larger. Asked to be 15% smaller than
 * that (spriteice, 5:50pm UTC+8: "make VS mode 15% smaller"; 7:32pm, on a
 * card: "15% smaller than you saw"): 1.125 * 0.85. */
#define PROMPT_BUTTONS_VS_UPRIGHT 0.956f

/* An upright size's mark (nv2a_prompts.h, NV2A_PROMPT_UPRIGHT_TAG): the
 * renderer draws the menus' upright faces after it at grow times their
 * faces' size. Written as prompt_buttons_push writes one; returns where it
 * went, or 0 for no device or no room. */
uint32_t prompt_buttons_upright(uint8_t *mem, uint32_t device, float grow,
                                widescreen_hud_space_fn make_space);

/* After the draw: the walk closed, and its sphere placed (as
 * prompt_buttons_place) and marked (as prompt_buttons_push) for the
 * renderer. The first time each button is drawn this way, in the box and
 * out of it, the log says where, with each node's sphere. Returns where
 * the mark went, or 0 for none. */
uint32_t prompt_buttons_end(PromptButtonsWalk *w, uint8_t *mem, uint32_t device, float focal, float tall,
                            const uint32_t view[4], widescreen_hud_space_fn make_space);

#endif
