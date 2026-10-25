/*
 * nv2a_vsh.c -- NV2A vertex program decoder and GLSL generator.
 *
 * The bit layout below is the one nouveau and xemu both use, and it is worth
 * saying why it is written out as a table rather than as shifts inline: the
 * fields are scattered across three of the four dwords, several straddle a
 * dword boundary (source C's register index is split 2 bits / 2 bits across
 * words 2 and 3), and every reference numbers the bits differently. A table
 * can be checked against a reference line by line; a page of shift-and-mask
 * cannot, and a single wrong offset silently produces a program that compiles
 * and draws the wrong shape.
 *
 * Word 0 carries nothing. That is not a mistake: the NV2A instruction is 128
 * bits of which the first 32 are unused, and a decoder that assumes otherwise
 * reads every field one dword early -- which is the failure mode this file
 * exists to avoid, so it is stated here rather than discovered later.
 */
#include "nv2a_vsh.h"
#include "nv2a_backend.h"
#include <stdlib.h>

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- field extraction --------------------------------------------------- */

typedef struct { uint8_t word, bit, len; } Field;

/*                             word  bit  len */
static const Field F_ILU        = { 1, 25, 3 };
static const Field F_MAC        = { 1, 21, 4 };
static const Field F_CONST      = { 1, 13, 8 };
static const Field F_V          = { 1,  9, 4 };

static const Field F_A_NEG      = { 1,  8, 1 };
static const Field F_A_SWZ_X    = { 1,  6, 2 };
static const Field F_A_SWZ_Y    = { 1,  4, 2 };
static const Field F_A_SWZ_Z    = { 1,  2, 2 };
static const Field F_A_SWZ_W    = { 1,  0, 2 };
static const Field F_A_R        = { 2, 28, 4 };
static const Field F_A_MUX      = { 2, 26, 2 };

static const Field F_B_NEG      = { 2, 25, 1 };
static const Field F_B_SWZ_X    = { 2, 23, 2 };
static const Field F_B_SWZ_Y    = { 2, 21, 2 };
static const Field F_B_SWZ_Z    = { 2, 19, 2 };
static const Field F_B_SWZ_W    = { 2, 17, 2 };
static const Field F_B_R        = { 2, 13, 4 };
static const Field F_B_MUX      = { 2, 11, 2 };

static const Field F_C_NEG      = { 2, 10, 1 };
static const Field F_C_SWZ_X    = { 2,  8, 2 };
static const Field F_C_SWZ_Y    = { 2,  6, 2 };
static const Field F_C_SWZ_Z    = { 2,  4, 2 };
static const Field F_C_SWZ_W    = { 2,  2, 2 };
static const Field F_C_R_HIGH   = { 2,  0, 2 };
static const Field F_C_R_LOW    = { 3, 30, 2 };
static const Field F_C_MUX      = { 3, 28, 2 };

static const Field F_OUT_MAC_MASK = { 3, 24, 4 };
static const Field F_OUT_R        = { 3, 20, 4 };
static const Field F_OUT_ILU_MASK = { 3, 16, 4 };
static const Field F_OUT_O_MASK   = { 3, 12, 4 };
static const Field F_OUT_ORB      = { 3, 11, 1 };   /* 1 = output register */
static const Field F_OUT_ADDRESS  = { 3,  3, 8 };
static const Field F_OUT_MUX      = { 3,  2, 1 };   /* 1 = value from ILU  */
static const Field F_A0X          = { 3,  1, 1 };
static const Field F_FINAL        = { 3,  0, 1 };

static uint32_t fld(const uint32_t *t, Field f)
{
    return (t[f.word] >> f.bit) & ((f.len == 32) ? 0xFFFFFFFFu
                                                 : ((1u << f.len) - 1u));
}

static void decode_src(const uint32_t *t, Nv2aSrc *s,
                       Field neg, Field x, Field y, Field z, Field w,
                       uint8_t mux, uint8_t reg)
{
    s->negate = (uint8_t)fld(t, neg);
    s->swz[0] = (uint8_t)fld(t, x);
    s->swz[1] = (uint8_t)fld(t, y);
    s->swz[2] = (uint8_t)fld(t, z);
    s->swz[3] = (uint8_t)fld(t, w);
    s->type   = mux;
    s->index  = reg;
}

int nv2a_vsh_decode(const uint32_t *tokens, int count, Nv2aVshProgram *out)
{
    int i, n = 0;

    memset(out, 0, sizeof *out);
    if (count > NV2A_VSH_MAX_INSNS)
        count = NV2A_VSH_MAX_INSNS;

    for (i = 0; i < count; i++) {
        const uint32_t *t = tokens + i * 4;
        Nv2aVshInsn *ins = &out->insns[n];
        uint32_t c_reg;

        memset(ins, 0, sizeof *ins);
        ins->mac = (Nv2aMacOp)fld(t, F_MAC);
        ins->ilu = (Nv2aIluOp)fld(t, F_ILU);

        ins->const_index = (int16_t)fld(t, F_CONST);
        ins->input_index = (uint8_t)fld(t, F_V);
        ins->rel_addr    = (uint8_t)fld(t, F_A0X);

        /* Source C's register index is split across two dwords, high part
         * first. Reassembling it the other way round yields a plausible but
         * wrong constant index, which is the kind of bug that survives a
         * review and shows up as one wrong matrix. */
        c_reg = (fld(t, F_C_R_HIGH) << 2) | fld(t, F_C_R_LOW);

        decode_src(t, &ins->src[0], F_A_NEG, F_A_SWZ_X, F_A_SWZ_Y, F_A_SWZ_Z,
                   F_A_SWZ_W, (uint8_t)fld(t, F_A_MUX), (uint8_t)fld(t, F_A_R));
        decode_src(t, &ins->src[1], F_B_NEG, F_B_SWZ_X, F_B_SWZ_Y, F_B_SWZ_Z,
                   F_B_SWZ_W, (uint8_t)fld(t, F_B_MUX), (uint8_t)fld(t, F_B_R));
        decode_src(t, &ins->src[2], F_C_NEG, F_C_SWZ_X, F_C_SWZ_Y, F_C_SWZ_Z,
                   F_C_SWZ_W, (uint8_t)fld(t, F_C_MUX), (uint8_t)c_reg);

        ins->mac_temp = (uint8_t)fld(t, F_OUT_R);
        ins->mac_mask = (uint8_t)fld(t, F_OUT_MAC_MASK);
        ins->ilu_mask = (uint8_t)fld(t, F_OUT_ILU_MASK);

        /* Paired issue: when both units are busy the ILU's temporary
         * destination is forced to R1 by the hardware, whatever the
         * instruction's destination field says. A decoder that honours the
         * field instead produces a program that is correct for every
         * instruction except the paired ones. */
        ins->ilu_temp = (ins->mac != NV2A_MAC_NOP && ins->ilu != NV2A_ILU_NOP)
                      ? 1u : (uint8_t)fld(t, F_OUT_R);

        ins->out_is_oreg  = (uint8_t)fld(t, F_OUT_ORB);
        ins->out_from_ilu = (uint8_t)fld(t, F_OUT_MUX);
        ins->out_reg      = (uint8_t)fld(t, F_OUT_ADDRESS);
        ins->out_mask     = (uint8_t)fld(t, F_OUT_O_MASK);
        ins->final        = (uint8_t)fld(t, F_FINAL);

        /* Which inputs the program actually reads decides the vertex layout
         * the backend has to bind. Count a v register only where a source
         * really selects the input bank. */
        {
            int k;
            for (k = 0; k < 3; k++)
                if (ins->src[k].type == NV2A_PARAM_V)
                    out->inputs_read |= (uint16_t)(1u << ins->input_index);
        }
        if (ins->out_is_oreg && ins->out_mask && ins->out_reg < 16)
            out->outputs_written |= (uint16_t)(1u << ins->out_reg);

        n++;
        if (ins->final)
            break;
    }
    out->length = n;
    return n;
}

/* ---- small string builder ----------------------------------------------- */

typedef struct { char *buf; int cap, pos; int overflow; } SB;

static void sb_add(SB *sb, const char *fmt, ...)
{
    va_list ap;
    int n, room = sb->cap - sb->pos;

    if (sb->overflow || room <= 1) { sb->overflow = 1; return; }
    va_start(ap, fmt);
    n = vsnprintf(sb->buf + sb->pos, (size_t)room, fmt, ap);
    va_end(ap);
    if (n < 0 || n >= room) { sb->overflow = 1; sb->pos = sb->cap - 1; return; }
    sb->pos += n;
}

/* ---- GLSL generation ---------------------------------------------------- */

static const char SWZ[4] = { 'x', 'y', 'z', 'w' };

static void emit_swizzle(SB *sb, const uint8_t s[4])
{
    if (s[0] == 0 && s[1] == 1 && s[2] == 2 && s[3] == 3)
        return;                              /* identity: leave it off */
    sb_add(sb, ".%c%c%c%c", SWZ[s[0]], SWZ[s[1]], SWZ[s[2]], SWZ[s[3]]);
}

static void emit_src(SB *sb, const Nv2aVshInsn *ins, int which)
{
    const Nv2aSrc *s = &ins->src[which];

    if (s->negate) sb_add(sb, "(-");
    switch (s->type) {
    case NV2A_PARAM_R:
        sb_add(sb, "%s", s->index == 12 ? "oPos" : "R");
        if (s->index != 12) sb_add(sb, "%u", s->index);
        break;
    case NV2A_PARAM_V:
        sb_add(sb, "v%u", ins->input_index);
        break;
    case NV2A_PARAM_C:
        /* c[] is indexed from the instruction's constant field, biased by
         * the address register when relative addressing is on. Clamping is
         * not decoration: an out-of-range index is undefined in GLSL and in
         * practice reads whatever follows the array. */
        if (ins->rel_addr)
            sb_add(sb, "c[clamp(A0 + %d, 0, %d)]", ins->const_index,
                   NV2A_VSH_NUM_CONSTS - 1);
        else
            /* Clamped, not masked. There are 192 constants, which is not a
             * power of two, so "& (192 - 1)" is a bit pattern rather than a
             * range check: it silently turns c[107] into c[43]. Every matrix
             * a title keeps in the upper bank is then read from the wrong
             * place, every position comes out as zero, and the frame is
             * black with no error anywhere. */
            sb_add(sb, "c[%d]",
                   ins->const_index < NV2A_VSH_NUM_CONSTS
                       ? ins->const_index : NV2A_VSH_NUM_CONSTS - 1);
        break;
    default:
        /* A source whose bank is "none" reads as zero on the hardware. */
        sb_add(sb, "vec4(0.0)");
        break;
    }
    emit_swizzle(sb, s->swz);
    if (s->negate) sb_add(sb, ")");
}

