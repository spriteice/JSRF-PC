/* nv2a_vsh_interp (src/nv2a_gl/nv2a_vsh_interp.c): a program run on the CPU
 * gives the position its instructions make, and its trace switch,
 * RECOMP_GL_INTERP_TRACE, is looked up once. The button prompts' upright
 * buttons run it for every vertex of a face, every frame; a lookup on every
 * instruction held the graffiti menus at 25 fps on Windows.
 *
 * Cases, one a run (run.py):
 *   once:  the switch set after the first run is not seen: no run traced.
 *   trace: the switch set from the start: the first two runs traced, no more.
 * Both check each run's position. run.py reads stderr for the trace. */
#include "nv2a_vsh.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("vsh_interp: FAIL line %d: %s\n", __LINE__, #c); g_fail = 1; } } while (0)

static Nv2aVshProgram g_prog;
static float g_c[NV2A_VSH_NUM_CONSTS][4];

/* dp4 oPos.x, v0, c[0] to dp4 oPos.w, v0, c[3], then mov oT0, v9: a
 * transform as the game's 2D programs make one. */
static void program(void)
{
    int i, k;
    memset(&g_prog, 0, sizeof g_prog);
    for (i = 0; i < 4; i++) {
        Nv2aVshInsn *x = &g_prog.insns[i];
        x->mac = NV2A_MAC_DP4;
        x->src[0].type = NV2A_PARAM_V;
        x->src[1].type = NV2A_PARAM_C;
        x->input_index = 0;
        x->const_index = (int16_t)i;
        for (k = 0; k < 4; k++) x->src[0].swz[k] = x->src[1].swz[k] = (uint8_t)k;
        x->out_is_oreg = 1;
        x->out_reg = NV2A_OREG_POS;
        x->out_mask = (uint8_t)(8u >> i);
    }
    g_prog.insns[4].mac = NV2A_MAC_MOV;
    g_prog.insns[4].src[0].type = NV2A_PARAM_V;
    g_prog.insns[4].input_index = 9;
    for (k = 0; k < 4; k++) g_prog.insns[4].src[0].swz[k] = (uint8_t)k;
    g_prog.insns[4].out_is_oreg = 1;
    g_prog.insns[4].out_reg = NV2A_OREG_TEX0;
    g_prog.insns[4].out_mask = 0xF;
    g_prog.insns[4].final = 1;
    g_prog.length = 5;
    /* x' = 2x + 10, y' = 3y + 20, z' = z, w' = 1. */
    memset(g_c, 0, sizeof g_c);
    g_c[0][0] = 2.0f; g_c[0][3] = 10.0f;
    g_c[1][1] = 3.0f; g_c[1][3] = 20.0f;
    g_c[2][2] = 1.0f;
    g_c[3][3] = 1.0f;
}

/* One run, from (x, y, 0.5, 1): its position checked. */
static void run_one(float x, float y)
{
    float v[16][4], o[4];
    memset(v, 0, sizeof v);
    v[0][0] = x; v[0][1] = y; v[0][2] = 0.5f; v[0][3] = 1.0f;
    v[9][0] = 0.25f; v[9][1] = 0.75f;
    CHECK(nv2a_vsh_interp(&g_prog, (const float (*)[4])v, (const float (*)[4])g_c, o) == 1);
    CHECK(o[0] == 2.0f * x + 10.0f && o[1] == 3.0f * y + 20.0f && o[2] == 0.5f && o[3] == 1.0f);
    CHECK(g_interp_out[NV2A_OREG_TEX0][0] == 0.25f && g_interp_out[NV2A_OREG_TEX0][1] == 0.75f);
}

int main(int argc, char **argv)
{
    static char set[] = "RECOMP_GL_INTERP_TRACE=1";
    const char *which = argc > 1 ? argv[1] : "";
    int i;
    program();
    if (!strcmp(which, "once")) {
        CHECK(getenv("RECOMP_GL_INTERP_TRACE") == NULL);
        run_one(1.0f, 2.0f);
        CHECK(putenv(set) == 0);
        CHECK(getenv("RECOMP_GL_INTERP_TRACE") != NULL);
        for (i = 0; i < 1000; i++) run_one((float)i, (float)(i & 7));
    } else if (!strcmp(which, "trace")) {
        CHECK(getenv("RECOMP_GL_INTERP_TRACE") != NULL);
        for (i = 0; i < 5; i++) run_one((float)i, 4.0f);
    } else {
        printf("vsh_interp: no case %s\n", which);
        return 2;
    }
    fflush(stderr);
    if (g_fail) return 1;
    printf("vsh_interp %s: pass\n", which);
    return 0;
}
