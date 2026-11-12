/*
 * Upscaling's passes (nv2a_upscale.h), for 2D art and 3D textures alike:
 * Super-xBR, written in our GLSL from Hyllian's published shaders,
 * super-xbr-pass0.glsl, -pass1.glsl and -pass2.glsl (libretro's
 * glsl-shaders, xbr/shaders/super-xbr/), at their
 * defaults, mode 0 ("Normal"), edge strength 0.6 and full anti-ringing, with
 * his filter weight a uniform (upWeight: 1, his default, unless Graphics >
 * Upscaling > Sharpness says otherwise). What is ours: the texels read whole (texelFetch) at the
 * source's wrap instead of sampled, colour premultiplied by alpha and edges
 * found in alpha as well as brightness (his read colour only), 4x as two
 * steps of 2x, and the store and copy passes. His notice, which goes with
 * this file and is in NOTICE:
 *
 *   Super XBR Shader
 *
 *   Copyright (c) 2015 Hyllian - sergiogdb@gmail.com
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the
 *   "Software"), to deal in the Software without restriction, including
 *   without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to
 *   permit persons to whom the Software is furnished to do so, subject to
 *   the following conditions:
 *
 *   The above copyright notice and this permission notice shall be included
 *   in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 *   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 *   CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 *   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 *   SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "nv2a_upscale.h"
#include "nv2a_backend.h"         /* NV2A_GLSL_VERSION */

const char *const nv2a_up_pass_names[NV2A_UP_PASSES] = {
    "upscaling pass 0", "upscaling pass 1", "upscaling pass 2",
    "upscaling store", "upscaling copy",
};

/* One triangle over the target; the fragment stages work from gl_FragCoord,
 * whose row 0 is the target's first row, as a texture's is. */
static const char glsl_vs[] =
    NV2A_GLSL_VERSION
    "void main()\n{\n"
    "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

/* What every fragment stage has: the uniforms, and a texel read whole at
 * the source's wrap. The taps reach at most three texels past an edge, so
 * adding a multiple of the size keeps % to operands that aren't negative,
 * the only ones GLSL defines it for. */
#define GLSL_HEAD \
    NV2A_GLSL_VERSION \
    "uniform sampler2D upSrc;\n" \
    "uniform sampler2D upAux;\n" \
    "uniform ivec2 upWrap;\n" \
    "uniform int upPremul;\n" \
    "uniform int upShift;\n" \
    "uniform int upKeep;\n" \
    "uniform int upMagLin;\n" \
    "uniform int upLevel;\n" \
    "uniform float upWeight;\n" \
    "out vec4 upOut;\n" \
    "int up_wrap(int p, int n, int mode)\n{\n" \
    "    if (mode == 1) return (p + 4 * n) % n;\n" \
    "    if (mode == 2) { int m = (p + 8 * n) % (2 * n); return m < n ? m : 2 * n - 1 - m; }\n" \
    "    return clamp(p, 0, n - 1);\n" \
    "}\n" \
    "vec4 up_at(sampler2D s, ivec2 p)\n{\n" \
    "    ivec2 n = textureSize(s, 0);\n" \
    "    return texelFetch(s, ivec2(up_wrap(p.x, n.x, upWrap.x), up_wrap(p.y, n.y, upWrap.y)), 0);\n" \
    "}\n" \
    /* upSrc's texel, premultiplied */ \
    "vec4 up_src(ivec2 p)\n{\n" \
    "    vec4 c = up_at(upSrc, p);\n" \
    "    return upPremul != 0 ? vec4(c.rgb * c.a, c.a) : c;\n" \
    "}\n"

/* Super-xBR's filter, the same in every pass but for its weights (WP1 to
 * WP6, WEIGHT1, WEIGHT2, the last two his filter weight, upWeight, times
 * each pass's own), on the 4x4 texels around the point it works out,
 * named as his are:
 *
 *     P0 B  C  P1
 *     D  E  F  F4        the point is between E, F, H and I
 *     G  H  I  I4
 *     P2 H5 I5 P3
 *
 * Edges are found in (brightness, alpha) pairs, a difference being the sum
 * of the two; the four directions' colours are blended as his are, and the
 * result clamped to E, F, H and I's range (anti-ringing), alpha with it. */
