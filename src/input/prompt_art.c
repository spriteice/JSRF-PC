/* The button prompts' art (prompt_art.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "prompt_art.h"
#include "../../third_party/miniz/miniz.h"

/* ---- each set's pictures ------------------------------------------------ */

/* A pad set's pictures of a host pad's inputs, in INPUT_PAD_...'s order,
 * the triggers last; NULL for none of its own. The touchpad is
 * PAD_SHARED's. Juliocacko's Free Input Prompts, the Xbox and PlayStation
 * ones shaded the Nintendo ones' grey but the Xbox face buttons, with
 * Xelu's PS Vita Select and Start as the PlayStation's Create and Options
 * (src/input/prompts/README.md; spriteice, 2026-11-22). */
#define PAD_SHARED NULL
static const char *const k_xbox[INPUT_PAD_INPUTS] = {
    "xbox_a", "xbox_b", "xbox_x", "xbox_y",
    "xbox_view", NULL, "xbox_menu",
    "xbox_ls", "xbox_rs", "xbox_lb", "xbox_rb",
    "xbox_dpad_up", "xbox_dpad_down", "xbox_dpad_left", "xbox_dpad_right",
    "xbox_share", NULL, NULL, NULL, NULL,
    PAD_SHARED,
    "xbox_lt", "xbox_rt",
};
static const char *const k_playstation[INPUT_PAD_INPUTS] = {
    "ps_cross", "ps_circle", "ps_square", "ps_triangle",
    "ps_create", NULL, "ps_options",
    "ps_l3", "ps_r3", "ps_l1", "ps_r1",
    "ps_dpad_up", "ps_dpad_down", "ps_dpad_left", "ps_dpad_right",
    NULL, NULL, NULL, NULL, NULL,
    PAD_SHARED,
    "ps_l2", "ps_r2",
};
/* SDL names a Nintendo pad's buttons by their labels (its
 * SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, on unless set otherwise, and
 * the engine doesn't): its A is the pad's A, on the right. Its Capture
 * button is SDL's Misc. The pack has no stick pressed: the sticks are its
 * L and R sticks. */
static const char *const k_nintendo[INPUT_PAD_INPUTS] = {
    "nintendo_a", "nintendo_b", "nintendo_x", "nintendo_y",
    "nintendo_minus", NULL, "nintendo_plus",
    "nintendo_ls", "nintendo_rs", "nintendo_l", "nintendo_r",
    "nintendo_dpad_up", "nintendo_dpad_down", "nintendo_dpad_left", "nintendo_dpad_right",
    "nintendo_capture", NULL, NULL, NULL, NULL,
    PAD_SHARED,
    "nintendo_zl", "nintendo_zr",
};
/* The shared one: the PlayStation's touchpad, for every set. */
static const char *const k_shared[INPUT_PAD_INPUTS] = {
    [INPUT_PAD_TOUCHPAD] = "touchpad",
};
/* The names for the inputs a set has no picture of, but a layout can put
 * on a control, written on the blank key: the paddles, for every set, as
 * the Xbox Elite labels them (SDL's first paddle is its P1, the second its
 * P3, the third its P2: SDL_gamecontroller.h), and the PlayStation's Mute
 * (SDL's Misc). */
static const char *const k_labels[INPUT_PAD_INPUTS] = {
    [INPUT_PAD_PADDLE1] = "P1",
    [INPUT_PAD_PADDLE2] = "P3",
    [INPUT_PAD_PADDLE3] = "P2",
    [INPUT_PAD_PADDLE4] = "P4",
};
static const char *const k_playstation_labels[INPUT_PAD_INPUTS] = {
    [INPUT_PAD_MISC1] = "Mute",
};

/* The keys' pictures, by SDL's number for the key (SDL_Scancode): Xelu's
 * dark keys, the keypad's numbers, /, -, + and Enter as the main ones,
 * and the right Ctrl, Shift, Alt and Windows keys as the left. The keys
 * without one (Scroll Lock, Pause, the keypad's full stop and the rest)
 * are drawn on the blank one with their names. */
