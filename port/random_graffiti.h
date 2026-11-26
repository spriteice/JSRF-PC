/* Random graffiti: the designs the player sprays, drawn at random from the
 * ones the save has unlocked, a design for each spot.
 *
 * Developer > Experimental > Random graffiti, the switch
 * JSRF_RANDOM_GRAFFITI=1 (port/developer.c), read once, off unless set.
 *
 * The game never picks a design at a spray. A spray writes who sprayed into
 * the spot, and the picture comes from a row of one shared texture, the
 * graffiti sheet, which a builder (resource type 0x19: sub_0002D240 starts
 * it, sub_0002DBE0 steps it) makes from the design files. Row 0 is the
 * player's: one design for each of the five sizes, the five chosen in the
 * garage, which the builder asks the progress object for (sub_0003A820, and
 * sub_0003A870 for whether a choice is one of the player's own from the
 * graffiti editor). Rows 1 to 3 are the chapter's other taggers. So the
 * game's sheet can show one of the player's designs a size, and no more.
 *
 * With the switch on, the builder is answered, for row 0 of the story's set,
 * from a set drawn here in place of the garage's five: for each size, one of
 * that size's unlocked designs, each as likely as the others. Unlocked is
 * the garage menu's own test (sub_0003A2B0), a design's bit in the progress
 * object's graffiti souls got. A size with none unlocked keeps the garage's
 * design. Each build of the story's sheet from the design files draws anew.
 *
 * The game builds the sheet at Load Game and around the garage, and not
 * between areas. So that each area's load brings new designs (spriteice's
 * card, 8:46pm on 2026-11-25), the port has the game build it there too, the
 * way the game's own scene task does on the way to the garage: at the
 * mission flow's step that asks for the sheet's textures (sub_0004AC00),
 * they are let go and the build asked for, and the flow's own step then
 * loads the new ones.
 *
 * A design for each spot (spriteice, 8:58pm on 2026-11-25) takes more sheets
 * than the game's one. At an area's load up to RG_PICKS designs of each size
 * are drawn, different wherever the save has them, and the game is asked
 * for up to RG_SETS builds, one after another: the last is the game's own
 * sheet, with the first of the designs in the player's row, and each before
 * it is made a texture of its own as the builder finishes it, from the
 * builder's sheet with the game's own loader call, under an id that is none
 * of the game's (RG_OWN_TEXTURE). On such a sheet all four rows are the
 * player's, four more of the designs (spriteice, 10:05pm on 2026-11-25,
 * "seeing alot of double ups": with the other taggers' rows kept on each,
 * as the first build of this had them, an area had four designs a size).
 * The other sheets are let go when the game lets its own two go.
 *
 * A spot is dealt one of the designs at its first draw after the draw of
 * them: the spots of a kind (which of the two sheets, and how many parts)
 * take them in turn, so as many spots of a size as there are designs differ
 * before one comes again, and a spot keeps its design until the next deal.
 * A spot's mesh names its texture by id and each part's layers by their row
 * of it (the sprayer's index, a quarter of the sheet each), and for the
 * spot's own draw the id is its design's sheet and the player's layers' row
 * the design's. A sheet of the player's has no other tagger's row: where a
 * spot has another tagger's layer too, a tag not sprayed over yet or one
 * fading out under the player's, it is drawn twice, the other taggers'
 * layers from the game's own sheet and the player's from the design's, the
 * layers not a draw's hidden as the game hides a part with no tag. A spot
 * with nothing of the player's, or dealt the game's own sheet's design, is
 * the game's own draw.
 *
 * No more designs than the save has in some size, and no sheets beyond the
 * game's own unless the game has room: RG_SPARE_BYTES of its texture
 * memory never used beyond the other sets' sheets (what they took of it at
 * an earlier load is theirs to take again), and RG_SPARE_PLACES of its
 * texture manager's places free beyond every set's. With one set this is
 * the game's one sheet, drawn at random. A New Game copies a ready-made
 * sheet and reads no design files: with one design a size unlocked nothing
 * is added, and its first area shows the game's own.
 *
 * A graffiti soul picked up brings new designs at once, the soul's own
 * among them (spriteice, 10:05pm on 2026-11-25, and the card at 10:22pm):
 * the game marks the soul's design as got as the pickup's own animation
 * ends (sub_00042A30, the soul's end, calls sub_00039E80), the designs are
 * drawn again, and the game is asked for the builds again, in play. Every
 * sheet's texture is made again in its place, the game's own two as well,
 * and each spot keeps its place among the designs, so its tag changes once,
 * to the new design there. The game's own flows ask the holder for their
 * loads without looking whether it took the ask, as nothing else of the
 * game's ever has it busy in play. So a build in play is run whole within
 * one of the holder's own updates, a build an update: the holder is asked,
 * and stepped until it is done, and the game never finds it busy.
 *
 * The game's rules don't change. The progress object is only read: the
 * garage's five stay what the menu shows and the save keeps, and are what
 * the builder gets again with the switch off. The other rows, VS's set and
 * the spray are the game's. Only the builder's own calls are answered, told
 * by where they return to, so the garage's menu, which asks the same two
 * functions, gets the game's own answers. The draw uses random numbers of
 * its own, never the game's, whose draws input playback counts
 * (port/playback.h).
 *
 * The hooks are in recomp_manual.c: the builder's start hands its mode to
 * random_graffiti_build, the two functions the builder asks hand each call
 * to random_graffiti_design, its two sheets' writes ask random_graffiti_sheet
 * for the id to make a texture under, the mission flow's step asks
 * random_graffiti_area whether it runs this pass, the sheet's textures'
 * letting go tells random_graffiti_let_go, a spot's mesh's draw asks
 * random_graffiti_spot for its draws, the marking of a soul's design tells
 * random_graffiti_soul, and the holder's update runs random_graffiti_play.
 * The switch, the pool, the draw, the builds' order, the deal, a spot's
 * draws, the room and the [GRAFFITI] lines are here, plain C on guest
 * memory, so a test links them (tests/random_graffiti). */
