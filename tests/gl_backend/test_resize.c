/* Run with: python tests/gl_backend/run.py
 *
 * A target made again at another size while it holds a picture (target_resize
 * in src/nv2a_gl/nv2a_gfx_gl.c), as a new internal resolution does while the
 * game runs, driven through the device interface the way the front half
 * drives it, in the context every test here runs in (context.h). The target
 * holds a white triangle at depth 0.5, stencil 1, over a black clear at depth
 * 1, stencil 0, and is resized up by 1.5 (as 480 lines to 720p), down by a
 * fraction that isn't a whole one, and to its own size, single-sampled and
 * multisampled:
 *
 *   1. The handle stays, and the target reads back at its new size.
 *   2. The picture is carried: inside the triangle white, outside black,
 *      wherever they now fall; and target_texture gives it too.
 *   3. The depth is carried: a quad at depth 0.75 drawn LESS lands only
 *      outside the triangle.
 *   4. The stencil is carried: a quad drawn where the stencil is 1 lands
 *      only inside it.
 *   5. Bound, its viewport is the new size: a quad over the whole of clip
 *      space reaches its last column and its last row.
 *   6. Multisampled, it is multisampled again after, with no single-sampled
 *      depth kept beside it, and its edges smoothed when drawn again; a
 *      single-sampled one's are hard.
 *   7. A size the device refuses leaves it as it was, with no GL error left
 *      behind.
 */
#define GL_GLEXT_PROTOTYPES 1      /* glBindFramebuffer and the like */
#include <stdlib.h>
#include <string.h>
#include "context.h"
#include "nv2a_backend.h"

unsigned nv2a_gfx_gl_fbo(uint32_t rt);  /* the window's: nv2a_gfx_gl.c */

#define W0 64                      /* the size it is made at */
#define H0 48
#define MAXPX (192 * 144)          /* the largest it is resized to */

static const Nv2aGfx *G;
static uint32_t rt, prog;
static uint32_t cw = W0, ch = H0;  /* its size now */
static const int attr_at[16] = { 0, -1, -1, -1, -1, -1, -1, -1,
                                 -1, -1, -1, -1, -1, -1, -1, -1 };
static const float no_const[16][4];
static uint8_t px[MAXPX * 4], tx[MAXPX * 4];

/* Clip space, z 0 (depth 0.5). A quarter of the way in from the bottom
 * left is inside it, seven eighths outside, at any size. */
static const float tri[3][4] = {
    { -0.8f, -0.8f, 0, 1 }, { 0.8f, -0.6f, 0, 1 }, { -0.6f, 0.8f, 0, 1 } };

static void check(int ok, const char *what)
{
    GLenum e = glGetError();
    if (!ok || e != GL_NO_ERROR) {
        fprintf(stderr, "FAIL %s%s (GL error 0x%x)\n", what, ok ? "" : ": wrong", e);
        exit(1);
    }
    printf("ok: %s\n", what);
}

/* Everything off but what the caller sets. */
static Nv2aRenderState plain(void)
{
    Nv2aRenderState rs;
    memset(&rs, 0, sizeof rs);
    rs.stencil_func = 0x207;
    rs.stencil_rmask = rs.stencil_wmask = 0xFF;
    rs.stencil_op[0] = rs.stencil_op[1] = rs.stencil_op[2] = 0x1E00;   /* KEEP */
    return rs;
}

static void draw(const float (*v)[4], uint32_t n, const Nv2aRenderState *rs,
                 float r, float g, float b)
{
    const float tint[4] = { r, g, b, 1 };
    G->prog_use(prog);
    G->uniform_f(NV2A_U_DRAWTINT, tint, 1);
    G->state(rs);
    G->vertices(&v[0][0], n, 4, attr_at, 0, no_const);
    G->draw(NV2A_PRIM_TRIANGLES, n);
}

/* Black at depth 1, stencil 0, then the white triangle at depth 0.5 writing
 * stencil 1. */
static void scene(void)
{
    static const float black[4] = { 0, 0, 0, 1 };
    Nv2aRenderState rs = plain();
    G->clear(7, black, 1.0f, 0, NULL);
    rs.depth_test = rs.depth_write = 1;
    rs.depth_func = 0x201;                                         /* LESS */
    rs.stencil_test = 1;
    rs.stencil_ref = 1;
    rs.stencil_op[2] = 0x1E01;                                     /* REPLACE */
    draw(tri, 3, &rs, 1, 1, 1);
}

static void quad(const Nv2aRenderState *rs, float z, float r, float g, float b)
{
    const float q[6][4] = { { -1, -1, z, 1 }, { 1, -1, z, 1 }, { 1, 1, z, 1 },
                            { -1, -1, z, 1 }, { 1, 1, z, 1 }, { -1, 1, z, 1 } };
    draw(q, 6, rs, r, g, b);
}

static void read_rt(uint8_t *out)
{
    check(G->target_read(rt, 0, 0, cw, ch, 0, out), "target_read");
}

/* The pixel a fraction fx, fy of the way across and up (bottom row first). */
static const uint8_t *at(const uint8_t *p, int bgra, double fx, double fy)
{
    const uint8_t *q = p + ((size_t)(fy * ch) * cw + (size_t)(fx * cw)) * 4;
    static uint8_t rgb[3];
    rgb[0] = q[bgra ? 2 : 0]; rgb[1] = q[1]; rgb[2] = q[bgra ? 0 : 2];
    return rgb;
}
static int is(const uint8_t *p, int r, int g, int b)
{
    return p[0] == r && p[1] == g && p[2] == b;
}
static const uint8_t *in(const uint8_t *p, int bgra)  { return at(p, bgra, 0.25, 0.25); }
static const uint8_t *out(const uint8_t *p, int bgra) { return at(p, bgra, 0.875, 0.875); }

