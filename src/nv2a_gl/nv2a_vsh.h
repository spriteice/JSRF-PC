/*
 * nv2a_vsh.h -- NV2A vertex program: decoder and GLSL generator.
 *
 * The NV2A runs a 128-bit VLIW vertex instruction: one MAC operation and one
 * ILU operation issue together, reading up to three shared source operands and
 * writing a temporary register, an output register, or both. A title uploads
 * the program through NV097_SET_TRANSFORM_PROGRAM as raw dwords, four per
 * instruction slot, and that is exactly what this decodes -- no D3D8 shader
 * blob, no declaration, no header to skip.
 *
 * The decoder depends on nothing but the C library. The emitters write GLSL
 * (#version 330 core, fixed attribute locations v0..v15 at 0..15, one uniform
 * array for the 192 program constants) and Metal Shading Language from the
 * same body generator; the uniforms both read are named in nv2a_backend.h.
 */
#ifndef NV2A_VSH_H
#define NV2A_VSH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NV2A_VSH_MAX_INSNS      136
#define NV2A_VSH_NUM_CONSTS     192
#define NV2A_VSH_NUM_INPUTS     16
#define NV2A_VSH_NUM_TEMPS      13      /* R0..R11, plus R12 aliasing oPos */

/* MAC (multiply-accumulate) unit opcodes. */
typedef enum {
    NV2A_MAC_NOP = 0, NV2A_MAC_MOV, NV2A_MAC_MUL, NV2A_MAC_ADD,
    NV2A_MAC_MAD, NV2A_MAC_DP3, NV2A_MAC_DPH, NV2A_MAC_DP4,
    NV2A_MAC_DST, NV2A_MAC_MIN, NV2A_MAC_MAX, NV2A_MAC_SLT,
    NV2A_MAC_SGE, NV2A_MAC_ARL
} Nv2aMacOp;

/* ILU (inverse/logarithm unit) opcodes. Reads source C only. */
typedef enum {
    NV2A_ILU_NOP = 0, NV2A_ILU_MOV, NV2A_ILU_RCP, NV2A_ILU_RCC,
    NV2A_ILU_RSQ, NV2A_ILU_EXP, NV2A_ILU_LOG, NV2A_ILU_LIT
} Nv2aIluOp;

/* Source operand register banks, as the "mux" field encodes them.
 *
 * Numbered from one, with zero meaning nothing: a decoder that assumes
 * 0/1/2 reads every constant as an input and every input as a temporary,
 * which still compiles and still draws -- just not the right geometry. */
typedef enum {
    NV2A_PARAM_NONE = 0,
    NV2A_PARAM_R    = 1,   /* temporary  R0..R11        */
    NV2A_PARAM_V    = 2,   /* input      v0..v15        */
    NV2A_PARAM_C    = 3    /* constant   c0..c191       */
} Nv2aParamType;

/* Output registers, as encoded in the instruction's output address field. */
typedef enum {
    NV2A_OREG_POS = 0,
    NV2A_OREG_DIFFUSE = 3,
    NV2A_OREG_SPECULAR = 4,
    NV2A_OREG_FOG = 5,
    NV2A_OREG_POINT_SIZE = 6,
    NV2A_OREG_BACK_DIFFUSE = 7,
    NV2A_OREG_BACK_SPECULAR = 8,
    NV2A_OREG_TEX0 = 9,
    NV2A_OREG_TEX1 = 10,
    NV2A_OREG_TEX2 = 11,
    NV2A_OREG_TEX3 = 12,
    NV2A_OREG_A0X = 15
} Nv2aOutputReg;

typedef struct {
    uint8_t type;           /* Nv2aParamType                      */
    uint8_t index;          /* register number within the bank    */
    uint8_t negate;
    uint8_t swz[4];         /* per-component selector, 0=x .. 3=w */
} Nv2aSrc;

typedef struct {
    Nv2aMacOp mac;
    Nv2aIluOp ilu;

    Nv2aSrc   src[3];       /* A, B, C -- shared by both units    */
    uint8_t   rel_addr;     /* source C-style constant indexed by A0 */
    int16_t   const_index;  /* c[] index carried in the instruction  */
    uint8_t   input_index;  /* v[] index carried in the instruction  */

    uint8_t   mac_temp;     /* temp register written by the MAC unit */
    uint8_t   mac_mask;     /* bit3=x bit2=y bit1=z bit0=w           */
    uint8_t   ilu_temp;
    uint8_t   ilu_mask;

    uint8_t   out_is_oreg;  /* output write targets an output register */
    uint8_t   out_from_ilu; /* ... and its value comes from the ILU    */
    uint8_t   out_reg;      /* Nv2aOutputReg                            */
    uint8_t   out_mask;
    uint8_t   final;        /* last instruction of the program          */
} Nv2aVshInsn;

