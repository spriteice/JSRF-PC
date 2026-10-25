/*
 * SMAA's shader sources and look-up textures, for both backends (see
 * nv2a_smaa.h). The shader is the reference implementation, smaa/SMAA.hlsl,
 * which nv2a_smaa_src.h carries as GLSL and as MSL (smaa/embed.py makes it);
 * what is written here is only what goes around it: the version line and the
 * settings it is built with, and the entry points of the three passes.
 */
#include "nv2a_smaa.h"
#include "nv2a_backend.h"         /* NV2A_GLSL_VERSION */
#include "nv2a_smaa_src.h"
#include "smaa/AreaTex.h"
#include "smaa/SearchTex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if AREATEX_WIDTH != NV2A_SMAA_AREA_W || AREATEX_HEIGHT != NV2A_SMAA_AREA_H \
 || SEARCHTEX_WIDTH != NV2A_SMAA_SEARCH_W || SEARCHTEX_HEIGHT != NV2A_SMAA_SEARCH_H
#error "smaa/AreaTex.h or smaa/SearchTex.h is not the size nv2a_smaa.h says"
#endif

const unsigned char *nv2a_smaa_area(void)   { return areaTexBytes; }
const unsigned char *nv2a_smaa_search(void) { return searchTexBytes; }

/* Pieces joined into one malloc'd string. */
static char *join(const char *const *part, int n)
{
    size_t len = 1, at = 0;
    int i;
    char *s;
    for (i = 0; i < n; i++) len += strlen(part[i]);
    s = (char *)malloc(len);
    if (!s) return NULL;
    for (i = 0; i < n; i++) {
        size_t k = strlen(part[i]);
        memcpy(s + at, part[i], k);
        at += k;
    }
    s[at] = 0;
    return s;
}

/* ---- GLSL ---------------------------------------------------------------- */

/* The triangle over the target, from gl_VertexID, and its texture coordinate:
 * an OpenGL target holds its bottom row first, which is t = 0, at clip y = -1. */
#define GLSL_TRI \
    "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n" \
    "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n" \
    "    smaaUv = p;\n"

static const char *const glsl_vs[3] = {
    /* edges */
    "out vec2 smaaUv;\nout vec4 smaaO0;\nout vec4 smaaO1;\nout vec4 smaaO2;\n"
    "void main()\n{\n" GLSL_TRI
    "    vec4 o[3];\n"
    "    SMAAEdgeDetectionVS(smaaUv, o);\n"
    "    smaaO0 = o[0]; smaaO1 = o[1]; smaaO2 = o[2];\n"
    "}\n",
    /* weights */
    "out vec2 smaaUv;\nout vec2 smaaPix;\nout vec4 smaaO0;\nout vec4 smaaO1;\nout vec4 smaaO2;\n"
    "void main()\n{\n" GLSL_TRI
    "    vec4 o[3];\n"
    "    SMAABlendingWeightCalculationVS(smaaUv, smaaPix, o);\n"
    "    smaaO0 = o[0]; smaaO1 = o[1]; smaaO2 = o[2];\n"
    "}\n",
    /* blend */
    "out vec2 smaaUv;\nout vec4 smaaO0;\n"
    "void main()\n{\n" GLSL_TRI
    "    SMAANeighborhoodBlendingVS(smaaUv, smaaO0);\n"
    "}\n",
};

static const char *const glsl_fs[3] = {
    "in vec2 smaaUv;\nin vec4 smaaO0;\nin vec4 smaaO1;\nin vec4 smaaO2;\n"
    "uniform sampler2D colorTex;\n"
    "out vec4 smaaOut;\n"
    "void main()\n{\n"
    "    vec4 o[3];\n"
    "    o[0] = smaaO0; o[1] = smaaO1; o[2] = smaaO2;\n"
    "    smaaOut = vec4(SMAALumaEdgeDetectionPS(smaaUv, o, colorTex), 0.0, 0.0);\n"
    "}\n",
    "in vec2 smaaUv;\nin vec2 smaaPix;\nin vec4 smaaO0;\nin vec4 smaaO1;\nin vec4 smaaO2;\n"
    "uniform sampler2D edgesTex;\nuniform sampler2D areaTex;\nuniform sampler2D searchTex;\n"
    "out vec4 smaaOut;\n"
    "void main()\n{\n"
    "    vec4 o[3];\n"
    "    o[0] = smaaO0; o[1] = smaaO1; o[2] = smaaO2;\n"
    "    smaaOut = SMAABlendingWeightCalculationPS(smaaUv, smaaPix, o, edgesTex, areaTex,\n"
    "                                              searchTex, vec4(0.0));\n"
    "}\n",
    "in vec2 smaaUv;\nin vec4 smaaO0;\n"
    "uniform sampler2D colorTex;\nuniform sampler2D blendTex;\n"
    "out vec4 smaaOut;\n"
    "void main()\n{\n"
    "    smaaOut = SMAANeighborhoodBlendingPS(smaaUv, smaaO0, colorTex, blendTex);\n"
    "}\n",
};

