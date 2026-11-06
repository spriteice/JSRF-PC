/*
 * The frame put in the window, halved first while it is more than twice the
 * size it is shown at (nv2a_fit.h).
 */
#include "nv2a_fit.h"
#include "nv2a_scale.h"

#define GL_GLEXT_PROTOTYPES 1
#if defined(_WIN32)
#  include <epoxy/gl.h>     /* opengl32.dll stops at 1.1: see nv2a_gl.c */
#else
#  include <GL/gl.h>
#  include <GL/glext.h>
#endif

#include <stdio.h>

/* A unit no shader samples, to make the halves' textures on, as the
 * presenter's hand-over does (nv2a_gl.c). */
#define FIT_TEXTURE_UNIT 7
/* How many changes of the halving the log is told of. */
#define FIT_SAY_MAX 8

/* Half k, made or made again at w x h. 0 if GL would not make it. */
static int half_need(Nv2aFit *f, int k, int w, int h)
{
    GLint act = 0, prev = 0;
    if (f->tex[k] && f->w[k] == w && f->h[k] == h) return 1;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &act);
    glActiveTexture(GL_TEXTURE0 + FIT_TEXTURE_UNIT);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
    if (!f->tex[k]) glGenTextures(1, &f->tex[k]);
    glBindTexture(GL_TEXTURE_2D, f->tex[k]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, (GLuint)prev);
    glActiveTexture((GLenum)act);
    if (!f->fbo[k]) glGenFramebuffers(1, &f->fbo[k]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, f->fbo[k]);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, f->tex[k], 0);
    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        f->w[k] = f->h[k] = 0;
        return 0;
    }
    f->w[k] = w;
    f->h[k] = h;
    return 1;
}

void nv2a_fit_blit(Nv2aFit *f, unsigned int src_fbo, int sw, int sh,
                   unsigned int dst_fbo, int x, int y, int dw, int dh)
{
    GLint rfb = 0;
    GLuint from = src_fbo;
    int hx = nv2a_scale_halvings(sw, dw), hy = nv2a_scale_halvings(sh, dh);
    int cw = sw, ch = sh, i, k = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &rfb);
    for (i = 0; i < hx || i < hy; i++) {
        int nw = i < hx ? cw / 2 : cw, nh = i < hy ? ch / 2 : ch;
        if (!half_need(f, k, nw, nh)) break;    /* the rest in the last blit */
        glBindFramebuffer(GL_READ_FRAMEBUFFER, from);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, f->fbo[k]);
        glBlitFramebuffer(0, 0, cw, ch, 0, 0, nw, nh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        from = f->fbo[k];
        cw = nw; ch = nh;
        k ^= 1;
    }
    /* The log is told when the frame starts or stops being halved, or is
     * halved to another size, a few times: not at every frame. */
    if ((cw != f->said_w || ch != f->said_h) && f->said < FIT_SAY_MAX) {
        if (cw != sw || ch != sh)
            fprintf(stderr, "[WIN] a %dx%d frame shown at %dx%d: halved to %dx%d first\n",
                    sw, sh, dw, dh, cw, ch);
        else if (f->said_w)
            fprintf(stderr, "[WIN] a %dx%d frame shown at %dx%d: not halved\n", sw, sh, dw, dh);
        f->said_w = cw; f->said_h = ch;
        f->said++;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, from);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dst_fbo);
    glBlitFramebuffer(0, 0, cw, ch, x, y, x + dw, y + dh, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)rfb);
}
