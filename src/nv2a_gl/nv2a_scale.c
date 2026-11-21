/*
 * The internal resolution: its choices, their names and keys, the lines each
 * gives and a surface's size at them, and where its frame goes in the window
 * (nv2a_scale.h).
 */
#include "nv2a_scale.h"

#include <stdlib.h>
#include <string.h>

const char *const nv2a_scale_names[NV2A_SCALE_CHOICES] = {
    "Default", "720p", "900p", "1080p", "1440p", "1600p", "2160p", "Match screen" };

static const char *const s_keys[NV2A_SCALE_CHOICES] = {
    "default", "720", "900", "1080", "1440", "1600", "2160", "match" };

/* The lines each choice but Match screen gives. */
static const uint32_t s_lines[NV2A_SCALE_CHOICES] = {
    NV2A_SCALE_FRAME_H, 720, 900, 1080, 1440, 1600, NV2A_SCALE_LINES_MAX, 0 };

const char *nv2a_scale_key(int choice)
{
    return choice >= 0 && choice < NV2A_SCALE_CHOICES ? s_keys[choice] : NULL;
}

int nv2a_scale_parse(const char *v)
{
    size_t n;
    int i;
    if (!v) return -1;
    n = strcspn(v, "\r\n");
    for (i = 0; i < NV2A_SCALE_CHOICES; i++)
        if (strlen(s_keys[i]) == n && !strncmp(v, s_keys[i], n)) return i;
    return -1;
}

int nv2a_scale_of_old(const char *v)
{
    char *end;
    long n;
    uint32_t lines;
    int i;
    if (!v) return -1;
    n = strtol(v, &end, 10);
    if (end == v || (*end && *end != '\r' && *end != '\n') || n < 1 || n > NV2A_SCALE_OLD_MAX)
        return -1;
    lines = (uint32_t)n * NV2A_SCALE_FRAME_H;
    for (i = NV2A_SCALE_XBOX; i < NV2A_SCALE_MATCH; i++)
        if (s_lines[i] >= lines) return i;
    return NV2A_SCALE_2160P;
}

uint32_t nv2a_scale_lines(int choice, int disp_h)
{
    if (choice < 0 || choice >= NV2A_SCALE_CHOICES) return 0;
    if (choice != NV2A_SCALE_MATCH) return s_lines[choice];
    if (disp_h < 1) return NV2A_SCALE_LINES_UNKNOWN;
    if (disp_h < NV2A_SCALE_LINES_MIN) return NV2A_SCALE_LINES_MIN;
    if (disp_h > NV2A_SCALE_LINES_MAX) return NV2A_SCALE_LINES_MAX;
    return (uint32_t)disp_h;
}

uint32_t nv2a_scale_px(uint32_t v, uint32_t lines)
{
    uint64_t px = ((uint64_t)v * lines + NV2A_SCALE_FRAME_H / 2) / NV2A_SCALE_FRAME_H;
    return px < 1 ? 1 : (uint32_t)px;
}

void nv2a_scale_fit(int fw, int fh, int ww, int wh, int *dw, int *dh)
{
    *dw = ww; *dh = wh;
    if ((long)ww * fh > (long)wh * fw) *dw = (int)((long)wh * fw / fh);
    else                               *dh = (int)((long)ww * fh / fw);
}

int nv2a_scale_halvings(int from, int to)
{
    int n = 0;
    if (to < 1) return 0;
    while (from > 2 * to) { from /= 2; n++; }
    return n;
}
