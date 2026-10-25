/*
 * The OpenGL backend's anti-aliasing, on a headless EGL context (Mesa's
 * llvmpipe is enough): SMAA (target_smaa) and multisampling (samples), as
 * the engine builds them -- nv2a_gfx_gl.c and nv2a_smaa.c as they are. Run by
 * run.py, once for OpenGL 3.3 core and once for OpenGL ES (NV2A_GLES).
 *
 * A white triangle with long, shallow edges on black: drawn without either,
 * every pixel is black or white; SMAA leaves values in between along its
 * edges and the rest as it was; so does 4x multisampling, before and after a
 * change of count, and the two together. The frame SMAA was given is left
 * as it was, a flat colour comes through it exactly, and the next draw after
 * it still lands where it should.
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#if defined(NV2A_GLES)
#  include <GLES3/gl32.h>
#else
#  define GL_GLEXT_PROTOTYPES 1
#  include <GL/gl.h>
#  include <GL/glext.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nv2a_backend.h"

#define W 160
#define H 120

static const Nv2aGfx *G;
static int failures;

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } \
                              else { printf("ok: " __VA_ARGS__); printf("\n"); } } while (0)

static int context(void)
{
    static const EGLint cfga[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
#if defined(NV2A_GLES)
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
#else
        EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
#endif
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    static const EGLint pba[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
#if defined(NV2A_GLES)
    static const EGLint ctxa[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 2, EGL_NONE };
#else
    static const EGLint ctxa[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE
    };
#endif
    EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    EGLConfig cfg;
    EGLint n, maj, mnr;
    EGLSurface s;
    EGLContext c;
    if (d == EGL_NO_DISPLAY || !eglInitialize(d, &maj, &mnr)) return 0;
#if defined(NV2A_GLES)
    eglBindAPI(EGL_OPENGL_ES_API);
#else
    eglBindAPI(EGL_OPENGL_API);
#endif
    if (!eglChooseConfig(d, cfga, &cfg, 1, &n) || n < 1) return 0;
    s = eglCreatePbufferSurface(d, cfg, pba);
    c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, ctxa);
    if (c == EGL_NO_CONTEXT) return 0;
    eglMakeCurrent(d, s, s, c);
    printf("%s | %s\n", (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER));
    return 1;
}

static uint32_t g_prog;

/* The white triangle (or, with `low`, one lower down), into target rt. */
static void triangle(uint32_t rt, int clear, int low)
{
    static const float black[4] = { 0, 0, 0, 1 };
    float v[12] = { -0.9f, -0.9f, 0, 1,   0.9f, -0.5f, 0, 1,   -0.7f, 0.9f, 0, 1 };
    float cattr[16][4];
    int at[16], i;
    Nv2aRenderState rs;
    if (low) for (i = 0; i < 3; i++) v[i * 4 + 1] = v[i * 4 + 1] * 0.3f - 0.6f;
    memset(&rs, 0, sizeof rs);
    memset(cattr, 0, sizeof cattr);
    for (i = 0; i < 16; i++) at[i] = -1;
    at[0] = 0;
    G->target_bind(rt);
    if (clear) G->clear(7, black, 1.0f, 0, NULL);
    G->prog_use(g_prog);
    G->state(&rs);
    G->vertices(v, 3, 4, at, 0, cattr);
    G->draw(NV2A_PRIM_TRIANGLES, 3);
}

static void readback(uint32_t rt, uint8_t *px)
{
    if (!G->target_read(rt, 0, 0, W, H, 0, px)) { failures++; printf("FAIL: read back %u\n", rt); }
}

/* GL_AA_DUMP=<prefix>: the pictures as PPM files, top row first, to look at. */
static void dump(const uint8_t *px, const char *name)
{
    const char *pfx = getenv("GL_AA_DUMP");
    char path[512];
    FILE *f;
    int y, x;
    if (!pfx) return;
    snprintf(path, sizeof path, "%s%s.ppm", pfx, name);
    if (!(f = fopen(path, "wb"))) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (y = H - 1; y >= 0; y--)
        for (x = 0; x < W; x++) fwrite(px + (y * W + x) * 4, 1, 3, f);
    fclose(f);
}

/* Pixels whose red is neither black nor white. */
static int between(const uint8_t *px)
{
    int i, n = 0;
    for (i = 0; i < W * H; i++) if (px[i * 4] > 8 && px[i * 4] < 247) n++;
    return n;
}

/* Pixels that differ from `a` in `b` where `a`'s 5x5 neighbourhood is one
 * value -- away from every edge, which nothing should touch. */
static int changed_away_from_edges(const uint8_t *a, const uint8_t *b)
{
    int x, y, dx, dy, n = 0;
    for (y = 2; y < H - 2; y++)
        for (x = 2; x < W - 2; x++) {
            int flat = 1;
            for (dy = -2; dy <= 2 && flat; dy++)
                for (dx = -2; dx <= 2 && flat; dx++)
                    if (memcmp(a + ((y + dy) * W + x + dx) * 4, a + (y * W + x) * 4, 4)) flat = 0;
            if (flat && memcmp(a + (y * W + x) * 4, b + (y * W + x) * 4, 4)) n++;
        }
    return n;
}