#define GLSL_XBR \
    "vec2 up_key(vec4 c) { return vec2(dot(c.rgb, vec3(0.2126, 0.7152, 0.0722)), c.a); }\n" \
    "float df(vec2 a, vec2 b) { vec2 d = abs(a - b); return d.x + d.y; }\n" \
    "float d_wd(vec2 b0, vec2 b1, vec2 c0, vec2 c1, vec2 c2, vec2 d0, vec2 d1, vec2 d2,\n" \
    "           vec2 d3, vec2 e1, vec2 e2, vec2 e3, vec2 f2, vec2 f3)\n{\n" \
    "    return WP1 * (df(c1, c2) + df(c1, c0) + df(e2, e1) + df(e2, e3))\n" \
    "         + WP2 * (df(d2, d3) + df(d0, d1)) + WP3 * (df(d1, d3) + df(d0, d2))\n" \
    "         + WP4 * df(d1, d2) + WP5 * (df(c0, c2) + df(e1, e3)) + WP6 * (df(b0, b1) + df(f2, f3));\n" \
    "}\n" \
    "float hv_wd(vec2 i1, vec2 i2, vec2 i3, vec2 i4, vec2 e1, vec2 e2, vec2 e3, vec2 e4)\n{\n" \
    "    return WP4 * (df(i1, i2) + df(i3, i4))\n" \
    "         + WP1 * (df(i1, e1) + df(i2, e2) + df(i3, e3) + df(i4, e4))\n" \
    "         + WP3 * (df(i1, e2) + df(i3, e4) + df(e1, i2) + df(e3, i4));\n" \
    "}\n" \
    "vec4 xbr(vec4 P0, vec4 B, vec4 C, vec4 P1, vec4 D, vec4 E, vec4 F, vec4 F4,\n" \
    "         vec4 G, vec4 H, vec4 I, vec4 I4, vec4 P2, vec4 H5, vec4 I5, vec4 P3)\n{\n" \
    "    vec2 b = up_key(B), c = up_key(C), d = up_key(D), e = up_key(E);\n" \
    "    vec2 f = up_key(F), g = up_key(G), h = up_key(H), i = up_key(I);\n" \
    "    vec2 i4 = up_key(I4), i5 = up_key(I5), h5 = up_key(H5), f4 = up_key(F4);\n" \
    "    vec2 p0 = up_key(P0), p1 = up_key(P1), p2 = up_key(P2), p3 = up_key(P3);\n" \
    "    float d_edge = d_wd(d, b, g, e, c, p2, h, f, p1, h5, i, f4, i5, i4)\n" \
    "                 - d_wd(c, f4, b, f, i4, p0, e, i, p3, d, h, i5, g, h5);\n" \
    "    float hv_edge = hv_wd(f, i, e, h, c, i5, b, h5) - hv_wd(e, f, h, i, d, f4, g, i4);\n" \
    "    float strength = smoothstep(0.0, 0.6 + 0.000001, abs(d_edge));\n" \
    "    vec4 w1 = vec4(-WEIGHT1, WEIGHT1 + 0.5, WEIGHT1 + 0.5, -WEIGHT1);\n" \
    "    vec4 w2 = vec4(-WEIGHT2, WEIGHT2 + 0.25, WEIGHT2 + 0.25, -WEIGHT2);\n" \
    "    vec4 c1 = mat4(P2, H, F, P1) * w1;\n" \
    "    vec4 c2 = mat4(P0, E, I, P3) * w1;\n" \
    "    vec4 c3 = mat4(D + G, E + H, F + I, F4 + I4) * w2;\n" \
    "    vec4 c4 = mat4(C + B, F + E, I + H, I5 + H5) * w2;\n" \
    "    vec4 col = mix(mix(c1, c2, step(0.0, d_edge)), mix(c3, c4, step(0.0, hv_edge)),\n" \
    "                   1.0 - strength);\n" \
    "    return clamp(col, min(min(E, F), min(H, I)), max(max(E, F), max(H, I)));\n" \
    "}\n"

/* Pass 0 and pass 2 read one image, upSrc, at offsets from the texel being
 * worked out: pass 0 from it, pass 2 from one up and to the left. */
#define GLSL_ONE_IMAGE(o) \
    "vec4 S(ivec2 p, int x, int y) { return up_src(p + ivec2(x + " o ", y + " o ")); }\n" \
    "void main()\n{\n" \
    "    ivec2 p = ivec2(gl_FragCoord.xy);\n" \
    "    upOut = xbr(S(p, -1, -1), S(p, 0, -1), S(p, 1, -1), S(p, 2, -1),\n" \
    "                S(p, -1,  0), S(p, 0,  0), S(p, 1,  0), S(p, 2,  0),\n" \
    "                S(p, -1,  1), S(p, 0,  1), S(p, 1,  1), S(p, 2,  1),\n" \
    "                S(p, -1,  2), S(p, 0,  2), S(p, 1,  2), S(p, 2,  2));\n" \
    "}\n"

static const char glsl_pass0[] =
    GLSL_HEAD
    "#define WP1 2.0\n#define WP2 1.0\n#define WP3 -1.0\n"
    "#define WP4 4.0\n#define WP5 -1.0\n#define WP6 1.0\n"
    "#define WEIGHT1 (upWeight * 1.29633 / 10.0)\n#define WEIGHT2 (upWeight * 1.75068 / 10.0 / 2.0)\n"
    GLSL_XBR
    GLSL_ONE_IMAGE("0");

