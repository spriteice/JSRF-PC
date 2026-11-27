/*
 * JSRF-PC Tools' graffiti page (tools.h; b3 row 35): the player's own
 * pictures made into designs in the garage's own-designs tab.
 *
 *   Adding: pictures dropped on the window, folders of them, or several
 *     picked at once with the system's dialog. JPEG, PNG, WebP, AVIF, SVG
 *     and TIFF (graffiti_image.h): a folder is walked for their names, and
 *     each file's own first bytes say what it is.
 *   The folder: each design as the game will have it, grouped by size.
 *   Size: Auto takes the nearest shape of S, M, L and XL; any size, SS
 *     among them, by hand (spriteice, 7:41pm on 2026-11-26).
 *   Fitting: the whole picture with clear margins (the default), a crop
 *     box moved and sized one picture at a time, or stretched.
 *   Clear: kept as the game keeps it, a pixel solid or clear; a white
 *     background made clear when ticked, as it starts for a solid picture
 *     whose edge is white.
 *   Slots: each design's shown (page, row, column): the first free ones, in
 *     the folder's order, or one chosen by hand; below
 *     JSRF_GRAFFITI_SLOTS_SAFE, as the saves page keeps them, until a
 *     design past it has been seen in the game.
 *   Writing: only from Import, after the shell's warning, then a
 *     dated backup (jsrf_backup). A design only ever goes into a free slot.
 *
 * A picture keeps its working copy (at most WORK_SIDE pixels a side) only
 * while it is chosen; the others keep their design, and read their file
 * again when one is changed.
 */
#include <SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "imgui.h"

#include "file_dialog.h"
#include "graffiti_fit.h"
#include "graffiti_image.h"
#include "graffiti_save.h"
#include "setup.h"
#include "setup_job.h"   /* js_path */
#include "tools.h"

#define WORK_SIDE   2048            /* a working copy's longest side: twice XL's width */
#define SHOW_SIDE   1024            /* the crop box's picture on screen */
#define READ_MAX    (512u << 20)    /* the largest file read */
#define MAX_PICS    GRAFFITI_SLOTS
#define FOLDER_DEPTH 4              /* how deep a dropped folder is walked */
#define SLOTS_USED  JSRF_GRAFFITI_SLOTS_SAFE

/* ---- OpenGL, for the previews: the few calls, from SDL ------------------------------------ */

#ifdef _WIN32
#define G_GLAPI __stdcall
#else
#define G_GLAPI
#endif
typedef void (G_GLAPI *gen_textures_fn)(int, unsigned *);
typedef void (G_GLAPI *delete_textures_fn)(int, const unsigned *);
typedef void (G_GLAPI *bind_texture_fn)(unsigned, unsigned);
typedef void (G_GLAPI *tex_parameteri_fn)(unsigned, unsigned, int);
typedef void (G_GLAPI *tex_image_2d_fn)(unsigned, int, int, int, int, int, unsigned, unsigned, const void *);
typedef void (G_GLAPI *pixel_storei_fn)(unsigned, int);
#define G_TEXTURE_2D            0x0DE1u
#define G_TEXTURE_MAG_FILTER    0x2800u
#define G_TEXTURE_MIN_FILTER    0x2801u
#define G_TEXTURE_WRAP_S        0x2802u
#define G_TEXTURE_WRAP_T        0x2803u
#define G_NEAREST               0x2600
#define G_LINEAR                0x2601
#define G_REPEAT                0x2901
#define G_CLAMP_TO_EDGE         0x812F
#define G_RGBA                  0x1908u
#define G_UNSIGNED_BYTE         0x1401u
#define G_UNPACK_ALIGNMENT      0x0CF5u
#define G_UNPACK_ROW_LENGTH     0x0CF2u

static struct {
    int tried, ok;
    gen_textures_fn gen;
    delete_textures_fn del;
    bind_texture_fn bind;
    tex_parameteri_fn param;
    tex_image_2d_fn image;
    pixel_storei_fn store;
} gl;

static int gl_load(void)
{
    if (!gl.tried) {
        gl.tried = 1;
        gl.gen = (gen_textures_fn)SDL_GL_GetProcAddress("glGenTextures");
        gl.del = (delete_textures_fn)SDL_GL_GetProcAddress("glDeleteTextures");
        gl.bind = (bind_texture_fn)SDL_GL_GetProcAddress("glBindTexture");
        gl.param = (tex_parameteri_fn)SDL_GL_GetProcAddress("glTexParameteri");
        gl.image = (tex_image_2d_fn)SDL_GL_GetProcAddress("glTexImage2D");
        gl.store = (pixel_storei_fn)SDL_GL_GetProcAddress("glPixelStorei");
        gl.ok = gl.gen && gl.del && gl.bind && gl.param && gl.image && gl.store;
    }
    return gl.ok;
}

/* rgba (w x h) into *tex, made the first time. nearest: the design's own
 * pixels, as the game draws them, when shown bigger. */
static void tex_put(unsigned *tex, const uint8_t *rgba, int w, int h, int nearest, int repeat)
{
    if (!gl_load() || !rgba) return;
    if (!*tex) gl.gen(1, tex);
    gl.bind(G_TEXTURE_2D, *tex);
    gl.param(G_TEXTURE_2D, G_TEXTURE_MIN_FILTER, G_LINEAR);
    gl.param(G_TEXTURE_2D, G_TEXTURE_MAG_FILTER, nearest ? G_NEAREST : G_LINEAR);
    gl.param(G_TEXTURE_2D, G_TEXTURE_WRAP_S, repeat ? G_REPEAT : G_CLAMP_TO_EDGE);
    gl.param(G_TEXTURE_2D, G_TEXTURE_WRAP_T, repeat ? G_REPEAT : G_CLAMP_TO_EDGE);
    gl.store(G_UNPACK_ALIGNMENT, 4);
    gl.store(G_UNPACK_ROW_LENGTH, 0);
    gl.image(G_TEXTURE_2D, 0, (int)G_RGBA, w, h, 0, G_RGBA, G_UNSIGNED_BYTE, rgba);
    gl.bind(G_TEXTURE_2D, 0);
}

static void tex_drop(unsigned *tex)
{
    if (*tex && gl_load()) gl.del(1, tex);
    *tex = 0;
}

