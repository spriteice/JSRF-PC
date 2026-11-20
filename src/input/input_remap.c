/* The layouts in use, their settings and their menu pages (input_remap.h). */
#include <stdio.h>
#include <string.h>
#include "input_remap.h"
#if defined(XBOXRECOMP_MENU)
#  include "menu.h"
#endif

/* The layouts' own file, beside the settings file, and the keys the window
 * keeps for itself: the SDL window's (nv2a_gl.c), so weak, as a run without
 * it has neither. */
#define INPUT_REMAP_FILE "remap.cfg"
extern int  nv_window_file_str(const char *file, const char *key, char *buf, size_t n) __attribute__((weak));
extern void nv_window_file_set_str(const char *file, const char *key, const char *val) __attribute__((weak));
extern int  nv_window_key_reserved(int scancode) __attribute__((weak));

/* Each control: its place, its name in the settings (pad_<name>=,
 * key_<name>=, and _2 for its second input) and its row's label. The
 * buttons, in the controller page's order; the sticks' directions, which
 * the keyboard's page alone has, above its buttons; and the port's own,
 * which the controller's page alone has, below its buttons. */
#define INPUT_REMAP_BUTTONS(F) \
    F(A, a, "A") F(B, b, "B") F(X, x, "X") F(Y, y, "Y") \
    F(BLACK, black, "Black") F(WHITE, white, "White") \
    F(LTRIGGER, ltrigger, "L trigger") F(RTRIGGER, rtrigger, "R trigger") \
    F(START, start, "Start") F(BACK, back, "Back") \
    F(LTHUMB, lthumb, "Left stick click") F(RTHUMB, rthumb, "Right stick click") \
    F(DPAD_UP, dpad_up, "D-pad up") F(DPAD_DOWN, dpad_down, "D-pad down") \
    F(DPAD_LEFT, dpad_left, "D-pad left") F(DPAD_RIGHT, dpad_right, "D-pad right")
#define INPUT_REMAP_STICKS(F) \
    F(LSTICK_UP, lstick_up, "Left stick up") F(LSTICK_DOWN, lstick_down, "Left stick down") \
    F(LSTICK_LEFT, lstick_left, "Left stick left") F(LSTICK_RIGHT, lstick_right, "Left stick right") \
    F(RSTICK_UP, rstick_up, "Right stick up") F(RSTICK_DOWN, rstick_down, "Right stick down") \
    F(RSTICK_LEFT, rstick_left, "Right stick left") F(RSTICK_RIGHT, rstick_right, "Right stick right")
#define INPUT_REMAP_OWN(F) \
    F(FREECAM, freecam, "Toggle free camera")

#define CTL_NAMES(C, nm, lbl) [INPUT_CTL_##C] = { #nm, lbl },
static const struct { const char *name, *label; } k_ctl[INPUT_CTLS] = {
    INPUT_REMAP_BUTTONS(CTL_NAMES) INPUT_REMAP_STICKS(CTL_NAMES) INPUT_REMAP_OWN(CTL_NAMES)
};

/* A layout: what it is, and the layout now. */
typedef struct {
    int         layout;                 /* INPUT_REMAP_PAD or INPUT_REMAP_KEYS */
    const char *prefix;                 /* its settings' names start with this */
    const char *title;                  /* for the log */
    void      (*defaults)(InputLayout *l);
    InputLayout now;
    char        text[INPUT_CTLS][64];   /* each row's text(), the window's thread's */
} Page;

static Page g_page[2] = {
    { .layout = INPUT_REMAP_PAD, .prefix = "pad", .title = "controller layout",
      .defaults = input_layout_pad_default },
    { .layout = INPUT_REMAP_KEYS, .prefix = "key", .title = "keyboard layout",
      .defaults = input_layout_keys_default },
};

/* The controls a layout has: the pad's, the buttons and the port's own (its
 * sticks are the game's as they are); the keyboard's, the buttons and the
 * sticks' directions (the free camera's key, F8, is the window's). */
static int has(const Page *p, int c)
{
    return p->layout == INPUT_REMAP_PAD ? c < INPUT_CTL_STICKS || c >= INPUT_CTL_OWN : c < INPUT_CTL_OWN;
}