#ifndef JSRF_RANDOM_GRAFFITI_H
#define JSRF_RANDOM_GRAFFITI_H

#include <stdint.h>

#define RG_SIZES          5u            /* SS, S, M, L, XL */
#define RG_DESIGNS        28u           /* the game's designs of one size: a design is size * 28 + n */
#define RG_PROGRESS       0x001EFFB0u   /* the progress object (port/missions.c's MSN_PROGRESS) */
#define RG_SOULS_GOT      0x00F8u       /* its graffiti souls got, a bit a design (its PRG_SOULS_GOT) */
#define RG_GUEST_RAM      (64u << 20)   /* the guest's memory, which mem names */

/* The sets of sheets built at once: the game's own, and three more. */
#define RG_SETS           4u
#define RG_SHEETS         2u            /* a set's sheets: SS to L in one, XL in the other */
#define RG_ROWS           4u            /* a sheet's rows: the player's and three other taggers' in the game's */
/* A size's designs at once: the one in the game's own sheet's player's row,
 * and four on each other set's. Design 0 is the game's sheet's; design d
 * from 1 is on set 1 + (d - 1) / RG_ROWS, in row (d - 1) % RG_ROWS. */
#define RG_PICKS          (1u + (RG_SETS - 1u) * RG_ROWS)
#define RG_SHEET_BYTES    0x100000u     /* a sheet, and its texture: 1024 x 1024, DXT */
#define RG_SPARE_BYTES    (8u << 20)    /* the texture memory left never used, beyond the other sets' sheets' */
#define RG_SPARE_PLACES   64u           /* the texture manager's places left free, beyond every set's sheets' */

/* The game's resources this asks its holder for (the tables at 0x1EC068). */
#define RG_SHEET            0x19u       /* the graffiti sheet's build; its word is the mode */
#define RG_SHEET_TEXTURES   0x1Au       /* the built sheet as textures (0x102 and 0x103) */

/* The game's texture manager (0x252790: sub_0006E000 sets it up), a hash
 * table of nodes by id, each with a place in the device's table of textures:
 * as much of it as the feature reads. */
#define RG_TEXTURES         0x00252790u
#define RG_TM_FREE          0x0008u     /* the first of its nodes not in use, or 0 */
#define RG_TM_BUCKETS       0x0014u     /* 256 lists of nodes, by the id's low byte */
#define RG_TM_PLACES        0x0814u     /* RG_TM_PLACE_COUNT words: 1 where a place is taken */
#define RG_TM_PLACE_COUNT   500u
#define RG_TM_NODES         1024u       /* the nodes it has in all */
#define RG_NODE_ID          0x00u
#define RG_NODE_PLACE       0x08u
#define RG_NODE_NEXT        0x10u

/* The graffiti spots' records (the manager at 0x2314B0: sub_0003F9E0 finds
 * a spot's by its id), and a spot's mesh's layers, which that draw writes
 * from the record each time before the mesh's own draw (sub_00042EE0): for
 * each part two, the tag fading out and the tag, each with how much of it
 * shows and its row of the sheet, the sprayer's index times a quarter. */
