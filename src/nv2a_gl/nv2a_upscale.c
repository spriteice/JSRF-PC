/*
 * Upscaling's rules, their census, the setting and the cap (nv2a_upscale.h).
 */
#include "nv2a_upscale.h"

#include <stdio.h>

#define UP_STR_(x) #x
#define UP_STR(x) UP_STR_(x)

const char *const nv2a_up_verdict_names[NV2A_UP_VERDICTS] = {
    "taken",
    "the draw is neither screen-space nor over the picture",
    "the draw isn't into the frame",
    "it has a chain of its own",
    "it is linear",
    ("it is larger than " UP_STR(NV2A_UP_SIDE_MAX)),
    "it is a bump source",
};

int nv2a_up_rule(const Nv2aUpBind *b)
{
    if (!b->screen && !b->over) return NV2A_UP_NOT_2D;
    if (!b->into_frame) return NV2A_UP_NOT_FRAME;
    if (b->levels > 1) return NV2A_UP_CHAIN;
    if (b->layout == NV2A_UP_LINEAR) return NV2A_UP_LINEAR_TEX;
    if (b->w > (uint32_t)NV2A_UP_SIDE_MAX || b->h > (uint32_t)NV2A_UP_SIDE_MAX) return NV2A_UP_TOO_BIG;
    if (b->bump) return NV2A_UP_BUMP;
    return NV2A_UP_TAKEN;
}

int nv2a_up_of_texture(int verdict)
{
    return verdict != NV2A_UP_NOT_2D && verdict != NV2A_UP_NOT_FRAME;
}

int nv2a_up_census_wants(const Nv2aUpBind *b, int verdict)
{
    Nv2aUpBind as_2d;
    if (verdict != NV2A_UP_NOT_2D) return nv2a_up_of_texture(verdict);
    if (b->depth_test || b->depth_write) return 0;
    as_2d = *b;
    as_2d.screen = 1;
    return nv2a_up_rule(&as_2d) == NV2A_UP_TAKEN;
}

int nv2a_up_census_line(char *buf, size_t n, uint32_t addr, uint32_t fmt,
                        const Nv2aUpBind *b, int vp, int verdict)
{
    static const char *const layout[] = { "swizzled", "DXT", "linear" };
    static const char *const depth[] = { "off", "tested", "written", "tested and written" };
    const char *how = b->layout >= NV2A_UP_SWIZZLED && b->layout <= NV2A_UP_LINEAR
                    ? layout[b->layout] : "?";
    const char *draw = b->screen ? "screen-space" : vp ? "vp" : "ff";
    const char *over = b->over ? " over the picture" : "";
    const char *dep = depth[(b->depth_test ? 1 : 0) | (b->depth_write ? 2 : 0)];
    const char *why = verdict >= 0 && verdict < NV2A_UP_VERDICTS
                    ? nv2a_up_verdict_names[verdict] : "?";
    if (verdict == NV2A_UP_TAKEN)
        return snprintf(buf, n, "[UPSCALE] taken %08X %ux%u fmt %02X %s, %u level%s (draw: %s%s, depth %s)\n",
                        addr, b->w, b->h, fmt, how, b->levels, b->levels == 1 ? "" : "s", draw, over, dep);
    return snprintf(buf, n, "[UPSCALE] left %08X %ux%u fmt %02X %s, %u level%s (draw: %s%s, depth %s): %s\n",
                    addr, b->w, b->h, fmt, how, b->levels, b->levels == 1 ? "" : "s", draw, over, dep, why);
}

int nv2a_up_factor(int n)
{
    return n >= 8 ? 8 : n >= 4 ? 4 : n >= 2 ? 2 : 0;
}

void nv2a_up_over_clear(Nv2aUpOver *o, int colour, int depth)
{
    if (colour) {
        o->drawn = o->over = 0;
        o->cleared = 1;
    } else if (depth && (o->drawn || o->cleared)) {
        o->over = 1;
    }
}

int nv2a_up_over_draw(Nv2aUpOver *o, int depth_test)
{
    if (depth_test && !o->over) o->drawn = 1;
    return o->over;
}

