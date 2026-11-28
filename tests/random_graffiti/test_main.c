/* Run with: python tests/random_graffiti/run.py
 *
 * Made-up guest memory for port/random_graffiti.c: the progress object's
 * graffiti souls got, a bit a design, where the game keeps them, a texture
 * manager where the game's is, with nodes in lists by id and places taken
 * or free, as sub_0006E660 and sub_0006E7B0 leave them, and graffiti spots,
 * each a record in the game's lists and a mesh with its parts' layers as
 * the spot's own draw writes them. Each case asks as the hooks on
 * sub_0003A820 and sub_0003A870 do, with where the call returns to, steps
 * an area's load as the hook on sub_0004AC00 does, with a made-up holder
 * that counts what it is asked, makes the textures the builder's write
 * would, asks for spots' draws as the hook on sub_00042EE0 does, and tells
 * of a soul and runs the holder's update as the hooks on sub_00039E80 and
 * sub_00025310 do. The player's own designs are made-up headers and
 * lengths, answered as the hooks' reader answers (rg_own_header), a save's
 * stage table and spots' tags are where the game keeps them, and the kept
 * tags' file is written in a folder of the case's own. It prints a "check"
 * line for each check; run.py reads those and the module's own log lines,
 * and sets the switch for the case's run, as it is read once. The first
 * argument picks the case, the second is the case's own folder.
 */
#include <stdio.h>
#include <string.h>
#include "random_graffiti.h"
#include "random_graffiti_kept.h"

#define RAM       0x400000u     /* the made-up guest memory: the texture manager is at 0x252790 */
#define NODES     0x300000u     /* ... and its nodes are made here, 0x18 bytes each */
#define BUILDER   0x0002DD05u   /* where the builder's first call of sub_0003A820 returns to */
#define OWN       0x0002DAD6u   /* ... and its first of sub_0003A870, from the copy into the sheet */
#define MENU      0x001245CAu   /* ... and the garage's menu's first */
#define STORY     0u            /* the story's set */
#define VS        1u            /* VS's */
#define VS_BUILD  3u            /* a build of VS's sheet */
#define MB        (1u << 20)
#define RECORDS   0x380000u     /* the made-up spots' records, 0x108 bytes each */
#define MESHES    0x3A0000u     /* ... and their meshes, 0xC0 bytes each */
#define SPOTS     80u           /* the spots a case makes, at most */
#define ONE       0x3F800000u   /* 1.0: a tag showing in full */
#define HALF      0x3F000000u   /* 0.5: a tag half on */
#define OLD       0x3FC00000u   /* 1.5: what the game writes for a tag fading out */
#define T         12.5
#define CODE      0x00020003u   /* a stage code: its digits 2 and 3 */
#define STAGE     3u            /* ... which the made-up stage table numbers 3 in the save */

static const uint32_t k_row[RG_ROWS] = { 0x00000000u, 0x3E800000u, 0x3F000000u, 0x3F400000u };

static uint8_t s_ram[RAM];
static int s_failed;

static void check(const char *what, int ok)
{
    printf("check: %s %s\n", what, ok ? "ok" : "FAILED");
    s_failed |= !ok;
}

static uint32_t get(uint32_t at) { uint32_t w; memcpy(&w, s_ram + at, 4); return w; }
static void put(uint32_t at, uint32_t w) { memcpy(s_ram + at, &w, 4); }

static void unlock(uint32_t size, uint32_t n)
{
    uint32_t design = size * RG_DESIGNS + n;
    uint32_t at = RG_PROGRESS + RG_SOULS_GOT + (design >> 5) * 4u;
    put(at, get(at) | 1u << (design & 31u));
}

static int ask(uint32_t from, uint32_t row, uint32_t size, uint32_t set)
{
    return random_graffiti_design(s_ram, from, row, size, set, T);
}

/* The made-up texture manager: empty, with this many of its places taken and
 * a node not in use (or none). */
static uint32_t s_nodes;
static void tm_reset(uint32_t taken, int node)
{
    uint32_t n;
    memset(s_ram + RG_TEXTURES, 0, RG_TM_PLACES + RG_TM_PLACE_COUNT * 4u);
    put(RG_TEXTURES + RG_TM_FREE, node ? NODES + 0x8000u : 0);
    for (n = 0; n < taken; n++) put(RG_TEXTURES + RG_TM_PLACES + n * 4u, 1);
    s_nodes = 0;
}
/* A texture made, as sub_0006E660 does: in the place its id has, or else a
 * node at the end of its id's list, in the first place free. */
static int tm_make(uint32_t id)
{
    uint32_t node, at = RG_TEXTURES + RG_TM_BUCKETS + (id & 0xFFu) * 4u;
    uint32_t place = 0;
    if (random_graffiti_texture(s_ram, id) >= 0) return random_graffiti_texture(s_ram, id);
    node = NODES + s_nodes++ * RG_NODE_BYTES;
    while (place < RG_TM_PLACE_COUNT && get(RG_TEXTURES + RG_TM_PLACES + place * 4u)) place++;
    while (get(at)) at = get(at) + RG_NODE_NEXT;
    put(at, node);
    put(node + RG_NODE_ID, id);
    put(node + RG_NODE_PLACE, place);
    put(node + RG_NODE_NEXT, 0);
    put(RG_TEXTURES + RG_TM_PLACES + place * 4u, 1);
    return (int)place;
}
/* ... and let go, as sub_0006E7B0 does. */
static void tm_free(uint32_t id)
{
    uint32_t at = RG_TEXTURES + RG_TM_BUCKETS + (id & 0xFFu) * 4u, node;
    for (; (node = get(at)) != 0; at = node + RG_NODE_NEXT)
        if (get(node + RG_NODE_ID) == id) {
            put(RG_TEXTURES + RG_TM_PLACES + get(node + RG_NODE_PLACE) * 4u, 0);
            put(at, get(node + RG_NODE_NEXT));
            return;
        }
}

/* A made-up holder: busy or not, taking an ask or not, and what it was
 * asked. A taken ask starts the build, as the game's holder does at once;
 * letting the sheet's textures go lets the other sets' go too, as the hook
 * on sub_0002D040 does. */
static int s_busy, s_takes;
static unsigned s_let_go, s_asks, s_other_asks;
static uint32_t s_gone[2], s_asked[2], s_unused = 9u * MB;
static int holder_busy(void) { return s_busy; }
static void holder_let_go(uint32_t resource, uint32_t word)
{
    uint32_t n;
    if (s_let_go < 2) s_gone[s_let_go] = resource;
    s_let_go++;
    (void)word;
    if (resource != RG_SHEET_TEXTURES) return;
    for (n = 0; n < RG_SETS * RG_SHEETS; n++)
        tm_free(n < RG_SHEETS ? RG_TEXTURE + n : RG_OWN_TEXTURE + n);
    random_graffiti_let_go();
}
static int holder_ask(uint32_t resource, uint32_t word)
{
    s_asks++;
    s_other_asks += resource != RG_SHEET || word != RG_BUILD_GARAGE;
    s_asked[0] = resource;
    s_asked[1] = word;
    if (!s_takes) return 0;
    random_graffiti_build(word);
    s_busy = 1;
    return 1;
}
static uint32_t holder_unused(void) { return s_unused; }

/* The build the holder took runs to its end: its two sheets are written,
 * each first made a texture where the module names an id for it (but for
 * `not`, which the device makes none of), and the holder is free. What the
 * builder would be answered for each design of each size is kept, by the
 * design's place among the size's, and the ids the sheets were made under. */