static void emit_mask(SB *sb, uint8_t mask)
{
    if (mask == 0xF) return;
    sb_add(sb, ".");
    if (mask & 8) sb_add(sb, "x");
    if (mask & 4) sb_add(sb, "y");
    if (mask & 2) sb_add(sb, "z");
    if (mask & 1) sb_add(sb, "w");
}

static int mask_width(uint8_t mask)
{
    return ((mask >> 3) & 1) + ((mask >> 2) & 1) + ((mask >> 1) & 1) + (mask & 1);
}

/* The destination name for a temporary write. R12 is the position register:
 * on the NV2A it is the same storage as oPos, so a program may write R12 and
 * read it back, and both have to land in one variable. */
static void emit_temp_name(SB *sb, uint8_t idx)
{
    if (idx == 12) sb_add(sb, "oPos");
    else           sb_add(sb, "R%u", idx);
}

static const char *OREG_NAME[16] = {
    "oPos", 0, 0, "oD0", "oD1", "oFog", "oPts", "oB0", "oB1",
    "oT0", "oT1", "oT2", "oT3", 0, 0, 0
};

/* One MAC operation as a vec4-valued GLSL expression. */
static void emit_mac_expr(SB *sb, const Nv2aVshInsn *ins)
{
    switch (ins->mac) {
    case NV2A_MAC_MOV: emit_src(sb, ins, 0); break;
    case NV2A_MAC_MUL:
        sb_add(sb, "("); emit_src(sb, ins, 0); sb_add(sb, " * ");
        emit_src(sb, ins, 1); sb_add(sb, ")");
        break;
    case NV2A_MAC_ADD:
        sb_add(sb, "("); emit_src(sb, ins, 0); sb_add(sb, " + ");
        emit_src(sb, ins, 2); sb_add(sb, ")");
        break;
    case NV2A_MAC_MAD:
        sb_add(sb, "("); emit_src(sb, ins, 0); sb_add(sb, " * ");
        emit_src(sb, ins, 1); sb_add(sb, " + "); emit_src(sb, ins, 2);
        sb_add(sb, ")");
        break;
    case NV2A_MAC_DP3:
        sb_add(sb, "vec4(dot("); emit_src(sb, ins, 0); sb_add(sb, ".xyz, ");
        emit_src(sb, ins, 1); sb_add(sb, ".xyz))");
        break;
    case NV2A_MAC_DPH:
        sb_add(sb, "vec4(dot("); emit_src(sb, ins, 0); sb_add(sb, ".xyz, ");
        emit_src(sb, ins, 1); sb_add(sb, ".xyz) + "); emit_src(sb, ins, 1);
        sb_add(sb, ".w)");
        break;
    case NV2A_MAC_DP4:
        sb_add(sb, "vec4(dot("); emit_src(sb, ins, 0); sb_add(sb, ", ");
        emit_src(sb, ins, 1); sb_add(sb, "))");
        break;
    case NV2A_MAC_DST:
        /* Distance vector: (1, d.y*e.y, d.z, e.w) -- the classic attenuation
         * helper, spelled out because GLSL has no equivalent. */
        sb_add(sb, "vec4(1.0, ");
        emit_src(sb, ins, 0); sb_add(sb, ".y * "); emit_src(sb, ins, 1);
        sb_add(sb, ".y, "); emit_src(sb, ins, 0); sb_add(sb, ".z, ");
        emit_src(sb, ins, 1); sb_add(sb, ".w)");
        break;
    case NV2A_MAC_MIN:
        sb_add(sb, "min("); emit_src(sb, ins, 0); sb_add(sb, ", ");
        emit_src(sb, ins, 1); sb_add(sb, ")");
        break;
    case NV2A_MAC_MAX:
        sb_add(sb, "max("); emit_src(sb, ins, 0); sb_add(sb, ", ");
        emit_src(sb, ins, 1); sb_add(sb, ")");
        break;
    case NV2A_MAC_SLT:
        sb_add(sb, "vec4(lessThan("); emit_src(sb, ins, 0); sb_add(sb, ", ");
        emit_src(sb, ins, 1); sb_add(sb, "))");
        break;
    case NV2A_MAC_SGE:
        sb_add(sb, "vec4(greaterThanEqual("); emit_src(sb, ins, 0);
        sb_add(sb, ", "); emit_src(sb, ins, 1); sb_add(sb, "))");
        break;
    default:
        sb_add(sb, "vec4(0.0)");
        break;
    }
}

/* One ILU operation. All of them are scalar in C.x and replicate. */
static void emit_ilu_expr(SB *sb, const Nv2aVshInsn *ins)
{
    switch (ins->ilu) {
    case NV2A_ILU_MOV: emit_src(sb, ins, 2); break;
    case NV2A_ILU_RCP:
        sb_add(sb, "vec4(1.0 / "); emit_src(sb, ins, 2); sb_add(sb, ".x)");
        break;
    case NV2A_ILU_RCC:
        /* Reciprocal clamped to the hardware's range, which is what makes it
         * different from RCP: a program relying on RCC not returning
         * infinity gets a NaN through the rest of the shader without it.
         * The range is a MAGNITUDE (nv_rcc in the preludes): this used to
         * clamp to +5.4e-20..+1.88e19, so a negative reciprocal -- the 1/w of
         * every vertex behind the camera, which is what the title's epilogue
         * takes RCC of -- came out as +5.4e-20, and the vertex was thrown to
         * the middle of the screen. */
        sb_add(sb, "vec4(nv_rcc("); emit_src(sb, ins, 2); sb_add(sb, ".x))");
        break;
    case NV2A_ILU_RSQ:
        sb_add(sb, "vec4(inversesqrt(max(abs("); emit_src(sb, ins, 2);
        sb_add(sb, ".x), 1e-30)))");
        break;
    case NV2A_ILU_EXP:
        sb_add(sb, "vec4(exp2("); emit_src(sb, ins, 2); sb_add(sb, ".x))");
        break;
    case NV2A_ILU_LOG:
        sb_add(sb, "vec4(log2(max(abs("); emit_src(sb, ins, 2);
        sb_add(sb, ".x), 1.17549435e-38)))");
        break;
    case NV2A_ILU_LIT:
        sb_add(sb, "nv_lit("); emit_src(sb, ins, 2); sb_add(sb, ")");
        break;
    default:
        sb_add(sb, "vec4(0.0)");
        break;
    }
}

static const char *GLSL_PRELUDE =
NV2A_GLSL_VERSION
"// NV2A vertex program, translated.\n"
"// OpenGL's clip volume puts z in [-w, w]; the console's, like D3D's, in\n"
"// [0, w]. The epilogue converts.\n"
"#define NV2A_CLIP_Z01 0\n"
"uniform vec4 c[192];\n"
"uniform vec4 vpScale;   // NV097_SET_VIEWPORT_SCALE\n"
"uniform vec2 zClip;        // NV097_SET_CLIP_MIN / _MAX\n"
"uniform vec4 vpOff;     // NV097_SET_VIEWPORT_OFFSET\n"
"uniform vec2 vpSurface; // render target size, for the NDC conversion\n"
"uniform int  posMode;   // how to read what the program wrote to oPos\n"
"uniform float wideShift; // widescreen: a framed 3D view moved toward the centre, pixels\n"
"uniform int  fogEnable; // NV097_SET_FOG_ENABLE\n"
"uniform int  fogMode;   // NV097_SET_FOG_MODE: 0 linear, 1 exp, 2 exp2, +4 the _ABS forms\n"
"uniform vec2 fogParam;  // NV097_SET_FOG_PARAMS 0 and 1\n"
"uniform vec4 fogPlane;  // NV097_SET_FOG_PLANE\n"
"uniform vec4 vpRect;    // the viewport on the surface: centre x, y, half width, half height (pixels)\n"
"out vec4 oD0;\n"
"out vec4 oD1;\n"
"out vec4 oT0;\n"
"out vec4 oT1;\n"
"out vec4 oT2;\n"
"out vec4 oT3;\n"
"out float oFogC;\n"
"\n"
"// The fog factor the combiner's FOG register carries in its alpha.\n"
"//\n"
"// What arrives is a fog DISTANCE, not a factor: a vertex program writes one\n"
"// to oFog (this title writes eye distance, in the hundreds and thousands),\n"
"// and the fixed-function transform makes one as NV097_SET_FOG_GEN_MODE\n"
"// says. The hardware turns it into a factor with the mode and the two\n"
"// parameters the title set (xemu, hw/xbox/nv2a/pgraph/glsl/vsh.c); for\n"
"// linear fog the parameters are 1 + end / (end - start) and\n"
"// -1 / (end - start), so a distance of zero gives 1.0 and keeps the\n"
"// fragment, and one past the end gives 0.0 and paints it in the fog colour.\n"
"//\n"
"// It stays unclamped here: the fragment stage clamps it after\n"
"// interpolation, as the hardware does, so a triangle reaching past the end\n"
"// of the fog still shades evenly across. Fog switched off is a factor of\n"
"// 1.0 -- the fragment as it is.\n"
"float nv_fog_factor(float d, int en, int mode, vec2 p) {\n"
"    if (en == 0) return 1.0;\n"
"    float f;\n"
"    int m = mode & 3;\n"
"    if (m == 0) {          // linear: (end - d) / (end - start)\n"
"        if (isinf(d)) d = 0.0;\n"
"        f = p.x + d * p.y - 1.0;\n"
"    } else if (m == 1) {   // exp: 1 / e^(d * density)\n"
"        if (mode < 4 && isinf(d)) d = 0.0;\n"
"        f = p.x + exp2(d * p.y * 16.0) - 1.5;\n"
"    } else {               // exp2: 1 / e^((d * density)^2)\n"
"        f = p.x + exp2(-d * d * p.y * p.y * 32.0) - 1.5;\n"
"    }\n"
"    return mode >= 4 ? abs(f) : f;\n"
"}\n"
"float nv_fog(float coord) {\n"
"    return nv_fog_factor(coord, fogEnable, fogMode, fogParam);\n"
"}\n"
"\n"
"// The NV2A's LIT, which differs from the GL fixed-function one in the\n"
"// clamping of the specular exponent -- a program that relies on the clamp\n"
"// produces a black highlight without it rather than a wrong one.\n"
"vec4 nv_lit(vec4 src) {\n"
"    float d = src.x, s = src.y, p = clamp(src.w, -127.9961, 127.9961);\n"
"    float spec = (d > 0.0 && s > 0.0) ? exp2(p * log2(max(s, 1e-30))) : 0.0;\n"
"    return vec4(1.0, max(d, 0.0), spec, 1.0);\n"
"}\n"
"\n"
"// The NV2A's range for a reciprocal: a magnitude from 2^-64 to 2^64, with the\n"
"// sign KEPT (+0 counts as positive, -0 as negative). RCC is 1/x held to it,\n"
"// and the hardware holds a vertex's w to it before the rasteriser sees it\n"
"// (xemu: clampAwayZeroInf). Behind the camera w is negative, and so is the\n"
"// 1/w the title's own epilogue takes with RCC; a range of +2^-64..+2^64\n"
"// turned every such 1/w into +2^-64, and every vertex behind the camera of\n"
"// a character, a car or a pedestrian into a point in the middle of the\n"
"// screen -- the triangles from it to the visible ones are the shards.\n"
"float nv_away0(float t) {\n"
"    if (t > 0.0 || floatBitsToUint(t) == 0u) return clamp(t, 5.421010862e-20, 1.844674407e19);\n"
"    return clamp(t, -1.844674407e19, -5.421010862e-20);\n"
"}\n"
"float nv_rcc(float x) { return nv_away0(1.0 / x); }\n";