#define RG_SPOT_LISTS       0x002314B8u /* 256 lists of records, by the spot's id's low byte */
#define RG_RECORD_PARTS     0x98u       /* a record's parts: RG_PARTS at most */
#define RG_RECORD_ID        0x9Cu       /* ... its spot's id */
#define RG_RECORD_NEXT      0x104u      /* ... and the next of its list */
#define RG_PARTS            10u
#define RG_MESH_SHOWN       0x0Cu       /* part b's two words at + 8 * b: how much of each layer shows, 0 for none */
#define RG_MESH_ROW         0x64u       /* ... and their rows, floats: 0 is the player's */
#define RG_ROW_NONE         0x3FE00000u /* 1.75, the row the game writes for no tag */

/* The sheet's two textures' ids, 0x102 and 0x103 (the table at 0x20CC40 the
 * game's loader takes them from), and the ids of the other sets' sheets:
 * RG_OWN_TEXTURE + set * RG_SHEETS + sheet, for sets 1 to RG_SETS - 1. */
#define RG_TEXTURE          0x00000102u
#define RG_OWN_TEXTURE      0x52470000u

/* The builder's modes (sub_0002D240's argument) for the story's sheet: any
 * other is VS's (3 and 5 to 7). */
#define RG_BUILD_NEW_GAME   0u          /* a ready-made sheet copied, no design files read */
#define RG_BUILD_LOAD_GAME  1u          /* built from the design files, after Load Game */
#define RG_BUILD_GARAGE     2u          /* ... around the garage, and at an area's load here */
#define RG_BUILD_MENU       4u          /* ... after a change in the garage's graffiti menu */

/* Whether JSRF_RANDOM_GRAFFITI is set, to anything but 0: read once. */
int random_graffiti_on(void);

/* The builder starts a build, in this mode. One an area's load or a soul
 * asked for here answers from the set it was asked for. Any other is the
 * game's own: the designs are forgotten, so the build draws its own one a
 * size, and a soul's builds still to ask for are left. */
void random_graffiti_build(uint32_t mode);

/* The game's holder of resources, as an area's load asks it. */
typedef struct {
    int  (*busy)(void);                             /* it is serving a command (sub_00025680) */
    void (*let_go)(uint32_t resource, uint32_t word);   /* sub_00025740 */
    int  (*ask)(uint32_t resource, uint32_t word);      /* sub_000256A0: 0 when it isn't taken */
    uint32_t (*unused)(void);                       /* the bytes of the game's texture memory never yet handed out */
    void (*step)(void);                             /* its update, once more (sub_00025310): a step of what it serves */
} RANDOM_GRAFFITI_GAME;

/* The mission flow's step that asks for the sheet's textures is about to
 * run, at t seconds: 1 when the game's step is to run this pass, 0 when it
 * is held. textures is whether the textures are loaded, as that step asks,
 * and stage whether a stage is, for the log. With the switch on and the last
 * build the story's, the sheet and its textures are let go where they are
 * loaded, as the garage's scene lets them go, and how many designs a size,
 * on how many sets of sheets, the save and the game's room allow is worked
 * out. Once the holder is free the designs are drawn, and a build is asked
 * for for each set as soon as the holder is free again, the game's own
 * sheet's last: the step is held until the holder has taken that one, and
 * then finds no textures and asks for the new ones itself. Where the
 * textures aren't loaded, the game has just built the sheet itself: with
 * one set there is nothing to add, and with more the builds are asked for
 * all the same. */
int random_graffiti_area(const RANDOM_GRAFFITI_GAME *game, const uint8_t *mem, int textures,
                         int stage, double t);

/* A call of sub_0003A820 or sub_0003A870 for (row, size, set), returning to
 * from: the design to answer with, or -1 for the game's own answer. A
 * design is answered only with the switch on, to the builder's own calls,
 * for set 0, and for a size with a design unlocked: for row 0 in a build of
 * the game's own sheet, and for every row in a build of another set's. The
 * first such call of a build of the game's own draws its designs from mem,
 * the guest's memory, and says so in the log, at t seconds. An answered
 * design is never one of the player's own, so sub_0003A870's answer for it
 * is 0. RG_AS_ROW_0: another set's sheet, a size with none unlocked: the
 * game's own answer for row 0, the garage's design, in every row. */
#define RG_AS_ROW_0   (-2)
int random_graffiti_design(const uint8_t *mem, uint32_t from, uint32_t row, uint32_t size,
                           uint32_t set, double t);

/* The builder is about to write a sheet it has built (0: SS to L, 1: XL):
 * the id to make a texture of it under, or 0 for none. A build that is one
 * of the other sets' has its own id. The game's own sheet's has none at an
 * area's load, where the game loads it itself, and the game's id for a
 * build in play, where the texture is there to be made again in its place.
 * A build in play has none once the game has let the load's textures go. */