static const char *const k_keys[INPUT_KEY_SCANCODES] = {
    [4] = "key_a", [5] = "key_b", [6] = "key_c", [7] = "key_d",
    [8] = "key_e", [9] = "key_f", [10] = "key_g", [11] = "key_h",
    [12] = "key_i", [13] = "key_j", [14] = "key_k", [15] = "key_l",
    [16] = "key_m", [17] = "key_n", [18] = "key_o", [19] = "key_p",
    [20] = "key_q", [21] = "key_r", [22] = "key_s", [23] = "key_t",
    [24] = "key_u", [25] = "key_v", [26] = "key_w", [27] = "key_x",
    [28] = "key_y", [29] = "key_z",
    [30] = "key_1", [31] = "key_2", [32] = "key_3", [33] = "key_4",
    [34] = "key_5", [35] = "key_6", [36] = "key_7", [37] = "key_8",
    [38] = "key_9", [39] = "key_0",
    [40] = "key_enter", [41] = "key_esc", [42] = "key_backspace",
    [43] = "key_tab", [44] = "key_space", [45] = "key_minus",
    [46] = "key_plus",                          /* = and + */
    [47] = "key_bracket_left", [48] = "key_bracket_right",
    [49] = "key_backslash",
    [51] = "key_semicolon", [52] = "key_quote", [53] = "key_grave",
    [54] = "key_comma",                         /* , and < */
    [55] = "key_period",                        /* . and > */
    [56] = "key_slash",                         /* / and ? */
    [57] = "key_caps_lock",
    [58] = "key_f1", [59] = "key_f2", [60] = "key_f3", [61] = "key_f4",
    [62] = "key_f5", [63] = "key_f6", [64] = "key_f7", [65] = "key_f8",
    [66] = "key_f9", [67] = "key_f10", [68] = "key_f11", [69] = "key_f12",
    [70] = "key_print_screen", [73] = "key_insert", [74] = "key_home",
    [75] = "key_page_up", [76] = "key_delete", [77] = "key_end",
    [78] = "key_page_down",
    [INPUT_KEY_RIGHT] = "key_right", [INPUT_KEY_LEFT] = "key_left",
    [INPUT_KEY_DOWN] = "key_down", [INPUT_KEY_UP] = "key_up",
    [83] = "key_num_lock",
    [84] = "key_slash", [85] = "key_asterisk",
    [86] = "key_minus", [87] = "key_plus", [INPUT_KEY_KP_ENTER] = "key_enter",
    [89] = "key_1", [90] = "key_2", [91] = "key_3", [92] = "key_4",
    [93] = "key_5", [94] = "key_6", [95] = "key_7", [96] = "key_8",
    [97] = "key_9", [98] = "key_0",
    [INPUT_KEY_LCTRL] = "key_ctrl", [INPUT_KEY_LSHIFT] = "key_shift",
    [226] = "key_alt", [227] = "key_win",
    [INPUT_KEY_RCTRL] = "key_ctrl", [229] = "key_shift",
    [230] = "key_alt", [231] = "key_win",
};
/* The mouse's buttons, from its left: left, middle, right, back, forward
 * (the last two the mouse with none lit). */
static const char *const k_mouse[INPUT_KEY_MOUSE_BUTTONS] = {
    "mouse_left", "mouse_middle", "mouse_right",
    "mouse_none", "mouse_none",
};

const char *prompt_art_name(int art, int input)
{
    const char *const *set;
    if (art == PROMPT_ART_KEYS) {
        if (input >= 0 && input < INPUT_KEY_SCANCODES) return k_keys[input];
        if (input >= INPUT_KEY_MOUSE && input < INPUT_KEYS) return k_mouse[input - INPUT_KEY_MOUSE];
        return NULL;
    }
    switch (art) {
    case PROMPT_ART_XBOX:        set = k_xbox; break;
    case PROMPT_ART_PLAYSTATION: set = k_playstation; break;
    case PROMPT_ART_NINTENDO:    set = k_nintendo; break;
    default: return NULL;
    }
    if (input < 0 || input >= INPUT_PAD_INPUTS) return NULL;
    return set[input] ? set[input] : k_shared[input];
}