/* The layouts are written on the window's thread (the menu) and at start,
 * and read on the game's too: a copy each way, under a spin lock, which
 * holds for no more than the copy. */
static int g_lock, g_ready;

static void lock(void)
{
    while (__atomic_exchange_n(&g_lock, 1, __ATOMIC_ACQUIRE)) { }
    if (!g_ready) {
        input_layout_pad_default(&g_page[INPUT_REMAP_PAD].now);
        input_layout_keys_default(&g_page[INPUT_REMAP_KEYS].now);
        g_ready = 1;
    }
}

static void unlock(void)
{
    __atomic_store_n(&g_lock, 0, __ATOMIC_RELEASE);
}

void input_remap_layouts(InputLayout *pad, InputLayout *keys)
{
    lock();
    if (pad)  *pad = g_page[INPUT_REMAP_PAD].now;
    if (keys) *keys = g_page[INPUT_REMAP_KEYS].now;
    unlock();
}

static void put(Page *p, const InputLayout *l) { lock(); p->now = *l; unlock(); }

/* An input a layout can have: one of its own, with a name the settings
 * keep and read back as the same input, and not one the menu or the window
 * keeps for itself: the pad's Guide, the system's, and its Back, the
 * menu's (docs/menu-format.md, Bind rows); the window's keys
 * (nv_window_key_reserved: F1, F9 to F11, and the keys the game binds, as
 * F8). */
static int usable(const Page *p, int input)
{
    const char *kept;
    if (input < 0 || input >= (p->layout == INPUT_REMAP_PAD ? INPUT_PAD_INPUTS : INPUT_KEYS)) return 0;
    if (p->layout == INPUT_REMAP_PAD && (input == INPUT_PAD_GUIDE || input == INPUT_PAD_BACK)) return 0;
    if (p->layout == INPUT_REMAP_KEYS && input < INPUT_KEY_SCANCODES
     && nv_window_key_reserved && nv_window_key_reserved(input)) return 0;
    kept = input_remap_name(p->layout, input, INPUT_NAME_KEPT);
    return kept && *kept && input_remap_from_name(p->layout, kept) == input;
}

/* A control's inputs, as the menu shows them: "Space, J", or "None". */
static void names(const Page *p, const InputLayout *l, int c, char *buf, size_t n)
{
    int b;
    buf[0] = 0;
    for (b = 0; b < INPUT_BINDS; b++) {
        const char *name;
        size_t at = strlen(buf);
        if (l->bind[c][b] == INPUT_NONE) continue;
        name = input_remap_name(p->layout, l->bind[c][b], INPUT_NAME_SHOWN);
        snprintf(buf + at, n - at, "%s%s", at ? ", " : "", name ? name : "?");
    }
    if (!buf[0]) snprintf(buf, n, "None");
}

/* In the log: each control whose inputs differ between was and now. */
static void say(const Page *p, const InputLayout *was, const InputLayout *now, const char *from)
{
    char buf[64];
    int c;
    for (c = 0; c < INPUT_CTLS; c++)
        if (has(p, c) && memcmp(was->bind[c], now->bind[c], sizeof now->bind[c])) {
            names(p, now, c, buf, sizeof buf);
            fprintf(stderr, "[INPUT] %s: %s is %s%s\n", p->title, k_ctl[c].label, buf, from);
        }
}

/* A control's input's line in the settings: pad_a=, and pad_a_2= for its
 * second. */
static void slot_key(const Page *p, int c, int b, char *buf, size_t n)
{
    if (b) snprintf(buf, n, "%s_%s_%d", p->prefix, k_ctl[c].name, b + 1);
    else   snprintf(buf, n, "%s_%s", p->prefix, k_ctl[c].name);
}

/* At start: remap.cfg's lines over the defaults. A name that is no input
 * the layout can have keeps the default, and the log says so. */
