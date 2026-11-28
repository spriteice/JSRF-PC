/*
 * The vertex epilogue's depth worked out on the CPU (src/nv2a_gl/nv2a_vsh.c,
 * nv2a_vsh_ndc_z), for a line's pictures drawn at its letters' depth
 * (src/nv2a_gl/nv2a_prompts.h, NV2A_PROMPT_LINE_TAG): each posMode, forward
 * and reversed, with and without NV2A_Z01, as the GLSL does it; and the
 * pictures' quarter of a step behind, which way and how far
 * (nv2a_prompts_depth.h). That the GPU stores them at their letters' step
 * is tests/gl_backend/test_picture_depth.c's.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "nv2a_vsh.h"
#include "nv2a_prompts_depth.h"

static int g_failed;
#define CHECK(c) do { if (!(c)) { g_failed = 1; printf("[CHECK] %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CLOSE(a, b) (fabsf((a) - (b)) <= 1e-6f)

#define FAR 16777215.0f   /* the clip range the title declares */

static float at(float z, float w, int mode, float lo, float hi, float off, float scale, int z01)
{
    const float opos[4] = { 320.0f, 240.0f, z, w };
    const float clip[2] = { lo, hi };
    return nv2a_vsh_ndc_z(opos, mode, clip, off, scale, z01);
}

int main(void)
{
    /* forward: GL's -1..1, nearer smaller */
    nv2a_glsl_set_revz(0);
    CHECK(CLOSE(at(0.75f * FAR, 1.0f, 0, 0.0f, FAR, 0.0f, 0.0f, 0), 0.5f));      /* by the clip range */
    CHECK(CLOSE(at(0.25f * FAR, 1.0f, 0, 0.0f, 0.0f, 0.0f, FAR, 0), -0.5f));     /* none: the viewport's z */
    CHECK(CLOSE(at(0.75f, 1.0f, 0, 0.0f, 0.0f, 0.5f, 0.0f, 0), -0.5f));          /* a scale of 0 is 1 */
    CHECK(CLOSE(at(1.0f, 4.0f, 0, 0.0f, FAR, 0.0f, FAR, 1), -0.5f));             /* NV2A_Z01: z over w */
    CHECK(CLOSE(at(0.5f, 2.0f, 1, 0.0f, FAR, 0.0f, FAR, 0), -0.5f));             /* clip space */
    CHECK(isnan(at(0.5f, 0.0f, 1, 0.0f, FAR, 0.0f, FAR, 0)));                     /* no w */
    CHECK(CLOSE(at(FAR, 1.0f, 2, 0.0f, 0.0f, 0.0f, 0.0f, 0), 1.0f));              /* pixels: 0..16777215 */
    CHECK(CLOSE(at(0.0f, 1.0f, 2, 0.0f, 0.0f, 0.0f, 0.0f, 0), -1.0f));
    /* the same depth whatever w is, in screen space (posMode 0 and 2) */
    CHECK(at(0.75f * FAR, 0.0f, 0, 0.0f, FAR, 0.0f, 0.0f, 0) == at(0.75f * FAR, 3.0f, 0, 0.0f, FAR, 0.0f, 0.0f, 0));

    /* reversed: 0..1, counted down from the far end, nearer larger */
    nv2a_glsl_set_revz(1);
    CHECK(CLOSE(at(0.75f * FAR, 1.0f, 0, 0.0f, FAR, 0.0f, 0.0f, 0), 0.25f));
    CHECK(CLOSE(at(0.75f * FAR, 1.0f, 0, 0.0f, 0.0f, 0.0f, FAR, 0), 0.25f));
    CHECK(CLOSE(at(1.0f, 4.0f, 0, 0.0f, FAR, 0.0f, FAR, 1), 0.75f));
    CHECK(CLOSE(at(0.5f, 2.0f, 1, 0.0f, FAR, 0.0f, FAR, 0), 0.75f));
    CHECK(CLOSE(at(FAR, 1.0f, 2, 0.0f, 0.0f, 0.0f, 0.0f, 0), 0.0f));
    /* near the far end, where the title layers its 2D a step apart: a step
     * there is a step here, exactly */
    CHECK(at(FAR - 1.0f, 1.0f, 0, 0.0f, FAR, 0.0f, 0.0f, 0) == 1.0f / FAR);
    CHECK(at(FAR - 2.0f, 1.0f, 0, 0.0f, FAR, 0.0f, 0.0f, 0) == 2.0f / FAR);

    /* a quarter of a step behind: farther each way, never as far as half a
     * step (a step's edge), kept inside */
    {
        const float step = NV2A_PROMPT_DEPTH_STEP, z = 3.0f / FAR;
        CHECK(NV2A_PROMPT_DEPTH_BEHIND == 0.25f && step == 1.0f / 16777216.0f);
        CHECK(nv2a_prompts_behind(z, 1) < z && nv2a_prompts_behind(z, 1) > z - 0.5f * step);
        CHECK(nv2a_prompts_behind(0.0f, 1) == 0.0f);
        CHECK(nv2a_prompts_behind(-0.25f, 0) > -0.25f
              && nv2a_prompts_behind(-0.25f, 0) < -0.25f + 2.0f * 0.5f * step);
        CHECK(nv2a_prompts_behind(1.0f, 0) == 1.0f);
    }
    if (g_failed) return 1;
    printf("vsh_ndc_z: all cases pass\n");
    return 0;
}
