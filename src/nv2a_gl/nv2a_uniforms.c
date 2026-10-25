/*
 * The uniforms the generated shaders read, in one table.
 *
 * Three things have to agree about every one of them: the GLSL emitters that
 * declare it, the Metal emitters that put it in a struct, and the Metal
 * backend that writes it into a buffer at some offset. The first was always
 * true by construction (the OpenGL backend looks every uniform up by name);
 * the other two are kept true by generating both the struct declaration and
 * the offsets from this table, with the Metal Shading Language's own layout
 * rules, rather than by writing a struct twice -- once in MSL, once in C --
 * and hoping. A struct written twice drifts the first time someone adds a
 * member to one copy, and the symptom is a shader reading every uniform after
 * that one from the wrong place: plausible numbers, wrong picture.
 */

#include "nv2a_backend.h"

#include <stdio.h>
#include <string.h>

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
};

#undef V
#undef F

/* The Metal backend keeps a bit per uniform in a 64-bit mask. */
typedef char nv2a_uniform_count_fits_a_mask[(NV2A_U_COUNT <= 64) ? 1 : -1];

/* In a struct: c[] has a buffer of its own and samplers are arguments. */
static int in_struct(int id)
{
    return id != NV2A_U_C && nv2a_uniform_desc[id].type != NV2A_UT_SAMPLER;
}

/* MSL sizes and alignments (Metal Shading Language specification, "Alignment
 * and Size of Vector Data Types" and "...Matrix Data Types"). */
static void msl_size(int type, uint32_t *size, uint32_t *align)
{
    switch (type) {
    case NV2A_UT_FLOAT: case NV2A_UT_INT: *size = 4;  *align = 4;  break;
    case NV2A_UT_VEC2:                    *size = 8;  *align = 8;  break;
    case NV2A_UT_VEC3: case NV2A_UT_VEC4: *size = 16; *align = 16; break;
    case NV2A_UT_MAT2:                    *size = 16; *align = 8;  break;
    default:                              *size = 0;  *align = 1;  break;
    }
}

static const char *msl_type(int type)
{
    switch (type) {
    case NV2A_UT_FLOAT: return "float";
    case NV2A_UT_INT:   return "int";
    case NV2A_UT_VEC2:  return "float2";
    case NV2A_UT_VEC3:  return "float3";
    case NV2A_UT_VEC4:  return "float4";
    case NV2A_UT_MAT2:  return "float2x2";
    default:            return "?";
    }
}

/* Offsets, worked out once: each member placed at the next multiple of its
 * alignment, in table order, which is the order the struct declares them. */
static uint32_t g_off[NV2A_U_COUNT], g_size[2];
static int g_laid_out;

static void lay_out(void)
{
    uint32_t pos[2] = { 0, 0 };
    int id;
    for (id = 0; id < NV2A_U_COUNT; id++) {
        const Nv2aUniformDesc *d = &nv2a_uniform_desc[id];
        uint32_t size, align;
        if (!in_struct(id)) continue;
        msl_size(d->type, &size, &align);
        pos[d->frag] = (pos[d->frag] + align - 1) & ~(align - 1);
        g_off[id] = pos[d->frag];
        pos[d->frag] += size * d->count;
    }
    /* A struct's size is a multiple of its largest member's alignment. */
    g_size[0] = (pos[0] + 15u) & ~15u;
    g_size[1] = (pos[1] + 15u) & ~15u;
    g_laid_out = 1;
}

int nv2a_uniform_msl_offset(int id, uint32_t *offset, uint32_t *stride)
{
    uint32_t size, align;
    if (id < 0 || id >= NV2A_U_COUNT || !in_struct(id)) return 0;
    if (!g_laid_out) lay_out();
    msl_size(nv2a_uniform_desc[id].type, &size, &align);
    if (offset) *offset = g_off[id];
    if (stride) *stride = size;
    return 1;
}

uint32_t nv2a_uniform_msl_struct_size(int frag)
{
    if (!g_laid_out) lay_out();
    return g_size[frag ? 1 : 0];
}

int nv2a_uniform_msl_struct(char *buf, int cap, int frag)
{
    int pos = 0, id, n;
    n = snprintf(buf, (size_t)cap, "struct %s {\n", frag ? "Nv2aFU" : "Nv2aVU");
    if (n < 0 || n >= cap) return 0;
    pos += n;
    for (id = 0; id < NV2A_U_COUNT; id++) {
        const Nv2aUniformDesc *d = &nv2a_uniform_desc[id];
        if (!in_struct(id) || d->frag != (frag ? 1 : 0)) continue;
        if (d->count > 1)
            n = snprintf(buf + pos, (size_t)(cap - pos), "    %s %s[%u];\n",
                         msl_type(d->type), d->name, (unsigned)d->count);
        else
            n = snprintf(buf + pos, (size_t)(cap - pos), "    %s %s;\n",
                         msl_type(d->type), d->name);
        if (n < 0 || n >= cap - pos) return 0;
        pos += n;
    }
    n = snprintf(buf + pos, (size_t)(cap - pos), "};\n");
    if (n < 0 || n >= cap - pos) return 0;
    return pos + n;
}

int nv2a_uniform_msl_defines(char *buf, int cap, int frag, const char *prefix)
{
    int pos = 0, id, n;
    if (cap > 0) buf[0] = 0;
    for (id = 0; id < NV2A_U_COUNT; id++) {
        const Nv2aUniformDesc *d = &nv2a_uniform_desc[id];
        if (!in_struct(id) || d->frag != (frag ? 1 : 0)) continue;
        n = snprintf(buf + pos, (size_t)(cap - pos), "#define %s %s.%s\n",
                     d->name, prefix, d->name);
        if (n < 0 || n >= cap - pos) return 0;
        pos += n;
    }
    return pos;
}
