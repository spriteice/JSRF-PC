/*
 * SMAA (Enhanced Subpixel Morphological Antialiasing; Jimenez, Echevarria,
 * Sousa, Gutierrez 2012) for the backends: what both of them need to run it --
 * the shader sources, made from the reference implementation in smaa/, and
 * its two look-up textures. How each backend draws the three passes is its
 * own business (nv2a_gfx_gl.c, nv2a_metal.m).
 *
 * The passes, over the finished frame:
 *
 *   1. edges    from the frame's brightness (luma edge detection): an RG8
 *               image, a texel's red set where there is an edge on its left,
 *               green where there is one above. Pixels without one are
 *               discarded, so the image is cleared to 0 first.
 *   2. weights  from the edges and the two textures here, how much of each
 *               pixel's neighbours each side of an edge is to be blended in:
 *               RGBA8, cleared to 0 first for the same reason.
 *   3. blend    the frame and the weights into the anti-aliased picture.
 *
 * Quality is SMAA's "high" preset: threshold 0.1, 16 search steps, diagonal
 * patterns and corner rounding on.
 */
#ifndef NV2A_SMAA_H
#define NV2A_SMAA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NV2A_SMAA_AREA_W   160      /* RG8, two bytes a texel */
#define NV2A_SMAA_AREA_H   560
#define NV2A_SMAA_SEARCH_W  64      /* R8 */
#define NV2A_SMAA_SEARCH_H  16

/* The look-up textures, row 0 first -- uploaded as they are on both APIs: a
 * texture's first row is its coordinate 0 on each, and that is all SMAA
 * addresses them by. The area texture is sampled with linear filtering, the
 * search texture with point filtering. */
const unsigned char *nv2a_smaa_area(void);
const unsigned char *nv2a_smaa_search(void);

/* GLSL for pass 0, 1 or 2 (edges, weights, blend): its vertex stage
 * (fragment 0) or its fragment stage (1), for NV2A_GLSL_VERSION. The vertex
 * stage draws one triangle over the target from gl_VertexID, with texture
 * coordinates running the way an OpenGL target is held (t = 0 at the bottom
 * row). Uniforms: smaaRt (1/width, 1/height, width, height of the target);
 * samplers colorTex (passes 0 and 2), edgesTex, areaTex, searchTex (pass 1)
 * and blendTex (pass 2). malloc'd; NULL if out of memory. */
char *nv2a_smaa_glsl_source(int pass, int fragment);

/* MSL: one library with a vertex and a fragment function per pass --
 * smaa_edge_v/_f, smaa_weight_v/_f, smaa_blend_v/_f -- for a target of
 * pw x ph (baked in: Metal has no uniform a helper function can reach),
 * coordinates running the way a Metal target is held (row 0 at the top).
 * Textures: edges [[texture(0)]] colour; weights 0 edges, 1 area, 2 search;
 * blend 0 colour, 1 weights. malloc'd; NULL if out of memory. */
char *nv2a_smaa_msl_source(uint32_t pw, uint32_t ph);

#ifdef __cplusplus
}
#endif

#endif /* NV2A_SMAA_H */
