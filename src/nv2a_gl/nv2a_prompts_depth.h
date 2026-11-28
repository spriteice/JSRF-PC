/*
 * The depth a line's pictures are drawn at (nv2a_prompts.h,
 * NV2A_PROMPT_LINE_TAG): their letters', which the renderer works out on
 * the CPU (nv2a_vsh.h, nv2a_vsh_ndc_z), a little behind. On its own, with
 * nothing of the input layer's, for the backend's test of it
 * (tests/gl_backend/test_picture_depth.c).
 */
#ifndef NV2A_PROMPTS_DEPTH_H
#define NV2A_PROMPTS_DEPTH_H

/* How far behind its letters a line's picture is drawn, in steps of a
 * 24-bit depth buffer (in GL's -1..1, or 0..1 with reversed depth, a step
 * is twice or once 2^-24): a quarter of one. Worked out on the CPU, the
 * letters' depth can come out a little in front of theirs as the GPU works
 * it; a quarter of a step behind, it is stored at their step (or, where
 * theirs falls between two, the one behind), never the one in front and
 * never on a step's edge. So what the game draws later at the letters' own
 * depth (a fade) still covers the picture, and what it draws behind them
 * (a screen behind the text) doesn't. */
#define NV2A_PROMPT_DEPTH_BEHIND 0.25f
/* A 24-bit depth buffer's step in GL's 0..1. */
#define NV2A_PROMPT_DEPTH_STEP   (1.0f / 16777216.0f)

/* GL's NDC depth z moved NV2A_PROMPT_DEPTH_BEHIND steps farther: up in
 * -1..1 (a step is two of NV2A_PROMPT_DEPTH_STEP there), down with reversed
 * depth (revz) in 0..1, and kept inside. */
static inline float nv2a_prompts_behind(float z, int revz)
{
    if (revz) {
        z -= NV2A_PROMPT_DEPTH_BEHIND * NV2A_PROMPT_DEPTH_STEP;
        return z < 0.0f ? 0.0f : z;
    }
    z += NV2A_PROMPT_DEPTH_BEHIND * 2.0f * NV2A_PROMPT_DEPTH_STEP;
    return z > 1.0f ? 1.0f : z;
}

#endif
