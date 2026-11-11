/*
 * 2D upscaling's rule and its census (nv2a_upscale.h).
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

uint64_t nv2a_up_bytes(uint32_t w, uint32_t h, int factor, int mip)
{
    uint64_t n = 0;
    uint64_t lw = (uint64_t)w * (uint64_t)factor, lh = (uint64_t)h * (uint64_t)factor;
    if (factor < 1 || !w || !h) return 0;
    for (;;) {
        n += lw * lh * 4;
        if (lw <= w && lh <= h && !mip) break;
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
