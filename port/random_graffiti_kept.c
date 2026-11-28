/* random_graffiti_kept.c -- random graffiti's kept tags, and their file in
 * the game save's folder (random_graffiti_kept.h). */
#include "random_graffiti_kept.h"
#include "random_graffiti.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#  include <windows.h>
#endif

#define KEPT_HEAD     "jsrf-pc kept graffiti 2"
#define KEPT_PATH     1024

static RG_KEPT s_tag[RG_KEPT_STAGES][RG_KEPT_SPOTS];     /* the play's */
static RG_KEPT s_slot[RG_KEPT_SLOTS][RG_KEPT_STAGES][RG_KEPT_SPOTS];    /* the file's, as last read */
static int s_ready;             /* the play's table is set up: every key empty */
static char s_path[KEPT_PATH];

static void empty(RG_KEPT *tag, size_t n)
{
    size_t k;
    for (k = 0; k < n; k++) {
        tag[k].size = RG_KEPT_NONE;
        tag[k].parts = 0;
        tag[k].design = -1;
    }
}

static void ready(void)
{
    if (s_ready) return;
    empty(&s_tag[0][0], RG_KEPT_STAGES * RG_KEPT_SPOTS);
    s_ready = 1;
}

/* The file's calls, its path UTF-8 on every system: Windows' own calls for
 * wide names there, as the shader cache's (src/nv2a_gl/nv2a_shader_cache.c),
 * and its rename() replaces no file. */
#if defined(_WIN32)
static int wide(const char *path, wchar_t *w)
{
    return MultiByteToWideChar(CP_UTF8, 0, path, -1, w, KEPT_PATH + 8) > 0;
}
static FILE *open_file(const char *path, const wchar_t *mode)
{
    wchar_t w[KEPT_PATH + 8];
    return wide(path, w) ? _wfopen(w, mode) : NULL;
}
#  define OPEN_READ  L"rb"
#  define OPEN_WRITE L"wb"
static void remove_file(const char *path)
{
    wchar_t w[KEPT_PATH + 8];
    if (wide(path, w)) _wremove(w);
}
static int replace_file(const char *from, const char *to)
{
    wchar_t wf[KEPT_PATH + 8], wt[KEPT_PATH + 8];
    return wide(from, wf) && wide(to, wt) && MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
}
#else
static FILE *open_file(const char *path, const char *mode) { return fopen(path, mode); }
#  define OPEN_READ  "rb"
#  define OPEN_WRITE "wb"               /* a line ends in LF on every system */
static void remove_file(const char *path) { remove(path); }
static int replace_file(const char *from, const char *to) { return rename(from, to); }
#endif

/* Whether a design is one the table keeps: one of the game's of this size,
 * or one of the player's own. */
static int keeps(uint32_t size, int design)
{
    if (size >= RG_SIZES) return 0;
    if (design >= (int)RG_OWN) return design < (int)(RG_OWN + RG_OWN_SLOTS);
    return design >= (int)(size * RG_DESIGNS) && design < (int)((size + 1u) * RG_DESIGNS);
}

void rg_kept_file(const char *path)
{
    snprintf(s_path, sizeof s_path, "%s", path ? path : "");
}

/* The file read into the slots' tables, each empty first: how many tags it
 * had, or -1 when there is no file or it isn't this one. */
static int read_file(int *bad)
{
    char line[160];
    FILE *in;
    int read = 0, wrong = 0;

    empty(&s_slot[0][0][0], RG_KEPT_SLOTS * RG_KEPT_STAGES * RG_KEPT_SPOTS);
    if (bad) *bad = 0;
    if (!s_path[0] || !(in = open_file(s_path, OPEN_READ)))
        return -1;
    if (!fgets(line, sizeof line, in) || strncmp(line, KEPT_HEAD, strlen(KEPT_HEAD)) != 0) {
        fclose(in);
        return -1;
    }
    while (fgets(line, sizeof line, in)) {
        unsigned slot, stage, spot, parts, size, n;
        char kind, end;
        int design;
        RG_KEPT *tag;

        if (line[0] == '\n' || line[0] == '\r' || line[0] == '#')
            continue;
        if (sscanf(line, "%u %u %u %u %u %c%u %c", &slot, &stage, &spot, &parts, &size, &kind, &n, &end) != 7
            || (kind != 'g' && kind != 'o') || slot >= RG_KEPT_SLOTS || stage >= RG_KEPT_STAGES
            || spot >= RG_KEPT_SPOTS || parts == 0 || parts > RG_PARTS || n > 0xFFFFu) {
            wrong++;
            continue;
        }
        design = kind == 'o' ? (int)(RG_OWN + n) : (int)n;
        if (!keeps(size, design)) {
            wrong++;
            continue;
        }
        tag = &s_slot[slot][stage][spot];
        tag->size = (uint8_t)size;
        tag->parts = (uint8_t)parts;
        tag->design = (int16_t)design;
        read++;
    }
    fclose(in);
    if (bad) *bad = wrong;
    return read;
}