typedef struct {
    Nv2aVshInsn insns[NV2A_VSH_MAX_INSNS];
    int         length;
    uint16_t    inputs_read;    /* bit N set if vN is read anywhere */
    uint16_t    outputs_written;/* bit N set if output register N is written */
} Nv2aVshProgram;

/*
 * Decode `count` instruction slots (4 dwords each) into `out`.
 *
 * Stops early at the instruction whose final bit is set. Returns the number of
 * instructions decoded, which is also `out->length`.
 */
int nv2a_vsh_decode(const uint32_t *tokens, int count, Nv2aVshProgram *out);

/*
 * Emit GLSL 330 core for a decoded program.
 *
 * Conventions the backend must match:
 *   - inputs   `layout(location = N) in vec4 vN;` for every v register read
 *   - constants`uniform vec4 c[192];`
 *   - outputs  gl_Position, plus `out vec4 oD0, oD1, oT0..oT3;` and
 *              `out float oFogC;`
 *   - viewport The program produces NV2A screen-space coordinates; the shader
 *              converts them to clip space with `uniform vec4 vpScale, vpOff;`
 *              so the backend does not have to undo the title's viewport.
 *
 * Returns the number of characters written, or 0 if the buffer was too small.
 */
int nv2a_vsh_emit_glsl(const Nv2aVshProgram *p, char *buf, int bufsize);

/*
 * Emit Metal Shading Language for the same decoded program.
 *
 * Same translation, different wrapper: the body is generated by the same code
 * that produces the GLSL, word for word, so the two backends cannot disagree
 * about what a program means. What a Metal backend has to match:
 *
 *   - inputs    pulled, not declared: buffer NV2A_MSL_BUF_VERTS holds packed
 *               floats and NV2A_MSL_BUF_ATTRS an Nv2aMslAttrTable saying
 *               where each attribute is, or its value for the whole draw
 *   - constants c[] at NV2A_MSL_BUF_CONSTS, everything else in the Nv2aVU
 *               struct at NV2A_MSL_BUF_VU (nv2a_uniform_msl_offset())
 *   - outputs   `Nv2aVshOut`, whose members the fragment stage receives
 *   - entry     `nv2a_vsh_main`
 *
 * Returns the number of characters written, or 0 if the buffer was too small.
 */
int nv2a_vsh_emit_msl(const Nv2aVshProgram *p, char *buf, int bufsize);

/* The `struct Nv2aVshOut` declaration both Metal stages must share, so the
 * fragment emitter can repeat it verbatim rather than keeping its own copy. */
const char *nv2a_msl_varying_struct(void);

/* Human-readable disassembly, for bring-up and for diffing against a
 * reference. Returns characters written. */
int nv2a_vsh_disasm(const Nv2aVshProgram *p, char *buf, int bufsize);

/*
 * Run a decoded program on the CPU for one vertex and return what it wrote to
 * oPos. Written from the instruction stream rather than from the generated
 * shader, so the two are independent: if they disagree, the translation is
 * wrong, and if they agree, the constants are.
 */
int nv2a_vsh_interp(const Nv2aVshProgram *p, const float v[16][4],
                    const float c[][4], float out_pos[4]);

/*
 * The same run, reporting every address the program's ARL instructions set.
 *
 * A title that skins its characters reads a bone matrix out of the constant
 * file at c[A0 + n], and whether A0 lands on a matrix is not something that
 * can be worked out from the vertex data alone: the index is scaled by a
 * constant the program picks, and different programs pick different ones.
 * Running the program is the only way to know what it meant.
 *
 * Returns how many were recorded, up to a0_max.
 */
/* Filled by the last nv2a_vsh_interp() call: every output register it wrote,
 * indexed by Nv2aOutputReg, plus a bitmask of which ones it actually wrote. */
extern float    g_interp_out[16][4];
extern uint16_t g_interp_written;

int nv2a_vsh_interp_a0(const Nv2aVshProgram *p, const float v[16][4],
                       const float c[][4], int *a0_out, int a0_max);