int nv2a_up_choice(int factor)
{
    int f = nv2a_up_factor(factor);
    return f == 8 ? 3 : f == 4 ? 2 : f == 2 ? 1 : 0;
}

int nv2a_up_choice_factor(int choice)
{
    return choice >= 3 ? 8 : choice == 2 ? 4 : choice == 1 ? 2 : 0;
}

uint64_t nv2a_up_texels(uint32_t w, uint32_t h, int factor)
{
    return (uint64_t)w * h * (uint64_t)factor * (uint64_t)factor;
}

uint64_t nv2a_up_bytes(uint32_t w, uint32_t h, int factor, int mip, uint32_t levels)
{
    uint64_t n = 0;
    uint64_t lw = (uint64_t)w * (uint64_t)factor, lh = (uint64_t)h * (uint64_t)factor;
    uint32_t game = 0;          /* levels at the game's size or below, so far */
    if (factor < 1 || !w || !h) return 0;
    for (;;) {
        n += lw * lh * 4;
        if (lw <= w && lh <= h) {
            game++;
            if (!mip || (levels > 1 && game >= levels)) break;
        }
        if (lw == 1 && lh == 1) break;
        lw = lw > 1 ? lw / 2 : 1;
        lh = lh > 1 ? lh / 2 : 1;
    }
    return n;
}

int nv2a_up_second_line(char *buf, size_t n, const Nv2aUpSecond *s)
{
    return snprintf(buf, n, "[UPSCALE] %dx: %u taken, %u made (%llu texels), %u failed, "
                    "%u waiting; %u copies held, %.1f MB\n",
                    s->factor, s->taken, s->made, (unsigned long long)s->texels, s->failed,
                    s->waiting, s->held, (double)s->held_bytes / (1024.0 * 1024.0));
}

/* ---- 3D upscaling ---------------------------------------------------------- */

const char *const nv2a_up3_verdict_names[NV2A_UP3_VERDICTS] = {
    "taken",
    "the draw is 2D",
    "the draw has the depth test off",
    "the 2D rule took it",
    "its stage doesn't sample it as a plain 2D texture",
    "it is linear",
    ("a side is under " UP_STR(NV2A_UP3_SIDE_MIN)),
    ("it is larger than " UP_STR(NV2A_UP_SIDE_MAX)),
    "it is a bump source",
    "it is alpha-only",
};

int nv2a_up3_rule(const Nv2aUp3Bind *b)
{
    if (b->screen || b->over) return NV2A_UP3_2D_DRAW;
    if (!b->depth_test) return NV2A_UP3_NO_DEPTH;
    if (b->taken_2d) return NV2A_UP3_TAKEN_2D;
    if (b->mode != NV2A_UP3_MODE_PROJECT2D && b->mode != NV2A_UP3_MODE_PROJECT3D)
        return NV2A_UP3_MODE;
    if (b->layout == NV2A_UP_LINEAR) return NV2A_UP3_LINEAR_TEX;
    if (b->w < (uint32_t)NV2A_UP3_SIDE_MIN || b->h < (uint32_t)NV2A_UP3_SIDE_MIN)
        return NV2A_UP3_TOO_SMALL;
    if (b->w > (uint32_t)NV2A_UP_SIDE_MAX || b->h > (uint32_t)NV2A_UP_SIDE_MAX) return NV2A_UP3_TOO_BIG;
    if (b->bump) return NV2A_UP3_BUMP;
    if (b->alpha_only) return NV2A_UP3_ALPHA_ONLY;
    return NV2A_UP3_TAKEN;
}

int nv2a_up3_of_texture(int verdict)
{
    return verdict != NV2A_UP3_2D_DRAW && verdict != NV2A_UP3_NO_DEPTH;
}

int nv2a_up3_census_wants(const Nv2aUp3Bind *b, int verdict)
{
    Nv2aUp3Bind tested;
    if (verdict != NV2A_UP3_NO_DEPTH) return nv2a_up3_of_texture(verdict);
    tested = *b;
    tested.depth_test = 1;
    return nv2a_up3_rule(&tested) == NV2A_UP3_TAKEN;
}

