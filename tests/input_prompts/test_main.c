/* tests/input_prompts: the button prompts' choices (src/input/input_prompts.c),
 * linked whole with the layouts (src/input/input_layout.c), with the
 * settings file and the menu stood in for (run.py says which cases). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "input_layout.h"
#include "input_prompts.h"
#include "menu.h"

static int g_fail;
#define CHECK(cond, ...) do { if (!(cond)) { g_fail = 1; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

/* SDL's numbers for kinds the prompts take as Xbox: unknown, Xbox 360,
 * Xbox One, virtual, Luna, Stadia, Shield. */
static const int k_xbox_types[] = { 0, 1, 2, 6, 8, 9, 10 };

/* The settings file: one number, button_prompts=, or none. */
static int s_have;
static double s_val;
static int s_sets;
int nv_window_pref_num(const char *key, double *v)
{
    if (strcmp(key, "button_prompts") || !s_have) return 0;
    *v = s_val;
    return 1;
}
void nv_window_pref_set_num(const char *key, double v)
{
    CHECK(!strcmp(key, "button_prompts"), "kept as %s", key);
    s_have = 1;
    s_val = v;
    s_sets++;
}

/* The menu: the rows added. */
static XBOX_MENU_ITEM s_rows[8];
static int s_nrows, s_adds;
void xbox_MenuAdd(const XBOX_MENU_ITEM *items, int count)
{
    int i;
    s_adds++;
    for (i = 0; i < count && s_nrows < 8; i++) s_rows[s_nrows++] = items[i];
}

/* A pad's kind from what SDL reports: PlayStation's three types, the
 * Switch Pro pad and Joy-Cons (one, or a pair), and Xbox for everything
 * else: the PC's pad, and the Deck's and the Ally X's controls, whose
 * types are unknown or virtual (Steam Input's) among them. */
static void case_kind(void)
{
    size_t i;
    CHECK(input_prompts_kind(PROMPT_SDL_PS3) == PROMPT_PAD_PLAYSTATION, "PS3");
    CHECK(input_prompts_kind(PROMPT_SDL_PS4) == PROMPT_PAD_PLAYSTATION, "PS4");
    CHECK(input_prompts_kind(PROMPT_SDL_PS5) == PROMPT_PAD_PLAYSTATION, "PS5");
    CHECK(input_prompts_kind(PROMPT_SDL_SWITCH_PRO) == PROMPT_PAD_NINTENDO, "Switch Pro");
    CHECK(input_prompts_kind(PROMPT_SDL_JOYCON_LEFT) == PROMPT_PAD_NINTENDO, "a left Joy-Con");
    CHECK(input_prompts_kind(PROMPT_SDL_JOYCON_RIGHT) == PROMPT_PAD_NINTENDO, "a right Joy-Con");
    CHECK(input_prompts_kind(PROMPT_SDL_JOYCON_PAIR) == PROMPT_PAD_NINTENDO, "a pair");
    for (i = 0; i < sizeof k_xbox_types / sizeof k_xbox_types[0]; i++)
        CHECK(input_prompts_kind(k_xbox_types[i]) == PROMPT_PAD_XBOX, "type %d", k_xbox_types[i]);
    printf("input_prompts kind: %s\n", g_fail ? "FAIL" : "pass");
}

/* The art for each style, kind of pad and last input: Default the game's
 * own, Xbox, PlayStation and Nintendo their pack's set and Keyboard the
 * keys', whatever was used last; only Auto follows it, the keys' once the
 * keyboard was last used and the pad's kind's otherwise, an Xbox-style pad
 * or one SDL doesn't know the pack's Xbox set, never the game's own. And
 * the strip's style: Default's Auto's, every other its own. */