/* Reversed depth for the GLSL emitters, vertex and fragment: 1 once the
 * OpenGL backend has clip control on, before any program is emitted, and the
 * precise fixed-function depth that goes with it. See nv2a_vsh.c. */
void nv2a_glsl_set_revz(int on);
int  nv2a_glsl_revz(void);
void nv2a_glsl_set_precz(int on);
int  nv2a_glsl_precz(void);

/* The 0.53125 of a pixel the console's D3D driver adds to every position it
 * hands the hardware, and the two viewport uniforms the epilogues read, both
 * with it taken back off. See nv2a_vsh.c. */
#define NV2A_D3D_NUDGE 0.53125f
/* ffVpOff, from NV097_SET_VIEWPORT_OFFSET as written. */
void nv2a_vsh_ff_vpoff(const float reg[4], float out[4]);
/* vpRect, from the viewport offset and scale registers and the size of the
 * surface they apply to, in pixels. */
void nv2a_vsh_vprect(const float off[4], const float scale[4],
                     float surf_w, float surf_h, float out[4]);

#ifdef __cplusplus
}
#endif


/* The fixed-function transform, described by the state that changes its
 * shape. Everything else it needs -- the composite matrix, the texture
 * matrices, the viewport -- arrives through the same uniforms a translated
 * program uses, so this carries only what has to be baked into the code.
 * The shader cache keeps it as bytes: a change here changes SC_LAYOUT
 * (nv2a_gl.c). */
typedef struct {
    uint16_t inputs_read;   /* v registers the emitted shader declares */
    uint8_t  tex_matrix;    /* bit n: transform oTn by the stage matrix */
    uint8_t  has_diffuse;   /* v3 is a real array or a set constant */
    uint8_t  has_specular;  /* v4 likewise */
    /* The title asked the hardware T&L unit to light these vertices. */
    uint8_t  lit;
    /* The shape of that lighting, all zero when lit is: */
    uint8_t  has_normal;    /* v2 is a real array or a set constant */
    uint8_t  colmat;        /* NV097_SET_COLOR_MATERIAL: 2 bits each, from bit 0,
                             * emission / ambient / diffuse / specular source,
                             * 0 the material, 1 the vertex diffuse, 2 its specular */
    uint8_t  lflags;        /* 1 separate specular, 2 local eye,
                             * 4 specular enable, 8 normalization */
    uint8_t  fog_gen;       /* NV097_SET_FOG_GEN_MODE: how the fog distance is
                             * made -- 0 specular alpha, 1 radial, 2 planar,
                             * 3 abs planar, 6 the fog coordinate (v5) */
    uint16_t light_mask;    /* NV097_SET_LIGHT_ENABLE_MASK, 2 bits a light:
                             * 0 off, 1 infinite, 2 local, 3 spot */
    /* NV097_SET_TEXGEN_S..Q: where each stage's texture coordinates come
     * from, a code per component (S, T, R, Q) -- NV2A_TEXGEN_*, 0 the
     * vertex's own. */
    uint8_t  texgen[4][4];
    uint8_t  tg_normalize;  /* NV097_SET_NORMALIZATION_ENABLE, for the normal
                             * the generated coordinates of an unlit batch are
                             * made from (a lit one has it in lflags) */
} Nv2aVshFixed;

enum {
    NV2A_TEXGEN_OFF = 0,        /* the vertex's own coordinate */
    NV2A_TEXGEN_EYE_LINEAR,     /* the eye-space position against a plane */
    NV2A_TEXGEN_OBJECT_LINEAR,  /* the position as given against a plane */
    NV2A_TEXGEN_SPHERE_MAP,     /* S, T: the reflected eye ray, sphere-mapped */
    NV2A_TEXGEN_NORMAL_MAP,     /* S, T, R: the eye-space normal */
    NV2A_TEXGEN_REFLECTION_MAP  /* S, T, R: the reflected eye ray */
};

int nv2a_vsh_emit_ff_glsl(const Nv2aVshFixed *f, char *buf, int bufsize);

/* The same fixed-function transform as a Metal vertex function, with the same
 * inputs and uniforms as nv2a_vsh_emit_msl's. The fixed-function matrices are
 * members of their own, never the 192-entry file -- see the note in
 * nv2a_vsh_emit_ff_glsl. */
int nv2a_vsh_emit_ff_msl(const Nv2aVshFixed *f, char *buf, int bufsize);

#endif /* NV2A_VSH_H */