int main(void)
{
    static const char *vs = NV2A_GLSL_VERSION
        "layout(location = 0) in vec4 pos;\nvoid main() { gl_Position = pos; }\n";
    static const char *fs = NV2A_GLSL_VERSION
        "out vec4 c;\nvoid main() { c = vec4(1.0); }\n";
    static uint8_t raw[W * H * 4], aa[W * H * 4], again[W * H * 4], tmp[W * H * 4];
    uint32_t rt, out;
    int n, max, got, i;
    GLenum err;

    if (!context()) { printf("SKIP: no EGL context here\n"); return 0; }
    G = nv2a_gfx_gl();
    if (!G->init()) { printf("FAIL: init\n"); return 1; }
    g_prog = G->prog_new(vs, fs, "the test's triangle");
    CHECK(g_prog != 0, "the triangle's program builds");
    rt = G->target_new(W, H);
    CHECK(rt != 0, "a target");

    /* ---- SMAA ---- */
    triangle(rt, 1, 0);
    readback(rt, raw);
    CHECK(between(raw) == 0, "without anti-aliasing, black and white only (%d between)", between(raw));
    out = G->target_smaa(rt);
    CHECK(out != 0 && out != rt, "SMAA gives a target of its own (%u)", out);
    readback(out, aa);
    dump(raw, "raw");
    dump(aa, "smaa");
    n = between(aa);
    CHECK(n > 40, "SMAA leaves values in between along the edges (%d pixels)", n);
    n = changed_away_from_edges(raw, aa);
    CHECK(n == 0, "SMAA leaves everything away from the edges as it was (%d changed)", n);
    readback(rt, again);
    CHECK(!memcmp(raw, again, sizeof raw), "the frame SMAA was given is as it was");
    /* The next draw, with nothing set again, still lands in the bound target. */
    {
        float v[12] = { -1, 0.6f, 0, 1,   1, 0.6f, 0, 1,   -1, 1, 0, 1 };
        float cattr[16][4];
        int at[16];
        memset(cattr, 0, sizeof cattr);
        for (i = 0; i < 16; i++) at[i] = -1;
        at[0] = 0;
        G->vertices(v, 3, 4, at, 0, cattr);
        G->draw(NV2A_PRIM_TRIANGLES, 3);
        readback(rt, tmp);
        /* row H-1 is the top: covered by the new triangle at its left end */
        CHECK(tmp[((H - 1) * W + 2) * 4] == 255 && raw[((H - 1) * W + 2) * 4] == 0,
              "a draw after SMAA goes into the target that was bound");
    }
    {
        static const float c[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
        G->target_bind(rt);
        G->clear(1, c, 1.0f, 0, NULL);
        readback(rt, raw);
        readback(G->target_smaa(rt), aa);
        CHECK(!memcmp(raw, aa, sizeof raw), "a flat colour comes through SMAA exactly");
    }

    /* ---- multisampling ---- */
    max = G->samples_max ? G->samples_max() : 1;
    printf("samples: up to %d\n", max);
    if (max >= 4) {
        got = G->samples(4);
#if defined(NV2A_GLES)
        CHECK(got == 1, "OpenGL ES: no new count once there are targets (%d)", got);
#else
        uint32_t rt2;
        CHECK(got == 4, "4 samples taken while running (%d)", got);
        triangle(rt, 1, 0);
        readback(rt, raw);
        dump(raw, "msaa4");
        n = between(raw);
        CHECK(n > 40, "4x: values in between along the edges (%d pixels)", n);
        rt2 = G->target_new(W, H);
        triangle(rt2, 1, 1);
        readback(rt2, tmp);
        CHECK(between(tmp) > 20, "a target made at 4x is multisampled too (%d)", between(tmp));
        /* A change of count keeps the picture. */
        got = G->samples(2);
        readback(rt, again);
        CHECK(got == 2 && !memcmp(raw, again, sizeof raw), "4x to 2x keeps the picture (%d)", got);
        got = G->samples(1);
        readback(rt, again);
        CHECK(got == 1 && !memcmp(raw, again, sizeof raw), "2x to none keeps the picture");
        triangle(rt, 1, 0);
        readback(rt, again);
        CHECK(between(again) == 0, "none again: black and white only (%d)", between(again));
        G->samples(4);
        triangle(rt, 1, 0);
        readback(G->target_smaa(rt), aa);
        dump(aa, "msaa4_smaa");
        CHECK(between(aa) > 40, "SMAA over a multisampled frame (%d between)", between(aa));
        /* What the window would be given and what a draw samples: resolved. */
        {
            unsigned nv2a_gfx_gl_fbo(uint32_t rt);
            G->target_bind(rt);
            triangle(rt, 1, 1);
            CHECK(nv2a_gfx_gl_fbo(rt) != 0, "the window's framebuffer for a multisampled target");
            readback(rt, tmp);
            CHECK(between(tmp) > 20, "and its latest draw, resolved (%d)", between(tmp));
        }
        G->target_free(rt2);
#endif
    } else {
        printf("SKIP: fewer than 4 samples here\n");
    }
    while ((err = glGetError()) != GL_NO_ERROR) { failures++; printf("FAIL: GL error 0x%x\n", err); }
    G->report();
    printf("%s (%d failed)\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