static ImTextureRef tex_ref(unsigned tex)
{
    return ImTextureRef((ImTextureID)(intptr_t)tex);
}

/* ---- the pictures ------------------------------------------------------------------------- */

#define PIC_READY   0
#define PIC_WROTE   1
#define PIC_FAILED  2

struct pic {
    char path[JSRF_PATH_MAX];
    char name[256];             /* the file's name, as shown */
    int kind;                   /* GRAFFITI_IMAGE_* */
    int src_w, src_h;           /* the file's picture, upright */
    int opaque;
    uint8_t *work;              /* the working copy, ww x wh; NULL when not kept */
    int ww, wh;
    int size_auto, size;        /* size: the one in use, Auto's when size_auto */
    int mode;                   /* GRAFFITI_FIT_* */
    graffiti_box crop;          /* in the working copy's pixels, when has_crop */
    int has_crop;
    int white, white_all;
    uint8_t *design;            /* graffiti_width(size) x graffiti_height(size) x 4 */
    unsigned tex;               /* the design, on screen */
    int dirty;                  /* the design to be made again */
    int slot, slot_hand;        /* slot: -1 when there's no free one; slot_hand: chosen by hand */
    int state;                  /* PIC_* */
    char why[800];              /* the last word on it */
};

struct page_state {
    ImVector<pic *> pics;
    ImVector<char *> pending;   /* files to read, a frame each */
    int pending_total;
    int selected;               /* -1 none */
    bool white_default;         /* new solid pictures with a white edge: their white made clear */
    uint8_t taken[GRAFFITI_SLOTS];
    double taken_at;
    char taken_title[JSRF_PATH_MAX];
    unsigned checker;
    unsigned show_tex;          /* the chosen picture, for its crop box */
    const pic *show_of;
    int show_w, show_h;
    char note[JSRF_PATH_MAX + 600];     /* the page's one outcome (say), shown where it happened */
    int note_at, note_kind;     /* AT_*; TOOLS_OK, TOOLS_WARN or TOOLS_ERROR */
    ImGuiTextBuffer unread;     /* the files that couldn't be read, a line each */
    int nunread;
    ImGuiTextBuffer warning;
    char warning_id[80];        /* the warning's question, and its id */
    int ran;                    /* the import ran: the folder says how each went */
    char backup[JSRF_PATH_MAX]; /* where the backup went, after an import; empty when none was needed */
    int place_edit[3];          /* the place fields: page, row and column, 1 based */
    int slot_shown;             /* the chosen design's slot when the fields last took it */
    float import_h;             /* the Import section's height, last frame */
};

/* Where an outcome is shown: under the section it came from. */
#define AT_PICTURES 0
#define AT_DESIGN   1
#define AT_IMPORT   2
static page_state g;   /* zeroed as a static: no picture chosen is set on the first draw */
static bool g_started;

static void pic_free(pic *p)
{
    free(p->work);
    free(p->design);
    tex_drop(&p->tex);
    free(p);
}

static const char *base_name(const char *path)
{
    const char *b = path;
    for (const char *c = path; *c; c++)
        if (*c == '/' || *c == '\\') b = c + 1;
    return b;
}

/* The page's outcome, one at a time: it replaces the last, and stays until
 * the next. */
static void say(int at, int kind, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g.note, sizeof g.note, fmt, ap);
    va_end(ap);
    g.note_at = at;
    g.note_kind = kind;
}

static void unsay(int at)
{
    if (g.note_at == at) g.note[0] = 0;
}

static void show_note(int at)
{
    if (g.note[0] && g.note_at == at) tools_status(g.note_kind, "%s", g.note);
}

/* How much of a picture's edge is white: 0 to 1. */
static double edge_white(const uint8_t *rgba, int w, int h)
{
    long white = 0, all = 0;
    const int lo = 255 - GRAFFITI_WHITE_TOLERANCE;
    for (int y = 0; y < h; y++) {
        int step = (y == 0 || y == h - 1) ? 1 : (w > 1 ? w - 1 : 1);
        for (int x = 0; x < w; x += step) {
            const uint8_t *q = rgba + ((size_t)y * w + x) * 4;
            all++;
            white += q[3] == 255 && q[0] >= lo && q[1] >= lo && q[2] >= lo;
        }
    }
    return all ? (double)white / all : 0;
}

/* The file read into p's working copy: its picture, scaled down to
 * WORK_SIDE a side when it's bigger. 0, or -1 with why. */
static int read_work(pic *p, char *why, size_t wn)
{
    unsigned char *data = NULL;
    size_t n = 0;
    graffiti_image img;
    memset(&img, 0, sizeof img);
    if (js_read_file(p->path, READ_MAX, &data, &n) != 0 || !data) {
        free(data);
        snprintf(why, wn, js_is_file(p->path) ? "It couldn't be read, or is bigger than 512 MB." : "It isn't there.");
        return -1;
    }
    int r = graffiti_image_read(data, n, &img, why, wn);
    free(data);
    if (r != 0) return -1;
    int w = img.width, h = img.height, side = w > h ? w : h;
    p->kind = img.kind;
    p->opaque = img.opaque;
    if (side > WORK_SIDE) {
        int ww = (int)((double)w * WORK_SIDE / side + 0.5), wh = (int)((double)h * WORK_SIDE / side + 0.5);
        if (ww < 1) ww = 1;
        if (wh < 1) wh = 1;
        uint8_t *small = (uint8_t *)malloc((size_t)ww * wh * 4);
        if (!small || graffiti_scale(img.rgba, w, h, small, ww, wh) != 0) {
            free(small);
            graffiti_image_free(&img);
            snprintf(why, wn, "There wasn't enough memory to read it.");
            return -1;
        }
        graffiti_image_free(&img);
        img.rgba = small;
        w = ww;
        h = wh;
    }
    if (p->work && (w != p->ww || h != p->wh)) p->has_crop = 0;   /* the file changed since */
    free(p->work);
    p->work = img.rgba;
    p->ww = w;
    p->wh = h;
    p->src_w = img.width;
    p->src_h = img.height;
    return 0;
}

static int need_work(pic *p)
{
    char why[400];
    if (p->work) return 0;
    if (read_work(p, why, sizeof why) == 0) return 0;
    snprintf(p->why, sizeof p->why, "%s: %s It's kept as it was.", p->name, why);
    return -1;
}