static int s_built[RG_PICKS][RG_SIZES];
static uint32_t s_ids[RG_SHEETS];
static int s_answers_ok;
static void build_ends(uint32_t not)
{
    uint32_t sheet, row, size, set;
    for (sheet = 0; sheet < RG_SHEETS; sheet++)
        s_ids[sheet] = random_graffiti_sheet(s_ram, sheet);
    set = s_ids[0] >= RG_OWN_TEXTURE ? (s_ids[0] - RG_OWN_TEXTURE) / RG_SHEETS : 0;
    s_answers_ok = 1;
    for (row = 0; row < RG_ROWS; row++)
        for (size = 0; size < RG_SIZES; size++) {
            int design = ask(BUILDER, row, size, STORY);
            s_answers_ok &= ask(OWN, row, size, STORY) == design;
            if (set) s_built[1u + (set - 1u) * RG_ROWS + row][size] = design;
            else if (!row) s_built[0][size] = design;
            else s_answers_ok &= design == -1;
        }
    for (sheet = 0; sheet < RG_SHEETS; sheet++)
        if (s_ids[sheet])
            random_graffiti_made(s_ids[sheet], s_ids[sheet] == not ? RG_NOT_MADE : tm_make(s_ids[sheet]), T);
    s_busy = 0;
}
/* The holder's update, once more: the build it serves runs to its end,
 * unless it is slow. */
static int s_slow;
static unsigned s_steps;
static void holder_step(void)
{
    s_steps++;
    if (s_busy && !s_slow) build_ends(0);
}
/* The player's own designs, as the builder's reader opens them: a slot's
 * header's size, or -1 for no design there, and its file's length. */
static int s_own_head[RG_OWN_SLOTS];
static uint32_t s_own_bytes[RG_OWN_SLOTS];
static unsigned s_own_asks;
static int own_header(uint32_t slot, uint32_t *length)
{
    s_own_asks++;
    *length = slot < RG_OWN_SLOTS ? s_own_bytes[slot] : 0;
    return slot < RG_OWN_SLOTS ? s_own_head[slot] : -1;
}
/* An own design's file's length for a size, as the reader wants it: the
 * game's table's four words, made up here, and its own extra. */
static uint32_t own_length(uint32_t size) { return RG_OWN_EXTRA + 0x40u + 0x100u * size; }
static void own_lengths(void)
{
    uint32_t size;
    for (size = 0; size < RG_SIZES; size++) {
        put(RG_OWN_LENGTHS + size * 16u, 0x40u);
        put(RG_OWN_LENGTHS + size * 16u + 4u, 0x100u * size);
    }
}
static void own_make(uint32_t slot, int size)
{
    s_own_head[slot] = size;
    s_own_bytes[slot] = own_length((uint32_t)size);
}
static const RANDOM_GRAFFITI_GAME k_holder = { holder_busy, holder_let_go, holder_ask, holder_unused,
                                               holder_step, own_header };
/* An area's load step, of the stage code s_code: 0, which the made-up
 * stage table numbers none, unless a case sets it. */
static uint32_t s_code;
static int area(int textures, int stage)
{
    return random_graffiti_area(&k_holder, s_ram, textures, stage, s_code, T);
}
static void play(void) { random_graffiti_play(&k_holder, s_ram, T); }
/* The game loads its own sheet's two textures. */
static void textures_load(void)
{
    tm_make(RG_TEXTURE);
    tm_make(RG_TEXTURE + 1);
}

/* A made-up spot n: its id as the game numbers them, its record with this
 * many parts at the head of its id's list, and its mesh, with no tag in any
 * part. */
static uint32_t spot_id(uint32_t n) { return 0xF0u + n * 0x10u; }
static uint32_t spot_mesh(uint32_t n) { return MESHES + n * 0xC0u; }
/* A part's two layers, as the spot's own draw writes them: how much of the
 * tag fading out shows and its row, then the tag's. */
static void spot_layers(uint32_t n, uint32_t part, uint32_t old_shown, uint32_t old_row, uint32_t shown,
                        uint32_t row)
{
    put(spot_mesh(n) + RG_MESH_SHOWN + 8u * part, old_shown);
    put(spot_mesh(n) + RG_MESH_SHOWN + 8u * part + 4u, shown);
    put(spot_mesh(n) + RG_MESH_ROW + 8u * part, old_row);
    put(spot_mesh(n) + RG_MESH_ROW + 8u * part + 4u, row);
}
static void spot_make(uint32_t n, uint32_t parts)
{
    uint32_t record = RECORDS + n * RG_RECORD_BYTES, list = RG_SPOT_LISTS + (spot_id(n) & 0xFFu) * 4u, part;
    put(record + RG_RECORD_PARTS, parts);
    put(record + RG_RECORD_ID, spot_id(n));
    put(record + RG_RECORD_NEXT, get(list));
    put(list, record);
    for (part = 0; part < RG_PARTS; part++)
        spot_layers(n, part, 0, RG_ROW_NONE, 0, RG_ROW_NONE);
}
/* A spot with a tag of the player's in every part. */
static void spot_sprayed(uint32_t n, uint32_t parts)
{
    uint32_t part;
    spot_make(n, parts);
    for (part = 0; part < parts; part++)
        spot_layers(n, part, 0, RG_ROW_NONE, ONE, k_row[0]);
}
/* A spot's draw, as the hook asks: with the mesh's texture this id. */
static RANDOM_GRAFFITI_DRAW s_draw;
static uint32_t spot_draw(uint32_t n, uint32_t id)
{
    return random_graffiti_spot(s_ram, spot_id(n), id, spot_mesh(n), RG_NO_KEY, RG_NO_KEY, RG_NO_KEY, T,
                                &s_draw);
}
/* A spot's draw as the hook asks with its key: on stage CODE, this number,
 * and this size. */
static uint32_t spot_keyed(uint32_t n, uint32_t id, uint32_t number, uint32_t size)
{
    return random_graffiti_spot(s_ram, spot_id(n), id, spot_mesh(n), CODE, number, size, T, &s_draw);
}
/* The save's tags of a spot on STAGE: none of the player's, in either
 * layer, as after another tagger's tag or a New Game. */
static void save_gone(uint32_t number)
{
    uint32_t which;
    for (which = 0; which < 2u; which++)
        put(RG_PROGRESS + RG_SPOT_TAGS + ((STAGE * RG_KEPT_SPOTS + number) * 2u + which) * 4u, 0x3FFFFFFFu);
}
/* The kept tags' file read into text: whether it is want, or has it. */
static void file_read(const char *path, char *text, size_t room)
{
    FILE *f = fopen(path, "rb");
    size_t got = f ? fread(text, 1, room - 1, f) : 0;
    if (f) fclose(f);
    text[got] = 0;
}
static int file_is(const char *path, char *text, size_t room, const char *want)
{
    file_read(path, text, room);
    return !strcmp(text, want);
}
static int file_has(const char *path, char *text, size_t room, const char *want)
{
    file_read(path, text, room);
    return strstr(text, want) != NULL;
}
/* The design the draw just asked for shows the player's tag as, by its
 * place among the size's: 0 for the game's own draw. layer says which of
 * the first part's two layers is the player's. */
static uint32_t drawn(uint32_t passes, uint32_t layer)
{
    uint32_t pass, row, set;
    for (pass = 0; pass < passes && s_draw.texture[pass] < RG_OWN_TEXTURE; pass++)
        ;
    if (pass == passes) return 0;
    set = (s_draw.texture[pass] - RG_OWN_TEXTURE) / RG_SHEETS;
    for (row = 0; row < RG_ROWS && k_row[row] != s_draw.layer[pass][0][2u + layer]; row++)
        ;
    return row < RG_ROWS ? 1u + (set - 1u) * RG_ROWS + row : 99u;
}
/* Whether a part's words in a draw are these. */
static int layers_are(uint32_t pass, uint32_t part, uint32_t old_shown, uint32_t old_row, uint32_t shown,
                      uint32_t row)
{
    const uint32_t *layer = s_draw.layer[pass][part];
    return layer[0] == old_shown && layer[1] == shown && layer[2] == old_row && layer[3] == row;
}