const char *prompt_art_label(int art, int input)
{
    if (art < PROMPT_ART_XBOX || art > PROMPT_ART_NINTENDO || input < 0 || input >= INPUT_PAD_INPUTS)
        return NULL;
    if (art == PROMPT_ART_PLAYSTATION && k_playstation_labels[input]) return k_playstation_labels[input];
    return k_labels[input];
}

int prompt_art_pad_input(int c)
{
    InputLayout d;
    int b;
    if (c == INPUT_CTL_RTHUMB) return INPUT_PAD_RSTICK;
    if (c == INPUT_CTL_BACK) return INPUT_PAD_BACK;
    if (c < 0 || c >= INPUT_CTL_STICKS) return INPUT_NONE;
    input_layout_pad_default(&d);
    for (b = 0; b < INPUT_BINDS; b++)
        if (d.bind[c][b] != INPUT_NONE) return d.bind[c][b];
    return INPUT_NONE;
}

/* ---- PNG ---------------------------------------------------------------- */

#define PNG_SIGNATURE   "\x89PNG\r\n\x1a\n"
#define PNG_PALETTE     3       /* IHDR's colour types: indexed */
#define PNG_RGBA        6       /* ... and truecolour with alpha (the pictures') */
#define PNG_CHANNELS    4
#define PNG_COLOURS     256     /* a palette's most entries, at 8 bits */
#define PNG_MAX_SIDE    4096

static uint32_t be32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

uint8_t *prompt_art_png(const unsigned char *png, unsigned size, int *w_out, int *h_out)
{
    const unsigned char *p = png + 8, *end = png + size;
    unsigned char *idat = NULL, *raw = NULL, *px = NULL;
    uint8_t *rgba = NULL, palette[PNG_COLOURS][PNG_CHANNELS];
    size_t nidat = 0, stride, need;
    mz_ulong got;
    uint32_t w = 0, h = 0, x, y, ncolours = 0, i;
    int type = -1, bpp;
    if (size < 8 || memcmp(png, PNG_SIGNATURE, 8)) return NULL;
    /* Opaque where tRNS gives a colour no alpha. */
    memset(palette, 0xFF, sizeof palette);
    while (p + 12 <= end) {
        uint32_t len = be32(p);
        const unsigned char *kind = p + 4, *data = p + 8;
        if (len > (size_t)(end - data) - 4) goto fail;
        if (!memcmp(kind, "IHDR", 4)) {
            if (len < 13) goto fail;
            w = be32(data);
            h = be32(data + 4);
            type = data[9];
            /* 8 bits, RGBA or a palette, deflate, filters per row, no interlace */
            if (data[8] != 8 || (type != PNG_RGBA && type != PNG_PALETTE) || data[10] || data[11]
             || data[12])
                goto fail;
            if (!w || !h || w > PNG_MAX_SIDE || h > PNG_MAX_SIDE) goto fail;
        } else if (!memcmp(kind, "PLTE", 4)) {
            if (len % 3 || len / 3 > PNG_COLOURS) goto fail;
            ncolours = len / 3;
            for (i = 0; i < ncolours; i++) memcpy(palette[i], data + i * 3, 3);
        } else if (!memcmp(kind, "tRNS", 4)) {
            if (type != PNG_PALETTE || len > ncolours) goto fail;
            for (i = 0; i < len; i++) palette[i][3] = data[i];
        } else if (!memcmp(kind, "IDAT", 4)) {
            unsigned char *more = (unsigned char *)realloc(idat, nidat + len);
            if (!more) goto fail;
            idat = more;
            memcpy(idat + nidat, data, len);
            nidat += len;
        } else if (!memcmp(kind, "IEND", 4)) {
            break;
        }
        p = data + len + 4;                     /* past the chunk's CRC */
    }
    if (!w || !nidat || (type == PNG_PALETTE && !ncolours)) goto fail;
    bpp = type == PNG_PALETTE ? 1 : PNG_CHANNELS;   /* bytes a texel, which the filters step by */
    stride = (size_t)w * (size_t)bpp;
    need = (stride + 1) * h;                    /* each row's filter byte, then its bytes */
    raw = (unsigned char *)malloc(need);
    px = (unsigned char *)malloc(stride * h);
    if (!raw || !px) goto fail;
    got = (mz_ulong)need;
    if (mz_uncompress(raw, &got, idat, (mz_ulong)nidat) != MZ_OK || got != need) goto fail;
    for (y = 0; y < h; y++) {
        const unsigned char *in = raw + y * (stride + 1) + 1;
        unsigned char *row = px + y * stride, *up = y ? row - stride : NULL;
        int filter = in[-1];
        for (x = 0; x < stride; x++) {
            int a = x >= (uint32_t)bpp ? row[x - (uint32_t)bpp] : 0;
            int b = up ? up[x] : 0;
            int c = up && x >= (uint32_t)bpp ? up[x - (uint32_t)bpp] : 0;
            int v;
            switch (filter) {
            case 0: v = 0; break;
            case 1: v = a; break;
            case 2: v = b; break;
            case 3: v = (a + b) / 2; break;
            case 4: v = paeth(a, b, c); break;
            default: goto fail;
            }
            row[x] = (unsigned char)(in[x] + v);
        }
    }
    if (type == PNG_RGBA) {
        rgba = px;
        px = NULL;
    } else {
        rgba = (uint8_t *)malloc((size_t)w * h * PNG_CHANNELS);
        if (!rgba) goto fail;
        for (i = 0; i < w * h; i++) {
            if (px[i] >= ncolours) goto fail;
            memcpy(rgba + (size_t)i * PNG_CHANNELS, palette[px[i]], PNG_CHANNELS);
        }
    }
    free(idat);
    free(raw);
    free(px);
    *w_out = (int)w;
    *h_out = (int)h;
    return rgba;
fail:
    free(idat);
    free(raw);
    free(px);
    free(rgba);
    return NULL;
}