/*
 * Reversed depth: set by the OpenGL backend once clip control gives it the
 * console's 0..w depth range, before any program is emitted (nv2a_gl.c,
 * revz_start), unless RECOMP_GL_NOREVZ is set. Metal never sets it.
 *
 * The title layers decals and its menus a step or two of the 24-bit range
 * apart, near the far end, with LEQUAL and no polygon offset. Converted to
 * 0..1, then -1..1 and back, a depth there passes through floats at their
 * coarsest and moves by up to two steps, so the depth test settles those ties
 * by rounding. Counted down from the far end instead -- the far end of the
 * clip range minus the screen z -- the subtraction is exact and the result
 * small, where floats are finest. Nearer is then larger: the backend turns
 * every comparison, the clear and the polygon offset round to match.
 */
static int g_revz;
void nv2a_glsl_set_revz(int on) { g_revz = on; }
int  nv2a_glsl_revz(void) { return g_revz; }

/*
 * Precise depth (with reversed depth, unless RECOMP_GL_NOPRECZ is set): the
 * fixed-function depth without float32's rounding at the far end.
 *
 * The composite matrix has the 0..16777215 depth scale folded in, so a wall
 * near the far end has a z of about three billion before the divide, where
 * a float's step is 256: summed on the GPU and divided, its screen z lands
 * up to a depth step or two off, differently every frame as the camera
 * moves. That is the distant flicker reversed depth alone left. Counted
 * down from the far end the same depth is (far * w - z) / (range * w). The
 * backend works out that row, (far * row w - row z) / range, in double once
 * a draw, where the cancellation is exact, and hands it over as ffRevZ, so
 * the shader's own sum is of small numbers and its error a few thousandths
 * of a step (tests/gl_backend/test_revz.c).
 */
static int g_precz;
void nv2a_glsl_set_precz(int on) { g_precz = on; }
int  nv2a_glsl_precz(void) { return g_precz; }

/* The reversed 0..1 depth from the clip range, or the viewport z when the
 * title set no range: the forward form's 1 - z, done without the 1. */
static const char REVZ_FROM_RANGE[] =
    "    ndc.z = (zClip.y > zClip.x)\n"
    "          ? (zClip.y - oPos.z) / (zClip.y - zClip.x)\n"
    "          : (vpOff.z + (vpScale.z != 0.0 ? vpScale.z : 1.0) - oPos.z)\n"
    "            / (vpScale.z != 0.0 ? vpScale.z : 1.0);\n";

/*
 * The driver's nudge, taken off both paths alike.
 *
 * The console's D3D driver adds 0.53125 of a pixel to every screen position
 * it hands the hardware: through the viewport offset register for a
 * fixed-function draw (0.53125, or a viewport's corner plus 0.53125), and
 * through the constants for a vertex program (320.53, 240.53 for a whole
 * 640 by 480 viewport, and the 0.53125 its pass-through program for
 * pre-transformed vertices adds). D3D puts a pixel's centre at a whole
 * number and the console's rasteriser at a half, and the nudge turns one
 * into the other. The 1/32 over the half is half a sixteenth: the
 * rasteriser truncates positions to sixteenths of a pixel (xemu,
 * hw/xbox/nv2a/pgraph/glsl/vsh.c, roundScreenCoords), and with it they
 * round instead.
 *
 * The two paths have to agree. The vertex programs' viewport took the nudge
 * off and the fixed-function offset kept it, which put every program's draw
 * half a pixel up and left of the fixed-function world; on a wall seen at
 * an angle half a pixel is many depth steps, and the garage's graffiti
 * tiles, drawn by a program onto fixed-function walls at the wall's own
 * depth, fell behind them (the M tile at the end of the Create your own
 * graffiti pan: 17 steps in front of its wall with the two agreeing, 3 to
 * 5 behind with them half a pixel apart).
 *
 * And it comes off rather than staying on, because this renderer draws at
 * several times the console's resolution. Kept, the nudge puts a title's
 * whole pixels, where its 2D starts and stops, half a 640 by 480 pixel in:
 * a cutscene's letterbox bar, drawn from x = 0 and y = 0, left a pixel and
 * a half bare along the top and left of the screen at 3x. Off, whole pixels
 * are pixel edges at every scale, so 2D drawn edge to edge covers the
 * screen. What moves is the picture as a whole, half a pixel up and left
 * (tests/gl_backend/test_nudge.c).
 */
void nv2a_vsh_ff_vpoff(const float reg[4], float out[4])
{
    out[0] = reg[0] - NV2A_D3D_NUDGE;
    out[1] = reg[1] - NV2A_D3D_NUDGE;
    out[2] = reg[2];
    out[3] = reg[3];
}

/* Where the viewport is on the surface, for a vertex program's screen-space
 * oPos (the mode-0 epilogue below): its centre, the offset register less the
 * nudge -- exactly half the surface for a whole-surface viewport -- and its
 * half size. A viewport with no size is the whole surface. */
void nv2a_vsh_vprect(const float off[4], const float scale[4],
                     float surf_w, float surf_h, float out[4])
{
    out[0] = off[0] - NV2A_D3D_NUDGE;
    out[1] = off[1] - NV2A_D3D_NUDGE;
    out[2] = fabsf(scale[0]);
    out[3] = fabsf(scale[1]);
    if (out[2] < 1e-6f || out[3] < 1e-6f) {
        out[0] = surf_w * 0.5f; out[1] = surf_h * 0.5f;
        out[2] = surf_w * 0.5f; out[3] = surf_h * 0.5f;
    }
}

/*
 * Screen space back to clip space.
 *
 * Shared by the translated programs and by the fixed-function emitter: the
 * console applies the viewport transform in hardware for fixed-function
 * draws and inside the shader's own epilogue for programmable ones, so both
 * arrive here having written oPos in the same space, and one conversion
 * serves them both.
 */