static int is_chosen(const pic *p)
{
    return g.selected >= 0 && g.selected < g.pics.Size && g.pics[g.selected] == p;
}

/* p's design made again from its working copy, read again if need be. */
static void refit(pic *p)
{
    p->dirty = 0;
    if (need_work(p) != 0) return;
    if (p->size_auto) {
        int s = graffiti_auto_size(p->src_w, p->src_h);
        if (s != p->size) p->has_crop = 0;
        p->size = s;
    }
    if (!p->has_crop) {
        p->crop = graffiti_crop_default(p->ww, p->wh, p->size);
        p->has_crop = 1;
    }
    p->crop = graffiti_crop_clamp(p->crop, p->ww, p->wh, p->size);
    int dw = graffiti_width(p->size), dh = graffiti_height(p->size);
    uint8_t *d = (uint8_t *)realloc(p->design, (size_t)dw * dh * 4);
    if (!d) {
        snprintf(p->why, sizeof p->why, "There wasn't enough memory for its design.");
        return;
    }
    p->design = d;
    const uint8_t *src = p->work;
    uint8_t *copy = NULL;
    if (p->white) {
        copy = (uint8_t *)malloc((size_t)p->ww * p->wh * 4);
        if (copy) {
            memcpy(copy, p->work, (size_t)p->ww * p->wh * 4);
            graffiti_white_to_clear(copy, p->ww, p->wh, GRAFFITI_WHITE_TOLERANCE, p->white_all);
            src = copy;
        }
    }
    if (graffiti_fit(src, p->ww, p->wh, p->size, p->mode, &p->crop, p->design) == 0) {
        tex_put(&p->tex, p->design, dw, dh, 1, 0);
        p->why[0] = 0;
    } else {
        snprintf(p->why, sizeof p->why, "There wasn't enough memory to fit it.");
    }
    free(copy);
    if (!is_chosen(p)) {
        free(p->work);   /* read again when it's next changed */
        p->work = NULL;
    }
    if (p->state == PIC_FAILED) p->state = PIC_READY;
}

static void choose(int i)
{
    if (g.selected >= 0 && g.selected < g.pics.Size && g.selected != i) {
        pic *old = g.pics[g.selected];
        free(old->work);
        old->work = NULL;
    }
    g.selected = i;
    unsay(AT_DESIGN);
    g.slot_shown = -2;   /* the fields take the design's place when it's drawn */
}

static void remove_pic(int i)
{
    if (i < 0 || i >= g.pics.Size) return;
    if (g.show_of == g.pics[i]) g.show_of = NULL;
    pic_free(g.pics[i]);
    g.pics.erase(g.pics.Data + i);
    if (g.selected == i) g.selected = -1;
    else if (g.selected > i) g.selected--;
}

static void clear_all(void)
{
    while (g.pics.Size) remove_pic(g.pics.Size - 1);
    for (int i = 0; i < g.pending.Size; i++) free(g.pending[i]);
    g.pending.clear();
    g.pending_total = 0;
    g.unread.clear();
    g.nunread = 0;
    g.ran = 0;
    g.note[0] = 0;
}

/* ---- adding ------------------------------------------------------------------------------- */

static int known(const char *path)
{
    for (int i = 0; i < g.pics.Size; i++)
        if (!strcmp(g.pics[i]->path, path)) return 1;
    for (int i = 0; i < g.pending.Size; i++)
        if (!strcmp(g.pending[i], path)) return 1;
    return 0;
}

static void queue(const char *path)
{
    if (known(path)) return;
    if (g.pics.Size + g.pending.Size >= MAX_PICS) {
        say(AT_PICTURES, TOOLS_WARN, "There's room for %d designs at most, one for each place in the garage.",
            MAX_PICS);
        return;
    }
    size_t n = strlen(path) + 1;
    char *c = (char *)malloc(n);
    if (!c) return;
    memcpy(c, path, n);
    g.pending.push_back(c);
    g.pending_total++;
}

struct walk {
    char dir[JSRF_PATH_MAX];
    ImVector<char *> names;     /* the pictures' names, then the folders', each marked */
};

static int walk_each(void *ctx, const char *name, int is_dir)
{
    walk *w = (walk *)ctx;
    if (!is_dir && !graffiti_image_name_ok(name)) return 0;
    size_t n = strlen(name) + 2;
    char *c = (char *)malloc(n);
    if (!c) return 0;
    c[0] = is_dir ? 'd' : 'f';
    memcpy(c + 1, name, n - 1);
    w->names.push_back(c);
    return 0;
}

static int by_name(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* A folder's pictures, by name, then its folders', down to FOLDER_DEPTH. */
static void queue_folder(const char *dir, int depth)
{
    walk w;
    snprintf(w.dir, sizeof w.dir, "%s", dir);
    if (js_list_dir(dir, walk_each, &w) < 0) {
        say(AT_PICTURES, TOOLS_ERROR, "The folder %s couldn't be read.", base_name(dir));
        return;
    }
    if (w.names.Size) qsort(w.names.Data, (size_t)w.names.Size, sizeof w.names[0], by_name);
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < w.names.Size; i++) {
            char p[JSRF_PATH_MAX];
            if ((w.names[i][0] == 'f') != (pass == 0)) continue;
            js_path(p, sizeof p, dir, w.names[i] + 1);
            if (pass == 0) queue(p);
            else if (depth < FOLDER_DEPTH) queue_folder(p, depth + 1);
        }
    for (int i = 0; i < w.names.Size; i++) free(w.names[i]);
}

static void add_path(const char *path)
{
    g.ran = 0;
    if (js_is_dir(path)) queue_folder(path, 0);
    else queue(path);
}

static void add_picked(void *ctx, const char *path)
{
    (void)ctx;
    add_path(path);
}

/* One file of those waiting read into the folder: a frame each, so the
 * window keeps drawing while a folder of big photos is read. */