static void read_page(Page *p)
{
    InputLayout def, l;
    char key[48], val[128];
    int c, b;
    p->defaults(&def);
    l = def;
    if (nv_window_file_str)
        for (c = 0; c < INPUT_CTLS; c++)
            for (b = 0; has(p, c) && b < INPUT_BINDS; b++) {
                int in = INPUT_NONE;
                slot_key(p, c, b, key, sizeof key);
                if (nv_window_file_str(INPUT_REMAP_FILE, key, val, sizeof val) <= 0) continue;
                if (strcmp(val, "none")) {
                    in = input_remap_from_name(p->layout, val);
                    if (in == INPUT_NONE || !usable(p, in)) {
                        fprintf(stderr, "[INPUT] " INPUT_REMAP_FILE ": %s=%s is %s; %s keeps its default\n",
                                key, val, in == INPUT_NONE ? "no input known" : "not one a layout can have",
                                k_ctl[c].label);
                        continue;
                    }
                }
                l.bind[c][b] = (short)in;
            }
    say(p, &def, &l, " (" INPUT_REMAP_FILE ")");
    put(p, &l);
}

#if defined(XBOXRECOMP_MENU)
/* ---- the menu's pages (menu.h, XBOX_GROUP_PAD_LAYOUT and
 * XBOX_GROUP_KEY_LAYOUT) -------------------------------------------------
 * On the window's thread, the layouts' only writer once the game runs. A
 * row binds the input pressed to its control alone, and whatever control
 * had that input gets the row's old one (input_layout_bind); Reset to
 * default puts each control back. Each change is kept in remap.cfg at
 * once, and said in the log. */

static void get(const Page *p, InputLayout *l) { lock(); *l = p->now; unlock(); }

/* Each input that changed from was to now, kept in remap.cfg: its line,
 * or none where it is its default again, so the file holds only what a
 * player changed. */
static void keep(const Page *p, const InputLayout *was, const InputLayout *now)
{
    InputLayout def;
    char key[48];
    int c, b;
    if (!nv_window_file_set_str) return;
    p->defaults(&def);
    for (c = 0; c < INPUT_CTLS; c++)
        for (b = 0; has(p, c) && b < INPUT_BINDS; b++) {
            int in = now->bind[c][b];
            if (in == was->bind[c][b]) continue;
            slot_key(p, c, b, key, sizeof key);
            nv_window_file_set_str(INPUT_REMAP_FILE, key, in == def.bind[c][b] ? NULL
                                   : in == INPUT_NONE ? "none"
                                   : input_remap_name(p->layout, in, INPUT_NAME_KEPT));
        }
}

static const char *row_text(Page *p, int c)
{
    InputLayout l;
    get(p, &l);
    names(p, &l, c, p->text[c], sizeof p->text[c]);
    return p->text[c];
}

static int row_bind(Page *p, int c, int input)
{
    InputLayout was, now;
    if (!usable(p, input)) return 0;
    get(p, &was);
    now = was;
    input_layout_bind(&now, c, input);
    put(p, &now);
    keep(p, &was, &now);
    say(p, &was, &now, "");
    return 1;
}

static void reset(Page *p)
{
    InputLayout was, now;
    get(p, &was);
    p->defaults(&now);
    put(p, &now);
    keep(p, &was, &now);
    fprintf(stderr, "[INPUT] %s: reset to default\n", p->title);
}

/* Each row's text() and bind(), which the menu calls with no word of which
 * row it is. */
#define PAD_FNS(C, nm, lbl) \
    static const char *pad_text_##nm(void) { return row_text(&g_page[INPUT_REMAP_PAD], INPUT_CTL_##C); } \
    static int pad_bind_##nm(int in) { return row_bind(&g_page[INPUT_REMAP_PAD], INPUT_CTL_##C, in); }
#define KEY_FNS(C, nm, lbl) \
    static const char *key_text_##nm(void) { return row_text(&g_page[INPUT_REMAP_KEYS], INPUT_CTL_##C); } \
    static int key_bind_##nm(int in) { return row_bind(&g_page[INPUT_REMAP_KEYS], INPUT_CTL_##C, in); }
INPUT_REMAP_BUTTONS(PAD_FNS)
INPUT_REMAP_OWN(PAD_FNS)
INPUT_REMAP_BUTTONS(KEY_FNS)
INPUT_REMAP_STICKS(KEY_FNS)

static void pad_reset(void) { reset(&g_page[INPUT_REMAP_PAD]); }
static void key_reset(void) { reset(&g_page[INPUT_REMAP_KEYS]); }