uint32_t random_graffiti_sheet(const uint8_t *mem, uint32_t sheet);

/* The texture for id was made in place place, or couldn't be: for the log,
 * at t seconds. */
#define RG_NO_SHEET   (-1)      /* the builder had no sheet to make it of */
#define RG_NO_PLACE   (-2)      /* the texture manager had no node or no place free */
#define RG_NOT_MADE   (-3)      /* the device made no texture for the place */
void random_graffiti_made(uint32_t id, int place, double t);

/* The game lets the sheet's textures go (the other sets' go with them): the
 * next area's load builds them all again. */
void random_graffiti_let_go(void);

/* A spot's mesh, at mesh in mem, is about to be drawn with the texture id,
 * at t seconds: how many draws it takes, each with the texture and the
 * layers' words draw gives. 0 is the game's own one draw, as it is: for any
 * texture but the sheet's two, with the switch off or one design drawn, for
 * a spot dealt the game's own sheet's design or one whose sheet isn't a
 * texture, and for a spot with nothing of the player's. 1 is one draw from
 * the design's sheet, the player's layers in the design's row. 2 is for a
 * spot with another tagger's layer too: one draw of those from the game's
 * own sheet and one of the player's from the design's, each with the
 * other's layers hidden, the player's last unless another tagger's tag is
 * going on over the player's. spot is the spot's own id (its +0x58). The
 * log has each deal's first spots, its first spots drawn twice, and how
 * many spots the deal before it had.
 *
 * Of two draws, the first, the one underneath, is made with a strict depth
 * test (RG_Z_LESS), the second with the game's own. The game's one draw
 * mixes a part's two layers before anything is written, so the layer on
 * top hides the one under it pixel by pixel. Two draws of the same mesh
 * are at the same depth, and the see-through fix (Display > Enhancements,
 * src/nv2a_gl/nv2a_seethru.h) holds back a draw's see-through pixels, the
 * soft edges of a tag's strokes, to the end of the pass, after the second
 * draw: with the game's test they were drawn over the second's tag there,
 * as thin outlines of the tag underneath (spriteice's graffiti4 on the Ally
 * X, 26 November 2026). A strict test fails them where the second draw is,
 * and passes them where it isn't, as before; the second's own see-through
 * pixels still fall on the first's. */
#define RG_Z_LESS           0x201u      /* D3DCMP_LESS (the GPU's own), the first of two draws' depth test */
typedef struct {
    uint32_t passes;
    uint32_t parts;                     /* the spot's parts: as many of layer's are given */
    uint32_t texture[2];                /* each draw's texture id */
    uint32_t depth_test[2];             /* ... its depth test (D3D's ZFUNC), or 0 for the game's own */
    uint32_t layer[2][RG_PARTS][4];     /* ... and each part's words: RG_MESH_SHOWN's two, then RG_MESH_ROW's two */
} RANDOM_GRAFFITI_DRAW;
uint32_t random_graffiti_spot(const uint8_t *mem, uint32_t spot, uint32_t id, uint32_t mesh, double t,
                              RANDOM_GRAFFITI_DRAW *draw);

/* The game marks a graffiti soul's design as got, at t seconds: where the
 * sheets are this load's, built here, they are to be built again in play,
 * with new designs, the soul's own one of its size's. */
void random_graffiti_soul(uint32_t design, double t);

/* The holder's update has run, at t seconds. After a soul, with the holder
 * free and the game's own sheet a texture, the designs are drawn again, as
 * many as the save and the room now allow, and a spot keeps its place among
 * them unless there are fewer. Then, a pass a build, the other sets' first
 * and the game's own sheet's last: the build is asked for and the holder
 * stepped until it is done, RG_PLAY_STEPS times at most (beyond that it is
 * left to the holder's own pace, and the next waits for it). An area's load,
 * a build of the game's own or the textures' letting go ends it. */
#define RG_PLAY_STEPS   4096u
void random_graffiti_play(const RANDOM_GRAFFITI_GAME *game, const uint8_t *mem, double t);

/* The place of the texture manager's texture id, as its own lookup
 * (sub_0006E130) gives it for any id but its two that stand for another
 * (0x100 and 0x101): -1 when it has none. */
int random_graffiti_texture(const uint8_t *mem, uint32_t id);

/* How many of the texture manager's places are free: 0 when it has no node
 * left to give one to. */
uint32_t random_graffiti_places(const uint8_t *mem);

/* The draw's random numbers start from seed: for a test. Left alone, they
 * start from the host's clock at the first draw. */
void random_graffiti_seed(uint32_t seed);

#endif