char *nv2a_smaa_glsl_source(int pass, int fragment)
{
    const char *part[4];
    if (pass < 0 || pass > 2) return NULL;
    part[0] = NV2A_GLSL_VERSION;
    part[1] = fragment
        ? "#define SMAA_GLSL_3 1\n#define SMAA_PRESET_HIGH 1\n"
          "#define SMAA_INCLUDE_VS 0\n#define SMAA_INCLUDE_PS 1\n"
          "uniform vec4 smaaRt;\n#define SMAA_RT_METRICS smaaRt\n"
        : "#define SMAA_GLSL_3 1\n#define SMAA_PRESET_HIGH 1\n"
          "#define SMAA_INCLUDE_VS 1\n#define SMAA_INCLUDE_PS 0\n"
          "uniform vec4 smaaRt;\n#define SMAA_RT_METRICS smaaRt\n";
    part[2] = nv2a_smaa_glsl;
    part[3] = fragment ? glsl_fs[pass] : glsl_vs[pass];
    return join(part, 4);
}

/* ---- MSL ----------------------------------------------------------------- */

/* SMAA_CUSTOM_SL's side of the bargain: how to sample and what HLSL's
 * intrinsics are called here. step() and lerp() with a scalar beside a vector
 * are SMAA's habit; the overloads below take any scalar there (embed.py points
 * those calls at them). */