/* Whether a size's 13 designs in the builds are the ones in had, in any
 * order. */
static int same_designs(const int *had, uint32_t size)
{
    uint32_t k, n;
    for (k = 0; k < RG_PICKS; k++) {
        for (n = 0; n < RG_PICKS && s_built[n][size] != had[k]; n++)
            ;
        if (n == RG_PICKS) return 0;
    }
    return 1;
}
/* Whether a design is among a size's first picks places in the builds. */
static int placed(int design, uint32_t size, uint32_t picks)
{
    uint32_t k;
    for (k = 0; k < picks; k++)
        if (s_built[k][size] == design) return 1;
    return 0;
}

/* SS: 3 unlocked; S: 1; M: none; L: all 28; XL: its first and last. */
static void some_unlocked(void)
{
    uint32_t n;
    unlock(0, 2); unlock(0, 9); unlock(0, 27);
    unlock(1, 5);
    for (n = 0; n < RG_DESIGNS; n++) unlock(3, n);
    unlock(4, 0); unlock(4, 27);
}

static int in_set(int design, uint32_t size)
{
    static const int k_ss[] = { 2, 9, 27 };
    int n = design - (int)(size * RG_DESIGNS), k;
    switch (size) {
    case 0: for (k = 0; k < 3; k++) if (n == k_ss[k]) return 1; return 0;
    case 1: return n == 5;
    case 3: return n >= 0 && n < (int)RG_DESIGNS;
    case 4: return n == 0 || n == 27;
    }
    return 0;
}

/* An area's load with the textures loaded, the holder taking every ask and
 * each build running to its end: how many builds it asked for. */
static unsigned load_runs(void)
{
    unsigned before = s_asks, passes;
    s_takes = 1;
    for (passes = 0; passes < 20 && !area(passes == 0, 0); passes++)
        build_ends(0);
    build_ends(0);
    textures_load();
    return s_asks - before;
}

/* Whether the designs kept from the builds are 13 different ones of L's
 * 28, with this one among them (or any, for -1). */
static int thirteen_of_l(int with)
{
    uint32_t k, n;
    int ok = 1, found = with < 0;
    for (k = 0; k < RG_PICKS; k++) {
        ok &= in_set(s_built[k][3], 3);
        found |= s_built[k][3] == with;
        for (n = 0; n < k; n++) ok &= s_built[n][3] != s_built[k][3];
    }
    return ok && found;
}