static void emit_viewport_epilogue(SB *sb, int ff)
{
    sb_add(sb,
        "\n"
        "    vec4 ndc;\n");
    if (ff) {
        /*
         * The fixed-function half, done the way the hardware does it (xemu,
         * hw/xbox/nv2a/pgraph/glsl/vsh-ff.c):
         *
         *     oPos = position * composite;  oPos.xy /= oPos.w;
         *     oPos.xy += c[VPOFF].xy;
         *     oPos.xy = (2 * oPos.xy - surfaceSize) / surfaceSize;
         *
         * The composite matrix D3D uploads already carries the viewport
         * scale AND the screen centre, so the divide lands in 0..640 by
         * 0..480 pixels; the viewport OFFSET register, which the driver sets
         * to (0.53125, 0.53125) for the duration of its fixed-function draws
         * and to (320.53, 240.53) for its vertex-program draws, is added on
         * top by the hardware and is only the half-pixel nudge here. The
         * body has added it already, with the nudge taken back off as the
         * vertex programs' viewport takes it off (nv2a_vsh_ff_vpoff, above).
         * Then back to clip space by the surface size, as the programmable
         * path in xemu does.
         *
         * Before this, the shared mode-0 conversion below subtracted that
         * (0.53, 0.53) offset and divided by the viewport scale -- which is
         * the inverse of the programmable path's (320.53, 240.53) viewport,
         * not of this one. Screen centre (320, 240) therefore landed at the
         * bottom-right CORNER of the frame: the whole fixed-function world --
         * every building, road and deck -- was drawn half a screen right and
         * half a screen down of where the title put it, while every vertex-
         * program draw -- every character, vehicle and pedestrian -- was
         * where it belonged. Pedestrians in the sky above the rooftops, a
         * bus in the air beside a deck that stops mid-span, and Beat
         * standing on nothing were all one displacement.
         */
        sb_add(sb,
            "    ndc.x = (oPos.x - wideShift) * 2.0 / vpSurface.x - 1.0;\n"
            "    ndc.y = 1.0 - oPos.y * 2.0 / vpSurface.y;\n");
        if (g_revz && g_precz)
            sb_add(sb, "    ndc.z = dot(v0, ffRevZ) / oPos.w;   // precise depth\n");
        else if (g_revz)
            sb_add(sb, "%s", REVZ_FROM_RANGE);
        else
            sb_add(sb,
            "    ndc.z = (zClip.y > zClip.x)\n"
            "          ? (oPos.z - zClip.x) / (zClip.y - zClip.x)\n"
            "          : (oPos.z - vpOff.z) / (vpScale.z != 0.0 ? vpScale.z : 1.0);\n"
            "#if !NV2A_CLIP_Z01\n"
            "    ndc.z = ndc.z * 2.0 - 1.0;   // D3D depth [0,1] -> GL [-1,1]\n"
            "#endif\n");
        sb_add(sb,
            "    ndc.w = oPos.w;\n"
            "    gl_Position = vec4(ndc.xyz * ndc.w, ndc.w);\n"
            "    oFogC = nv_fog(oFog.x);\n"
            "    gl_PointSize = max(oPts.x, 1.0);\n");
        return;
    }
    sb_add(sb,
        "    if (posMode == 1) {\n"
        "        // The program left clip space alone: hand it over as is,\n"
        "        // remapping depth only where the target API wants [-w,w].\n");
    if (g_revz)
        sb_add(sb,
        "        gl_Position = vec4(oPos.xy, oPos.w - oPos.z, oPos.w);\n");
    else
        sb_add(sb,
        "#if NV2A_CLIP_Z01\n"
        "        gl_Position = vec4(oPos.xyz, oPos.w);\n"
        "#else\n"
        "        gl_Position = vec4(oPos.xy, 2.0 * oPos.z - oPos.w, oPos.w);\n"
        "#endif\n");
    sb_add(sb,
        "        gl_Position.x -= wideShift * 2.0 / vpSurface.x * oPos.w;\n"
        "    } else if (posMode == 2) {\n"
        "        // Screen space in pixels, converted with the render target's\n"
        "        // own size rather than the viewport registers -- which a\n"
        "        // title may be using for its clip volume rather than for the\n"
        "        // transform its program already applied.\n"
        "        ndc.x = (oPos.x - wideShift) * 2.0 / vpSurface.x - 1.0;\n"
        "        ndc.y = 1.0 - oPos.y * 2.0 / vpSurface.y;\n");
    if (g_revz)
        sb_add(sb,
        "        ndc.z = (16777215.0 - oPos.z) / 16777215.0;\n");
    else
        sb_add(sb,
        "        ndc.z = oPos.z / 16777215.0;\n"
        "#if !NV2A_CLIP_Z01\n"
        "        ndc.z = ndc.z * 2.0 - 1.0;\n"
        "#endif\n");
    sb_add(sb,
        "        ndc.w = nv_away0(oPos.w);\n"
        "        gl_Position = vec4(ndc.xyz * ndc.w, ndc.w);\n"
        "    } else {\n"
        "    ndc.x = (oPos.x - wideShift - vpOff.x) / max(vpScale.x, 1e-9);\n"
        "    ndc.y = (oPos.y - vpOff.y) / (vpScale.y != 0.0 ? vpScale.y : 1.0);\n"
        "    // That is the position inside the viewport, -1..1 across it; the\n"
        "    // surface is drawn with one viewport covering all of it, so the\n"
        "    // viewport's own rectangle is put back where it is on the surface.\n"
        "    // A viewport that is the whole surface (every scene but a few)\n"
        "    // comes out unchanged. One that is not -- the character select's\n"
        "    // two preview windows, a split screen -- used to be stretched over\n"
        "    // the whole surface: its skaters, drawn by vertex programs, came out\n"
        "    // too big, low and to the right of the scenery around them, which\n"
        "    // the fixed-function path draws in surface pixels and got right.\n"
        "    ndc.x = (vpRect.x + ndc.x * vpRect.z) * 2.0 / vpSurface.x - 1.0;\n"
        "    ndc.y = 1.0 - (vpRect.y - ndc.y * vpRect.w) * 2.0 / vpSurface.y;\n"
        "    // Depth: the range the title actually asked for.\n"
        "    //\n"
        "    // NV097_SET_CLIP_MIN and _MAX give the screen-space z the\n"
        "    // viewport maps into, and this title sets them before nearly\n"
        "    // every batch. Assuming 0..16777215 instead was right only when\n"
        "    // the title happened to agree.\n"
        "    //\n"
        "    // Measured, though, this title does not agree either -- it\n"
        "    // leaves depth in D3D\'s 0..1 and never applies the viewport z\n"
        "    // scale, while declaring a clip range of 0..16777215. Dividing\n"
        "    // by that range then crushes every fragment in the scene to\n"
        "    // z = 0: the depth readback reports every pixel inside the\n"
        "    // first tenth, and a shader that paints gl_FragCoord.z comes\n"
        "    // back uniformly white at a linear stretch of that tenth. Two\n"
        "    // instruments, same answer. Its own projection matrix agrees:\n"
        "    // the z and w rows give z/w between 0 and 1 across the whole\n"
        "    // frustum, not 0 and sixteen million.\n"
        "    //\n"
        "    // With every surface at the same depth value, LESS stops\n"
        "    // meaning anything -- whichever draw reaches a pixel first\n"
        "    // keeps it and everything behind it in submission order is\n"
        "    // rejected, however far away it really is. NV2A_Z01 takes the\n"
        "    // program\'s depth as the 0..1 it already is.\n"
        "#ifdef NV2A_Z01\n"
        "    // The perspective divide, which oPos has not had yet.\n"
        "    //\n"
        "    // Taking oPos.z straight clipped the whole scene to black: the\n"
        "    // projection\'s z row is 0.113*z - 135.5 against a w row of\n"
        "    // 0.026*z, so oPos.z reaches forty at the far plane and only\n"
        "    // the ratio is the 0..1 depth. x and y are recovered from the\n"
        "    // viewport registers, which is the same divide wearing a\n"
        "    // different hat; z has no viewport scale to recover it with,\n"
        "    // because this title never applied one.\n");
    if (g_revz)
        sb_add(sb,
        "    ndc.z = (nv_away0(oPos.w) - oPos.z) / nv_away0(oPos.w);\n"
        "#else\n"
        "%s"
        "#endif\n", REVZ_FROM_RANGE);
    else
        sb_add(sb,
        "    ndc.z = oPos.z / nv_away0(oPos.w);\n"
        "#else\n"
        "    ndc.z = (zClip.y > zClip.x)\n"
        "          ? (oPos.z - zClip.x) / (zClip.y - zClip.x)\n"
        "          : (oPos.z - vpOff.z) / (vpScale.z != 0.0 ? vpScale.z : 1.0);\n"
        "#endif\n"
        "#if !NV2A_CLIP_Z01\n"
        "    ndc.z = ndc.z * 2.0 - 1.0;   // D3D depth [0,1] -> GL [-1,1]\n"
        "#endif\n");
    sb_add(sb,
        "    // w held away from zero and infinity, as the hardware does: a w of\n"
        "    // exactly zero gave (0, 0, 0, 0), a vertex the rasteriser divides by\n"
        "    // nothing.\n"
        "    ndc.w = nv_away0(oPos.w);\n"
        "    gl_Position = vec4(ndc.xyz * ndc.w, ndc.w);\n"
        "    }\n"
        "    oFogC = nv_fog(oFog.x);\n"
        "    gl_PointSize = max(oPts.x, 1.0);\n");
}

/*
 * The translated program itself: local state, the instruction stream, and the
 * viewport epilogue.
 *
 * Shared verbatim by the GLSL and Metal emitters. The two languages differ in
 * how a shader declares its inputs, outputs and constants -- and in nothing
 * else that appears here, because both are C-like and agree on dot, clamp,
 * mix, floor, fract, exp2 and log2. Keeping one body generator is not tidiness:
 * a title with a thousand distinct programs cannot be checked by eye, so two
 * copies would differ silently and only on the programs nobody looked at.
 *
 * The caller supplies, in scope and by these names: v0..v15 for the inputs it
 * reads, `c` indexable to 191, vpScale, vpOff, vpSurface, posMode, the
 * varyings oD0, oD1, oT0..oT3, oFogC, and gl_Position / gl_PointSize.
 */