/* A player's own: no preset sets them (docs/menu-format.md, File >
 * Presets), and each page has its own way back, Reset to default. */
#define PAD_ROW(C, nm, lbl) \
    { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_BIND, .label = lbl, \
      .group = XBOX_GROUP_PAD_LAYOUT, .takes = XBOX_BIND_PAD, \
      .text = pad_text_##nm, .bind = pad_bind_##nm, .presets = XBOX_PRESETS_LEFT, \
      .hint = "Sets the button that presses this control." },
#define KEY_ROW(C, nm, lbl) \
    { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_BIND, .label = lbl, \
      .group = XBOX_GROUP_KEY_LAYOUT, .takes = XBOX_BIND_KEYS, \
      .text = key_text_##nm, .bind = key_bind_##nm, .presets = XBOX_PRESETS_LEFT, \
      .hint = "Sets the key or mouse button that presses this control." },

static void remap_menu(void)
{
    static const XBOX_MENU_ITEM rows[] = {
        { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_GROUP, .label = XBOX_GROUP_PAD_LAYOUT,
          .hint = "Sets which button presses each control." },
        INPUT_REMAP_BUTTONS(PAD_ROW)
        INPUT_REMAP_OWN(PAD_ROW)
        { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_ACTION, .label = "Reset to default",
          .group = XBOX_GROUP_PAD_LAYOUT, .run = pad_reset, .presets = XBOX_PRESETS_LEFT,
          .hint = "Puts every control back on its default button." },
        { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_GROUP, .label = XBOX_GROUP_KEY_LAYOUT,
          .hint = "Sets which key or mouse button presses each control." },
        INPUT_REMAP_STICKS(KEY_ROW)
        INPUT_REMAP_BUTTONS(KEY_ROW)
        { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_ACTION, .label = "Reset to default",
          .group = XBOX_GROUP_KEY_LAYOUT, .run = key_reset, .presets = XBOX_PRESETS_LEFT,
          .hint = "Puts every control back on its default key." },
    };
    xbox_MenuAdd(rows, (int)(sizeof rows / sizeof rows[0]));
}
#endif

void input_remap_start(void)
{
    read_page(&g_page[INPUT_REMAP_PAD]);
    read_page(&g_page[INPUT_REMAP_KEYS]);
#if defined(XBOXRECOMP_MENU)
    remap_menu();
#endif
}

/* ---- the port's own controls on the pad (input_remap.h) ------------------
 * Each pad's inputs held, a bit each, kept on the window's thread, its only
 * reader and writer once the game runs; pads told apart by SDL's numbers
 * for them, modulo INPUT_REMAP_PADS, as the menu tells their triggers
 * apart (src/menu/menu_sdl.cpp). */
#define INPUT_REMAP_PADS 8
_Static_assert(INPUT_PAD_INPUTS <= 32, "a pad's inputs held are a word's bits");
static uint32_t g_pad_held[INPUT_REMAP_PADS];
static void (*g_hotkey[INPUT_CTLS - INPUT_CTL_OWN])(void);

void input_remap_pad_hotkey(int control, void (*run)(void))
{
    if (control >= INPUT_CTL_OWN && control < INPUT_CTLS) g_hotkey[control - INPUT_CTL_OWN] = run;
}

void input_remap_pad_event(int pad, int input, int value, int shut)
{
    uint32_t *held, bit;
    InputLayout l;
    int c, b;
    if (input < 0 || input >= INPUT_PAD_INPUTS) return;
    held = &g_pad_held[(unsigned)pad % INPUT_REMAP_PADS];
    bit = 1u << input;
    if (value < INPUT_LAYOUT_DOWN) { *held &= ~bit; return; }
    if (*held & bit) return;            /* still held: a trigger moving on */
    *held |= bit;
    if (!shut) return;                  /* the menu's, and not once it shuts */
    input_remap_layouts(&l, NULL);
    for (c = INPUT_CTL_OWN; c < INPUT_CTLS; c++)
        for (b = 0; b < INPUT_BINDS; b++)
            if (l.bind[c][b] == input && g_hotkey[c - INPUT_CTL_OWN]) {
                g_hotkey[c - INPUT_CTL_OWN]();
                break;
            }
}
