/* Run with: python tests/gl_backend/run.py
 *
 * The frame put in the window (src/nv2a_gl/nv2a_fit.c), on a real GL:
 *
 *   1. 6x's 3840x2880 frame of one-pixel squares, black and white, shown at
 *      1440x1080 (a 1080-line screen): every pixel shown is the grey of the
 *      squares' average, as when every square is counted. One bilinear blit
 *      straight there, as before, skips some of them: its pixels are far
 *      from that grey, so the check sees the difference.
 *   2. The same frame shown at 1920x1440, exactly 2 to 1: blitted once, as
 *      before, with no halves made, and grey too.
 *   3. Only the place it is given is drawn: the black bars beside it keep
 *      what the window was cleared to.
 *   4. A frame squeezed one way only, as a widened scene is: 3840x2880 shown
 *      at 1920x1080 is halved along y, not along x.
 */
#include "context.h"
#include "nv2a_fit.h"

#include <stdlib.h>
#include <string.h>

#define SRC_W 3840
#define SRC_H 2880
#define GREY 128
#define NEAR_GREY 8         /* off by rounding, at most */
#define FAR_FROM_GREY 32    /* squares skipped: llvmpipe gives 57 */

static int s_fail;

static GLuint target(int w, int h, const unsigned char *rgba, GLuint *tex_out)
{
    GLuint tex, fbo;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "FAIL a %dx%d target is incomplete\n", w, h);
        exit(1);
    }
    if (tex_out) *tex_out = tex;
    return fbo;
}

static GLuint squares(void)
{
    unsigned char *p = malloc((size_t)SRC_W * SRC_H * 4);
    GLuint fbo;
    int x, y;
    for (y = 0; y < SRC_H; y++)
        for (x = 0; x < SRC_W; x++) {
            unsigned char v = ((x ^ y) & 1) ? 255 : 0;
            unsigned char *q = p + ((size_t)y * SRC_W + x) * 4;
            q[0] = q[1] = q[2] = v;
            q[3] = 255;
        }
    fbo = target(SRC_W, SRC_H, p, NULL);
    free(p);
    return fbo;
}

/* The window: w x h, cleared to red. */
static GLuint window(int w, int h)
{
    GLuint fbo = target(w, h, NULL, NULL);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    return fbo;
}

/* How far from grey the red of the window's rectangle (x, y, w, h) gets. */
static int spread(GLuint fbo, int x, int y, int w, int h)
{
    unsigned char *p = malloc((size_t)w * h * 4);
    int i, worst = 0;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, p);
    for (i = 0; i < w * h; i++) {
        int d = abs((int)p[i * 4] - GREY);
        if (d > worst) worst = d;
    }
    free(p);
    return worst;
}

static int is_red(GLuint fbo, int x, int y)
{
    unsigned char p[4];
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    return p[0] == 255 && p[1] == 0 && p[2] == 0;
}

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) s_fail = 1;
}

int main(void)
{
    Nv2aFit fit, fit2, fit3;
    GLuint src, win, before;
    int d;
    if (!gl_backend_context()) return 1;
    memset(&fit, 0, sizeof fit);
    memset(&fit2, 0, sizeof fit2);
    memset(&fit3, 0, sizeof fit3);
    src = squares();

    /* 1, and 3: in a 1920x1080 window, between bars 240 wide. */
    win = window(1920, 1080);
    nv2a_fit_blit(&fit, src, SRC_W, SRC_H, win, 240, 0, 1440, 1080);
    d = spread(win, 240, 0, 1440, 1080);
    printf("     6x at 1440x1080, halved first: %d from grey at most\n", d);
    check(d <= NEAR_GREY, "6x shown at 1440x1080 counts every square");
    check(fit.w[0] == 1920 && fit.h[0] == 1440, "it is halved once, to 1920x1440");
    check(is_red(win, 100, 500) && is_red(win, 1800, 500), "the bars beside it are untouched");

    before = window(1440, 1080);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, src);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, before);
    glBlitFramebuffer(0, 0, SRC_W, SRC_H, 0, 0, 1440, 1080, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    d = spread(before, 0, 0, 1440, 1080);
    printf("     6x at 1440x1080 in one blit, as before: %d from grey\n", d);
    check(d >= FAR_FROM_GREY, "one blit straight there skips squares (what this checks for)");

    /* 2 */
    win = window(1920, 1440);
    nv2a_fit_blit(&fit2, src, SRC_W, SRC_H, win, 0, 0, 1920, 1440);
    d = spread(win, 0, 0, 1920, 1440);
    check(d <= NEAR_GREY, "6x shown at 1920x1440 (2 to 1) counts every square");
    check(fit2.fbo[0] == 0 && fit2.tex[0] == 0, "and makes no halves");

    /* 4 */
    win = window(1920, 1080);
    nv2a_fit_blit(&fit3, src, SRC_W, SRC_H, win, 0, 0, 1920, 1080);
    check(fit3.w[0] == SRC_W && fit3.h[0] == SRC_H / 2, "squeezed one way, halved along y only");
    d = spread(win, 0, 0, 1920, 1080);
    check(d <= NEAR_GREY, "and every square counted");

    check(glGetError() == GL_NO_ERROR, "no GL error");
    if (s_fail) return 1;
    printf("fit: all cases pass\n");
    return 0;
}