int main(int argc, char **argv)
{
    const char *test = argc > 1 ? argv[1] : "";
    uint32_t size, row, n;
    int first[RG_SIZES], k;

    some_unlocked();
    tm_reset(0, 1);
    random_graffiti_seed(1);
    for (n = 0; n < RG_OWN_SLOTS; n++) s_own_head[n] = -1;
    own_lengths();
    put(RG_STAGE_TABLE + 23u * 4u, STAGE);     /* CODE's digits, 2 and 3 */
    if (!strcmp(test, "off") || !strcmp(test, "zero")) {
        /* Not set, or set to 0: the game's own answer to everything. */
        check("the switch is off", !random_graffiti_on());
        random_graffiti_build(RG_BUILD_LOAD_GAME);
        for (size = 0; size < RG_SIZES; size++)
            check("off, the builder gets the game's own answer", ask(BUILDER, 0, size, STORY) == -1);
        /* ... and an area's load is the game's own, the holder asked nothing. */
        s_unused = 28u * MB;
        check("off, an area's load runs the game's step", area(1, 0) == 1);
        check("off, the holder is asked nothing", !s_let_go && !s_asks);
        /* ... and no sheet is made a texture, a spot's draw is the game's
         * own, and a soul brings nothing. */
        check("off, no sheet is made a texture", !random_graffiti_sheet(s_ram, 0) && !random_graffiti_sheet(s_ram, 1));
        spot_sprayed(7, 1);
        check("off, a spot's draw is the game's own", spot_draw(7, RG_TEXTURE) == 0
              && spot_draw(7, RG_TEXTURE + 1) == 0);
        random_graffiti_soul(5, T);
        play();
        check("off, a soul asks the holder nothing", !s_asks && !s_steps);
    } else if (!strcmp(test, "on")) {
        check("the switch is on", random_graffiti_on());
        random_graffiti_build(RG_BUILD_LOAD_GAME);
        /* Nothing but the builder's own calls for the player's row of the
         * story's set, and nothing is drawn for the others. */
        check("the garage's menu gets the game's own answer", ask(MENU, 0, 0, STORY) == -1);
        for (row = 1; row < 4; row++)
            check("another tagger's row is the game's", ask(BUILDER, row, 0, STORY) == -1);
        check("a row the sheet doesn't have is the game's", ask(BUILDER, RG_ROWS, 0, STORY) == -1);
        check("VS's set is the game's", ask(BUILDER, 0, 0, VS) == -1);
        check("a size the game doesn't have is the game's", ask(BUILDER, 0, RG_SIZES, STORY) == -1);
        /* The builder's: each size's design is one of its unlocked ones, and
         * a size with none keeps the garage's. One line for the draw. */
        for (size = 0; size < RG_SIZES; size++) {
            first[size] = ask(BUILDER, 0, size, STORY);
            if (size == 2)
                check("a size with none unlocked keeps the garage's design", first[size] == -1);
            else
                check("a size's design is one of its unlocked ones", in_set(first[size], size));
            check("sub_0003A870's call gets the same", ask(OWN, 0, size, STORY) == first[size]);
        }
        /* One set for a whole build: asked again, each size's design is the same. */
        for (size = 0; size < RG_SIZES; size++)
            check("a build's set is the same when asked again", ask(BUILDER, 0, size, STORY) == first[size]);
        /* A build of the game's own is the game's one sheet: nothing more is
         * made a texture, and every spot's draw is the game's own. */
        check("the game's own build makes no texture more", !random_graffiti_sheet(s_ram, 0)
              && !random_graffiti_sheet(s_ram, 1));
        spot_sprayed(7, 1);
        check("with one design a size a spot's draw is the game's own", spot_draw(7, RG_TEXTURE) == 0);
        /* The next build draws its own: a second line. */
        random_graffiti_build(RG_BUILD_GARAGE);
        for (size = 0; size < RG_SIZES; size++)
            check("the next build's design is one of the unlocked ones too",
                  size == 2 ? ask(BUILDER, 0, size, STORY) == -1 : in_set(ask(BUILDER, 0, size, STORY), size));
    } else if (!strcmp(test, "area")) {
        /* Room for the game's one sheet and no more: one set, drawn again at
         * each area's load. After a build of VS's sheet, an area's load is
         * the game's own. */
        random_graffiti_build(VS_BUILD);
        check("VS: an area's load runs the game's step", area(1, 0) == 1 && !s_let_go && !s_asks);
        /* The story's, with the textures not loaded: the game has just built
         * the sheet itself, and loads it itself. */
        random_graffiti_build(RG_BUILD_LOAD_GAME);
        check("no textures loaded: the game's step runs", area(0, 0) == 1 && !s_let_go && !s_asks);
        /* The textures loaded: both are let go, and the step is held while
         * the holder is busy, with nothing asked of it yet. */
        s_busy = 1;
        check("the step is held while the holder is busy", area(1, 1) == 0);
        check("the sheet and its textures are let go, in the game's order",
              s_let_go == 2 && s_gone[0] == RG_SHEET && s_gone[1] == RG_SHEET_TEXTURES);
        check("a busy holder isn't asked", s_asks == 0);
        /* Free, but not taking the ask: still held, and nothing let go again. */
        s_busy = 0;
        check("the step is held until the ask is taken", area(0, 1) == 0 && s_asks == 1 && s_let_go == 2);
        /* Taken: the build is the garage's kind, and the game's step runs. */
        s_takes = 1;
        check("the ask taken, the game's step runs", area(0, 1) == 1);
        check("the build asked for is the story's, from the design files",
              s_asks == 2 && s_asked[0] == RG_SHEET && s_asked[1] == RG_BUILD_GARAGE);
        check("it is the game's own sheet's: no texture more is made", !random_graffiti_sheet(s_ram, 0));
        /* While it builds and loads, the step is the game's, and nothing more is asked. */
        check("after the ask, the game's step runs each pass", area(0, 1) == 1 && area(0, 1) == 1);
        check("nothing more is asked of the holder", s_asks == 2 && s_let_go == 2);
        /* The build is answered from the set the load drew. */
        check("the area's build is answered from a drawn set", in_set(ask(BUILDER, 0, 0, STORY), 0));
        /* The next area's load does it again, with no stage loaded. */
        s_busy = 0;
        check("the next area's load asks again", area(1, 0) == 1 && s_asks == 3 && s_let_go == 4);
        /* A build the game starts while an area's load waits to ask is the
         * one it waited for: nothing more is asked. */
        s_busy = 1;
        check("held again at the next", area(1, 0) == 0 && s_let_go == 6);
        random_graffiti_build(RG_BUILD_LOAD_GAME);
        s_busy = 0;
        check("the game's own build ends the wait", area(0, 0) == 1 && s_asks == 3);
    } else if (!strcmp(test, "spots")) {
        /* Room for four sets: after Load Game, the game has built its one
         * sheet, and its first area's load finds no textures. Thirteen
         * designs a size are drawn, and a build asked for for each of four
         * sets, the game's own sheet's last. */
        int ok = 1;
        uint32_t had[SPOTS], seen = 0, moved = 0, set, own_draws = 0, a, b, c;
        const uint32_t not = RG_OWN_TEXTURE + 1u * RG_SHEETS + 1u;      /* set 1's XL sheet */

        s_unused = 28u * MB;
        tm_reset(200, 1);
        s_takes = 1;
        random_graffiti_build(RG_BUILD_LOAD_GAME);
        check("the game's own build draws its one set", in_set(ask(BUILDER, 0, 0, STORY), 0));
        for (set = RG_SETS; set-- > 0; ) {
            check(set ? "the step is held for an other set's build" : "the game's own sheet's build asked for, the step runs",
                  area(0, 0) == !set);
            check("a build is asked for, and nothing let go", s_asks == RG_SETS - set && !s_let_go);
            check(set ? "its two sheets are to be made textures, under its ids" : "the game's own sheet is made no texture here",
                  random_graffiti_sheet(s_ram, 0) == (set ? RG_OWN_TEXTURE + set * RG_SHEETS : 0)
               && random_graffiti_sheet(s_ram, 1) == (set ? RG_OWN_TEXTURE + set * RG_SHEETS + 1 : 0)
               && random_graffiti_sheet(s_ram, 2) == 0);
            if (set) {
                check("the step is held while it builds, and nothing asked", area(0, 0) == 0 && s_asks == RG_SETS - set);
                build_ends(not);
                ok &= s_answers_ok;
            }
        }
        build_ends(0);
        ok &= s_answers_ok;
        check("every build asked for is the story's, from the design files", !s_other_asks);
        check("sub_0003A870's calls get the same, and the game's own sheet's other rows are the game's", ok);
        ok = 1;
        for (n = 0; n < RG_PICKS; n++)
            for (size = 0; size < RG_SIZES; size++)
                ok &= size == 2 ? s_built[n][size] == (n ? RG_AS_ROW_0 : -1) : in_set(s_built[n][size], size);
        check("every design is an unlocked one: the garage's where none is, in every row of an other set's", ok);
        check("with 13 or more unlocked, the 13 designs of a size differ", thirteen_of_l(-1));
        ok = s_built[0][0] != s_built[1][0] && s_built[0][0] != s_built[2][0] && s_built[1][0] != s_built[2][0];
        for (n = 3; n < RG_PICKS; n++)
            ok &= s_built[n][0] == s_built[n % 3][0] && s_built[n][4] == s_built[n % 2][4];
        check("with fewer, they all come up and start over", ok && s_built[0][4] != s_built[1][4]);
        textures_load();
        check("while the textures load, the game's step runs, and nothing more is asked",
              area(0, 0) == 1 && area(0, 0) == 1 && s_asks == RG_SETS);
        check("five of the other sets' six sheets are textures",
              random_graffiti_texture(s_ram, RG_OWN_TEXTURE + 6) == 200
           && random_graffiti_texture(s_ram, RG_OWN_TEXTURE + 2) == 204
           && random_graffiti_texture(s_ram, not) == -1
           && random_graffiti_places(s_ram) == RG_TM_PLACE_COUNT - 200 - 7);
        /* Spots of one part with the player's tag take the designs in turn:
         * the first 13 have the 13, then they come round again. A spot's
         * draw is the same each time: the game's own for the design on the
         * game's sheet, and one draw from the design's sheet, its row in
         * the player's layer, for the others. */
        ok = 1;
        for (n = 0; n < 40; n++) {
            uint32_t passes, pick;
            spot_sprayed(n, 1);
            passes = spot_draw(n, RG_TEXTURE);
            pick = had[n] = drawn(passes, 1);
            ok &= pick < RG_PICKS && passes == (pick ? 1u : 0u);
            if (pick)
                ok &= s_draw.parts == 1 && s_draw.texture[0] == RG_OWN_TEXTURE + (1u + (pick - 1u) / RG_ROWS) * RG_SHEETS
                   && layers_are(0, 0, 0, RG_ROW_NONE, ONE, k_row[(pick - 1u) % RG_ROWS])
                   && s_draw.depth_test[0] == 0;     /* one draw: the game's own depth test */
            ok &= spot_draw(n, RG_TEXTURE) == passes && drawn(passes, 1) == pick;
            ok &= spot_draw(n, 0x104) == 0 && spot_draw(n, 0x55) == 0;
            if (n < RG_PICKS) seen |= 1u << pick;
            else ok &= pick == had[n - RG_PICKS];
        }
        check("spots take the designs in turn, each one draw from its design's sheet, and keep them", ok);
        check("the first 13 spots of a kind have the 13 designs", seen == (1u << RG_PICKS) - 1);
        /* XL spots, ten parts, are dealt among themselves, from the XL
         * sheets: the game's own for the game's design and for the four on
         * the sheet that wasn't made. */
        ok = 1;
        for (n = 40; n < 40 + RG_PICKS; n++) {
            uint32_t passes, pick, part;
            spot_sprayed(n, RG_PARTS);
            passes = spot_draw(n, RG_TEXTURE + 1);
            pick = drawn(passes, 1);
            own_draws += !passes;
            ok &= passes < 2 && (!pick || pick > RG_ROWS);
            if (passes) {
                ok &= s_draw.parts == RG_PARTS && s_draw.texture[0] == RG_OWN_TEXTURE + (1u + (pick - 1u) / RG_ROWS) * RG_SHEETS + 1u;
                for (part = 0; part < RG_PARTS; part++)
                    ok &= layers_are(0, part, 0, RG_ROW_NONE, ONE, k_row[(pick - 1u) % RG_ROWS]);
            }
        }
        check("XL spots draw their design's XL sheet in every part, the game's own where it wasn't made",
              ok && own_draws == 1u + RG_ROWS);
        /* A spot with nothing of the player's is the game's own draw, and is
         * dealt no design: the next sprayed spot of its kind has the next. */
        spot_make(60, 1);
        spot_layers(60, 0, 0, RG_ROW_NONE, ONE, k_row[2]);
        spot_sprayed(61, 1);
        check("a spot with only another tagger's tag is the game's own draw", spot_draw(60, RG_TEXTURE) == 0);
        check("... and takes no design: the next sprayed spot has the next in turn",
              drawn(spot_draw(61, RG_TEXTURE), 1) == had[40 % RG_PICKS]);
        check("a spot with no record, or a mesh that is no address, is the game's own draw",
              spot_draw(79, RG_TEXTURE) == 0
           && random_graffiti_spot(s_ram, spot_id(0), RG_TEXTURE, 0x20u, RG_NO_KEY, RG_NO_KEY,
                                   RG_NO_KEY, T, &s_draw) == 0);
        /* Another tagger's tag in another part: two draws, the game's own
         * sheet's first with the player's part hidden, then the design's
         * with the other tagger's hidden. (Two spots alike: one of two in
         * turn is never the game's own design.) */
        for (n = 62; n < 64; n++) {
            spot_make(n, 2);
            spot_layers(n, 0, 0, RG_ROW_NONE, ONE, k_row[0]);
            spot_layers(n, 1, 0, RG_ROW_NONE, ONE, k_row[2]);
        }
        a = spot_draw(62, RG_TEXTURE) ? 62 : 63;
        check("another tagger's tag in another part: two draws", spot_draw(a, RG_TEXTURE) == 2 && s_draw.parts == 2);
        /* The first, underneath, with a strict depth test, so the see-through
         * fix's held-back edges of its tag can't land on the second's; the
         * second with the game's own (random_graffiti.h, RG_Z_LESS). */
        check("the first of two draws with a strict depth test, the second with the game's",
              s_draw.depth_test[0] == RG_Z_LESS && s_draw.depth_test[1] == 0);
        check("the game's own sheet's first, the player's part hidden",
              s_draw.texture[0] == RG_TEXTURE && layers_are(0, 0, 0, RG_ROW_NONE, 0, RG_ROW_NONE)
           && layers_are(0, 1, 0, RG_ROW_NONE, ONE, k_row[2]));
        check("then the design's, the other tagger's part hidden",
              s_draw.texture[1] >= RG_OWN_TEXTURE && drawn(2, 1) > 0 && drawn(2, 1) < RG_PICKS
           && layers_are(1, 1, 0, RG_ROW_NONE, 0, RG_ROW_NONE) && s_draw.layer[1][0][1] == ONE);
        /* The player's tag going on over another tagger's: the same order. */
        for (n = 64; n < 66; n++) {
            spot_make(n, 1);
            spot_layers(n, 0, OLD, k_row[1], HALF, k_row[0]);
        }
        b = spot_draw(64, RG_TEXTURE) ? 64 : 65;
        check("the player's tag going on over another tagger's: two draws, the design's last",
              spot_draw(b, RG_TEXTURE) == 2 && s_draw.texture[0] == RG_TEXTURE
           && layers_are(0, 0, OLD, k_row[1], 0, RG_ROW_NONE) && s_draw.texture[1] >= RG_OWN_TEXTURE
           && s_draw.layer[1][0][0] == 0 && s_draw.layer[1][0][2] == RG_ROW_NONE && s_draw.layer[1][0][1] == HALF
           && drawn(2, 1) > 0 && drawn(2, 1) < RG_PICKS
           && s_draw.depth_test[0] == RG_Z_LESS && s_draw.depth_test[1] == 0);
        /* Another tagger's going on over the player's: the game's own last. */
        for (n = 66; n < 68; n++) {
            spot_make(n, 1);
            spot_layers(n, 0, OLD, k_row[0], HALF, k_row[3]);
        }
        c = spot_draw(66, RG_TEXTURE) ? 66 : 67;
        check("another tagger's tag going on over the player's: two draws, the game's own sheet's last",
              spot_draw(c, RG_TEXTURE) == 2 && s_draw.texture[1] == RG_TEXTURE
           && layers_are(1, 0, 0, RG_ROW_NONE, HALF, k_row[3]) && s_draw.texture[0] >= RG_OWN_TEXTURE
           && s_draw.layer[0][0][0] == OLD && s_draw.layer[0][0][1] == 0 && s_draw.layer[0][0][3] == RG_ROW_NONE
           && drawn(2, 0) > 0 && drawn(2, 0) < RG_PICKS
           && s_draw.depth_test[0] == RG_Z_LESS && s_draw.depth_test[1] == 0);
        /* The next area's load, the textures loaded: all are let go, and
         * until the new ones are made every spot's draw is the game's own. */
        s_busy = 1;
        check("the next load lets the sheet and the textures go", area(1, 1) == 0 && s_let_go == 2);
        check("the other sets' textures went with the game's", random_graffiti_texture(s_ram, RG_OWN_TEXTURE + 2) == -1
           && random_graffiti_places(s_ram) == RG_TM_PLACE_COUNT - 200);
        ok = 1;
        for (n = 0; n < 40; n++) ok &= spot_draw(n, RG_TEXTURE) == 0;
        check("with no texture of its design's sheet, a spot's draw is the game's own", ok);
        s_busy = 0;
        for (set = RG_SETS; set-- > 0; ) {
            ok = area(0, 1) == !set;
            build_ends(0);
            check("the next load's build is asked for", ok && s_asks == 2 * RG_SETS - set);
        }
        textures_load();
        check("after it all six are textures", random_graffiti_places(s_ram) == RG_TM_PLACE_COUNT - 200 - 8);
        /* A new deal: the spots, met in another order, take the designs in
         * turn again. */
        seen = 0;
        for (n = 40; n-- > 0; ) {
            uint32_t pick = drawn(spot_draw(n, RG_TEXTURE), 1);
            moved += pick != had[n];
            if (n >= 40 - RG_PICKS) seen |= 1u << pick;
        }
        check("the spots are dealt again at the new draw", moved > 30 && seen == (1u << RG_PICKS) - 1);
        /* A build of the game's own, as on the way to the garage: one design
         * a size again, and every spot's draw the game's own. */
        random_graffiti_build(RG_BUILD_GARAGE);
        check("after the game's own build a spot's draw is the game's own", spot_draw(1, RG_TEXTURE) == 0
           && spot_draw(41, RG_TEXTURE + 1) == 0 && !random_graffiti_sheet(s_ram, 0));
    } else if (!strcmp(test, "soul")) {
        /* A graffiti soul got in play: the designs are drawn again, the
         * soul's among them, and every sheet built again, a build a pass of
         * the holder's, each run to its end at once. */
        static const int k_souls[] = { 3 * 28 + 7, 3 * 28 + 20, 3 * 28 + 11 };
        uint32_t had[40], set, places, asks;
        int ok;

        s_unused = 28u * MB;
        random_graffiti_build(RG_BUILD_LOAD_GAME);
        random_graffiti_soul(5, T);
        play();
        check("a soul before an area's load has built anything asks the holder nothing", !s_asks && !s_steps);
        textures_load();
        check("an area's load: four sets", load_runs() == 4);
        for (n = 0; n < 40; n++) {
            spot_sprayed(n, 1);
            had[n] = drawn(spot_draw(n, RG_TEXTURE), 1);
        }
        places = random_graffiti_places(s_ram);
        asks = s_asks;
        s_busy = 1;
        random_graffiti_soul((uint32_t)k_souls[0], T);
        play();
        check("after a soul, nothing is asked of a busy holder", s_asks == asks && !s_steps);
        s_busy = 0;
        for (set = RG_SETS; set-- > 0; ) {
            play();
            check(set ? "a pass of the holder's, a build: an other set's, run to its end, its sheets made textures"
                      : "the last is the game's own sheet's, and its two are made textures again too",
                  s_asks == asks + RG_SETS - set && !s_busy && s_answers_ok && s_steps == RG_SETS - set
               && s_ids[0] == (set ? RG_OWN_TEXTURE + set * RG_SHEETS : RG_TEXTURE) && s_ids[1] == s_ids[0] + 1u);
        }
        play();
        check("after the last nothing more is asked", s_asks == asks + RG_SETS && !s_other_asks && s_steps == RG_SETS);
        check("every texture is made again in the place it had", random_graffiti_places(s_ram) == places
           && random_graffiti_texture(s_ram, RG_OWN_TEXTURE + 6) == 0 && random_graffiti_texture(s_ram, RG_TEXTURE) == 6);
        check("the designs are 13 of the size's again, the soul's one of them", thirteen_of_l(k_souls[0]));
        ok = 1;
        for (n = 0; n < 40; n++) ok &= drawn(spot_draw(n, RG_TEXTURE), 1) == had[n];
        check("a spot keeps its place among the designs", ok);
        for (k = 1; k < 3; k++) {
            random_graffiti_soul((uint32_t)k_souls[k], T);
            for (n = 0; n < RG_SETS; n++) play();
            check("the next soul's design is one of the 13 too", thirteen_of_l(k_souls[k]) && s_asks == asks + (unsigned)(k + 1) * RG_SETS);
        }
        /* A build not done in the steps it is given is left to the holder's
         * own pace, and the next waits for it. */
        asks = s_asks;
        s_steps = 0;
        s_slow = 1;
        random_graffiti_soul((uint32_t)k_souls[0], T);
        play();
        check("a build not done in its steps is left to the holder", s_asks == asks + 1 && s_busy && s_steps == RG_PLAY_STEPS);
        play();
        check("... and nothing more is asked while it runs", s_asks == asks + 1 && s_steps == RG_PLAY_STEPS);
        s_slow = 0;
        build_ends(0);
        for (n = 1; n < RG_SETS; n++) play();
        play();
        check("... then the others follow, a pass each", s_asks == asks + RG_SETS && !s_busy
           && random_graffiti_places(s_ram) == places);
        /* An area's load ends a soul's builds: it builds everything itself. */
        random_graffiti_soul((uint32_t)k_souls[1], T);
        play();
        check("an area's load after a soul's first build builds its own four", load_runs() == 4);
        asks = s_asks;
        play();
        check("... and nothing more is asked in play", s_asks == asks);
        /* The game lets the textures go, as on the way to the garage: a
         * soul's builds are left, and a soul after it waits for the load. */
        random_graffiti_soul((uint32_t)k_souls[2], T);
        holder_let_go(RG_SHEET_TEXTURES, 0);
        play();
        check("the textures let go: a soul's builds are left", s_asks == asks);
        random_graffiti_soul((uint32_t)k_souls[2], T);
        play();
        check("... and a soul after it asks nothing", s_asks == asks);
    } else if (!strcmp(test, "room")) {
        /* No more sets than the game has room for: 2 MB a set beyond its
         * own, and 8 MB left never used. */
        random_graffiti_build(RG_BUILD_LOAD_GAME);
        textures_load();
        s_unused = 12u * MB;
        check("12 MB never used: three sets", load_runs() == 3);
        s_unused = 28u * MB;
        check("28 MB never used: four sets", load_runs() == 4);
        /* What the other sets' six sheets took of it is theirs to take again. */
        s_unused = 8u * MB;
        check("8 MB never used, after 6 MB of it went to these sheets: four sets still", load_runs() == 4);
        s_unused = 7u * MB;
        check("7 MB: three", load_runs() == 3);
        /* ... two places a set, the game's own too, and 64 left free. */
        s_unused = 28u * MB;
        tm_reset(RG_TM_PLACE_COUNT - 68, 1);
        textures_load();
        check("68 places free with no sheet a texture: two sets",
              random_graffiti_places(s_ram) == 66 && load_runs() == 2);
        /* ... and none without a node for the manager to give. */
        tm_reset(0, 0);
        check("no node free: the game's one set", random_graffiti_places(s_ram) == 0 && load_runs() == 1);
        /* No more designs than the save has in some size, and a set for
         * each four beyond the first. */
        memset(s_ram + RG_PROGRESS + RG_SOULS_GOT, 0, 20);
        unlock(0, 1); unlock(0, 2);
        tm_reset(0, 1);
        textures_load();
        check("two designs in the fullest size: two sets", load_runs() == 2);
        check("a size with none keeps the garage's in the game's own sheet", ask(BUILDER, 0, 4, STORY) == -1);
        unlock(0, 3); unlock(0, 4); unlock(0, 5);
        check("five designs: two sets still", load_runs() == 2);
        unlock(0, 6);
        check("six: three sets", load_runs() == 3);
        /* The manager's lists: an id is found behind another of its list,
         * and a list that never ends is given up on. */
        tm_reset(0, 1);
        tm_make(RG_TEXTURE);
        check("an id is found behind another in its list", tm_make(RG_OWN_TEXTURE + 2) == 1
           && random_graffiti_texture(s_ram, RG_OWN_TEXTURE + 2) == 1
           && random_graffiti_texture(s_ram, RG_TEXTURE) == 0
           && random_graffiti_texture(s_ram, RG_OWN_TEXTURE + 4) == -1);
        put(NODES + RG_NODE_BYTES + RG_NODE_NEXT, NODES);
        check("a list that never ends is given up on", random_graffiti_texture(s_ram, 0x202) == -1);
    } else if (!strcmp(test, "starts")) {
        /* Every build draws again: over many builds every unlocked design
         * of a size comes up, and nothing else does. */
        unsigned seen_ss = 0, seen_xl = 0, seen_l = 0;
        int ok = 1;
        for (k = 0; k < 2000; k++) {
            int ss, l, xl;
            random_graffiti_build(k & 1 ? RG_BUILD_LOAD_GAME : RG_BUILD_GARAGE);
            ss = ask(BUILDER, 0, 0, STORY);
            l = ask(BUILDER, 0, 3, STORY);
            xl = ask(BUILDER, 0, 4, STORY);
            ok &= in_set(ss, 0) && in_set(l, 3) && in_set(xl, 4) && ask(BUILDER, 0, 1, STORY) == 33
               && ask(BUILDER, 0, 2, STORY) == -1;
            if (!ok) break;
            seen_ss |= 1u << ss;
            seen_l |= 1u << (l - 3 * (int)RG_DESIGNS);
            seen_xl |= 1u << (xl - 4 * (int)RG_DESIGNS);
        }
        check("every build's set is from the unlocked designs", ok);
        check("each of SS's three comes up", seen_ss == ((1u << 2) | (1u << 9) | (1u << 27)));
        check("each of L's 28 comes up", seen_l == (1u << RG_DESIGNS) - 1);
        check("each of XL's two comes up", seen_xl == ((1u << 0) | (1u << 27)));
    } else if (!strcmp(test, "own")) {
        /* The player's own designs: two SS, an L and an XL, and two files
         * that aren't a design's, a header of M with another length and one
         * of a size there is none of. */
        int first_ss, left, ok;
        own_make(0, 0); own_make(1, 0); own_make(2, 3); own_make(5, 4);
        own_make(7, 2); s_own_bytes[7] += 1;
        s_own_head[9] = 7; s_own_bytes[9] = own_length(0);
        s_unused = 28u * MB;
        random_graffiti_option(RG_OPT_DESIGNS, RG_DESIGNS_OWN);
        random_graffiti_build(RG_BUILD_LOAD_GAME);
        textures_load();
        /* My own: each size's own, the unlocked ones where it has none. The
         * fullest size has two: two sets. */
        check("My own, two in the fullest size: two sets", load_runs() == 2);
        check("each slot is asked once at the load", s_own_asks == RG_OWN_SLOTS);
        check("SS draws from its two own designs only",
              ((s_built[0][0] == (int)RG_OWN && s_built[1][0] == (int)RG_OWN + 1)
               || (s_built[0][0] == (int)RG_OWN + 1 && s_built[1][0] == (int)RG_OWN)));
        check("S, with none of its own, from its unlocked one", s_built[0][1] == 33 && s_built[1][1] == 33);
        check("M, with neither, keeps the garage's", s_built[0][2] == -1);
        check("L and XL from their one own each", s_built[0][3] == (int)RG_OWN + 2 && s_built[1][3] == (int)RG_OWN + 2
              && s_built[0][4] == (int)RG_OWN + 5);
        /* A failed read of a place drawn here: the stand-in's write and
         * the own flag's are held back, the notice put down; of the
         * garage's own choice, the game's. */
        first_ss = s_built[0][0];
        left = first_ss == (int)RG_OWN ? (int)RG_OWN + 1 : (int)RG_OWN;
        check("another call's write is the game's", random_graffiti_unread(0x00012345u, 0, 0, 0, 0, T) == RG_UNREAD_GAME);
        check("the game's own sheet's own design that failed: its write held back, with no notice",
              random_graffiti_unread(RG_UNREAD_DESIGN, 0, 0, 0, 0, T) == RG_UNREAD_HELD);
        check("... and the own flag's after it", random_graffiti_unread(RG_UNREAD_OWN, 0, 0, 0, 1, T) == RG_UNREAD_HELD);
        check("... once", random_graffiti_unread(RG_UNREAD_OWN, 0, 0, 0, 1, T) == RG_UNREAD_GAME);
        check("the place shows the stand-in", ask(BUILDER, 0, 0, STORY) == 0);
        check("another tagger's row that failed is the game's",
              random_graffiti_unread(RG_UNREAD_DESIGN_XL, 1, 4, 0, 113, T) == RG_UNREAD_GAME);
        check("after it, a place of the feature's holds back its writes but keeps the notice",
              random_graffiti_unread(RG_UNREAD_DESIGN, 0, 3, 0, 84, T) == RG_UNREAD_NOTICE);
        check("a stand-in not of its size is the game's", random_graffiti_unread(RG_UNREAD_DESIGN_XL, 0, 4, 0, 5, T)
              == RG_UNREAD_GAME);
        /* The next load leaves the two out: SS draws from the one left, L
         * from its unlocked ones (13: four sets). */
        s_own_asks = 0;
        check("the next load, L's own left out: four sets, and no slot asked again", load_runs() == 4
              && s_own_asks == 0);
        ok = 1;
        for (n = 0; n < RG_PICKS; n++) ok &= s_built[n][0] == left && in_set(s_built[n][3], 3);
        check("the slots that failed are left out", ok);
        /* The garage builds its sheet (its editor may have changed one):
         * the next load asks each slot again. Both: SS's two unlocked and
         * own one together, all among the 13. */
        random_graffiti_build(RG_BUILD_GARAGE);
        random_graffiti_option(RG_OPT_DESIGNS, RG_DESIGNS_BOTH);
        load_runs();
        check("after the garage's build, each slot is asked again", s_own_asks == RG_OWN_SLOTS);
        check("Both: SS has its unlocked ones and its own", placed(left, 0, RG_PICKS) && placed(2, 0, RG_PICKS)
              && placed(9, 0, RG_PICKS) && placed(27, 0, RG_PICKS));
        /* Unlocked: none of the player's own, and nothing asked. */
        random_graffiti_option(RG_OPT_DESIGNS, RG_DESIGNS_UNLOCKED);
        s_own_asks = 0;
        load_runs();
        ok = !s_own_asks;
        for (n = 0; n < RG_PICKS; n++) ok &= in_set(s_built[n][0], 0) && in_set(s_built[n][4], 4);
        check("Unlocked: only unlocked designs, and no own design asked for", ok);
    } else if (!strcmp(test, "kept")) {
        /* Kept tags, on the save's stage 3, with room for four sets. */
        uint32_t had[RG_PICKS], asks;
        int design[12], before[RG_PICKS], ok;
        const RG_KEPT *tag;

        s_code = CODE;
        s_unused = 28u * MB;
        random_graffiti_option(RG_OPT_KEEP, 1);
        random_graffiti_build(RG_BUILD_LOAD_GAME);
        textures_load();
        check("an area's load on stage 3: four sets", load_runs() == 4);
        /* Six L spots sprayed: each kept, with the design it shows. */
        ok = 1;
        for (n = 0; n < 6; n++) {
            spot_sprayed(n, 1);
            had[n] = drawn(spot_keyed(n, RG_TEXTURE, n, 3), 1);
            design[n] = s_built[had[n]][3];
            tag = rg_kept_get(STAGE, n);
            ok &= tag && tag->size == 3 && tag->parts == 1 && tag->design == design[n];
        }
        check("a spot shown with the player's tag is kept, with the design it shows", ok && rg_kept_count(STAGE) == 6);
        /* None kept for a spot the save doesn't keep, a size its sheet
         * hasn't, or no key. */
        spot_sprayed(6, 1); spot_sprayed(7, 1); spot_sprayed(8, 1);
        spot_keyed(6, RG_TEXTURE, RG_KEPT_SPOTS, 3);
        spot_keyed(7, RG_TEXTURE, 7, 4);
        spot_draw(8, RG_TEXTURE);
        check("no tag kept for a number the save hasn't, a size not of its sheet, or no key",
              rg_kept_count(RG_KEPT_STAGES) == 6 && !rg_kept_get(STAGE, 7));
        /* The next load: the kept designs in the first places, each kept
         * spot showing its own, and a new spot one of the others. */
        check("the next load: four sets", load_runs() == 4);
        ok = 1;
        for (n = 0; n < 6; n++)
            ok &= s_built[n][3] == design[n] && s_built[drawn(spot_keyed(n, RG_TEXTURE, n, 3), 1)][3] == design[n];
        check("a kept spot shows its design after a shuffle", ok);
        spot_sprayed(10, 1);
        design[10] = s_built[drawn(spot_keyed(10, RG_TEXTURE, 10, 3), 1)][3];
        ok = 1;
        for (n = 0; n < 6; n++) ok &= design[10] != design[n];
        check("a new tag is dealt a design that isn't a kept one", ok && rg_kept_get(STAGE, 10)
              && rg_kept_get(STAGE, 10)->design == design[10]);
        /* Another tagger's tag over a kept one: the player's is gone. */
        spot_layers(2, 0, 0, RG_ROW_NONE, ONE, k_row[2]);
        check("a spot with only another tagger's tag has its tag ended", spot_keyed(2, RG_TEXTURE, 2, 3) == 0
              && !rg_kept_get(STAGE, 2) && rg_kept_count(STAGE) == 6);
        /* A soul: the kept ones stay. */
        asks = s_asks;
        random_graffiti_soul(91, T);
        for (n = 0; n < RG_SETS + 1u; n++) play();
        ok = s_asks == asks + RG_SETS;
        for (n = 0; n < 6; n++)
            if (n != 2) ok &= s_built[drawn(spot_keyed(n, RG_TEXTURE, n, 3), 1)][3] == design[n];
        check("after a soul a kept spot shows its design", ok
              && s_built[drawn(spot_keyed(10, RG_TEXTURE, 10, 3), 1)][3] == design[10]);
        /* A Load Game: at the stage's next load its tags are checked
         * against the save, once. */
        save_gone(0);
        save_gone(1);
        random_graffiti_build(RG_BUILD_LOAD_GAME);
        load_runs();
        check("after Load Game, the tags the save hasn't are ended", rg_kept_count(STAGE) == 4
              && !rg_kept_get(STAGE, 0) && !rg_kept_get(STAGE, 1) && rg_kept_get(STAGE, 3));
        save_gone(3);
        load_runs();
        check("... and only at the stage's first load", rg_kept_count(STAGE) == 4);
        /* Shuffle at Graffiti soul off: a soul draws nothing. */
        random_graffiti_option(RG_OPT_SOUL, 0);
        asks = s_asks;
        random_graffiti_soul(91, T);
        for (n = 0; n < RG_SETS + 1u; n++) play();
        check("Graffiti soul off: a soul asks nothing", s_asks == asks);
        /* Both shuffles off: a load holds the designs. */
        random_graffiti_option(RG_OPT_AREA, 0);
        for (n = 0; n < RG_PICKS; n++) before[n] = s_built[n][3];
        load_runs();
        check("Area load off: a load keeps the size's 13 designs", same_designs(before, 3));
        ok = 1;
        for (n = 3; n < 6; n++) ok &= s_built[drawn(spot_keyed(n, RG_TEXTURE, n, 3), 1)][3] == design[n];
        check("... and the kept spots theirs", ok);
        /* Keep off: a tag is kept in memory, and the next shuffle ends it. */
        random_graffiti_option(RG_OPT_KEEP, 0);
        load_runs();
        check("Keep off, held: the tags stay", rg_kept_count(STAGE) == 4);
        random_graffiti_option(RG_OPT_AREA, 1);
        load_runs();
        check("Keep off, a shuffle: every tag ended", rg_kept_count(RG_KEPT_STAGES) == 0);
        spot_sprayed(20, 1);
        spot_keyed(20, RG_TEXTURE, 20, 3);
        check("... and kept again in memory till the next", rg_kept_count(STAGE) == 1);
        /* With one design a size, the game's own draw keeps its tags too. */
        s_unused = 0;
        random_graffiti_option(RG_OPT_KEEP, 1);
        load_runs();
        spot_sprayed(21, 1);
        check("one design a size: the game's own draw, the tag kept with its design",
              spot_keyed(21, RG_TEXTURE, 21, 3) == 0 && rg_kept_get(STAGE, 21)
              && rg_kept_get(STAGE, 21)->design == ask(BUILDER, 0, 3, STORY));
    } else if (!strcmp(test, "file")) {
        /* The kept tags' file, in the case's own folder for the save's. */
        char path[600], text[700];
        int bad;
        FILE *f;
        const RG_KEPT *tag;
        static const char k_one[] = "jsrf-pc kept graffiti 2\n1 3 0 1 3 g89\n1 3 79 10 4 o12\n1 15 7 2 0 g2\n";
        static const char k_two[] = "jsrf-pc kept graffiti 2\n0 3 5 1 3 g90\n"
                                    "1 3 0 1 3 g89\n1 3 79 10 4 o12\n1 15 7 2 0 g2\n";

        snprintf(path, sizeof path, "%s/%s", argc > 2 ? argv[2] : ".", RG_KEPT_FILE);
        rg_kept_set(3, 0, 3, 1, 89);
        check("no file: nothing saved", rg_kept_save(0, &bad) == -1 && rg_kept_count(RG_KEPT_STAGES) == 1);
        rg_kept_file(path);
        check("no file yet: a slot's load has none", rg_kept_load(0, &bad) == -1 && !rg_kept_count(RG_KEPT_STAGES));
        rg_kept_set(3, 0, 3, 1, 89);
        rg_kept_set(3, 79, 4, 10, (int)RG_OWN + 12);
        rg_kept_set(15, 7, 0, 2, 2);
        rg_kept_set(RG_KEPT_STAGES, 0, 0, 1, 2);
        rg_kept_set(3, RG_KEPT_SPOTS, 0, 1, 2);
        rg_kept_set(3, 1, 1, 1, 84);
        check("three kept: none out of the table, or of a design not of its size",
              rg_kept_count(RG_KEPT_STAGES) == 3 && rg_kept_count(3) == 2);
        check("saved to slot 1, with no file yet", rg_kept_save(1, &bad) == 1 && !bad);
        check("the file is slot 1's tags, a line each", file_is(path, text, sizeof text, k_one));
        snprintf(text, sizeof text, "%s.tmp", path);
        f = fopen(text, "rb");
        check("no file left beside it", !f);
        if (f) fclose(f);
        /* Another slot's save keeps slot 1's lines. */
        rg_kept_clear();
        rg_kept_set(3, 5, 3, 1, 90);
        check("saved to slot 0", rg_kept_save(0, &bad) == 1 && file_is(path, text, sizeof text, k_two));
        check("slot 1 loaded: its own three", rg_kept_load(1, &bad) == 3 && !bad && (tag = rg_kept_get(3, 79))
              && tag->size == 4 && tag->parts == 10 && tag->design == (int)RG_OWN + 12
              && rg_kept_get(15, 7)->design == 2 && rg_kept_get(3, 0)->design == 89 && !rg_kept_get(3, 5));
        check("slot 0 loaded: its one", rg_kept_load(0, &bad) == 1 && rg_kept_get(3, 5)->design == 90
              && !rg_kept_get(3, 0));
        check("slot 2 loaded: none", rg_kept_load(2, &bad) == 0 && !rg_kept_count(RG_KEPT_STAGES));
        /* Lines that can't be read are left out and counted. */
        f = fopen(path, "ab");
        if (f) {
            fputs("1 3 2 0 3 g90\n# a note\n\nnonsense\n1 3 3 1 3 x5\n1 3 4 1 3 g200\n1 3 5 1 3 g90 more\n"
                  "3 3 6 1 3 g90\n", f);
            fclose(f);
        }
        check("six bad lines left out", rg_kept_load(1, &bad) == 3 && bad == 6);
        f = fopen(path, "wb");
        if (f) { fputs("another file\n1 3 0 1 3 g89\n", f); fclose(f); }
        check("another file is not read", rg_kept_load(1, &bad) == -1 && !rg_kept_count(RG_KEPT_STAGES));
        /* Through the game: a slot's load takes its tags, those the save
         * hasn't ended; the area's draw puts them first; a save keeps the
         * play's, ending those the save hasn't first. */
        f = fopen(path, "wb");
        if (f) { fputs("jsrf-pc kept graffiti 2\n2 3 0 1 3 g89\n2 3 1 1 3 g88\n", f); fclose(f); }
        save_gone(1);
        random_graffiti_slot(s_ram, 2, 0, path, T);
        check("slot 2's load: its tag the save has, not the one it hasn't", rg_kept_count(STAGE) == 1
              && rg_kept_get(STAGE, 0) && rg_kept_get(STAGE, 0)->design == 89);
        s_code = CODE;
        s_unused = 28u * MB;
        random_graffiti_option(RG_OPT_KEEP, 1);
        random_graffiti_build(RG_BUILD_LOAD_GAME);
        textures_load();
        load_runs();
        check("the area's load: its tag's design in the first place", s_built[0][3] == 89);
        spot_sprayed(5, 1);
        spot_keyed(5, RG_TEXTURE, 5, 3);
        random_graffiti_slot(s_ram, 0, 1, path, T);
        check("saved to slot 0: the play's two, slot 2's lines as they were",
              file_has(path, text, sizeof text, "\n0 3 0 1 3 g89\n") && file_has(path, text, sizeof text, "\n0 3 5 1 3 g")
              && file_has(path, text, sizeof text, "\n2 3 1 1 3 g88\n"));
        save_gone(5);
        random_graffiti_slot(s_ram, 2, 1, path, T);
        check("saved to slot 2: the one the save hasn't ended first", rg_kept_count(STAGE) == 1
              && file_has(path, text, sizeof text, "\n2 3 0 1 3 g89\n")
              && !file_has(path, text, sizeof text, "\n2 3 1 ") && !file_has(path, text, sizeof text, "\n2 3 5 "));
        random_graffiti_slot(s_ram, 1, 1, "", T);
        check("a save with no folder: the play's stay", rg_kept_count(STAGE) == 1
              && !file_has(path, text, sizeof text, "\n1 "));
        random_graffiti_build(RG_BUILD_NEW_GAME);
        check("a New Game: none in play", !rg_kept_count(RG_KEPT_STAGES)
              && file_has(path, text, sizeof text, "\n2 3 0 1 3 g89\n"));
    } else {
        printf("unknown case %s\n", test);
        return 2;
    }
    return s_failed;
}