/* ---- the pictures, decoded once ----------------------------------------- */

typedef struct { uint8_t *rgba; int w, h; } Decoded;
static Decoded *g_decoded;                      /* prompt_art_nfiles of them, once made */

static int by_name(const void *key, const void *file)
{
    return strcmp((const char *)key, ((const PromptArtFile *)file)->name);
}

int prompt_art_image(const char *name, PromptImage *out)
{
    const PromptArtFile *f;
    Decoded *all, *d;
    uint8_t *rgba, *was;
    int i, w, h;
    if (!name) return 0;
    f = (const PromptArtFile *)bsearch(name, prompt_art_files, (size_t)prompt_art_nfiles,
                                       sizeof prompt_art_files[0], by_name);
    if (!f) {
        static int said;
        if (!said++) fprintf(stderr, "[INPUT] button prompts: no picture %s built in\n", name);
        return 0;
    }
    i = (int)(f - prompt_art_files);
    all = __atomic_load_n(&g_decoded, __ATOMIC_ACQUIRE);
    if (!all) {
        Decoded *made = (Decoded *)calloc((size_t)prompt_art_nfiles, sizeof *made);
        if (!made) return 0;
        if (!__atomic_compare_exchange_n(&g_decoded, &all, made, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            free(made);                         /* another thread's is in place: all is it */
        else
            all = made;
    }
    d = &all[i];
    rgba = __atomic_load_n(&d->rgba, __ATOMIC_ACQUIRE);
    if (!rgba) {
        rgba = prompt_art_png(f->png, f->size, &w, &h);
        if (!rgba) {
            static int said;
            if (!said++) fprintf(stderr, "[INPUT] button prompts: %s.png doesn't decode\n", name);
            return 0;
        }
        d->w = w;
        d->h = h;
        was = NULL;
        if (!__atomic_compare_exchange_n(&d->rgba, &was, rgba, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            free(rgba);                         /* decoded twice at once: keep the first */
            rgba = was;
        }
    }
    out->w = d->w;
    out->h = d->h;
    out->rgba = rgba;
    return 1;
}