static void read_one(void)
{
    if (!g.pending.Size) {
        g.pending_total = 0;
        return;
    }
    char *path = g.pending[0];
    g.pending.erase(g.pending.Data);
    pic *p = (pic *)calloc(1, sizeof *p);
    char why[400];
    if (!p) {
        free(path);
        return;
    }
    snprintf(p->path, sizeof p->path, "%s", path);
    snprintf(p->name, sizeof p->name, "%s", base_name(path));
    free(path);
    p->size_auto = 1;
    p->size = GRAFFITI_S;
    p->mode = GRAFFITI_FIT_MARGINS;
    p->slot = -1;
    if (read_work(p, why, sizeof why) != 0) {
        g.unread.appendf("%s: %s\n", p->name, why);
        g.nunread++;
        free(p);
        return;
    }
    p->white = g.white_default && p->opaque && edge_white(p->work, p->ww, p->wh) >= 0.5;
    g.pics.push_back(p);
    refit(p);
}

/* ---- slots -------------------------------------------------------------------------------- */

/* Which slots have a design, looked at again every few seconds, as the game
 * may have saved one since. */
static void look_at_slots(tools_ctx *t, int now)
{
    double at = ImGui::GetTime();
    if (!now && strcmp(g.taken_title, t->title) == 0 && at - g.taken_at < 3.0) return;
    graffiti_slots_taken(t->title, g.taken);
    g.taken_at = at;
    snprintf(g.taken_title, sizeof g.taken_title, "%s", t->title);
}

/* The folder's order: by size, then as added. */
static int order(int *out)
{
    int n = 0;
    for (int s = 0; s < GRAFFITI_SIZES; s++)
        for (int i = 0; i < g.pics.Size; i++)
            if (g.pics[i]->size == s) out[n++] = i;
    return n;
}

/* Each design not yet imported given a slot: the one chosen by hand while
 * it's free, else the first free one in the folder's order. */
static void give_slots(void)
{
    uint8_t used[GRAFFITI_SLOTS];
    int idx[MAX_PICS], n = order(idx);
    memcpy(used, g.taken, sizeof used);
    for (int k = 0; k < n; k++) {
        pic *p = g.pics[idx[k]];
        if (p->state == PIC_WROTE) continue;
        if (p->slot_hand && p->slot >= 0 && p->slot < SLOTS_USED && !used[p->slot]) used[p->slot] = 2;
        else p->slot_hand = 0;
    }
    for (int k = 0; k < n; k++) {
        pic *p = g.pics[idx[k]];
        if (p->state == PIC_WROTE || p->slot_hand) continue;
        p->slot = -1;
        for (int s = 0; s < SLOTS_USED; s++)
            if (!used[s]) {
                p->slot = s;
                used[s] = 2;
                break;
            }
    }
}

static int free_slots(void)
{
    int n = 0;
    for (int s = 0; s < SLOTS_USED; s++) n += !g.taken[s];
    return n;
}

/* A slot as the player sees it, the garage's page, row and column from 1:
 * "page 1, row 2, column 5", or with a capital for a line of its own. */
static void place_words(int slot, int capital, char *out, size_t n)
{
    int page, row, column;
    graffiti_slot_place((unsigned)slot, &page, &row, &column);
    snprintf(out, n, "%cage %d, row %d, column %d", capital ? 'P' : 'p', page + 1, row + 1, column + 1);
}

static void place_text(const pic *p, char *out, size_t n)
{
    if (p->slot < 0) snprintf(out, n, "No free place");
    else place_words(p->slot, 1, out, n);
}

/* ---- import ------------------------------------------------------------------------------- */

static int to_write(void)
{
    int n = 0;
    for (int i = 0; i < g.pics.Size; i++) n += g.pics[i]->state != PIC_WROTE && g.pics[i]->slot >= 0;
    return n;
}

static void warning_text(tools_ctx *t)
{
    int n = to_write(), idx[MAX_PICS], m = order(idx), shown = 0;
    snprintf(g.warning_id, sizeof g.warning_id, "Import %d design%s?###graffiti-import", n, n == 1 ? "" : "s");
    g.warning.clear();
    g.warning.appendf("%s go%s into JSRF-PC's saves, each in the garage's own designs:\n\n", n == 1 ? "It" : "They",
                      n == 1 ? "es" : "");
    for (int k = 0; k < m; k++) {
        const pic *p = g.pics[idx[k]];
        char where[100];
        if (p->state == PIC_WROTE || p->slot < 0) continue;
        if (shown++ == 12) {
            g.warning.appendf("  and %d more\n", n - 12);
            break;
        }
        place_words(p->slot, 0, where, sizeof where);
        g.warning.appendf("  %s, %s: %s\n", p->name, graffiti_size_name(p->size), where);
    }
    g.warning.appendf("\nEach goes into a free place, so none replaces a design. First, every save is copied into "
                      "a dated backup in the %s folder.\n\nClose JSRF-PC before going on.", base_name(t->backups));
}

