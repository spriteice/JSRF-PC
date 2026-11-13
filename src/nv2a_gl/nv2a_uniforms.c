/*
 * The uniforms the generated shaders read, in one table: each one's name as
 * the GLSL emitters declare it, its type and its stage, in Nv2aUniformId
 * order. The front half sets a uniform by its id, and the OpenGL backend
 * looks every program's uniforms up by the names here (nv2a_gfx_gl.c).
 */

#include "nv2a_backend.h"
#include "nv2a_psh_consts.h"

#define V(nm, ty, n)  { nm, NV2A_UT_##ty, 0, n }
#define F(nm, ty, n)  { nm, NV2A_UT_##ty, 1, n }

const Nv2aUniformDesc nv2a_uniform_desc[NV2A_U_COUNT] = {
    /* vertex stage, in Nv2aUniformId order */
    V("c",          VEC4,  NV2A_VSH_NUM_CONSTS),
    V("vpScale",    VEC4,  1),
    V("vpOff",      VEC4,  1),
    V("zClip",      VEC2,  1),
    V("vpSurface",  VEC2,  1),
    V("posMode",    INT,   1),
    V("fogEnable",  INT,   1),
    V("ffMat",      VEC4,  4),
    V("ffVpOff",    VEC4,  1),
    V("litAmbient", FLOAT, 1),
    V("ffTexMat",   VEC4, 16),
    V("ffMV",       VEC4,  4),
    V("ffIMV",      VEC4,  4),
    V("ltSceneAmb", VEC3,  1),
    V("ltMatEm",    VEC3,  1),
    V("ltMatAlpha", FLOAT, 1),
    V("ltAmb",      VEC3,  8),
    V("ltDif",      VEC3,  8),
    V("ltSpc",      VEC3,  8),
    V("ltDir",      VEC3,  8),
    V("ltHalf",     VEC3,  8),
    V("ltPos",      VEC3,  8),
    V("ltAtt",      VEC3,  8),
    V("ltRange",    FLOAT, 8),
    V("ltSpotDir",  VEC4,  8),
    V("ltEye",      VEC4,  1),
    V("ltSpecPow",  FLOAT, 1),
    V("wideShift",  FLOAT, 1),
    V("fogMode",    INT,   1),
    V("fogParam",   VEC2,  1),
    V("fogPlane",   VEC4,  1),
    V("vpRect",     VEC4,  1),
    V("ffRevZ",     VEC4,  1),
    V("wideAcross", FLOAT, 1),
    V("wideOff",    FLOAT, 1),
    V("hudSize",    VEC3,  1),
    /* fragment stage */
    F("drawTint",   VEC4,  1),
    F("fogColor",   VEC4,  1),
    F("alphaFunc",  INT,   1),
    F("alphaRef",   FLOAT, 1),
    F("tex0", SAMPLER, 1), F("tex1", SAMPLER, 1),
    F("tex2", SAMPLER, 1), F("tex3", SAMPLER, 1),
    F("texScale0", VEC2, 1), F("texScale1", VEC2, 1),
    F("texScale2", VEC2, 1), F("texScale3", VEC2, 1),
    F("texOff0", VEC2, 1), F("texOff1", VEC2, 1),
    F("texOff2", VEC2, 1), F("texOff3", VEC2, 1),
    F("bumpMat0", MAT2, 1), F("bumpMat1", MAT2, 1),
    F("bumpMat2", MAT2, 1), F("bumpMat3", MAT2, 1),
    F("bumpScale0", FLOAT, 1), F("bumpScale1", FLOAT, 1),
    F("bumpScale2", FLOAT, 1), F("bumpScale3", FLOAT, 1),
    F("bumpOffset0", FLOAT, 1), F("bumpOffset1", FLOAT, 1),
    F("bumpOffset2", FLOAT, 1), F("bumpOffset3", FLOAT, 1),
    F("polyOffset", VEC2, 1),
    F("pshC", VEC4, NV2A_PSH_NCONSTS),
};

#undef V
#undef F