/* Pass 1, at twice the source's size: texel (2x, 2y) is the source's (x, y),
 * (2x+1, 2y+1) is pass 0's, and the other two are worked out from both, the
 * 4x4 taps turned to lie along the diagonal between them (ax across it, ay
 * along it). */
static const char glsl_pass1[] =
    GLSL_HEAD
    "#define WP1 8.0\n#define WP2 0.0\n#define WP3 0.0\n"
    "#define WP4 0.0\n#define WP5 0.0\n#define WP6 0.0\n"
    "#define WEIGHT1 (upWeight * 1.75068 / 10.0)\n#define WEIGHT2 (upWeight * 1.29633 / 10.0 / 2.0)\n"
    GLSL_XBR
    "vec4 O(ivec2 p) { return up_src(p); }\n"
    "vec4 Q(ivec2 p) { return up_at(upAux, p); }\n"
    "void main()\n{\n"
    "    ivec2 x = ivec2(gl_FragCoord.xy);\n"
    "    ivec2 b = x >> 1, k = x & 1;\n"
    "    if (k.x == k.y) { upOut = k.x == 0 ? O(b) : Q(b); return; }\n"
    "    ivec2 ax = k.x == 1 ? ivec2(1, 0) : ivec2(0, 1);\n"
    "    ivec2 ay = ax.yx;\n"
    "    upOut = xbr(O(b - ax), Q(b - ax - ay), O(b - ay), Q(b - 2 * ay),\n"
    "                Q(b - ax), O(b), Q(b - ay), O(b + ax - ay),\n"
    "                O(b + ay), Q(b), O(b + ax), Q(b + ax - ay),\n"
    "                Q(b + ay), O(b + ax + ay), Q(b + ax), O(b + 2 * ax));\n"
    "}\n";

static const char glsl_pass2[] =
    GLSL_HEAD
    "#define WP1 1.0\n#define WP2 0.0\n#define WP3 2.0\n"
    "#define WP4 3.0\n#define WP5 -2.0\n#define WP6 1.0\n"
    "#define WEIGHT1 (upWeight * 1.29633 / 10.0)\n#define WEIGHT2 (upWeight * 1.75068 / 10.0 / 2.0)\n"
    GLSL_XBR
    GLSL_ONE_IMAGE("-1");

/* Straight alpha again. Where the copy is clear (an alpha that stores as
 * 0) its colour is the game's own texel's under it, as a clear texel's
 * colour still shows where a linear filter blends it with its neighbours.
 *
 * With upKeep, the alpha is the game's own instead: its level 0 at the
 * centre of each texel of the copy, as the sampler magnifies it (upMagLin:
 * the four texels around it, blended; else the one it falls in), at the
 * source's wrap. That is what a draw sampled there before, so a cut-out's
 * edge stays where the title drew it; only the colour is Super-xBR's. */
static const char glsl_store[] =
    GLSL_HEAD
    "float up_kept_alpha(ivec2 p)\n{\n"
    "    if (upMagLin == 0) return up_at(upAux, p >> upShift).a;\n"
    "    vec2 g = (vec2(p) + 0.5) / float(1 << upShift) - 0.5;\n"
    "    ivec2 i = ivec2(floor(g));\n"
    "    vec2 f = g - vec2(i);\n"
    "    return mix(mix(up_at(upAux, i).a, up_at(upAux, i + ivec2(1, 0)).a, f.x),\n"
    "               mix(up_at(upAux, i + ivec2(0, 1)).a, up_at(upAux, i + ivec2(1, 1)).a, f.x), f.y);\n"
    "}\n"
    "void main()\n{\n"
    "    ivec2 p = ivec2(gl_FragCoord.xy);\n"
    "    vec4 c = texelFetch(upSrc, p, 0);\n"
    "    if (c.a < 0.5 / 255.0)\n"
    "        upOut = vec4(texelFetch(upAux, p >> upShift, 0).rgb, 0.0);\n"
    "    else\n"
    "        upOut = vec4(clamp(c.rgb / c.a, 0.0, 1.0), c.a);\n"
    "    if (upKeep != 0) upOut.a = up_kept_alpha(p);\n"
    "}\n";

/* Level upLevel of the game's texture, as it is. */
static const char glsl_copy[] =
    GLSL_HEAD
    "void main()\n{\n"
    "    upOut = texelFetch(upSrc, ivec2(gl_FragCoord.xy), upLevel);\n"
    "}\n";

const char *nv2a_up_glsl(int pass, int fragment)
{
    static const char *const fs[NV2A_UP_PASSES] = {
        glsl_pass0, glsl_pass1, glsl_pass2, glsl_store, glsl_copy,
    };
    if (pass < 0 || pass >= NV2A_UP_PASSES) return 0;
    return fragment ? fs[pass] : glsl_vs;
}