static void import(tools_ctx *t)
{
    char why[600], made[JSRF_PATH_MAX] = "";
    uint8_t *icon[GRAFFITI_SIZES] = { 0 };
    size_t icon_n[GRAFFITI_SIZES] = { 0 };
    int wrote = 0, failed = 0, idx[MAX_PICS], n = order(idx);

    g.ran = 1;
    g.backup[0] = 0;
    int r = jsrf_backup(t->title, t->backups, made, sizeof made, why, sizeof why);
    if (r == JSRF_FAILED) {
        g.ran = 0;
        say(AT_IMPORT, TOOLS_ERROR, "Nothing was written, because the backup couldn't be made. %s", why);
        return;
    }
    if (r == JSRF_WROTE) snprintf(g.backup, sizeof g.backup, "%s", made);
    look_at_slots(t, 1);
    give_slots();
    /* The icons the game's own designs carry, when its files are a folder. */
    if (t->game_files[0] && js_is_dir(t->game_files))
        for (int s = 0; s < GRAFFITI_SIZES; s++) graffiti_icon_read(t->game_files, s, &icon[s], &icon_n[s]);
    for (int k = 0; k < n; k++) {
        pic *p = g.pics[idx[k]];
        jsrf_save s;
        char where[100];
        if (p->state == PIC_WROTE) continue;
        if (p->slot < 0) {
            p->state = PIC_FAILED;
            snprintf(p->why, sizeof p->why, "Not written: there was no free place for it.");
            failed++;
            continue;
        }
        if (p->dirty || !p->design) refit(p);
        place_words(p->slot, 0, where, sizeof where);
        if (!p->design ||
            graffiti_save_make(&s, p->design, p->size, (unsigned)p->slot, icon[p->size], icon_n[p->size],
                               jsrf_signature_key) != 0) {
            p->state = PIC_FAILED;
            snprintf(p->why, sizeof p->why, "Not written: its save couldn't be made.");
            failed++;
            continue;
        }
        why[0] = 0;
        r = jsrf_save_write(t->title, &s, 0, why, sizeof why);
        jsrf_save_free(&s);
        if (r == JSRF_WROTE) {
            p->state = PIC_WROTE;
            snprintf(p->why, sizeof p->why, "Imported into %s.", where);
            wrote++;
        } else {
            p->state = PIC_FAILED;
            if (r == JSRF_EXISTS) snprintf(p->why, sizeof p->why, "Not written: %s holds a design now.", where);
            else snprintf(p->why, sizeof p->why, "Not written: %s", why);
            failed++;
        }
    }
    for (int s = 0; s < GRAFFITI_SIZES; s++) free(icon[s]);
    look_at_slots(t, 1);
    if (!failed)
        say(AT_IMPORT, TOOLS_OK, "Imported %d design%s. %s in the garage, under your own designs.", wrote,
            wrote == 1 ? "" : "s", wrote == 1 ? "It's" : "They're");
    else if (wrote)
        say(AT_IMPORT, TOOLS_WARN, "Imported %d design%s. %d %s written; choose %s to see why.", wrote,
            wrote == 1 ? "" : "s", failed, failed == 1 ? "wasn't" : "weren't", failed == 1 ? "it" : "one");
    else
        say(AT_IMPORT, TOOLS_ERROR, "Nothing was imported; choose a design to see why.");
}

/* ---- drawing ------------------------------------------------------------------------------ */

static void checker_make(void)
{
    static const uint8_t c[16] = { 150, 150, 158, 255, 96, 96, 104, 255, 96, 96, 104, 255, 150, 150, 158, 255 };
    if (!g.checker) tex_put(&g.checker, c, 2, 2, 1, 1);
}

/* Our textures drawn with their own filtering and wrapping (the checker
 * repeated, a design's pixels square), then the backend's own again. */
static void own_sampling(ImDrawList *dl, bool on)
{
    ImGuiPlatformIO &pio = ImGui::GetPlatformIO();
    ImDrawCallback cb = on ? pio.DrawCallback_SetSamplerFromTex : pio.DrawCallback_ResetRenderState;
    if (cb) dl->AddCallback(cb, NULL);
}

/* The checker, as clear shows, over (a, b): squares half a line high. */
static void draw_checker(ImDrawList *dl, ImVec2 a, ImVec2 b, float em)
{
    float sq = em * 0.5f;
    if (g.checker) dl->AddImage(tex_ref(g.checker), a, b, ImVec2(0, 0), ImVec2((b.x - a.x) / sq / 2, (b.y - a.y) / sq / 2));
}

/* A design at (a, b), over the checker. */
static void draw_design(ImDrawList *dl, const pic *p, ImVec2 a, ImVec2 b, float em)
{
    own_sampling(dl, true);
    draw_checker(dl, a, b, em);
    if (p->tex) dl->AddImage(tex_ref(p->tex), a, b);
    own_sampling(dl, false);
}

/* text cut to fit width, "..." at its end when it's cut, at a character's
 * start (UTF-8). */
static void text_fit(const char *text, float width)
{
    if (ImGui::CalcTextSize(text).x <= width) {
        ImGui::TextUnformatted(text);
        return;
    }
    char buf[300];
    size_t n = strlen(text);
    if (n > sizeof buf - 4) n = sizeof buf - 4;
    while (n > 0) {
        n--;
        while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) n--;
        memcpy(buf, text, n);
        memcpy(buf + n, "...", 4);
        if (ImGui::CalcTextSize(buf).x <= width) break;
    }
    ImGui::TextUnformatted(buf);
}

static const char *const MODE_NAMES[] = { "Whole, clear margins", "Cropped", "Stretched" };

/* The folder of designs, grouped by size. */
static void draw_folder(float em)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    float tile_h = em * 5.5f, gap = ImGui::GetStyle().ItemSpacing.x;
    for (int s = 0; s < GRAFFITI_SIZES; s++) {
        int count = 0;
        for (int i = 0; i < g.pics.Size; i++) count += g.pics[i]->size == s;
        if (!count) continue;
        ImGui::SeparatorText("");
        ImGui::SameLine(0, 0);
        ImGui::Text("%s, %d x %d: %d design%s", graffiti_size_name(s), graffiti_width(s), graffiti_height(s), count,
                    count == 1 ? "" : "s");
        float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        float aspect = (float)graffiti_width(s) / graffiti_height(s);
        float tw = tile_h * aspect, th = tile_h;
        float room = ImGui::GetContentRegionAvail().x;
        if (tw > room) {   /* XL in a narrow window */
            tw = room > em * 4 ? room : em * 4;
            th = tw / aspect;
        }
        float words = ImGui::CalcTextSize("Page 10, row 4, column 7").x;
        float cell = tw > words ? tw : words;   /* room for the words under a small one */
        bool first = true;
        for (int i = 0; i < g.pics.Size; i++) {
            pic *p = g.pics[i];
            char label[32], where[100];
            if (p->size != s) continue;
            if (!first) {
                if (ImGui::GetItemRectMax().x + gap + cell <= right) ImGui::SameLine();
            }
            first = false;
            ImGui::BeginGroup();
            ImVec2 at = ImGui::GetCursorScreenPos();
            snprintf(label, sizeof label, "##tile%d", i);
            if (ImGui::InvisibleButton(label, ImVec2(cell, th))) choose(i);
            ImVec2 a(at.x + (cell - tw) * 0.5f, at.y), b(a.x + tw, a.y + th);
            draw_design(dl, p, a, b, em);
            ImU32 edge = ImGui::GetColorU32(is_chosen(p) ? ImGuiCol_ButtonActive
                                            : ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered : ImGuiCol_Border);
            dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), edge, 0, 0, is_chosen(p) ? 3.0f : 1.0f);
            text_fit(p->name, cell);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (p->state == PIC_WROTE) text_fit("Imported", cell);
            else if (p->state == PIC_FAILED) text_fit("Not written", cell);
            else {
                place_text(p, where, sizeof where);
                text_fit(where, cell);
            }
            ImGui::PopStyleColor();
            ImGui::EndGroup();
            place_text(p, where, sizeof where);
            ImGui::SetItemTooltip("%s\n%s, %s\n%s%s%s", p->path, graffiti_size_name(p->size), MODE_NAMES[p->mode], where,
                                  p->why[0] ? "\n" : "", p->why);
        }
    }
}