static void emit_program_body(SB *sb, const Nv2aVshProgram *p)
{
    int i;
    for (i = 0; i < NV2A_VSH_NUM_TEMPS - 1; i++)
        sb_add(sb, "    vec4 R%d = vec4(0.0);\n", i);
    sb_add(sb,
        "    vec4 oPos = vec4(0.0, 0.0, 0.0, 1.0);\n"
        /* Fog defaults to NONE, not to full fog.
         *
         * oFog.x is a fog distance, which nv_fog() turns into the alpha of
         * the combiner's FOG register, where 1.0 means "keep the fragment"
         * and 0.0 means "replace it with the fog colour". A program that does
         * not write the register -- every character program in this title --
         * must come out unfogged: a distance of zero or one does, in every
         * fog mode the title uses (back when the register was handed to the
         * combiner as a factor, a zero here painted the skaters entirely in
         * the fog colour). A program that writes the register overrides this
         * on the very next line. */
        "    vec4 oFog = vec4(1.0);\n"
        "    vec4 oPts = vec4(1.0);\n"
        "    vec4 oB0 = vec4(0.0);\n"
        "    vec4 oB1 = vec4(0.0);\n"
        "    int  A0 = 0;\n"
        "    vec4 mac, ilu;\n");
    /* Outputs the program never writes still have to be defined: a varying
     * left undefined is not zero, it is whatever the last draw left in the
     * register, which shows up as colours flickering between frames. */
    sb_add(sb,
        "    oD0 = vec4(1.0); oD1 = vec4(0.0);\n"
        "    oT0 = vec4(0.0); oT1 = vec4(0.0);\n"
        "    oT2 = vec4(0.0); oT3 = vec4(0.0);\n"
        "    oFogC = 1.0;\n\n");

    for (i = 0; i < p->length; i++) {
        const Nv2aVshInsn *ins = &p->insns[i];
        int have_mac = (ins->mac != NV2A_MAC_NOP);
        int have_ilu = (ins->ilu != NV2A_ILU_NOP);

        if (!have_mac && !have_ilu)
            continue;
        sb_add(sb, "    // slot %d\n", i);

        /* Evaluate both units before either writes back. On the hardware they
         * issue together and read the same register file; doing it in two
         * steps is what makes "MUL R0, R0, c[0]" paired with an ILU reading
         * R0 come out the same here as on the console. */
        if (have_mac) {
            sb_add(sb, "    mac = "); emit_mac_expr(sb, ins);
            sb_add(sb, ";\n");
        }
        if (have_ilu) {
            sb_add(sb, "    ilu = "); emit_ilu_expr(sb, ins);
            sb_add(sb, ";\n");
        }

        if (have_mac && ins->mac == NV2A_MAC_ARL) {
            /* floor, nudged up by a thousandth as xemu does it
             * (vsh-prog.c, _ARL): an index computed as 3.0 by a title --
             * a bone number times the registers per bone -- arrives as
             * 2.9999998 often enough, and a plain floor then reads the last
             * row of the previous bone's matrix with the first two of this
             * one's. That vertex lands somewhere else entirely. */
            sb_add(sb, "    A0 = int(floor(");
            emit_src(sb, ins, 0);
            sb_add(sb, ".x + 0.001));\n");
        } else if (have_mac && ins->mac_mask) {
            sb_add(sb, "    ");
            emit_temp_name(sb, ins->mac_temp);
            emit_mask(sb, ins->mac_mask);
            sb_add(sb, " = mac");
            if (ins->mac_mask != 0xF) {
                int w = mask_width(ins->mac_mask), k;
                sb_add(sb, ".");
                for (k = 0; k < 4; k++)
                    if (ins->mac_mask & (8 >> k)) sb_add(sb, "%c", SWZ[k]);
                (void)w;
            }
            sb_add(sb, ";\n");
        }

        if (have_ilu && ins->ilu_mask) {
            sb_add(sb, "    ");
            emit_temp_name(sb, ins->ilu_temp);
            emit_mask(sb, ins->ilu_mask);
            sb_add(sb, " = ilu");
            if (ins->ilu_mask != 0xF) {
                int k;
                sb_add(sb, ".");
                for (k = 0; k < 4; k++)
                    if (ins->ilu_mask & (8 >> k)) sb_add(sb, "%c", SWZ[k]);
            }
            sb_add(sb, ";\n");
        }

        if (ins->out_is_oreg && ins->out_mask && ins->out_reg < 16
         && OREG_NAME[ins->out_reg]) {
            const char *src = ins->out_from_ilu ? "ilu" : "mac";
            sb_add(sb, "    %s", OREG_NAME[ins->out_reg]);
            emit_mask(sb, ins->out_mask);
            sb_add(sb, " = %s", src);
            if (ins->out_mask != 0xF) {
                int k;
                sb_add(sb, ".");
                for (k = 0; k < 4; k++)
                    if (ins->out_mask & (8 >> k)) sb_add(sb, "%c", SWZ[k]);
            }
            sb_add(sb, ";\n");
        }
    }

    /*
     * The NV2A's viewport is applied inside the vertex program: a title
     * multiplies by its own projection and then by the viewport scale and
     * offset the driver gave it, so what the program produces is already in
     * screen coordinates -- pixels across, pixels down, and the depth range
     * the title asked for -- not clip space.
     *
     * OpenGL wants clip space, so undo it here rather than in the backend:
     * this keeps the whole convention in one place, and it means a draw with
     * a different viewport only has to change two uniforms.
     *
     * The .w is left alone deliberately. Position is still homogeneous at
     * this point and the perspective divide has not happened, so scaling xyz
     * against w is what puts the geometry back where the title meant it.
     */
    emit_viewport_epilogue(sb, 0);
}


/* ---- fixed-function transform ------------------------------------------ */

/*
 * The NV2A's hardware T&L unit, as a shader.
 *
 * D3D8's SetVertexShader takes either a compiled program or an FVF code, and
 * an FVF draw does not run a program at all -- it runs the fixed pipeline,
 * selected by NV097_SET_TRANSFORM_EXECUTION_MODE. JSRF submits more than half
 * its geometry that way: measured over one title screen, 157,700 fixed-
 * function batches against 139,340 programmable ones.
 *
 * Nothing here read that register before, so every one of those batches was
 * drawn with whichever program happened to be loaded last -- usually the
 * driver's own pre-transformed-vertex passthrough, which multiplies position
 * by a screen-space scale and passes the texture coordinates straight
 * through. Fed world-space vertices, it scattered the scenery a thousand
 * units off-screen; what stayed visible was one enormous triangle per
 * surface, sampling a few texels' worth of its texture across the whole
 * frame. That is the flat backdrop.
 *
 * The composite matrix arrives by its own method rather than through the
 * constant file, and the hardware mirrors it into constants 0..3 -- the same
 * arrangement as the viewport registers at 58 and 59. Reading it from there
 * keeps this shader's inputs identical in kind to a translated program's.
 *
 * That matrix already has the viewport folded into it. One vertex read out
 * of a real batch settles it: its transformed z over w comes to 16,777,215
 * exactly -- the full depth range, which is what NV097_SET_VIEWPORT_SCALE
 * holds -- so the driver multiplied the viewport in rather than leaving it
 * to the hardware, and x and y land in pixels for the same reason. Applying
 * the scale a second time here multiplied y by -240, which is a vertical
 * flip, and the first textured frame came out upside down.
 */
static void msl_vertex_head(SB *sb, uint16_t inputs_read);
static void msl_vertex_tail(SB *sb);