/* Pixels whose red is neither 0 nor 255: an edge's coverage, which only
 * multisampling gives. */
static int partial(const uint8_t *p)
{
    uint32_t i, n = 0;
    for (i = 0; i < cw * ch; i++) n += p[i * 4] != 0 && p[i * 4] != 255;
    return (int)n;
}

/* Whether target t's single-sampled framebuffer has a depth buffer. */
static int flat_depth(uint32_t t)
{
    GLint type = GL_NONE;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, nv2a_gfx_gl_fbo(t));
    glGetFramebufferAttachmentParameteriv(GL_READ_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                          GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
    return type != GL_NONE;
}

/* The scene drawn at the size it has, then the target resized to w x h:
 * steps 1 to 6. */
static void resized(uint32_t w, uint32_t h, int ms)
{
    Nv2aRenderState rs = plain();
    uint32_t was = rt;
    char what[48], msg[128];
    snprintf(what, sizeof what, "%ux%u to %ux%u%s", cw, ch, w, h, ms ? ", multisampled" : "");
    G->target_bind(rt);
    scene();
    snprintf(msg, sizeof msg, "%s: target_resize", what);
    check(G->target_resize(rt, w, h) == 1, msg);
    cw = w; ch = h;
    snprintf(msg, sizeof msg, "%s: the handle stays", what);
    check(rt == was, msg);

    read_rt(px);
    snprintf(msg, sizeof msg, "%s: the picture is carried", what);
    check(is(in(px, 0), 255, 255, 255) && is(out(px, 0), 0, 0, 0), msg);
    check(G->tex_read(G->target_texture(rt), cw, ch, tx), "tex_read");
    snprintf(msg, sizeof msg, "%s: target_texture has it", what);
    check(is(in(tx, 1), 255, 255, 255) && is(out(tx, 1), 0, 0, 0), msg);
    snprintf(msg, sizeof msg, "%s: %s", what,
             ms ? "multisampled still, no single-sampled depth beside it"
                : "its depth buffer kept beside it");
    check(flat_depth(rt) == !ms, msg);

    rs.depth_test = 1;
    rs.depth_func = 0x201;                                         /* LESS */
    quad(&rs, 0.5f, 0, 1, 0);                                      /* depth 0.75 */
    read_rt(px);
    snprintf(msg, sizeof msg, "%s: the depth is carried", what);
    check(is(in(px, 0), 255, 255, 255) && is(out(px, 0), 0, 255, 0), msg);

    rs = plain();
    rs.stencil_test = 1;
    rs.stencil_func = 0x202;                                       /* EQUAL */
    rs.stencil_ref = 1;
    quad(&rs, 0, 0, 0, 1);
    read_rt(px);
    snprintf(msg, sizeof msg, "%s: the stencil is carried", what);
    check(is(in(px, 0), 0, 0, 255) && is(out(px, 0), 0, 255, 0), msg);

    rs = plain();
    quad(&rs, 0, 1, 0, 0);
    read_rt(px);
    snprintf(msg, sizeof msg, "%s: the viewport is the new size", what);
    check(is(at(px, 0, 0, 0), 255, 0, 0)
          && is(px + ((size_t)(ch - 1) * cw + cw - 1) * 4, 255, 0, 0), msg);

    /* Drawn again, its edges are smoothed only if it is multisampled. */
    scene();
    read_rt(px);
    snprintf(msg, sizeof msg, "%s: %s", what, ms ? "edges smoothed" : "edges hard");
    check((partial(px) > 0) == ms, msg);
}

int main(void)
{
    static uint8_t before[MAXPX * 4];
    int hi;

    if (!gl_backend_context()) return 1;
    G = nv2a_gfx_gl();
    check(G->init(), "init");
    check(G->target_resize != NULL, "the backend resizes targets");
    prog = G->prog_new(
        NV2A_GLSL_VERSION "layout(location = 0) in vec4 v0;\n"
        "void main() { gl_Position = v0; }\n",
        NV2A_GLSL_VERSION "uniform vec4 drawTint;\nout vec4 frag;\n"
        "void main() { frag = drawTint; }\n", "test");
    check(prog != 0, "prog_new");

    rt = G->target_new(W0, H0);
    check(rt != 0, "target_new");
    resized(96, 72, 0);                 /* 1.5 times: 480 lines to 720p */
    resized(40, 30, 0);                 /* down, by 5 / 12 */
    resized(40, 30, 0);                 /* its own size: nothing to do */

    /* A size the device refuses: the target as it was, its picture with it. */
    G->target_bind(rt);
    scene();
    read_rt(before);
    {
        GLint most = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &most);
        check(G->target_resize(rt, (uint32_t)most * 2, 16) == 0, "a size past the device's most is refused");
    }
    read_rt(px);
    check(!memcmp(before, px, (size_t)cw * ch * 4), "and the target is as it was");
    G->target_free(rt);

    /* Multisampled: the targets made after samples() are, and stay so. */
    hi = G->samples_max() >= 4 ? 4 : G->samples_max();
    check(hi >= 2, "the device multisamples");
    check(G->samples(hi) == hi, "samples()");
    cw = W0; ch = H0;
    rt = G->target_new(W0, H0);
    check(rt != 0, "target_new, multisampled");
    resized(192, 144, 1);               /* 3 times: 480 lines to 1440p */
    resized(80, 60, 1);
    G->target_free(rt);

    G->report();
    printf("gl_backend: target_resize passes\n");
    return 0;
}