/* The chosen picture, whole, with its crop box to move (drag) and size
 * (the wheel, or the slider). */
static void draw_crop(pic *p, float width, float em)
{
    if (need_work(p) != 0) {
        ImGui::TextWrapped("%s", p->why);
        return;
    }
    if (g.show_of != p || !g.show_tex) {
        int side = p->ww > p->wh ? p->ww : p->wh, sw = p->ww, sh = p->wh;
        if (side > SHOW_SIDE) {
            sw = (int)((double)p->ww * SHOW_SIDE / side + 0.5);
            sh = (int)((double)p->wh * SHOW_SIDE / side + 0.5);
            if (sw < 1) sw = 1;
            if (sh < 1) sh = 1;
        }
        uint8_t *show = (uint8_t *)malloc((size_t)sw * sh * 4);
        if (show && graffiti_scale(p->work, p->ww, p->wh, show, sw, sh) == 0) {
            tex_put(&g.show_tex, show, sw, sh, 0, 0);
            g.show_of = p;
            g.show_w = sw;
            g.show_h = sh;
        }
        free(show);
    }
    float maxh = em * 14, k = width / p->ww;
    if (p->wh * k > maxh) k = maxh / p->wh;
    ImVec2 size(p->ww * k, p->wh * k);
    ImVec2 at = ImGui::GetCursorScreenPos();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImGui::InvisibleButton("##crop", size);
    ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);   /* the wheel sizes the box here, not scrolls */
    bool dragging = ImGui::IsItemActive(), hovered = ImGui::IsItemHovered();
    own_sampling(dl, true);
    draw_checker(dl, at, ImVec2(at.x + size.x, at.y + size.y), em);
    if (g.show_tex && g.show_of == p) dl->AddImage(tex_ref(g.show_tex), at, ImVec2(at.x + size.x, at.y + size.y));
    own_sampling(dl, false);
    graffiti_box c = p->crop;
    if (dragging) {
        ImVec2 d = ImGui::GetIO().MouseDelta;
        if (d.x != 0 || d.y != 0) {
            c.x += d.x / k;
            c.y += d.y / k;
        }
    }
    float wheel = hovered ? ImGui::GetIO().MouseWheel : 0;
    if (wheel != 0) {
        double f = wheel > 0 ? 0.9 : 1 / 0.9, cx = c.x + c.w / 2, cy = c.y + c.h / 2;
        c.w *= f;
        c.h *= f;
        c.x = cx - c.w / 2;
        c.y = cy - c.h / 2;
    }
    /* How big, of the biggest the shape can be in the picture. */
    graffiti_box most = graffiti_crop_default(p->ww, p->wh, p->size);
    float share = (float)(c.w / most.w * 100);
    ImGuiStyle &st = ImGui::GetStyle();
    float centre_w = ImGui::CalcTextSize("Centre").x + st.FramePadding.x * 2;
    ImGui::SetNextItemWidth(size.x - centre_w - st.ItemSpacing.x > em * 5 ? size.x - centre_w - st.ItemSpacing.x : em * 5);
    if (ImGui::SliderFloat("##cropsize", &share, 5, 100, "Box %.0f%%", ImGuiSliderFlags_AlwaysClamp)) {
        double cx = c.x + c.w / 2, cy = c.y + c.h / 2;
        c.w = most.w * share / 100;
        c.h = most.h * share / 100;
        c.x = cx - c.w / 2;
        c.y = cy - c.h / 2;
    }
    ImGui::SameLine();
    if (ImGui::Button("Centre")) {
        c.x = (p->ww - c.w) / 2;
        c.y = (p->wh - c.h) / 2;
    }
    c = graffiti_crop_clamp(c, p->ww, p->wh, p->size);
    if (c.x != p->crop.x || c.y != p->crop.y || c.w != p->crop.w || c.h != p->crop.h) {
        p->crop = c;
        p->dirty = 1;
    }
    /* The box, and what's outside it dimmed. */
    ImVec2 a(at.x + (float)(p->crop.x * k), at.y + (float)(p->crop.y * k));
    ImVec2 b(a.x + (float)(p->crop.w * k), a.y + (float)(p->crop.h * k));
    ImU32 dim = IM_COL32(0, 0, 0, 140);
    dl->AddRectFilled(at, ImVec2(at.x + size.x, a.y), dim);
    dl->AddRectFilled(ImVec2(at.x, b.y), ImVec2(at.x + size.x, at.y + size.y), dim);
    dl->AddRectFilled(ImVec2(at.x, a.y), ImVec2(a.x, b.y), dim);
    dl->AddRectFilled(ImVec2(b.x, a.y), ImVec2(at.x + size.x, b.y), dim);
    dl->AddRect(a, b, IM_COL32(255, 255, 255, 230), 0, 0, 2.0f);
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("Drag the box to move it; the wheel sizes it.");
    ImGui::PopStyleColor();
}

static void size_label(int s, int is_auto, int auto_size, char *out, size_t n)
{
    if (is_auto) snprintf(out, n, "Auto (%s)", graffiti_size_name(auto_size));
    else snprintf(out, n, "%s, %d x %d", graffiti_size_name(s), graffiti_width(s), graffiti_height(s));
}