static void case_art(void)
{
    static const int k_auto[PROMPT_PADS] = { PROMPT_ART_XBOX, PROMPT_ART_PLAYSTATION, PROMPT_ART_NINTENDO };
    static const int k_fixed[PROMPT_STYLES] = { -1, PROMPT_ART_GAME, PROMPT_ART_XBOX, PROMPT_ART_PLAYSTATION,
                                                PROMPT_ART_NINTENDO, PROMPT_ART_KEYS };
    int style, kind;
    for (style = 0; style < PROMPT_STYLES; style++)
        for (kind = PROMPT_PAD_XBOX; kind < PROMPT_PADS; kind++) {
            int want = style == PROMPT_STYLE_AUTO ? k_auto[kind] : k_fixed[style];
            int keys = style == PROMPT_STYLE_AUTO ? PROMPT_ART_KEYS : want;
            CHECK(input_prompts_art(style, kind, 0) == want, "style %d, kind %d: art %d", style, kind,
                  input_prompts_art(style, kind, 0));
            CHECK(input_prompts_art(style, kind, 1) == keys, "style %d, kind %d, keys last: art %d", style, kind,
                  input_prompts_art(style, kind, 1));
        }
    CHECK(input_prompts_art(PROMPT_STYLE_AUTO, 9, 0) == PROMPT_ART_XBOX, "an unknown kind is Xbox's");
    for (style = 0; style < PROMPT_STYLES; style++)
        CHECK(input_prompts_strip_style(style) == (style == PROMPT_STYLE_GAME ? PROMPT_STYLE_AUTO : style),
              "style %d's strip", style);
    printf("input_prompts art: %s\n", g_fail ? "FAIL" : "pass");
}

#define ICON(art, c) input_prompts_icon(art, &pad, &keys, c)
#define IS(icon, a, in) ((icon).art == (a) && (icon).input == (in))

/* Each control's icon from the layouts: in the game's own art (Default),
 * the control itself whatever the layouts, and none for a control of the
 * port's own; the pad's input in a pack's set; the first key, or the
 * second where the first is none. Moved: a pack's set follows, the game's
 * own art doesn't. */
static void case_icon(void)
{
    InputLayout pad, keys;
    int c;
    input_layout_pad_default(&pad);
    input_layout_keys_default(&keys);
    for (c = INPUT_CTL_A; c < INPUT_CTLS; c++)
        CHECK(IS(ICON(PROMPT_ART_GAME, c), PROMPT_ART_GAME, c < INPUT_CTL_OWN ? c : INPUT_NONE),
              "control %d: the game's own icon of it", c);
    CHECK(IS(ICON(PROMPT_ART_XBOX, INPUT_CTL_A), PROMPT_ART_XBOX, INPUT_PAD_A), "Xbox's A");
    CHECK(IS(ICON(PROMPT_ART_PLAYSTATION, INPUT_CTL_A), PROMPT_ART_PLAYSTATION, INPUT_PAD_A), "PlayStation's A");
    CHECK(IS(ICON(PROMPT_ART_NINTENDO, INPUT_CTL_BLACK), PROMPT_ART_NINTENDO, INPUT_PAD_LSHOULDER), "Nintendo's Black");
    CHECK(IS(ICON(PROMPT_ART_PLAYSTATION, INPUT_CTL_RTRIGGER), PROMPT_ART_PLAYSTATION, INPUT_PAD_RTRIGGER),
          "PlayStation's R trigger");
    CHECK(IS(ICON(PROMPT_ART_KEYS, INPUT_CTL_A), PROMPT_ART_KEYS, INPUT_KEY_SPACE), "Space for A");
    CHECK(IS(ICON(PROMPT_ART_KEYS, INPUT_CTL_LSTICK_UP), PROMPT_ART_KEYS, INPUT_KEY_W), "W for up");
    CHECK(IS(ICON(PROMPT_ART_KEYS, INPUT_CTL_FREECAM), PROMPT_ART_KEYS, INPUT_NONE), "no key for the free camera");

    input_layout_bind(&pad, INPUT_CTL_A, INPUT_PAD_Y);          /* A on Y, Y on A */
    CHECK(IS(ICON(PROMPT_ART_GAME, INPUT_CTL_A), PROMPT_ART_GAME, INPUT_CTL_A), "A moved to Y: still the game's A");
    CHECK(IS(ICON(PROMPT_ART_XBOX, INPUT_CTL_A), PROMPT_ART_XBOX, INPUT_PAD_Y), "Xbox's Y for A");
    CHECK(IS(ICON(PROMPT_ART_PLAYSTATION, INPUT_CTL_A), PROMPT_ART_PLAYSTATION, INPUT_PAD_Y), "Triangle for A");
    input_layout_bind(&pad, INPUT_CTL_B, INPUT_PAD_PADDLE1);
    CHECK(IS(ICON(PROMPT_ART_XBOX, INPUT_CTL_B), PROMPT_ART_XBOX, INPUT_PAD_PADDLE1), "a paddle for B");
    CHECK(IS(ICON(PROMPT_ART_GAME, INPUT_CTL_B), PROMPT_ART_GAME, INPUT_CTL_B), "and the game's B");
    input_layout_bind(&pad, INPUT_CTL_X, INPUT_NONE);
    CHECK(IS(ICON(PROMPT_ART_XBOX, INPUT_CTL_X), PROMPT_ART_XBOX, INPUT_NONE), "none: the game's icon kept");

    input_layout_bind_key(&keys, INPUT_CTL_A, 0, INPUT_NONE);
    CHECK(IS(ICON(PROMPT_ART_KEYS, INPUT_CTL_A), PROMPT_ART_KEYS, INPUT_KEY_MOUSE_LEFT), "A's second, the mouse's left");
    input_layout_bind_key(&keys, INPUT_CTL_B, 0, INPUT_KEY_K);
    CHECK(IS(ICON(PROMPT_ART_KEYS, INPUT_CTL_B), PROMPT_ART_KEYS, INPUT_KEY_K), "B moved to K");
    CHECK(IS(input_prompts_icon(PROMPT_ART_GAME, &pad, &keys, INPUT_CTLS), PROMPT_ART_GAME, INPUT_NONE),
          "a control out of range");
    printf("input_prompts icon: %s\n", g_fail ? "FAIL" : "pass");
}