int rg_kept_load(uint32_t slot, int *bad)
{
    uint32_t stage, spot;
    int read = read_file(bad), n = 0;

    ready();
    empty(&s_tag[0][0], RG_KEPT_STAGES * RG_KEPT_SPOTS);
    if (read < 0 || slot >= RG_KEPT_SLOTS)
        return -1;
    memcpy(s_tag, s_slot[slot], sizeof s_tag);
    for (stage = 0; stage < RG_KEPT_STAGES; stage++)
        for (spot = 0; spot < RG_KEPT_SPOTS; spot++)
            n += s_tag[stage][spot].size != RG_KEPT_NONE;
    return n;
}

int rg_kept_save(uint32_t slot, int *bad)
{
    char tmp[KEPT_PATH + 8];
    uint32_t s, stage, spot;
    FILE *out;
    int ok;

    if (bad) *bad = 0;
    if (!s_path[0] || slot >= RG_KEPT_SLOTS)
        return -1;
    ready();
    read_file(bad);             /* the other slots' as the file has them (none where there is none yet) */
    memcpy(s_slot[slot], s_tag, sizeof s_tag);
    snprintf(tmp, sizeof tmp, "%s.tmp", s_path);
    if (!(out = open_file(tmp, OPEN_WRITE)))
        return -1;
    fprintf(out, "%s\n", KEPT_HEAD);
    for (s = 0; s < RG_KEPT_SLOTS; s++)
        for (stage = 0; stage < RG_KEPT_STAGES; stage++)
            for (spot = 0; spot < RG_KEPT_SPOTS; spot++) {
                const RG_KEPT *tag = &s_slot[s][stage][spot];
                if (tag->size == RG_KEPT_NONE) continue;
                fprintf(out, "%u %u %u %u %u %c%d\n", s, stage, spot, tag->parts, tag->size,
                        tag->design >= (int)RG_OWN ? 'o' : 'g',
                        tag->design >= (int)RG_OWN ? tag->design - (int)RG_OWN : tag->design);
            }
    ok = !ferror(out);
    ok &= fclose(out) == 0;
    if (!ok) {
        remove_file(tmp);
        return -1;
    }
    if (replace_file(tmp, s_path) != 0) {
        remove_file(tmp);
        return -1;
    }
    return 1;
}

const RG_KEPT *rg_kept_get(uint32_t stage, uint32_t spot)
{
    ready();
    if (stage >= RG_KEPT_STAGES || spot >= RG_KEPT_SPOTS || s_tag[stage][spot].size == RG_KEPT_NONE)
        return NULL;
    return &s_tag[stage][spot];
}

void rg_kept_set(uint32_t stage, uint32_t spot, uint32_t size, uint32_t parts, int design)
{
    RG_KEPT *tag;

    ready();
    if (stage >= RG_KEPT_STAGES || spot >= RG_KEPT_SPOTS || !keeps(size, design) || parts > 0xFFu)
        return;
    tag = &s_tag[stage][spot];
    tag->size = (uint8_t)size;
    tag->parts = (uint8_t)parts;
    tag->design = (int16_t)design;
}

void rg_kept_end(uint32_t stage, uint32_t spot)
{
    ready();
    if (stage >= RG_KEPT_STAGES || spot >= RG_KEPT_SPOTS)
        return;
    empty(&s_tag[stage][spot], 1);
}

void rg_kept_clear(void)
{
    ready();
    empty(&s_tag[0][0], RG_KEPT_STAGES * RG_KEPT_SPOTS);
}

uint32_t rg_kept_count(uint32_t stage)
{
    uint32_t s, spot, n = 0;

    ready();
    for (s = 0; s < RG_KEPT_STAGES; s++)
        if (stage >= RG_KEPT_STAGES || s == stage)
            for (spot = 0; spot < RG_KEPT_SPOTS; spot++)
                n += s_tag[s][spot].size != RG_KEPT_NONE;
    return n;
}