/* What can be changed of the chosen picture. */
static void draw_details(float width, float em)
{
    pic *p = g.pics[g.selected];
    char label[64], where[100];
    int auto_size = graffiti_auto_size(p->src_w, p->src_h);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    ImGui::TextUnformatted(p->name);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::Text("%s, %d x %d", graffiti_image_kind_name(p->kind), p->src_w, p->src_h);
    ImGui::PopStyleColor();
    if (p->why[0]) ImGui::TextWrapped("%s", p->why);
    ImGui::PopTextWrapPos();
    ImGui::BeginDisabled(p->state == PIC_WROTE);

    /* The design, bigger. */
    {
        float dw = (float)graffiti_width(p->size), dh = (float)graffiti_height(p->size), k = width / dw;
        if (dh * k > em * 8) k = em * 8 / dh;
        ImVec2 at = ImGui::GetCursorScreenPos(), b(at.x + dw * k, at.y + dh * k);
        ImGui::Dummy(ImVec2(dw * k, dh * k));
        draw_design(ImGui::GetWindowDrawList(), p, at, b, em);
        ImGui::GetWindowDrawList()->AddRect(ImVec2(at.x - 1, at.y - 1), ImVec2(b.x + 1, b.y + 1),
                                            ImGui::GetColorU32(ImGuiCol_Border));
    }

    size_label(p->size, p->size_auto, auto_size, label, sizeof label);
    ImGui::SetNextItemWidth(width * 0.6f);
    if (ImGui::BeginCombo("Size", label)) {
        char l[64];
        size_label(0, 1, auto_size, l, sizeof l);
        if (ImGui::Selectable(l, p->size_auto != 0)) {
            p->size_auto = 1;
            p->dirty = 1;
        }
        for (int s = 0; s < GRAFFITI_SIZES; s++) {
            size_label(s, 0, 0, l, sizeof l);
            if (ImGui::Selectable(l, !p->size_auto && p->size == s)) {
                if (p->size != s) p->has_crop = 0;
                p->size_auto = 0;
                p->size = s;
                p->dirty = 1;
            }
        }
        ImGui::EndCombo();
    }
    /* The three ways, on one line when there's room, as the shell's buttons. */
    for (int m = 0; m < 3; m++) {
        ImGuiStyle &st = ImGui::GetStyle();
        float w = ImGui::GetFrameHeight() + st.ItemInnerSpacing.x + ImGui::CalcTextSize(MODE_NAMES[m]).x;
        if (m && ImGui::GetItemRectMax().x + st.ItemSpacing.x + w <= ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x)
            ImGui::SameLine();
        if (ImGui::RadioButton(MODE_NAMES[m], p->mode == m) && p->mode != m) {
            p->mode = m;
            p->dirty = 1;
        }
    }
    if (p->mode == GRAFFITI_FIT_CROP) draw_crop(p, width, em);

    bool white = p->white != 0, all = p->white_all != 0;
    if (ImGui::Checkbox("Make the white background clear", &white)) {
        p->white = white;
        p->dirty = 1;
    }
    if (p->white) {
        ImGui::Indent();
        if (ImGui::Checkbox("White inside it too", &all)) {
            p->white_all = all;
            p->dirty = 1;
        }
        ImGui::Unindent();
    }

    /* Its place in the garage: the first free one, or one given by hand. */
    place_text(p, where, sizeof where);
    ImGui::Text("In the garage: %s", where);
    tools_help_marker(p->slot_hand ? "Given by hand. Reset puts it back in the first free place."
                                   : "The first free place, in the designs' order. Give it another below.");
    if (p->slot != g.slot_shown) {   /* the place moved (another design's size changed, say) */
        int page = 0, row = 0, column = 0;
        if (p->slot >= 0) graffiti_slot_place((unsigned)p->slot, &page, &row, &column);
        g.place_edit[0] = page + 1;
        g.place_edit[1] = row + 1;
        g.place_edit[2] = column + 1;
        g.slot_shown = p->slot;
    }
    static const char *const PLACE_NAMES[3] = { "Page", "Row", "Column" };
    static const int PLACE_MOST[3] = { GRAFFITI_SLOTS / 28, 4, 7 };
    for (int k = 0; k < 3; k++) {
        char id[16];
        snprintf(id, sizeof id, "##place%d", k);
        if (k) ImGui::SameLine(0, em);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(PLACE_NAMES[k]);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(em * 6);
        if (ImGui::InputInt(id, &g.place_edit[k], 1, 1)) {
            if (g.place_edit[k] < 1) g.place_edit[k] = 1;
            if (g.place_edit[k] > PLACE_MOST[k]) g.place_edit[k] = PLACE_MOST[k];
        }
    }
    if (tools_flow_button("Move", true)) {
        int s = ((g.place_edit[0] - 1) * 4 + g.place_edit[1] - 1) * 7 + g.place_edit[2] - 1;
        char asked[100];
        place_words(s, 1, asked, sizeof asked);
        unsay(AT_DESIGN);
        if (s >= SLOTS_USED) {
            place_words(SLOTS_USED - 1, 0, where, sizeof where);
            say(AT_DESIGN, TOOLS_WARN, "Only the places up to %s are used for now.", where);
        } else if (g.taken[s]) {
            say(AT_DESIGN, TOOLS_WARN, "%s holds a design already.", asked);
        } else {
            for (int i = 0; i < g.pics.Size; i++)
                if (g.pics[i] != p && g.pics[i]->state != PIC_WROTE && g.pics[i]->slot == s) g.pics[i]->slot_hand = 0;
            p->slot = s;
            p->slot_hand = 1;
        }
    }
    ImGui::BeginDisabled(!p->slot_hand);
    if (tools_flow_button("Reset", false)) {
        p->slot_hand = 0;
        unsay(AT_DESIGN);
    }
    ImGui::EndDisabled();
    show_note(AT_DESIGN);
    ImGui::EndDisabled();
    ImGui::Dummy(ImVec2(0, em * 0.25f));
    if (ImGui::Button("Remove")) remove_pic(g.selected);
    ImGui::SetItemTooltip("Take this design out of the list. Its picture file stays as it is.");
}