/* The prompts in use: Default, the game's own art, as it starts, and Auto
 * the pack's Xbox art before anything is pressed; a key, then
 * a pad of each kind, then a key again, each moving the count on once; the
 * same art again not moving it; a style picked by hand not following a key;
 * Default the game's own art; a style out of range changing nothing; and a
 * layout's change moving it. */
static void case_state(void)
{
    unsigned g;
    CHECK(input_prompts_style() == PROMPT_STYLE_GAME && input_prompts_art_now() == PROMPT_ART_GAME,
          "as it starts: Default");
    input_prompts_set_style(PROMPT_STYLE_AUTO);
    CHECK(input_prompts_art_now() == PROMPT_ART_XBOX, "Auto, with nothing pressed yet: the pack's Xbox art");
    g = input_prompts_generation();
    input_prompts_used(1, 0);
    CHECK(input_prompts_art_now() == PROMPT_ART_KEYS && input_prompts_generation() == g + 1, "a key");
    input_prompts_used(1, 0);
    CHECK(input_prompts_generation() == g + 1, "a key again: the same art");
    input_prompts_used(0, PROMPT_PAD_PLAYSTATION);
    CHECK(input_prompts_art_now() == PROMPT_ART_PLAYSTATION && input_prompts_pad_now() == PROMPT_PAD_PLAYSTATION
          && input_prompts_generation() == g + 2, "a PlayStation pad");
    input_prompts_used(0, PROMPT_PAD_NINTENDO);
    CHECK(input_prompts_art_now() == PROMPT_ART_NINTENDO && input_prompts_generation() == g + 3, "a Switch pad");
    input_prompts_set_style(PROMPT_STYLE_XBOX);
    CHECK(input_prompts_art_now() == PROMPT_ART_XBOX && input_prompts_generation() == g + 4, "Xbox chosen");
    input_prompts_used(1, 0);
    CHECK(input_prompts_art_now() == PROMPT_ART_XBOX && input_prompts_generation() == g + 4,
          "Xbox chosen, then a key: still Xbox");
    input_prompts_set_style(PROMPT_STYLE_GAME);
    CHECK(input_prompts_art_now() == PROMPT_ART_GAME && input_prompts_generation() == g + 5, "Default chosen");
    input_prompts_used(0, PROMPT_PAD_PLAYSTATION);
    CHECK(input_prompts_art_now() == PROMPT_ART_GAME && input_prompts_generation() == g + 5,
          "Default, then a PlayStation pad: still the game's own");
    input_prompts_set_style(PROMPT_STYLE_XBOX);
    input_prompts_set_style(PROMPT_STYLES);
    input_prompts_set_style(-1);
    CHECK(input_prompts_style() == PROMPT_STYLE_XBOX, "a style out of range changes nothing");
    g = input_prompts_generation();
    input_prompts_changed();
    CHECK(input_prompts_generation() == g + 1, "a layout's change");
    printf("input_prompts state: %s\n", g_fail ? "FAIL" : "pass");
}

