/*
 * The combiner constants, as uniforms.
 *
 * Each combiner stage reads two constant colours, C0 and C1
 * (NV097_SET_COMBINER_FACTOR0 / 1), and the final combiner a pair of its own
 * (NV097_SET_SPECULAR_FOG_FACTOR). nv2a_psh.c wrote them into the fragment
 * shader as literals, so the program key, which hashed every byte of
 * Nv2aPshState, held them too: each new value of one was a new program. JSRF
 * fades the spray can's effects and the sewer's splashes through them, a
 * shade a frame, and every shade was a program built on the GPU thread, about
 * 8 ms each: up to 3 a frame, 413 in one run on the Ally X, and 413 more
 * programs in the shader cache.
 *
 * Now the shader reads them from one uniform array, pshC, laid out as below,
 * and the key and the shader cache's states leave them out (nv2a_psh_key),
 * with the alpha test's reference, which was a uniform already. A shade is a
 * uniform upload, not a program.
 */
#ifndef NV2A_PSH_CONSTS_H
#define NV2A_PSH_CONSTS_H

#include "nv2a_psh.h"

#include <stdint.h>
#include <string.h>

/* Where each constant sits in pshC[]: every stage's C0, every stage's C1,
 * then the final combiner's pair. */
#define NV2A_PSH_C0(stage)  (stage)
#define NV2A_PSH_C1(stage)  (NV2A_PSH_STAGES + (stage))
#define NV2A_PSH_FINAL_C0   (2 * NV2A_PSH_STAGES)
#define NV2A_PSH_FINAL_C1   (2 * NV2A_PSH_STAGES + 1)
#define NV2A_PSH_NCONSTS    (2 * NV2A_PSH_STAGES + 2)

/* The state as the program key and the shader cache's states see it: every
 * byte the emitted source depends on, and none of what the shader reads as a
 * uniform. memcpy rather than assignment, so that the padding is copied too
 * and two equal states give equal bytes. */
static inline void nv2a_psh_key(Nv2aPshState *key, const Nv2aPshState *ps)
{
    memcpy(key, ps, sizeof *key);
    memset(key->c0, 0, sizeof key->c0);
    memset(key->c1, 0, sizeof key->c1);
    key->final_c0 = 0;
    key->final_c1 = 0;
    key->alpha_ref = 0.0f;
}

/* The constants in pshC[]'s order, as the title wrote them: A8R8G8B8. */
static inline void nv2a_psh_const_words(uint32_t w[NV2A_PSH_NCONSTS], const Nv2aPshState *ps)
{
    memcpy(w + NV2A_PSH_C0(0), ps->c0, sizeof ps->c0);
    memcpy(w + NV2A_PSH_C1(0), ps->c1, sizeof ps->c1);
    w[NV2A_PSH_FINAL_C0] = ps->final_c0;
    w[NV2A_PSH_FINAL_C1] = ps->final_c1;
}

/* ... and as the shader reads them: r, g, b, a, each byte over 255. */
static inline void nv2a_psh_const_values(float v[NV2A_PSH_NCONSTS * 4],
                                         const uint32_t w[NV2A_PSH_NCONSTS])
{
    int i;
    for (i = 0; i < NV2A_PSH_NCONSTS; i++) {
        v[4 * i + 0] = (float)((w[i] >> 16) & 0xFFu) / 255.0f;
        v[4 * i + 1] = (float)((w[i] >> 8) & 0xFFu) / 255.0f;
        v[4 * i + 2] = (float)(w[i] & 0xFFu) / 255.0f;
        v[4 * i + 3] = (float)(w[i] >> 24) / 255.0f;
    }
}

#endif /* NV2A_PSH_CONSTS_H */
