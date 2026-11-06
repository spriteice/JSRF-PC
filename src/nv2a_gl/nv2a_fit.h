/*
 * The frame put in the window (nv2a_fit.c): the last blit, from the frame's
 * framebuffer to its place in the window, with the frame halved first along
 * each axis where it is more than twice the size it is shown at
 * (nv2a_scale_halvings).
 *
 * A bilinear blit reads 2x2 pixels for each one it writes. Shrinking a frame
 * by more than 2 to 1 that way leaves some of its pixels out: 6x on a
 * 1080-line screen is 2880 lines shown as 1080, and most of what it drew
 * would never reach the screen. An exact 2:1 bilinear blit averages each 2x2
 * block, so each halving keeps every pixel, and the last blit is then 2 to 1
 * or less. A frame at 2 to 1 or less is blitted once, as it always was.
 *
 * GL's framebuffers are not shared between contexts, so each context that
 * shows frames keeps an Nv2aFit of its own: the presenter thread's, and the
 * GPU thread's when there is no presenter. Plain C with no GL in the header,
 * so a test links it (tests/gl_backend's test_fit.c).
 */
#ifndef NV2A_FIT_H
#define NV2A_FIT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Nv2aFit {
    unsigned int fbo[2], tex[2];    /* the halved frames, made as needed */
    int w[2], h[2];
    int said_w, said_h, said;       /* the size the log last had, and how often */
} Nv2aFit;

/* The frame in src_fbo, sw x sh, blitted into dst_fbo at (x, y), dw x dh
 * (counted from the bottom left, as glBlitFramebuffer does), halved first as
 * above. The scissor test must be off. Leaves dst_fbo bound to draw into,
 * and the read framebuffer, the active texture unit and its texture as it
 * found them. */
void nv2a_fit_blit(Nv2aFit *f, unsigned int src_fbo, int sw, int sh,
                   unsigned int dst_fbo, int x, int y, int dw, int dh);

#ifdef __cplusplus
}
#endif

#endif