int nv2a_up3_census_line(char *buf, size_t n, uint32_t addr, uint32_t fmt,
                         const Nv2aUp3Bind *b, int vp, int verdict)
{
    static const char *const layout[] = { "swizzled", "DXT", "linear" };
    static const char *const depth[] = { "off", "tested", "written", "tested and written" };
    static const char *const mode[] = { "NONE", "PROJECT2D", "PROJECT3D", "CUBEMAP", "PASSTHRU",
                                        "CLIPPLANE", "BUMPENVMAP", "BUMPENVMAP_LUM" };
    char other[16];
    const char *how = b->layout >= NV2A_UP_SWIZZLED && b->layout <= NV2A_UP_LINEAR
                    ? layout[b->layout] : "?";
    const char *draw = b->screen ? "screen-space" : vp ? "vp" : "ff";
    const char *over = b->over ? " over the picture" : "";
    const char *dep = depth[(b->depth_test ? 1 : 0) | (b->depth_write ? 2 : 0)];
    const char *md = other;
    const char *why = verdict >= 0 && verdict < NV2A_UP3_VERDICTS
                    ? nv2a_up3_verdict_names[verdict] : "?";
    if (b->mode >= 0 && b->mode < (int)(sizeof mode / sizeof mode[0])) md = mode[b->mode];
    else snprintf(other, sizeof other, "mode %d", b->mode);
    if (verdict == NV2A_UP3_TAKEN)
        return snprintf(buf, n, "[UPSCALE] 3D taken %08X %ux%u fmt %02X %s, %u level%s, stage %s "
                        "(draw: %s%s, depth %s)\n",
                        addr, b->w, b->h, fmt, how, b->levels, b->levels == 1 ? "" : "s", md,
                        draw, over, dep);
    return snprintf(buf, n, "[UPSCALE] 3D left %08X %ux%u fmt %02X %s, %u level%s, stage %s "
                    "(draw: %s%s, depth %s): %s\n",
                    addr, b->w, b->h, fmt, how, b->levels, b->levels == 1 ? "" : "s", md,
                    draw, over, dep, why);
}

int nv2a_up3_room(const Nv2aUp3Held *h, int n, uint64_t held, uint64_t need, uint64_t cap,
                  int *drop)
{
    uint64_t idle = 0, freed = 0;
    int i, k = 0, last = -1;
    if (need > cap) return -1;
    if (held <= cap - need) return 0;
    for (i = 0; i < n; i++)
        if (h[i].idle >= NV2A_UP3_IDLE_FRAMES) idle += h[i].bytes;
    if ((idle < held ? held - idle : 0) > cap - need) return -1;
    /* The longest idle first, the first of equals first: each pick the
     * greatest after the one before, in that order. */
    while (held - freed > cap - need) {
        int best = -1;
        for (i = 0; i < n; i++) {
            if (h[i].idle < NV2A_UP3_IDLE_FRAMES) continue;
            if (last >= 0 && (h[i].idle > h[last].idle || (h[i].idle == h[last].idle && i <= last)))
                continue;
            if (best < 0 || h[i].idle > h[best].idle) best = i;
        }
        if (best < 0) return -1;    /* held was more than the copies hold */
        drop[k++] = best;
        freed += h[best].bytes;
        last = best;
        if (freed >= held) break;
    }
    return k;
}

int nv2a_up3_factor(uint32_t w, uint32_t h, int factor)
{
    uint64_t side = w > h ? w : h;
    while (factor > 2 && side * (uint64_t)factor > NV2A_UP3_OUT_MAX) factor /= 2;
    return factor >= 2 && side * (uint64_t)factor <= NV2A_UP3_OUT_MAX ? factor : 0;
}

int nv2a_up3_second_line(char *buf, size_t n, const Nv2aUpSecond *s)
{
    return snprintf(buf, n, "[UPSCALE] 3D %dx: %u taken, %u made (%llu texels), %u failed, "
                    "%u waiting, %u dropped for the cap; %u copies held, %.1f MB of %.0f MB\n",
                    s->factor, s->taken, s->made, (unsigned long long)s->texels, s->failed,
                    s->waiting, s->dropped, s->held, (double)s->held_bytes / (1024.0 * 1024.0),
                    (double)NV2A_UP3_HELD_MAX / (1024.0 * 1024.0));
}