static int emit_ff(const Nv2aVshFixed *f, char *buf, int bufsize, int msl)
{
    SB sb = { buf, bufsize, 0, 0 };
    int i, j;
    /* Generated texture coordinates (NV097_SET_TEXGEN_*): what they are made
     * from -- the eye-space position, the eye-space normal, or both. */
    int tg_eye = 0, tg_nrm = 0, tg_sph = 0;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++)
            switch (f->texgen[i][j]) {
            case NV2A_TEXGEN_SPHERE_MAP:     tg_sph = 1; /* fall through */
            case NV2A_TEXGEN_REFLECTION_MAP: tg_eye = 1; tg_nrm = 1; break;
            case NV2A_TEXGEN_NORMAL_MAP:     tg_nrm = 1; break;
            default: break;
            }

    if (msl) {
        msl_vertex_head(&sb, f->inputs_read);
    } else {
    sb_add(&sb, "%s", GLSL_PRELUDE);
    for (i = 0; i < NV2A_VSH_NUM_INPUTS; i++)
        if (f->inputs_read & (1u << i))
            sb_add(&sb, "layout(location = %d) in vec4 v%d;\n", i, i);

    if (f->lit)
        sb_add(&sb,
            "\n// The T&L unit's lighting state (xemu vsh-ff.c reads the same from\n"
            "// its lighting context).\n"
            "uniform vec4 ffMV[4];       // NV097_SET_MODEL_VIEW_MATRIX 0, as sent\n"
            "uniform vec4 ffIMV[4];      // NV097_SET_INVERSE_MODEL_VIEW_MATRIX 0\n"
            "uniform vec3 ltSceneAmb;    // NV097_SET_SCENE_AMBIENT_COLOR\n"
            "uniform vec3 ltMatEm;       // NV097_SET_MATERIAL_EMISSION\n"
            "uniform float ltMatAlpha;   // NV097_SET_MATERIAL_ALPHA\n"
            "uniform vec3 ltAmb[8];      // NV097_SET_LIGHT_AMBIENT_COLOR\n"
            "uniform vec3 ltDif[8];      // NV097_SET_LIGHT_DIFFUSE_COLOR\n"
            "uniform vec3 ltSpc[8];      // NV097_SET_LIGHT_SPECULAR_COLOR\n"
            "uniform vec3 ltDir[8];      // NV097_SET_LIGHT_INFINITE_DIRECTION\n"
            "uniform vec3 ltHalf[8];     // NV097_SET_LIGHT_INFINITE_HALF_VECTOR\n"
            "uniform vec3 ltPos[8];      // NV097_SET_LIGHT_LOCAL_POSITION\n"
            "uniform vec3 ltAtt[8];      // NV097_SET_LIGHT_LOCAL_ATTENUATION\n"
            "uniform float ltRange[8];   // NV097_SET_LIGHT_LOCAL_RANGE\n"
            "uniform vec4 ltSpotDir[8];  // NV097_SET_LIGHT_SPOT_DIRECTION\n"
            "uniform vec4 ltEye;         // NV097_SET_EYE_POSITION\n"
            "uniform float ltSpecPow;    // specular exponent\n");
    if (!f->lit && (f->fog_gen == 1 || f->fog_gen == 2 || f->fog_gen == 3))
        sb_add(&sb,
            "\n// For the fog distance, which is measured in eye space.\n"
            "uniform vec4 ffMV[4];       // NV097_SET_MODEL_VIEW_MATRIX 0, as sent\n");
    if (!f->lit && (tg_eye || tg_nrm)) {
        sb_add(&sb, "\n// For the generated texture coordinates, made in eye space.\n");
        if (tg_eye && !(f->fog_gen == 1 || f->fog_gen == 2 || f->fog_gen == 3))
            sb_add(&sb, "uniform vec4 ffMV[4];       // NV097_SET_MODEL_VIEW_MATRIX 0, as sent\n");
        if (tg_nrm)
            sb_add(&sb, "uniform vec4 ffIMV[4];      // NV097_SET_INVERSE_MODEL_VIEW_MATRIX 0\n");
    }
    if (g_revz && g_precz)
        sb_add(&sb, "\nuniform vec4 ffRevZ;        // the reversed depth row (precise depth)\n");
    sb_add(&sb,
        "\nuniform vec4 ffMat[4];      // NV097_SET_COMPOSITE_MATRIX\n"
        "uniform vec4 ffVpOff;       // NV097_SET_VIEWPORT_OFFSET less the driver's nudge\n"
        "uniform float litAmbient;   // stand-in for the T&L unit's lighting\n"
        "uniform vec4 ffTexMat[16];  // NV097_SET_TEXTURE_MATRIX, 4 stages\n"
        "\nvoid main() {\n");
    }
    sb_add(&sb,
        "    vec4 oPos, oFog = vec4(0.0), oPts = vec4(1.0);\n"
        "    // Composite matrix: model x view x projection.\n"
        "    //\n"
        "    // Its own uniform, not the constant file. The hardware mirrors\n"
        "    // it into the low end of that file, and copying that faithfully\n"
        "    // destroyed the constants this title's own vertex programs read\n"
        "    // -- it writes them from c[0] upward. Every fixed-function draw\n"
        "    // was overwriting the next programmable draw's transform, which\n"
        "    // is why the traffic and the characters hung in mid-air while\n"
        "    // the city they belonged to was in the right place.\n"
        "    vec4 p = vec4(dot(v0, ffMat[0]), dot(v0, ffMat[1]),\n"
        "                  dot(v0, ffMat[2]), dot(v0, ffMat[3]));\n"
        "    // The viewport is already folded into the composite matrix --\n"
        "    // the driver multiplies it in rather than leaving it to the\n"
        "    // hardware -- so the divide by w lands straight in screen\n"
        "    // pixels, and the shared epilogue takes it from there.\n"
        "    // w held away from zero as the hardware holds it (nv_away0). A w of\n"
        "    // exactly zero used to put the vertex at the top-left corner.\n"
        "    float w = nv_away0(p.w);\n"
        "    float rhw = 1.0 / w;\n"
        "    // The hardware adds the viewport offset register after the\n"
        "    // divide (xemu vsh-ff.c); the epilogue then un-screens by the\n"
        "    // surface size. w stays the true clip w so the rasteriser\n"
        "    // interpolates texture coordinates perspective-correctly --\n"
        "    // handing it 1/w inverted the correction on every world polygon.\n"
        "    oPos.xy = p.xy * rhw + ffVpOff.xy;\n"
        "    oPos.z = p.z * rhw;\n"
        "    oPos.w = w;\n");

    /* Colour. Lighting is not emulated yet; a title that pre-bakes its
     * shading into the vertex colours -- which a cel-shaded one does -- is
     * correct either way, and one that does not comes out evenly lit rather
     * than black. */
    /*
     * Colour.
     *
     * Nothing here lights anything. A title that bakes its shading into the
     * vertex colours -- which a cel-shaded one does -- comes out right either
     * way; a title that asked the hardware to light the vertices does not,
     * because the stream colour is then only the material and the sun is
     * missing. This scene renders about two and a half times darker than the
     * reference and the title does enable lighting for some batches, so which
     * of the two this is has to be decided rather than assumed.
     */
    if (!f->lit) {
        sb_add(&sb, !f->has_diffuse ? "    oD0 = vec4(1.0);\n" : "    oD0 = v3;\n");
        sb_add(&sb, f->has_specular ? "    oD1 = v4;\n" : "    oD1 = vec4(0.0);\n");
    } else {
        /*
         * The T&L unit's lighting, as the hardware does it (the model and
         * the register roles follow xemu's vsh-ff.c).
         *
         * This used to pass the stream colour through, or white when the
         * batch had none -- and a batch with no colour of its own is exactly
         * the one that gets all of it from the lighting. This title's pause
         * map is drawn that way from end to end: one infinite light facing
         * the viewer, whose DIFFUSE colour the title sets per element --
         * teal districts, grey outlines, orange graffiti marks with dark red
         * rims -- and every one of them came out white.
         *
         * Register roles: the driver folds the material into these. With the
         * ambient taken from the material, "scene ambient" holds the whole
         * constant term; with it taken from a vertex colour, "material
         * emission" holds the scene ambient that colour is scaled by. The
         * light colours arrive premultiplied by the material likewise.
         */
        static const char *src_rgb[4] = { "ltSceneAmb", "inD.rgb", "inS.rgb", "ltSceneAmb" };
        static const char *src_mod[4] = { "vec3(1.0)", "inD.rgb", "inS.rgb", "vec3(1.0)" };
        static const char *src_a[4]   = { "ltMatAlpha", "inD.a", "inS.a", "ltMatAlpha" };
        unsigned em = f->colmat & 3, am = (f->colmat >> 2) & 3,
                 dm = (f->colmat >> 4) & 3, sm = (f->colmat >> 6) & 3;
        int li;
        sb_add(&sb, "    vec4 inD = %s;\n", f->has_diffuse ? "v3" : "vec4(1.0)");
        sb_add(&sb, "    vec4 inS = %s;\n", f->has_specular ? "v4" : "vec4(0.0)");
        sb_add(&sb, "    vec3 nrm = %s;\n", f->has_normal ? "v2.xyz" : "vec3(0.0, 0.0, 1.0)");
        sb_add(&sb,
            "    vec4 tPosition = vec4(dot(v0, ffMV[0]), dot(v0, ffMV[1]),\n"
            "                          dot(v0, ffMV[2]), dot(v0, ffMV[3]));\n"
            "    vec4 n4 = vec4(nrm, 0.0);\n"
            "    vec3 tNormal = vec3(dot(n4, ffIMV[0]), dot(n4, ffIMV[1]), dot(n4, ffIMV[2]));\n");
        if (f->lflags & 8) sb_add(&sb, "    tNormal = normalize(tNormal);\n");
        if (f->lflags & 2)
            sb_add(&sb, "    vec3 VPeye = normalize(ltEye.xyz / ltEye.w - tPosition.xyz / tPosition.w);\n");
        sb_add(&sb, "    oD0 = vec4(%s, %s);\n", src_rgb[am], src_a[dm]);
        sb_add(&sb, "    oD0.rgb *= ltMatEm;\n");
        sb_add(&sb, "    oD0.rgb += %s;\n", src_rgb[em]);
        sb_add(&sb, "    oD1 = vec4(0.0, 0.0, 0.0, inS.a);\n");
        for (li = 0; li < 8; li++) {
            unsigned mode = (f->light_mask >> (2 * li)) & 3;
            if (!mode) continue;
            sb_add(&sb, "    { // light %d, %s\n", li,
                   mode == 1 ? "infinite" : mode == 2 ? "local" : "spot");
            if (mode == 1) {
                sb_add(&sb,
                    "      float att = 1.0;\n"
                    "      vec3 L = normalize(ltDir[%d]);\n"
                    "      float nDotVP = max(0.0, dot(tNormal, L));\n", li);
                if (f->lflags & 2)
                    sb_add(&sb, "      float nDotHV = max(0.0, dot(tNormal, normalize(L + VPeye)));\n");
                else
                    sb_add(&sb, "      float nDotHV = max(0.0, dot(tNormal, ltHalf[%d]));\n", li);
                sb_add(&sb, "      {\n");
            } else {
                sb_add(&sb,
                    "      vec3 VP = ltPos[%d] - tPosition.xyz / tPosition.w;\n"
                    "      float d = length(VP);\n"
                    "      if (d <= ltRange[%d]) {\n"
                    "      VP = normalize(VP);\n"
                    "      float att = 1.0 / (ltAtt[%d].x + ltAtt[%d].y * d + ltAtt[%d].z * d * d);\n"
                    "      vec3 hv = normalize(VP + %s);\n"
                    "      float nDotVP = max(0.0, dot(tNormal, VP));\n"
                    "      float nDotHV = max(0.0, dot(tNormal, hv));\n",
                    li, li, li, li, li, (f->lflags & 2) ? "VPeye" : "vec3(0.0)");
                if (mode == 3)
                    sb_add(&sb,
                        "      vec4 sd = ltSpotDir[%d];\n"
                        "      float invScale = 1.0 / length(sd.xyz);\n"
                        "      float cosHalfPhi = -invScale * sd.w;\n"
                        "      float cosHalfTheta = invScale + cosHalfPhi;\n"
                        "      float sdDotVP = dot(sd.xyz, VP);\n"
                        "      float rho = invScale * sdDotVP;\n"
                        "      if (rho <= cosHalfPhi) att = 0.0;\n"
                        "      else if (rho <= cosHalfTheta) att *= sdDotVP + sd.w;\n", li);
            }
            sb_add(&sb,
                "      float pf = (nDotVP == 0.0 || nDotHV <= 0.0) ? 0.0 : pow(nDotHV, ltSpecPow);\n"
                "      oD0.rgb += ltAmb[%d] * att;\n"
                "      oD0.rgb += %s * ltDif[%d] * (att * nDotVP);\n"
                "      oD1.rgb += %s * ltSpc[%d] * (att * pf);\n"
                "      }\n"
                "    }\n", li, src_mod[dm], li, src_mod[sm], li);
        }
        if (!(f->lflags & 4))
            sb_add(&sb, "    oD1 = vec4(0.0, 0.0, 0.0, 1.0);\n");
        else if (!(f->lflags & 1))
            sb_add(&sb, "    oD0.rgb += oD1.rgb;\n    oD1 = inS;\n");
        sb_add(&sb, "    oD0 = clamp(oD0, 0.0, 1.0);\n    oD1 = clamp(oD1, 0.0, 1.0);\n");
    }

    /*
     * Generated texture coordinates, as the T&L unit makes them (xemu
     * vsh-ff.c, after OpenGL's texgen): per component, the eye-space normal
     * (NORMAL_MAP), the eye ray reflected about it (REFLECTION_MAP), or that
     * reflection folded onto a sphere map (SPHERE_MAP) -- and then the stage's
     * texture matrix, as for coordinates the vertex brings.
     *
     * None of it was done before: every one of these components was the
     * vertex's own texture coordinate instead. This title asks for the normal
     * on its first stage for a quarter of its batches -- a texture looked up
     * by the direction a surface faces -- and those batches sampled it at
     * whatever the vertex's own coordinates happened to be.
     *
     * The two linear modes, which dot the position with a plane, are not
     * made here: ff_describe hands their components over as the vertex's own.
     */
    if (tg_eye || tg_nrm) {
        if (!f->lit) {
            sb_add(&sb, "    // Generated texture coordinates are made in eye space.\n");
            if (tg_eye)
                sb_add(&sb,
                    "    vec4 tPosition = vec4(dot(v0, ffMV[0]), dot(v0, ffMV[1]),\n"
                    "                          dot(v0, ffMV[2]), dot(v0, ffMV[3]));\n");
            if (tg_nrm) {
                sb_add(&sb,
                    "    vec4 tgN4 = vec4(%s, 0.0);\n"
                    "    vec3 tNormal = vec3(dot(tgN4, ffIMV[0]), dot(tgN4, ffIMV[1]), dot(tgN4, ffIMV[2]));\n",
                    f->has_normal ? "v2.xyz" : "vec3(0.0, 0.0, 1.0)");
                if (f->tg_normalize) sb_add(&sb, "    tNormal = normalize(tNormal);\n");
            }
        }
        if (tg_eye)
            sb_add(&sb, "    vec3 tgR = reflect(normalize(tPosition.xyz), tNormal);\n");
        if (tg_sph)
            sb_add(&sb, "    float tgM = 1.0 / (2.0 * length(tgR + vec3(0.0, 0.0, 1.0)));\n");
    }
    for (i = 0; i < 4; i++) {
        const char *o[4] = { "oT0", "oT1", "oT2", "oT3" };
        int v = 9 + i;
        int tg = f->texgen[i][0] | f->texgen[i][1] | f->texgen[i][2] | f->texgen[i][3];
        if (tg) {
            /* The vertex's own coordinate for the components left to it --
             * the attribute's latched value when the batch has no array of
             * them, which ff_describe arranges. */
            if (f->inputs_read & (1u << v))
                sb_add(&sb, "    %s = v%d;   // stage %d: partly generated\n", o[i], v, i);
            else
                sb_add(&sb, "    %s = vec4(0.0, 0.0, 0.0, 1.0);   // stage %d: generated\n", o[i], i);
            for (j = 0; j < 4; j++) {
                char c = "xyzw"[j];
                switch (f->texgen[i][j]) {
                case NV2A_TEXGEN_NORMAL_MAP:
                    if (j < 3) sb_add(&sb, "    %s.%c = tNormal.%c;\n", o[i], c, c);
                    break;
                case NV2A_TEXGEN_REFLECTION_MAP:
                    if (j < 3) sb_add(&sb, "    %s.%c = tgR.%c;\n", o[i], c, c);
                    break;
                case NV2A_TEXGEN_SPHERE_MAP:
                    if (j < 2) sb_add(&sb, "    %s.%c = tgR.%c * tgM + 0.5;\n", o[i], c, c);
                    break;
                default:
                    break;
                }
            }
            if (f->tex_matrix & (1u << i)) {
                int b = 4 * i;
                sb_add(&sb,
                    "    %s = vec4(dot(%s, ffTexMat[%d]), dot(%s, ffTexMat[%d]),\n"
                    "              dot(%s, ffTexMat[%d]), dot(%s, ffTexMat[%d]));\n",
                    o[i], o[i], b, o[i], b + 1, o[i], b + 2, o[i], b + 3);
            }
            continue;
        }
        if (!(f->inputs_read & (1u << v))) {
            sb_add(&sb, "    %s = vec4(0.0);\n", o[i]);
        } else if (f->tex_matrix & (1u << i)) {
            /* NV097_SET_TEXTURE_MATRIX, in its own uniform. */
            int b = 4 * i;
            sb_add(&sb,
                "    %s = vec4(dot(v%d, ffTexMat[%d]), dot(v%d, ffTexMat[%d]),\n"
                "              dot(v%d, ffTexMat[%d]), dot(v%d, ffTexMat[%d]));\n",
                o[i], v, b, v, b + 1, v, b + 2, v, b + 3);
        } else {
            sb_add(&sb, "    %s = v%d;\n", o[i], v);
        }
    }
    /*
     * The fog distance, made the way NV097_SET_FOG_GEN_MODE says (xemu
     * vsh-ff.c, after NV_fog_distance): the epilogue's nv_fog() turns it into
     * the factor with the title's mode and parameters, as for a program.
     *
     * This used to leave oFog at zero, which nv_fog() then took as the
     * factor itself: zero, every fixed-function vertex painted entirely in
     * the fog colour whenever the title switched fog on. This title draws
     * its scenery fixed-function and switches fog on for the night stages --
     * linear, from 300 to 3000 units, in black -- so in the Skyscraper
     * District and Pharaoh Park the whole city came out black around a
     * character drawn as it should be, and the stages with a coloured fog
     * came out as a flat wall of that colour.
     */
    if (f->fog_gen == 1 || f->fog_gen == 2 || f->fog_gen == 3)
        sb_add(&sb,
            "    vec4 fogEye = vec4(dot(v0, ffMV[0]), dot(v0, ffMV[1]),\n"
            "                       dot(v0, ffMV[2]), dot(v0, ffMV[3]));\n");
    switch (f->fog_gen) {
    case 0:     /* SPEC_ALPHA: the alpha of the batch's specular colour */
        sb_add(&sb, f->has_specular ? "    oFog.x = clamp(v4.a, 0.0, 1.0);\n"
                                    : "    oFog.x = 0.0;\n");
        break;
    case 1:     /* RADIAL: distance from the eye */
        sb_add(&sb, "    oFog.x = length(fogEye.xyz);\n");
        break;
    case 2:     /* PLANAR: distance from a plane in eye space */
        sb_add(&sb, "    oFog.x = dot(fogPlane.xyz, fogEye.xyz) + fogPlane.w;\n");
        break;
    case 3:     /* ABS_PLANAR */
        sb_add(&sb, "    oFog.x = abs(dot(fogPlane.xyz, fogEye.xyz) + fogPlane.w);\n");
        break;
    case 6:     /* FOG_X: the batch's own fog coordinate */
        sb_add(&sb, (f->inputs_read & (1u << 5)) ? "    oFog.x = v5.x;\n"
                                                 : "    oFog.x = 0.0;\n");
        break;
    default:
        sb_add(&sb, "    oFog.x = 0.0;\n");
        break;
    }
    sb_add(&sb, "    oFogC = 1.0;\n");
    emit_viewport_epilogue(&sb, 1);
    if (msl) msl_vertex_tail(&sb);
    else     sb_add(&sb, "}\n");
    return sb.overflow ? 0 : sb.pos;
}

