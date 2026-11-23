/* The mark that a recording plays (playback_mark.h). */
#include "playback_mark.h"

#include <math.h>
#include <string.h>

#include "playback.h"

#define PB_MARK_VERTEX      0x18u   /* soul_indicator.h's SOUL_VERTEX */
#define PB_MARK_X           0x00u
#define PB_MARK_Y           0x04u
#define PB_MARK_DIFFUSE     0x08u
#define PB_MARK_SPECULAR    0x0Cu
#define PB_MARK_U           0x10u
#define PB_MARK_V           0x14u
#define PB_MARK_SPEC_VALUE  0x00FFFFFFu     /* the specular the game's HUD quads carry */

/* The play sign, from the disc's centre in its radii: its back edge, its
 * tip and its half height (the mock-up's). */
#define PB_MARK_SIGN_BACK   (-0.225f)
#define PB_MARK_SIGN_TIP    0.45f
#define PB_MARK_SIGN_HALF   0.425f

static float clamp01(double v)
{
    return v <= 0.0 ? 0.0f : v >= 1.0 ? 1.0f : (float)v;
}

static uint32_t with_alpha(uint32_t rgb, float a)
{
    return ((uint32_t)(a * 255.0f + 0.5f) << 24) | (rgb & 0x00FFFFFFu);
}

/* a to b, t of the way, each channel. */
static uint32_t mix(uint32_t a, uint32_t b, float t)
{
    uint32_t out = 0;
    int k;
    for (k = 0; k < 24; k += 8) {
        float ca = (float)((a >> k) & 0xFFu), cb = (float)((b >> k) & 0xFFu);
        out |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << k;
    }
    return out;
}

static void begin(PbMark *m, int phase, double now)
{
    m->phase = phase;
    m->at = now;
}

void playback_mark_play(PbMark *m)
{
    m->phase = PB_MARK_DUE;
    m->at = 0.0;
    m->colour = PB_MARK_WHITE;
}

void playback_mark_end(PbMark *m, int how, double now)
{
    if (m->phase == PB_MARK_HIDDEN || m->phase == PB_MARK_ENDED || m->phase == PB_MARK_OUT) return;
    m->colour = how == PLAYBACK_MARK_FINISHED ? PB_MARK_GREEN
              : how == PLAYBACK_MARK_TAKEN ? PB_MARK_BLUE : PB_MARK_RED;
    begin(m, PB_MARK_ENDED, now);
}

int playback_mark_frame(PbMark *m, double now, PbMarkShape *s)
{
    double t, scale = 1.0;
    float tint = 0.0f;
    memset(s, 0, sizeof *s);
    if (m->phase == PB_MARK_DUE) begin(m, PB_MARK_IN, now);
    if (m->phase == PB_MARK_IN && now - m->at >= PB_MARK_IN_TIME) begin(m, PB_MARK_ON, m->at + PB_MARK_IN_TIME);
    if (m->phase == PB_MARK_ENDED && now - m->at >= PB_MARK_TINT_TIME + PB_MARK_HOLD)
        begin(m, PB_MARK_OUT, m->at + PB_MARK_TINT_TIME + PB_MARK_HOLD);
    if (m->phase == PB_MARK_OUT && now - m->at >= PB_MARK_OUT_TIME) m->phase = PB_MARK_HIDDEN;
    t = now - m->at;
    switch (m->phase) {
    case PB_MARK_IN:
        s->alpha = clamp01(t / PB_MARK_IN_TIME);
        scale = PB_MARK_IN_FROM + (1.0 - PB_MARK_IN_FROM) * s->alpha;
        break;
    case PB_MARK_ON:
        s->alpha = 1.0f;
        break;
    case PB_MARK_ENDED:
        s->alpha = 1.0f;
        tint = clamp01(t / PB_MARK_TINT_TIME);
        break;
    case PB_MARK_OUT:
        s->alpha = 1.0f - clamp01(t / PB_MARK_OUT_TIME);
        scale = 1.0 + (PB_MARK_OUT_TO - 1.0) * clamp01(t / PB_MARK_OUT_TIME);
        tint = 1.0f;
        break;
    default:
        return 0;
    }
    if (s->alpha <= 0.0f) return 0;
    s->cx = PB_MARK_LEFT + PB_MARK_SIZE * 0.5f;
    s->cy = PB_MARK_MIDDLE_Y;
    s->r = (float)(PB_MARK_SIZE * 0.5 * scale);
    s->disc = with_alpha(mix(PB_MARK_WHITE, m->colour, tint), s->alpha);
    s->sign = with_alpha(PB_MARK_SIGN, s->alpha);
    return 1;
}

/* One quad's four vertices at v: (x0, y0), (x1, y0), (x0, y1), (x1, y1),
 * here any four corners in that order, with the colour. */
static void quad(uint8_t *v, const float x[4], const float y[4], uint32_t colour)
{
    const uint32_t specular = PB_MARK_SPEC_VALUE;
    const float corner[2] = { 0.0f, 1.0f };
    unsigned k;
    for (k = 0; k < 4; k++, v += PB_MARK_VERTEX) {
        memcpy(v + PB_MARK_X, &x[k], 4);
        memcpy(v + PB_MARK_Y, &y[k], 4);
        memcpy(v + PB_MARK_DIFFUSE, &colour, 4);
        memcpy(v + PB_MARK_SPECULAR, &specular, 4);
        memcpy(v + PB_MARK_U, &corner[k & 1u], 4);
        memcpy(v + PB_MARK_V, &corner[k >> 1], 4);
    }
}

void playback_mark_vertices(uint8_t *mem, uint32_t at, const PbMarkShape *s)
{
    const double pi = 3.14159265358979323846;
    uint8_t *v = mem + at;
    float x[4], y[4];
    int b;
    /* The disc: band b between the circle's points at angles b and b + 1
     * of PB_MARK_BANDS from its top, each side's. */
    for (b = 0; b < PB_MARK_BANDS; b++, v += 4u * PB_MARK_VERTEX) {
        double a0 = pi * b / PB_MARK_BANDS, a1 = pi * (b + 1) / PB_MARK_BANDS;
        float w0 = (float)(s->r * sin(a0)), w1 = (float)(s->r * sin(a1));
        float y0 = (float)(s->cy - s->r * cos(a0)), y1 = (float)(s->cy - s->r * cos(a1));
        x[0] = s->cx - w0; x[1] = s->cx + w0; x[2] = s->cx - w1; x[3] = s->cx + w1;
        y[0] = y0; y[1] = y0; y[2] = y1; y[3] = y1;
        quad(v, x, y, s->disc);
    }
    /* The play sign: its back edge's ends, and its tip twice. */
    x[0] = x[2] = s->cx + PB_MARK_SIGN_BACK * s->r;
    x[1] = x[3] = s->cx + PB_MARK_SIGN_TIP * s->r;
    y[0] = s->cy - PB_MARK_SIGN_HALF * s->r;
    y[2] = s->cy + PB_MARK_SIGN_HALF * s->r;
    y[1] = y[3] = s->cy;
    quad(v, x, y, s->sign);
}