/* At start: the submenu, Button prompts, then its Style row, in one add,
 * Input's, the row a choice of the six that each preset sets to Default,
 * each with a hint the strip can hold. Without a line, Default, saying
 * nothing, as JSRF-PC default's preset has it; with one, its style; a line
 * that is no style, Default, said. Choosing a style keeps it. */
static void case_start(const char *setting)
{
    static const char *const names[PROMPT_STYLES] = { "Auto", "Default", "Xbox", "PlayStation", "Nintendo",
                                                      "Keyboard" };
    const XBOX_MENU_ITEM *row = &s_rows[1];
    int i;
    if (setting) { s_have = 1; s_val = atof(setting); }
    input_prompts_start();
    CHECK(s_adds == 1 && s_nrows == 2, "%d adds, %d rows", s_adds, s_nrows);
    CHECK(s_rows[0].kind == XBOX_ITEM_GROUP && !strcmp(s_rows[0].label, XBOX_GROUP_PROMPTS)
          && !strcmp(XBOX_GROUP_PROMPTS, "Button prompts") && !s_rows[0].group, "the submenu first");
    CHECK(row->kind == XBOX_ITEM_CHOICE && !strcmp(row->label, "Style") && !strcmp(row->group, XBOX_GROUP_PROMPTS)
          && row->nchoices == PROMPT_STYLES && row->state && row->pick, "then Style, a choice");
    for (i = 0; i < PROMPT_STYLES; i++)
        CHECK(!strcmp(row->choices[i], names[i]), "choice %d is %s", i, row->choices[i]);
    CHECK(row->presets == XBOX_PRESETS_SET, "set by the presets");
    for (i = 0; i < XBOX_PRESETS; i++)
        CHECK(row->preset[i] == PROMPT_STYLE_GAME, "preset %d: %g, not Default", i, row->preset[i]);
    for (i = 0; i < s_nrows; i++) {
        size_t h = s_rows[i].hint ? strlen(s_rows[i].hint) : 0;
        CHECK(s_rows[i].menu == XBOX_MENU_INPUT, "%s: Input's", s_rows[i].label);
        CHECK(h > 0 && h <= MENU_HINT_LONGEST && s_rows[i].hint[h - 1] == '.', "%s: its hint", s_rows[i].label);
    }
    if (!setting) CHECK(row->state() == row->preset[XBOX_PRESET_BALANCED], "JSRF-PC default's, with no line");
    if (!setting || !strcmp(setting, "6")) CHECK(row->state() == PROMPT_STYLE_GAME, "Default");
    else CHECK(row->state() == atoi(setting), "the line's style");
    row->pick(PROMPT_STYLE_NINTENDO);
    CHECK(input_prompts_style() == PROMPT_STYLE_NINTENDO && s_sets == 1 && s_val == PROMPT_STYLE_NINTENDO, "kept");
    printf("input_prompts start: %s\n", g_fail ? "FAIL" : "pass");
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    if (!strcmp(c, "kind")) case_kind();
    else if (!strcmp(c, "art")) case_art();
    else if (!strcmp(c, "icon")) case_icon();
    else if (!strcmp(c, "state")) case_state();
    else if (!strcmp(c, "start")) case_start(argc > 2 ? argv[2] : NULL);
    else { printf("no case %s\n", c); return 2; }
    return g_fail;
}