int nv2a_vsh_emit_ff_glsl(const Nv2aVshFixed *f, char *buf, int bufsize)
{
    return emit_ff(f, buf, bufsize, 0);
}

/* The same transform as a Metal vertex function: the body above, word for
 * word, inside the Metal head and tail below. Its constants come through the
 * same Nv2aVU struct a translated program's do -- the matrices have their own
 * members there and never touch c[], for the reason given above. */
int nv2a_vsh_emit_ff_msl(const Nv2aVshFixed *f, char *buf, int bufsize)
{
    return emit_ff(f, buf, bufsize, 1);
}


int nv2a_vsh_emit_glsl(const Nv2aVshProgram *p, char *buf, int bufsize)
{
    SB sb = { buf, bufsize, 0, 0 };
    int i;

    sb_add(&sb, "%s", GLSL_PRELUDE);

    /* Only the inputs the program reads. Declaring all sixteen would compile
     * but would make every draw look like it needs sixteen bound arrays. */
    for (i = 0; i < NV2A_VSH_NUM_INPUTS; i++)
        if (p->inputs_read & (1u << i))
            sb_add(&sb, "layout(location = %d) in vec4 v%d;\n", i, i);

    sb_add(&sb, "\nvoid main() {\n");
    emit_program_body(&sb, p);
    sb_add(&sb, "}\n");

    return sb.overflow ? 0 : sb.pos;
}


/* ---- Metal ------------------------------------------------------------- */

/*
 * The Metal Shading Language is C++, and the parts of it this translation
 * touches agree with GLSL on almost everything: dot, min, max, clamp, floor,
 * abs, exp2, log2, mix, fract, normalize, pow, and constructing a vector from
 * a scalar all spell and behave the same. What differs is the shape around
 * the code -- how a shader receives its inputs and constants, and how it
 * returns its outputs -- plus three function names.
 *
 * So a Metal vertex function here is the GLSL body, word for word, between a
 * head and a tail that make its names mean the same things:
 *
 *   - The uniforms are members of one struct (Nv2aVU, generated from the
 *     table in nv2a_uniforms.c) and a #define per member gives each one its
 *     bare GLSL name, so `ffMat[2]` in the body reads U.ffMat[2]. c[] is a
 *     buffer of its own, bound under the name c.
 *   - The inputs are fetched, not declared: vertex pulling from one buffer of
 *     packed floats through a table of offsets (Nv2aAttrTable), with one
 *     value for the whole draw for an attribute that has no array. That is
 *     exactly the OpenGL arrangement -- an enabled array or a generic
 *     attribute value -- without a pipeline per vertex layout.
 *   - The varyings are locals, gathered into Nv2aVshOut at the end.
 *
 * The alternative, a second copy of the emitters in the other dialect, was
 * tried: it fell behind the GLSL within a fortnight (no fog, no lighting, no
 * bump mapping) and nothing noticed, because nothing ran it. JSRF compiles
 * over a thousand distinct programs, no one reads them, and a divergence
 * surfaces as one wrong-looking object in one scene.
 */

/*
 * What crosses from the vertex stage to the fragment stage.
 *
 * Metal matches the two by the shape of this struct, so both shaders have to
 * declare it identically -- and the fragment emitter lives in another file.
 * One definition, handed out, is what stops a member added on one side from
 * silently shifting every varying on the other.
 */
static const char *MSL_VARYINGS =
"struct Nv2aVshOut {\n"
"    float4 gl_Position [[position]];\n"
"    float  gl_PointSize [[point_size]];\n"
"    float4 oD0;\n"
"    float4 oD1;\n"
"    float4 oT0;\n"
"    float4 oT1;\n"
"    float4 oT2;\n"
"    float4 oT3;\n"
"    float  oFogC;\n"
"};\n";

const char *nv2a_msl_varying_struct(void) { return MSL_VARYINGS; }