static const char msl_prelude[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "#define SMAA_CUSTOM_SL 1\n"
    "#define SMAA_PRESET_HIGH 1\n"
    "#define SMAA_INCLUDE_VS 0\n"
    "#define SMAA_INCLUDE_PS 1\n"
    "constexpr sampler smaa_lin(coord::normalized, address::clamp_to_edge, filter::linear);\n"
    "constexpr sampler smaa_pt(coord::normalized, address::clamp_to_edge, filter::nearest);\n"
    "#define SMAATexture2D(tex) texture2d<float> tex\n"
    "#define SMAATexturePass2D(tex) tex\n"
    "#define SMAASampleLevelZero(tex, coord) tex.sample(smaa_lin, coord, level(0.0))\n"
    "#define SMAASampleLevelZeroPoint(tex, coord) tex.sample(smaa_pt, coord, level(0.0))\n"
    "#define SMAASampleLevelZeroOffset(tex, coord, offset) tex.sample(smaa_lin, coord, level(0.0), offset)\n"
    "#define SMAASample(tex, coord) tex.sample(smaa_lin, coord)\n"
    "#define SMAASamplePoint(tex, coord) tex.sample(smaa_pt, coord)\n"
    "#define SMAASampleOffset(tex, coord, offset) tex.sample(smaa_lin, coord, offset)\n"
    "#define SMAA_FLATTEN\n"
    "#define SMAA_BRANCH\n"
    "#define mad(a, b, c) (a * b + c)\n"
    "#define lerp(a, b, t) smaa_lerp(a, b, t)\n"
    "#define discard discard_fragment()\n"
    "template <typename E> inline float2 smaa_step(E e, float2 x) { return step(float2(e), x); }\n"
    "template <typename T> inline float4 smaa_lerp(float4 a, float4 b, T t) { return mix(a, b, float4(t)); }\n";

/* The entry points. A Metal target holds row 0 at the top, at clip y = +1,
 * and that row is texture coordinate 0: t = (1 - y) / 2. */
static const char msl_entry[] =
    "struct SmaaV { float4 pos [[position]]; float2 uv; float2 pix; float4 o0; float4 o1; float4 o2; };\n"
    "inline SmaaV smaa_tri(uint vid)\n"
    "{\n"
    "    float2 p = float2(float((vid << 1) & 2), float(vid & 2));\n"
    "    SmaaV o;\n"
    "    o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0);\n"
    "    o.uv = float2(p.x, 1.0 - p.y);\n"
    "    o.pix = o.uv * SMAA_RT_METRICS.zw;\n"
    "    o.o0 = o.o1 = o.o2 = float4(0.0);\n"
    "    return o;\n"
    "}\n"
    "vertex SmaaV smaa_edge_v(uint vid [[vertex_id]])\n"
    "{\n"
    "    SmaaV o = smaa_tri(vid);\n"
    "    o.o0 = mad(SMAA_RT_METRICS.xyxy, float4(-1.0, 0.0, 0.0, -1.0), o.uv.xyxy);\n"
    "    o.o1 = mad(SMAA_RT_METRICS.xyxy, float4( 1.0, 0.0, 0.0,  1.0), o.uv.xyxy);\n"
    "    o.o2 = mad(SMAA_RT_METRICS.xyxy, float4(-2.0, 0.0, 0.0, -2.0), o.uv.xyxy);\n"
    "    return o;\n"
    "}\n"
    "fragment float4 smaa_edge_f(SmaaV in [[stage_in]], texture2d<float> colorTex [[texture(0)]])\n"
    "{\n"
    "    float4 off[3] = { in.o0, in.o1, in.o2 };\n"
    "    return float4(SMAALumaEdgeDetectionPS(in.uv, off, colorTex), 0.0, 0.0);\n"
    "}\n"
    "vertex SmaaV smaa_weight_v(uint vid [[vertex_id]])\n"
    "{\n"
    "    SmaaV o = smaa_tri(vid);\n"
    "    o.o0 = mad(SMAA_RT_METRICS.xyxy, float4(-0.25, -0.125,  1.25, -0.125), o.uv.xyxy);\n"
    "    o.o1 = mad(SMAA_RT_METRICS.xyxy, float4(-0.125, -0.25, -0.125,  1.25), o.uv.xyxy);\n"
    "    o.o2 = mad(SMAA_RT_METRICS.xxyy, float4(-2.0, 2.0, -2.0, 2.0) * float(SMAA_MAX_SEARCH_STEPS),\n"
    "               float4(o.o0.xz, o.o1.yw));\n"
    "    return o;\n"
    "}\n"
    "fragment float4 smaa_weight_f(SmaaV in [[stage_in]], texture2d<float> edgesTex [[texture(0)]],\n"
    "                              texture2d<float> areaTex [[texture(1)]],\n"
    "                              texture2d<float> searchTex [[texture(2)]])\n"
    "{\n"
    "    float4 off[3] = { in.o0, in.o1, in.o2 };\n"
    "    return SMAABlendingWeightCalculationPS(in.uv, in.pix, off, edgesTex, areaTex, searchTex,\n"
    "                                           float4(0.0));\n"
    "}\n"
    "vertex SmaaV smaa_blend_v(uint vid [[vertex_id]])\n"
    "{\n"
    "    SmaaV o = smaa_tri(vid);\n"
    "    o.o0 = mad(SMAA_RT_METRICS.xyxy, float4(1.0, 0.0, 0.0, 1.0), o.uv.xyxy);\n"
    "    return o;\n"
    "}\n"
    "fragment float4 smaa_blend_f(SmaaV in [[stage_in]], texture2d<float> colorTex [[texture(0)]],\n"
    "                             texture2d<float> blendTex [[texture(1)]])\n"
    "{\n"
    "    return SMAANeighborhoodBlendingPS(in.uv, in.o0, colorTex, blendTex);\n"
    "}\n";

char *nv2a_smaa_msl_source(uint32_t pw, uint32_t ph)
{
    char metrics[192];
    const char *part[4];
    if (!pw || !ph) return NULL;
    snprintf(metrics, sizeof metrics, "#define SMAA_RT_METRICS float4(%.9g, %.9g, %u.0, %u.0)\n",
             1.0 / (double)pw, 1.0 / (double)ph, pw, ph);
    part[0] = msl_prelude;
    part[1] = metrics;
    part[2] = nv2a_smaa_msl;
    part[3] = msl_entry;
    return join(part, 4);
}
