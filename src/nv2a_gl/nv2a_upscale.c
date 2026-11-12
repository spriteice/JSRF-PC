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

const char *const nv2a_up_sharp_names[NV2A_UP_SHARP_CHOICES] = { "Soft", "Normal", "Sharp" };

int nv2a_up_sharp(int n)
{
    return n >= 0 && n < NV2A_UP_SHARP_CHOICES ? n : NV2A_UP_SHARP_DEFAULT;
}

float nv2a_up_sharp_weight(int choice)
{
    static const float weight[NV2A_UP_SHARP_CHOICES] = { 0.5f, 1.0f, 1.5f };
    return weight[nv2a_up_sharp(choice)];
}

const char *const nv2a_up_edge_names[NV2A_UP_EDGE_CHOICES] = { "Soft", "Normal", "Strong" };

int nv2a_up_edge(int n)
{
    return n >= 0 && n < NV2A_UP_EDGE_CHOICES ? n : NV2A_UP_EDGE_DEFAULT;
}

float nv2a_up_edge_strength(int choice)
{
    static const float strength[NV2A_UP_EDGE_CHOICES] = { 1.8f, 0.6f, 0.2f };
    return strength[nv2a_up_edge(choice)];
}

const char *const nv2a_up_clean_names[NV2A_UP_CLEANS] = { "DXT deblock", "De-dither" };

unsigned nv2a_up_clean(int deblock, int dedither)
{
    return (deblock == 1 ? NV2A_UP_DEBLOCK : 0u) | (dedither == 1 ? NV2A_UP_DEDITHER : 0u);
}

unsigned nv2a_up_clean_of(unsigned want, uint32_t fmt, Nv2aUpClean *c)
{
    int r, g, b, dxt = 0;
    switch (fmt) {
    case 0x0C: case 0x0E: case 0x0F:    /* DXT1, 3, 5: R5G6B5 colours */
        dxt = 1; r = 5; g = 6; b = 5; break;
    case 0x05: case 0x11:               /* R5G6B5, swizzled and linear */
        r = 5; g = 6; b = 5; break;
    case 0x02: case 0x10:               /* A1R5G5B5 */
    case 0x03: case 0x1C:               /* X1R5G5B5 */
        r = g = b = 5; break;
    case 0x04: case 0x1D:               /* A4R4G4B4 */
        r = g = b = 4; break;
    default:
        r = g = b = 0; break;
    }
    c->what = 0;
    c->step[0] = r ? 1.0f / (float)((1 << r) - 1) : 0.0f;
    c->step[1] = g ? 1.0f / (float)((1 << g) - 1) : 0.0f;
    c->step[2] = b ? 1.0f / (float)((1 << b) - 1) : 0.0f;
    if (!r) return 0;
    if (dxt) c->what |= want & NV2A_UP_DEBLOCK;
    c->what |= want & NV2A_UP_DEDITHER;
    return c->what;
}

const char *nv2a_up_clean_said(unsigned want)
{
    switch (want & (NV2A_UP_DEBLOCK | NV2A_UP_DEDITHER)) {
    case NV2A_UP_DEBLOCK: return "DXT deblock";
    case NV2A_UP_DEDITHER: return "De-dither";
    case NV2A_UP_DEBLOCK | NV2A_UP_DEDITHER: return "DXT deblock and De-dither";
    default: return "none";
    }
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
    ("it is " UP_STR(NV2A_UP_SIDE_MAX) " or less, the 3D rule's"),
    ("it is larger than " UP_STR(NV2A_UPG_SIDE_MAX)),
};

/* The 3D rule for textures up to side_max a side, too_big the verdict past
 * it: the 3D rule's own, and the graffiti rule's for larger, which weighs
 * neither the draw nor the layout where the draw is into a picture the title
 * keeps (kept_any). */
static int up3_rule_upto(const Nv2aUp3Bind *b, uint32_t side_max, int too_big, int kept_any)
{
    int any = kept_any && b->kept;
    if (!any && (b->screen || b->over)) return NV2A_UP3_2D_DRAW;
    if (!any && !b->depth_test) return NV2A_UP3_NO_DEPTH;
    if (b->taken_2d) return NV2A_UP3_TAKEN_2D;
    if (b->mode != NV2A_UP3_MODE_PROJECT2D && b->mode != NV2A_UP3_MODE_PROJECT3D)
        return NV2A_UP3_MODE;
    if (!any && b->layout == NV2A_UP_LINEAR) return NV2A_UP3_LINEAR_TEX;
    if (b->w < (uint32_t)NV2A_UP3_SIDE_MIN || b->h < (uint32_t)NV2A_UP3_SIDE_MIN)
        return NV2A_UP3_TOO_SMALL;
    if (b->w > side_max || b->h > side_max) return too_big;
    if (b->bump) return NV2A_UP3_BUMP;
    if (b->alpha_only) return NV2A_UP3_ALPHA_ONLY;
    return NV2A_UP3_TAKEN;
}