static const char *MSL_PRELUDE =
"#include <metal_stdlib>\n"
"using namespace metal;\n"
"// NV2A vertex program, translated.\n"
"// Metal's clip volume puts z in [0, w], the same as the console's, so the\n"
"// epilogue leaves depth alone -- a remap that is right for OpenGL halves\n"
"// the depth range here and buries everything in the near half.\n"
"#define NV2A_CLIP_Z01 1\n"
"typedef float2 vec2;\n"
"typedef float3 vec3;\n"
"typedef float4 vec4;\n"
"\n"
"// GLSL spellings the body uses that Metal names differently.\n"
"static inline float4 lessThan(float4 a, float4 b)         { return float4(a <  b); }\n"
"static inline float4 greaterThanEqual(float4 a, float4 b) { return float4(a >= b); }\n"
"static inline float  inversesqrt(float v)                 { return rsqrt(v); }\n"
"static inline float4 inversesqrt(float4 v)                { return rsqrt(v); }\n"
"\n"
"// The NV2A's LIT, which differs from the fixed-function one in the clamping\n"
"// of the specular exponent -- a program that relies on the clamp produces a\n"
"// black highlight without it rather than a wrong one.\n"
"static inline float4 nv_lit(float4 src) {\n"
"    float d = src.x, s = src.y, p = clamp(src.w, -127.9961f, 127.9961f);\n"
"    float spec = (d > 0.0f && s > 0.0f) ? exp2(p * log2(max(s, 1e-30f))) : 0.0f;\n"
"    return float4(1.0f, max(d, 0.0f), spec, 1.0f);\n"
"}\n"
"\n"
"// The NV2A's reciprocal range, sign kept: see the GLSL prelude.\n"
"static inline float nv_away0(float t) {\n"
"    if (t > 0.0f || as_type<uint>(t) == 0u) return clamp(t, 5.421010862e-20f, 1.844674407e19f);\n"
"    return clamp(t, -1.844674407e19f, -5.421010862e-20f);\n"
"}\n"
"static inline float nv_rcc(float x) { return nv_away0(1.0f / x); }\n"
"\n"
"// The fog factor from a fog distance: see the GLSL prelude.\n"
"static inline float nv_fog_factor(float d, int en, int mode, float2 p) {\n"
"    if (en == 0) return 1.0f;\n"
"    float f;\n"
"    int m = mode & 3;\n"
"    if (m == 0) {\n"
"        if (isinf(d)) d = 0.0f;\n"
"        f = p.x + d * p.y - 1.0f;\n"
"    } else if (m == 1) {\n"
"        if (mode < 4 && isinf(d)) d = 0.0f;\n"
"        f = p.x + exp2(d * p.y * 16.0f) - 1.5f;\n"
"    } else {\n"
"        f = p.x + exp2(-d * d * p.y * p.y * 32.0f) - 1.5f;\n"
"    }\n"
"    return mode >= 4 ? abs(f) : f;\n"
"}\n";

/* Everything before the body: the prelude, the structs, the function's
 * signature, the uniforms' names, the inputs, and the varyings as locals. */
static void msl_vertex_head(SB *sb, uint16_t inputs_read)
{
    char tmp[4096];
    int i;

    sb_add(sb, "%s\n%s\n", MSL_PRELUDE, MSL_VARYINGS);
    if (nv2a_uniform_msl_struct(tmp, (int)sizeof tmp, 0)) sb_add(sb, "%s\n", tmp);
    else sb->overflow = 1;
    sb_add(sb,
        "// Nv2aMslAttrTable on the host side (nv2a_backend.h).\n"
        "struct Nv2aAttrTable {\n"
        "    uint   stride;     // floats per vertex\n"
        "    uint   mask;       // bit N: attribute N comes from the array\n"
        "    uint   pad[2];\n"
        "    uint   off[16];    // where each sits within a vertex, in floats\n"
        "    float4 value[16];  // the rest: one value for the whole draw\n"
        "};\n"
        "\n"
        "static inline float4 nv_attr(constant Nv2aAttrTable &A,\n"
        "                             const device float *vb, uint vid, uint i)\n"
        "{\n"
        "    if (A.mask & (1u << i)) {\n"
        "        const device float *p = vb + vid * A.stride + A.off[i];\n"
        "        return float4(p[0], p[1], p[2], p[3]);\n"
        "    }\n"
        "    return A.value[i];\n"
        "}\n"
        "\n"
        "vertex Nv2aVshOut nv2a_vsh_main(uint vid [[vertex_id]],\n"
        "        const device float *nv_vb [[buffer(%d)]],\n"
        "        constant float4 *c [[buffer(%d)]],\n"
        "        constant Nv2aVU &U [[buffer(%d)]],\n"
        "        constant Nv2aAttrTable &A [[buffer(%d)]])\n"
        "{\n",
        NV2A_MSL_BUF_VERTS, NV2A_MSL_BUF_CONSTS, NV2A_MSL_BUF_VU,
        NV2A_MSL_BUF_ATTRS);
    if (nv2a_uniform_msl_defines(tmp, (int)sizeof tmp, 0, "U")) sb_add(sb, "%s", tmp);
    else sb->overflow = 1;
    /* The GLSL prelude's nv_fog() reads the fog uniforms, which at file
     * scope in Metal are not in sight; as a macro they are. */
    sb_add(sb, "#define nv_fog(coord) nv_fog_factor((coord), fogEnable, fogMode, fogParam)\n");
    for (i = 0; i < NV2A_VSH_NUM_INPUTS; i++)
        if (inputs_read & (1u << i))
            sb_add(sb, "    float4 v%d = nv_attr(A, nv_vb, vid, %d);\n", i, i);
    sb_add(sb,
        "    float4 oD0, oD1, oT0, oT1, oT2, oT3;\n"
        "    float  oFogC;\n"
        "    float4 gl_Position = float4(0.0);\n"
        "    float  gl_PointSize = 1.0;\n"
        "    // nv2a body\n");
}

static void msl_vertex_tail(SB *sb)
{
    sb_add(sb,
        "    Nv2aVshOut o;\n"
        "    o.gl_Position = gl_Position;\n"
        "    o.gl_PointSize = gl_PointSize;\n"
        "    o.oD0 = oD0; o.oD1 = oD1;\n"
        "    o.oT0 = oT0; o.oT1 = oT1; o.oT2 = oT2; o.oT3 = oT3;\n"
        "    o.oFogC = oFogC;\n"
        "    return o;\n"
        "}\n");
}

int nv2a_vsh_emit_msl(const Nv2aVshProgram *p, char *buf, int bufsize)
{
    SB sb = { buf, bufsize, 0, 0 };
    msl_vertex_head(&sb, p->inputs_read);
    emit_program_body(&sb, p);
    msl_vertex_tail(&sb);
    return sb.overflow ? 0 : sb.pos;
}

/* ---- disassembly -------------------------------------------------------- */

static const char *MAC_NAME[16] = {
    "NOP","MOV","MUL","ADD","MAD","DP3","DPH","DP4",
    "DST","MIN","MAX","SLT","SGE","ARL","?","?"
};
static const char *ILU_NAME[8] = {
    "NOP","MOV","RCP","RCC","RSQ","EXP","LOG","LIT"
};

static void disasm_src(SB *sb, const Nv2aVshInsn *ins, int which)
{
    const Nv2aSrc *s = &ins->src[which];

    sb_add(sb, "%s", s->negate ? "-" : "");
    switch (s->type) {
    case NV2A_PARAM_R: sb_add(sb, "R%u", s->index); break;
    case NV2A_PARAM_V: sb_add(sb, "v%u", ins->input_index); break;
    case NV2A_PARAM_C:
        if (ins->rel_addr) sb_add(sb, "c[A0+%d]", ins->const_index);
        else               sb_add(sb, "c[%d]", ins->const_index);
        break;
    default: sb_add(sb, "0"); break;
    }
    if (!(s->swz[0] == 0 && s->swz[1] == 1 && s->swz[2] == 2 && s->swz[3] == 3))
        sb_add(sb, ".%c%c%c%c", SWZ[s->swz[0]], SWZ[s->swz[1]],
               SWZ[s->swz[2]], SWZ[s->swz[3]]);
}

static void disasm_mask(SB *sb, uint8_t m)
{
    if (m == 0xF) return;
    sb_add(sb, ".");
    if (m & 8) sb_add(sb, "x");
    if (m & 4) sb_add(sb, "y");
    if (m & 2) sb_add(sb, "z");
    if (m & 1) sb_add(sb, "w");
}

int nv2a_vsh_disasm(const Nv2aVshProgram *p, char *buf, int bufsize)
{
    SB sb = { buf, bufsize, 0, 0 };
    int i;

    for (i = 0; i < p->length; i++) {
        const Nv2aVshInsn *ins = &p->insns[i];
        int n;

        sb_add(&sb, "%3d: ", i);
        if (ins->mac != NV2A_MAC_NOP) {
            sb_add(&sb, "%s ", MAC_NAME[ins->mac & 15]);
            if (ins->mac_mask) {
                sb_add(&sb, "R%u", ins->mac_temp);
                disasm_mask(&sb, ins->mac_mask);
                sb_add(&sb, ", ");
            }
            n = (ins->mac == NV2A_MAC_MOV || ins->mac == NV2A_MAC_ARL) ? 1
              : (ins->mac == NV2A_MAC_MAD) ? 3 : 2;
            {
                int k;
                for (k = 0; k < n; k++) {
                    int which = (n == 2 && ins->mac == NV2A_MAC_ADD && k == 1)
                              ? 2 : k;
                    if (k) sb_add(&sb, ", ");
                    disasm_src(&sb, ins, which);
                }
            }
        }
        if (ins->ilu != NV2A_ILU_NOP) {
            if (ins->mac != NV2A_MAC_NOP) sb_add(&sb, "  + ");
            sb_add(&sb, "%s ", ILU_NAME[ins->ilu & 7]);
            if (ins->ilu_mask) {
                sb_add(&sb, "R%u", ins->ilu_temp);
                disasm_mask(&sb, ins->ilu_mask);
                sb_add(&sb, ", ");
            }
            disasm_src(&sb, ins, 2);
        }
        if (ins->out_is_oreg && ins->out_mask) {
            const char *nm = (ins->out_reg < 16) ? OREG_NAME[ins->out_reg] : 0;
            sb_add(&sb, "   -> %s", nm ? nm : "o?");
            disasm_mask(&sb, ins->out_mask);
            sb_add(&sb, " (%s)", ins->out_from_ilu ? "ilu" : "mac");
        }
        if (ins->final) sb_add(&sb, "   [END]");
        sb_add(&sb, "\n");
    }
    return sb.overflow ? 0 : sb.pos;
}
