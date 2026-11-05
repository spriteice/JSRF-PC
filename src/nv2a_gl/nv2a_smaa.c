/*
 * SMAA's shader sources and look-up textures, for the backend (see
 * nv2a_smaa.h). The shader is the reference implementation, smaa/SMAA.hlsl,
 * which nv2a_smaa_src.h carries as GLSL (smaa/embed.py makes it);
 * what is written here is only what goes around it: the version line and the
 * settings it is built with, and the entry points of the three passes.
 */
#include "nv2a_smaa.h"
#include "nv2a_backend.h"         /* NV2A_GLSL_VERSION */
#include "nv2a_smaa_src.h"
#include "smaa/AreaTex.h"
#include "smaa/SearchTex.h"

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