int nv2a_up3_rule(const Nv2aUp3Bind *b)
{
    return up3_rule_upto(b, NV2A_UP_SIDE_MAX, NV2A_UP3_TOO_BIG, 0);
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

/* A census line of the 3D rule's or the graffiti rule's, named `row`;
 * `more` ends what it says of the draw. */
static int up3_line(char *buf, size_t n, const char *row, uint32_t addr, uint32_t fmt,
                    const Nv2aUp3Bind *b, int vp, int verdict, const char *more)
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
        return snprintf(buf, n, "[UPSCALE] %s taken %08X %ux%u fmt %02X %s, %u level%s, stage %s "
                        "(draw: %s%s, depth %s%s)\n",
                        row, addr, b->w, b->h, fmt, how, b->levels, b->levels == 1 ? "" : "s",
                        md, draw, over, dep, more);
    return snprintf(buf, n, "[UPSCALE] %s left %08X %ux%u fmt %02X %s, %u level%s, stage %s "
                    "(draw: %s%s, depth %s%s): %s\n",
                    row, addr, b->w, b->h, fmt, how, b->levels, b->levels == 1 ? "" : "s", md,
                    draw, over, dep, more, why);
}

int nv2a_up3_census_line(char *buf, size_t n, uint32_t addr, uint32_t fmt,
                         const Nv2aUp3Bind *b, int vp, int verdict)
{
    return up3_line(buf, n, "3D", addr, fmt, b, vp, verdict, b->blue_alpha ? ", blue-to-alpha" : "");
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

/* The factor, halved until a side times it is at most out_max. */
static int up3_fit(uint32_t w, uint32_t h, int factor, uint64_t out_max)
{
    uint64_t side = w > h ? w : h;
    while (factor > 2 && side * (uint64_t)factor > out_max) factor /= 2;
    return factor >= 2 && side * (uint64_t)factor <= out_max ? factor : 0;
}

int nv2a_up3_factor(uint32_t w, uint32_t h, int factor)
{
    return up3_fit(w, h, factor, NV2A_UP3_OUT_MAX);
}

/* A second's summary line of the 3D row's or the graffiti row's, named
 * `row`, under its cap. */
static int up3_second(char *buf, size_t n, const char *row, const Nv2aUpSecond *s, uint64_t cap)
{
    return snprintf(buf, n, "[UPSCALE] %s %dx: %u taken, %u made (%llu texels), %u failed, "
                    "%u waiting, %u dropped for the cap; %u copies held, %.1f MB of %.0f MB\n",
                    row, s->factor, s->taken, s->made, (unsigned long long)s->texels, s->failed,
                    s->waiting, s->dropped, s->held, (double)s->held_bytes / (1024.0 * 1024.0),
                    (double)cap / (1024.0 * 1024.0));
}

int nv2a_up3_second_line(char *buf, size_t n, const Nv2aUpSecond *s)
{
    return up3_second(buf, n, "3D", s, NV2A_UP3_HELD_MAX);
}

/* ---- Graffiti upscaling ----------------------------------------------------- */

int nv2a_upg_rule(const Nv2aUp3Bind *b)
{
    if (b->w <= (uint32_t)NV2A_UP_SIDE_MAX && b->h <= (uint32_t)NV2A_UP_SIDE_MAX)
        return NV2A_UPG_NOT_GRAFFITI;
    return up3_rule_upto(b, NV2A_UPG_SIDE_MAX, NV2A_UPG_TOO_BIG, 1);
}

int nv2a_upg_census_wants(const Nv2aUp3Bind *b, int verdict)
{
    Nv2aUp3Bind tested;
    if (verdict == NV2A_UPG_NOT_GRAFFITI) return 0;
    if (verdict != NV2A_UP3_NO_DEPTH) return nv2a_up3_of_texture(verdict);
    tested = *b;
    tested.depth_test = 1;
    return nv2a_upg_rule(&tested) == NV2A_UP3_TAKEN;
}

int nv2a_upg_census_line(char *buf, size_t n, uint32_t addr, uint32_t fmt,
                         const Nv2aUp3Bind *b, int vp, int verdict)
{
    static const char *const more[2][2] = {
        { ", no blue-to-alpha", ", blue-to-alpha" },
        { ", into a picture of its own, no blue-to-alpha", ", into a picture of its own, blue-to-alpha" }
    };
    return up3_line(buf, n, "graffiti", addr, fmt, b, vp, verdict,
                    more[b->kept != 0][b->blue_alpha != 0]);
}

int nv2a_upg_factor(uint32_t w, uint32_t h, int factor)
{
    return up3_fit(w, h, factor, NV2A_UPG_OUT_MAX);
}

int nv2a_upg_second_line(char *buf, size_t n, const Nv2aUpSecond *s)
{
    return up3_second(buf, n, "graffiti", s, NV2A_UPG_HELD_MAX);
}