static void draw(tools_ctx *t)
{
    float em = ImGui::GetFontSize();
    if (!g_started) {
        g_started = true;
        g.selected = -1;
        g.white_default = true;
    }
    checker_make();
    read_one();
    if (g.pending.Size) t->busy = 1;   /* one picture a frame, until all are read */
    look_at_slots(t, 0);                /* every 3 s: the shell draws at least once a second */
    for (int i = 0; i < g.pics.Size; i++)
        if (g.pics[i]->dirty) refit(g.pics[i]);
    give_slots();

    /* Pictures: adding them. */
    tools_heading("Pictures");
    tools_hint("Drop pictures or folders of them on this window, or add them here.");
    tools_help_marker("JPEG, PNG, WebP, AVIF, SVG and TIFF are read, each by what it holds, not by its name. A folder "
                      "is looked through up to four folders deep. An SVG draws its shapes, paths and gradients, but "
                      "not text, reused symbols or style sheets.");
    ImGui::Dummy(ImVec2(0, em * 0.25f));
    if (tools_flow_button("Add pictures...", true)) {
        static const char *const pats[] = { "*.jpg", "*.jpeg", "*.jpe", "*.jfif", "*.png", "*.webp", "*.avif",
                                            "*.svg", "*.tif", "*.tiff", NULL };
        unsay(AT_PICTURES);
        int r = jsrf_pick_files("Pick pictures to make into graffiti", "Pictures", pats, 1, add_picked, NULL);
        if (r == JSRF_NO_DIALOG)
            say(AT_PICTURES, TOOLS_WARN, "There's no file dialog here (zenity or kdialog). Drop the pictures on this "
                                         "window instead.");
    }
    if (tools_flow_button("Add folder...", false)) {
        char picked[JSRF_PATH_MAX] = "";
        unsay(AT_PICTURES);
        int r = jsrf_pick_folder("Pick a folder of pictures to make into graffiti", picked, sizeof picked);
        if (r == JSRF_PICKED) add_path(picked);
        if (r == JSRF_NO_DIALOG)
            say(AT_PICTURES, TOOLS_WARN, "There's no folder dialog here (zenity or kdialog). Drop the folder on this "
                                         "window instead.");
    }
    ImGui::BeginDisabled(!g.pics.Size && !g.pending.Size);
    if (tools_flow_button("Remove all", false)) clear_all();
    ImGui::EndDisabled();
    ImGui::Checkbox("Make white backgrounds clear", &g.white_default);
    ImGui::SetItemTooltip("For pictures added from now on with no clear pixels and a mostly white edge: the white "
                          "joined to the edge becomes clear. Each design can change it.");
    if (g.pending.Size) {
        int done = g.pending_total - g.pending.Size;
        ImGui::TextDisabled("Reading %d of %d: %s", done + 1, g.pending_total, base_name(g.pending[0]));
    }
    show_note(AT_PICTURES);
    if (g.nunread) {
        char head[80];
        snprintf(head, sizeof head, "%d file%s couldn't be read###unread", g.nunread, g.nunread == 1 ? "" : "s");
        if (ImGui::TreeNode(head)) {
            ImGui::TextWrapped("%s", g.unread.c_str());
            if (ImGui::SmallButton("Clear")) {
                g.unread.clear();
                g.nunread = 0;
            }
            ImGui::TreePop();
        }
    }

    /* Designs: the folder of them, and beside it the chosen one; in a narrow
     * window, one or the other. They take the room the Import section left
     * last frame. */
    tools_heading("Designs");
    if (!g.pics.Size) {
        ImGui::TextDisabled(g.pending.Size ? "Reading the pictures." : "No designs yet. Add pictures to make some.");
    } else {
        tools_hint("Choose a design to change its size, its fit or its place in the garage.");
        ImVec2 room = ImGui::GetContentRegionAvail();
        float h = room.y - g.import_h;
        if (h < em * 6) h = em * 6;
        bool side = room.x >= em * 44, chosen = g.selected >= 0 && g.selected < g.pics.Size;
        float detail_w = room.x * 0.4f > em * 20 ? room.x * 0.4f : em * 20;
        if (side || !chosen) {
            ImGui::BeginChild("folder",
                              ImVec2(side && chosen ? room.x - detail_w - ImGui::GetStyle().ItemSpacing.x : room.x, h),
                              ImGuiChildFlags_Borders);
            draw_folder(em);
            ImGui::EndChild();
        }
        if (chosen) {
            if (side) ImGui::SameLine();
            ImGui::BeginChild("details", ImVec2(side ? detail_w : room.x, h), ImGuiChildFlags_Borders);
            bool back = !side && ImGui::Button("Back");
            if (back) choose(-1);
            else draw_details(ImGui::GetContentRegionAvail().x, em);
            ImGui::EndChild();
        }
    }

    /* Import: the only thing that writes, after the warning and a backup. */
    float import_y = ImGui::GetCursorPosY();
    tools_heading("Import");
    tools_hint("Adds the designs to JSRF-PC's saves, after a backup.");
    tools_help_marker("Each design is saved as the game saves one, in a save of its own, and never over a design. "
                      "The garage's first 228 places are used (pages 1 to 8, and page 9's first four) until a "
                      "design past them has been checked in the game. Each size's icon is copied from your own "
                      "game files when they are a folder; with a disc image, the saves have none.");
    ImGui::Dummy(ImVec2(0, em * 0.25f));
    int n = to_write(), wait = 0, wrote = 0;
    for (int i = 0; i < g.pics.Size; i++) {
        wait += g.pics[i]->state != PIC_WROTE && g.pics[i]->slot < 0;
        wrote += g.pics[i]->state == PIC_WROTE;
    }
    ImGui::BeginDisabled(!n || g.pending.Size > 0);
    if (tools_flow_button("Import", true)) {
        unsay(AT_IMPORT);
        warning_text(t);
        ImGui::OpenPopup(g.warning_id);
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!wrote);
    if (tools_flow_button("Clear imported", false)) {
        for (int i = g.pics.Size - 1; i >= 0; i--)
            if (g.pics[i]->state == PIC_WROTE) remove_pic(i);
        g.ran = 0;
        g.backup[0] = 0;
        unsay(AT_IMPORT);
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("%d to import. The garage has %d free place%s of %d.%s", n, free_slots(),
                        free_slots() == 1 ? "" : "s", SLOTS_USED, wait ? " The rest wait for one." : "");
    show_note(AT_IMPORT);
    if (g.ran && g.backup[0]) tools_path("Backup:", g.backup);
    else if (g.ran) ImGui::TextDisabled("There were no saves yet, so nothing needed a backup.");
    if (g.warning_id[0] && tools_warning(g.warning_id, g.warning.c_str(), "Import")) import(t);
    g.import_h = ImGui::GetCursorPosY() - import_y;
}

static void dropped(tools_ctx *t, const char *path)
{
    (void)t;
    add_path(path);
}

extern const tools_page tools_graffiti_page = { "Graffiti", draw, dropped };
