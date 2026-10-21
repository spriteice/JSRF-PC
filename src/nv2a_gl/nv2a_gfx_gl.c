/*
 * The NV2A's back half, in OpenGL 3.3 core.
 *
 * Everything in here was in nv2a_gl.c until the Metal backend arrived, and
 * moved rather than changed: the same calls, the same state caching, the same
 * reasons, now behind the device interface in nv2a_backend.h so that the other
 * API can stand beside it. The front half (nv2a_gl.c) still owns every cache
 * and every decision; this file owns the GL objects.
 *
 * The context is not made here. nv2a_gl.c brings one up (EGL headless, SDL on
 * Linux, CGL on a Mac), makes it current on the pushbuffer thread, and only
 * then calls init(). Presenting to a window stays there too, because it is a
 * property of how the context was made.
 */

#include "nv2a_backend.h"
#include "nv2a_shader_cache.h"

#define GL_GLEXT_PROTOTYPES 1
#if defined(NV2A_GLES)
   /* OpenGL ES 3.2 (Android; Linux/Mesa for testing): see nv2a_gl.c. */
#  undef GL_GLEXT_PROTOTYPES
#  include <GLES3/gl32.h>
#  include <GLES2/gl2ext.h>
   /* The NDK's gl2ext.h predates EXT_depth_clamp (Mesa's has it). */
#  ifndef GL_DEPTH_CLAMP_EXT
#    define GL_DEPTH_CLAMP_EXT 0x864F
#  endif
#elif defined(NV2A_GL_USE_CGL) || (defined(NV2A_GL_USE_SDL) && defined(__APPLE__))
#  include <OpenGL/gl3.h>
#  include <OpenGL/gl3ext.h>
#elif defined(_WIN32)
#  include <epoxy/gl.h>     /* opengl32.dll stops at 1.1: see nv2a_gl.c */
#else
#  include <GL/gl.h>
#  include <GL/glext.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>      /* QueryPerformanceCounter, for the shader cache's budget */
#else
#  include <time.h>
#endif

#if defined(NV2A_GLES)
#include <string.h>
/* Whether the context has an extension (asked once per name by the caller). */
static int gles_has_ext(const char *name)
{
    GLint n = 0, i;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (i = 0; i < n; i++) {
        const char *e = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);
        if (e && !strcmp(e, name)) return 1;
    }
    return 0;
}
static int gles_has_depth_clamp(void)
{
    static int has = -1;
    if (has < 0) {
        has = gles_has_ext("GL_EXT_depth_clamp");
        fprintf(stderr, "  [GL] GL_EXT_depth_clamp: %s\n", has ? "yes" : "no (draws are clipped)");
    }
    return has;
}
#endif
#include <string.h>

/* Texture units 0..3 are the combiner stages. Creating or reading a texture
 * binds it somewhere, and it used to be wherever the active unit happened to
 * be -- which is how an upload on stage 1 once rebound stage 0 for that draw.
 * A unit no shader samples keeps that out of the way. */
#define SCRATCH_UNIT 7

/* ---- render targets ------------------------------------------------------ */

#define MAX_TARGETS 64
static struct {
    GLuint   fbo, tex, rb;
    uint32_t pw, ph;
    int      used;
} g_rt[MAX_TARGETS];
static uint32_t g_cur_rt;           /* handle bound for drawing, 0 none */
static GLuint   g_vao, g_vbo;
static GLuint   g_copy_tex;         /* target_snapshot()'s copy */
static uint32_t g_copy_w, g_copy_h;
static int      g_copies;

static int rt_ok(uint32_t h) { return h && h <= MAX_TARGETS && g_rt[h - 1].used; }

static void scratch_bind(GLuint tex)
{
    glActiveTexture(GL_TEXTURE0 + SCRATCH_UNIT);
    glBindTexture(GL_TEXTURE_2D, tex);
}

static uint32_t glx_target_new(uint32_t pw, uint32_t ph)
{
    int i;
    for (i = 0; i < MAX_TARGETS; i++) if (!g_rt[i].used) break;
    if (i == MAX_TARGETS) {
        fprintf(stderr, "  [GL] out of render targets\n");
        return 0;
    }
    glGenFramebuffers(1, &g_rt[i].fbo);
    glGenTextures(1, &g_rt[i].tex);
    glGenRenderbuffers(1, &g_rt[i].rb);
    scratch_bind(g_rt[i].tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)pw, (GLsizei)ph, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindRenderbuffer(GL_RENDERBUFFER, g_rt[i].rb);
    /* With a stencil: the title keeps its shadow counts there. */
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                          (GLsizei)pw, (GLsizei)ph);
    glBindFramebuffer(GL_FRAMEBUFFER, g_rt[i].fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, g_rt[i].tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                              GL_RENDERBUFFER, g_rt[i].rb);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "  [GL] framebuffer incomplete at %ux%u\n", pw, ph);
        glDeleteFramebuffers(1, &g_rt[i].fbo);
        glDeleteTextures(1, &g_rt[i].tex);
        glDeleteRenderbuffers(1, &g_rt[i].rb);
        if (rt_ok(g_cur_rt))
            glBindFramebuffer(GL_FRAMEBUFFER, g_rt[g_cur_rt - 1].fbo);
        return 0;
    }
    g_rt[i].pw = pw; g_rt[i].ph = ph; g_rt[i].used = 1;
    /* Creating it bound it; the target being drawn into stays the target. */
    if (rt_ok(g_cur_rt))
        glBindFramebuffer(GL_FRAMEBUFFER, g_rt[g_cur_rt - 1].fbo);
    return (uint32_t)i + 1;
}

static void glx_target_free(uint32_t h)
{
    if (!rt_ok(h)) return;
    glDeleteFramebuffers(1, &g_rt[h - 1].fbo);
    glDeleteTextures(1, &g_rt[h - 1].tex);
    glDeleteRenderbuffers(1, &g_rt[h - 1].rb);
    memset(&g_rt[h - 1], 0, sizeof g_rt[0]);
    if (g_cur_rt == h) g_cur_rt = 0;
}

static void glx_target_bind(uint32_t h)
{
    if (!rt_ok(h)) return;
    glBindFramebuffer(GL_FRAMEBUFFER, g_rt[h - 1].fbo);
    glViewport(0, 0, (GLsizei)g_rt[h - 1].pw, (GLsizei)g_rt[h - 1].ph);
    g_cur_rt = h;
}

static uint32_t glx_target_texture(uint32_t h)
{
    return rt_ok(h) ? g_rt[h - 1].tex : 0;
}

/* Sampling the surface being drawn into: GL forbids reading a texture that is
 * attached to the framebuffer being written (a feedback loop, undefined), so
 * the picture as it stands is copied out first and the copy is sampled. */
static uint32_t glx_target_snapshot(uint32_t h)
{
    uint32_t pw, ph;
    if (!rt_ok(h)) return 0;
    pw = g_rt[h - 1].pw; ph = g_rt[h - 1].ph;
    if (!g_copy_tex) glGenTextures(1, &g_copy_tex);
    scratch_bind(g_copy_tex);
    if (g_copy_w != pw || g_copy_h != ph) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)pw, (GLsizei)ph, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        g_copy_w = pw; g_copy_h = ph;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_rt[h - 1].fbo);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, (GLsizei)pw, (GLsizei)ph);
    if (rt_ok(g_cur_rt))
        glBindFramebuffer(GL_FRAMEBUFFER, g_rt[g_cur_rt - 1].fbo);
    g_copies++;
    return g_copy_tex;
}

#if defined(NV2A_GLES)
/* OpenGL ES reads back RGBA bytes and nothing else for certain: BGRA is
 * made here, in place. */
static void rgba_to_bgra(uint8_t *p, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++, p += 4) { uint8_t t = p[0]; p[0] = p[2]; p[2] = t; }
}
#endif

static int glx_target_read(uint32_t h, uint32_t x, uint32_t y, uint32_t w,
                           uint32_t ht, int bgra, void *out)
{
    if (!rt_ok(h) || !out) return 0;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_rt[h - 1].fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
#if defined(NV2A_GLES)
    glReadPixels((GLint)x, (GLint)y, (GLsizei)w, (GLsizei)ht, GL_RGBA, GL_UNSIGNED_BYTE, out);
    if (bgra) rgba_to_bgra((uint8_t *)out, (size_t)w * ht);
#else
    glReadPixels((GLint)x, (GLint)y, (GLsizei)w, (GLsizei)ht,
                 bgra ? GL_BGRA : GL_RGBA,
                 bgra ? GL_UNSIGNED_INT_8_8_8_8_REV : GL_UNSIGNED_BYTE, out);
#endif
    if (rt_ok(g_cur_rt))
        glBindFramebuffer(GL_READ_FRAMEBUFFER, g_rt[g_cur_rt - 1].fbo);
    return 1;
}

static int glx_target_read_depth(uint32_t h, uint32_t pw, uint32_t ph, float *out)
{
#if defined(NV2A_GLES)
    /* OpenGL ES cannot read a depth buffer back (a debugging dump only). */
    (void)h; (void)pw; (void)ph; (void)out;
    return 0;
#endif
    if (!rt_ok(h) || !out) return 0;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_rt[h - 1].fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, (GLsizei)pw, (GLsizei)ph, GL_DEPTH_COMPONENT, GL_FLOAT, out);
    if (rt_ok(g_cur_rt))
        glBindFramebuffer(GL_READ_FRAMEBUFFER, g_rt[g_cur_rt - 1].fbo);
    return 1;
}

/* ---- textures ------------------------------------------------------------ */

static uint32_t glx_tex_new(uint32_t w, uint32_t h, const void *rgba,
                            const Nv2aSampler *s)
{
    static const GLint wrap_of[3] = { GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT };
    GLuint t = 0;
    GLenum mn, mg;

    glGenTextures(1, &t);
    if (!t) return 0;
    scratch_bind(t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)w, (GLsizei)h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    mg = s->mag_linear ? GL_LINEAR : GL_NEAREST;
    if (s->mip) {
        glGenerateMipmap(GL_TEXTURE_2D);
        mn = s->mip == 1 ? (s->min_linear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST)
                         : (s->min_linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR);
        if (s->aniso > 1) {
            /* Asked once: a driver without the extension flags the query as
             * an error, and every upload repeating it would pin errors on
             * the draws the GL error check looks at. */
            static GLfloat cap = -1.0f;
            GLfloat a = (GLfloat)s->aniso;
            if (cap < 0.0f) { cap = 1.0f; glGetFloatv(0x84FF, &cap); }   /* MAX_TEXTURE_MAX_ANISOTROPY */
            if (a > cap) a = cap;
            if (a > 1.0f) glTexParameterf(GL_TEXTURE_2D, 0x84FE, a);
        }
    } else {
        mn = s->min_linear ? GL_LINEAR : GL_NEAREST;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)mn);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)mg);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_of[s->wrap_s < 3 ? s->wrap_s : 0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_of[s->wrap_t < 3 ? s->wrap_t : 0]);
    return t;
}

static void glx_tex_free(uint32_t t)
{
    GLuint n = t;
    if (n) glDeleteTextures(1, &n);
}

static int glx_tex_read(uint32_t t, uint32_t w, uint32_t h, void *bgra)
{
    (void)w; (void)h;
    if (!t || !bgra) return 0;
#if defined(NV2A_GLES)
    /* No glGetTexImage in OpenGL ES: read it through a framebuffer of its
     * own, then put the read framebuffer back. */
    {
        static GLuint fb;
        GLint prev = 0;
        if (!fb) glGenFramebuffers(1, &fb);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fb);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, bgra);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prev);
        rgba_to_bgra((uint8_t *)bgra, (size_t)w * h);
    }
#else
    scratch_bind(t);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_BYTE, bgra);
#endif
    return 1;
}

/* ---- programs and uniforms ----------------------------------------------- */

#define MAX_PROGS 4096
static int g_bin_ok;             /* program binaries are saved and loaded */
static struct {
    GLuint prog;
    GLint  loc[NV2A_U_COUNT];
    /* How many elements of c[] the program kept (a compiler trims an array
     * read only at fixed indices to the highest one read), if element k sits
     * at location loc + k; 0 if not known to, and then it is only ever
     * uploaded whole. */
    GLint  csize;
    int    used;
} *g_prog;
static uint32_t g_cur_prog;      /* handle in use, 0 none */

#if defined(NV2A_GLES)
/* The shading language the context takes: 300, 310 or 320 (ES 3.0 to 3.2).
 * The emitters write "#version 320 es" (NV2A_GLSL_VERSION) and use nothing
 * newer than ES 3.0 does, so an older context -- the Android emulator on a
 * Mac offers 3.0 -- gets the same source under its own version line.
 * RECOMP_GLSL_ES=300|310|320 overrides it, to test that on a newer one. */
static int gles_glsl(void)
{
    static int v;
    if (!v) {
        const char *e = getenv("RECOMP_GLSL_ES");
        GLint maj = 3, mnr = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &maj);
        glGetIntegerv(GL_MINOR_VERSION, &mnr);
        v = (maj > 3 || (maj == 3 && mnr >= 2)) ? 320 : (maj == 3 && mnr == 1) ? 310 : 300;
        if (e && (atoi(e) == 300 || atoi(e) == 310 || atoi(e) == 320)) v = atoi(e);
        fprintf(stderr, "  [GL] OpenGL ES %d.%d: shaders as GLSL ES %d\n", (int)maj, (int)mnr, v);
    }
    return v;
}
#endif

static GLuint compile(GLenum stage, const char *src, const char *what)
{
    GLuint s = glCreateShader(stage);
    GLint ok = 0;
#if defined(NV2A_GLES)
    char *own = NULL;
    if (gles_glsl() != 320 && !strncmp(src, "#version 320 es", 15)) {
        size_t n = strlen(src);
        if ((own = (char *)malloc(n + 1)) != NULL) {
            memcpy(own, src, n + 1);
            own[10] = gles_glsl() == 310 ? '1' : '0';     /* "#version 3?0 es" */
            src = own;
        }
    }
#endif
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        GLsizei n = 0;
        glGetShaderInfoLog(s, sizeof log, &n, log);
        fprintf(stderr, "  [GL] %s failed to compile:\n%.*s\n", what, (int)n, log);
        if (getenv("RECOMP_GL_DUMP_SHADER"))
            fprintf(stderr, "----- source -----\n%s\n------------------\n", src);
        glDeleteShader(s);
#if defined(NV2A_GLES)
        free(own);
#endif
        return 0;
    }
#if defined(NV2A_GLES)
    free(own);
#endif
    return s;
}

/* Compile and link: a GL program object, or 0 having said why. */
static GLuint glx_build(const char *vsrc, const char *fsrc, const char *what)
{
    GLuint vs, fs, prog;
    GLint ok = 0;

    vs = compile(GL_VERTEX_SHADER, vsrc, what);
    if (!vs) return 0;
    fs = compile(GL_FRAGMENT_SHADER, fsrc, what);
    if (!fs) { glDeleteShader(vs); return 0; }
    prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    if (g_bin_ok) glProgramParameteri(prog, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        char log[4096]; GLsizei n = 0;
        glGetProgramInfoLog(prog, sizeof log, &n, log);
        fprintf(stderr, "  [GL] %s: link failed:\n%.*s\n", what, (int)n, log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

/* A linked program into the table, with its uniforms looked up. */
static uint32_t glx_adopt(GLuint prog)
{
    int i, id;
    if (!g_prog) {
        g_prog = calloc(MAX_PROGS, sizeof *g_prog);
        if (!g_prog) { glDeleteProgram(prog); return 0; }
    }
    for (i = 0; i < MAX_PROGS; i++) if (!g_prog[i].used) break;
    if (i == MAX_PROGS) { fprintf(stderr, "  [GL] out of program slots\n"); glDeleteProgram(prog); return 0; }
    g_prog[i].prog = prog;
    g_prog[i].used = 1;
    /* Where this program keeps each uniform, looked up once. A name looked
     * up in one place and not another is a uniform that silently stays at
     * zero, so there is exactly one list: nv2a_uniform_desc. */
    for (id = 0; id < NV2A_U_COUNT; id++)
        g_prog[i].loc[id] = glGetUniformLocation(prog, nv2a_uniform_desc[id].name);
    /* Part of c[] at a time (uniform_f_part) only where the locations are
     * known to run on from c[0]: every driver lays an array out that way,
     * but the specification promises it only for explicit locations, so it
     * is checked at both ends rather than assumed. */
    g_prog[i].csize = 0;
    if (g_prog[i].loc[NV2A_U_C] >= 0) {
        char nm[48];
        const GLchar *names[1];
        GLuint ui = GL_INVALID_INDEX;
        GLint sz = 0, l0 = g_prog[i].loc[NV2A_U_C];
        snprintf(nm, sizeof nm, "%s[0]", nv2a_uniform_desc[NV2A_U_C].name);
        names[0] = nm;
        glGetUniformIndices(prog, 1, names, &ui);
        if (ui != GL_INVALID_INDEX)
            glGetActiveUniformsiv(prog, 1, &ui, GL_UNIFORM_SIZE, &sz);
        if (sz > 0) {
            int ok = 1;
            if (sz > 1) {
                snprintf(nm, sizeof nm, "%s[1]", nv2a_uniform_desc[NV2A_U_C].name);
                ok = glGetUniformLocation(prog, nm) == l0 + 1;
                snprintf(nm, sizeof nm, "%s[%d]", nv2a_uniform_desc[NV2A_U_C].name, (int)sz - 1);
                ok = ok && glGetUniformLocation(prog, nm) == l0 + sz - 1;
            }
            if (ok) g_prog[i].csize = sz;
        }
    }
    return (uint32_t)i + 1;
}

/* ---- the shader cache (nv2a_shader_cache.h) ----------------------------------
 *
 * A program the game asks for is, in order: one already built ahead of it
 * (g_warm: from the cache, on a frame with little to draw), one this driver
 * compiled in an earlier run and saved (a program binary: loading one takes a
 * fraction of a millisecond where compiling takes several, and on some
 * drivers tens), or compiled now -- and then saved both ways for next time.
 * Programs the cache has without a binary (another driver, or one that keeps
 * none) are queued and built by cache_idle, a few per loading-screen frame. */
#define MAX_WARM 4096
static struct { uint64_t key; GLuint prog; } g_warm[MAX_WARM];
static int g_nwarm;
/* Keys the game has asked for this run: cache_idle has no need to build them. */
static uint64_t g_asked[MAX_WARM];
static int g_nasked;
static int *g_todo, g_ntodo, g_itodo;
static struct { unsigned warm, bin, bin_bad, compiled, saved, idle_built; double idle_ms; } g_cst;

static double glx_now_ms(void)
{
#if defined(_WIN32)
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1000.0 / (double)f.QuadPart;
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec * 1e-6;
#endif
}

static GLuint warm_take(uint64_t key)
{
    int i;
    for (i = 0; i < g_nwarm; i++)
        if (g_warm[i].key == key) {
            GLuint p = g_warm[i].prog;
            g_warm[i] = g_warm[--g_nwarm];
            return p;
        }
    return 0;
}

static void save_binary(uint64_t key, GLuint prog)
{
    GLint len = 0;
    GLenum format = 0;
    GLsizei got = 0;
    void *buf;
    glGetProgramiv(prog, GL_PROGRAM_BINARY_LENGTH, &len);
    if (len <= 0 || len > (8 << 20) || !(buf = malloc((size_t)len))) return;
    glGetProgramBinary(prog, len, &got, &format, buf);
    if (got > 0) { nv2a_sc_bin_put(key, buf, (uint32_t)got, (uint32_t)format); g_cst.saved++; }
    free(buf);
}

/* A saved binary for key, linked: the program, or 0 (none, or refused -- a
 * driver update -- and then forgotten). */
static GLuint load_binary(uint64_t key)
{
    uint32_t len = 0, format = 0;
    void *data = nv2a_sc_bin_get(key, &len, &format);
    GLuint prog;
    GLint ok = 0;
    if (!data) return 0;
    prog = glCreateProgram();
    glProgramBinary(prog, (GLenum)format, data, (GLsizei)len);
    free(data);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        glDeleteProgram(prog);
        nv2a_sc_bin_drop(key);
        g_cst.bin_bad++;
        return 0;
    }
    return prog;
}

static int asked(uint64_t key)
{
    int i;
    for (i = 0; i < g_nasked; i++) if (g_asked[i] == key) return 1;
    return 0;
}

static uint32_t glx_prog_new(const char *vsrc, const char *fsrc, const char *what)
{
    uint64_t key = nv2a_sc_is_open() ? nv2a_sc_key(vsrc, fsrc) : 0;
    GLuint prog = 0;
    if (key && g_nasked < MAX_WARM && !asked(key)) g_asked[g_nasked++] = key;
    if (key) {
        if ((prog = warm_take(key)) != 0) g_cst.warm++;
        else if (g_bin_ok && (prog = load_binary(key)) != 0) g_cst.bin++;
    }
    if (!prog) {
        prog = glx_build(vsrc, fsrc, what);
        if (!prog) return 0;
        g_cst.compiled++;
        if (key) {
            nv2a_sc_add(key, vsrc, fsrc);
            if (g_bin_ok) save_binary(key, prog);
        }
    }
    return glx_adopt(prog);
}

/* Whether this context can hand back and take program binaries:
 * ARB_get_program_binary (core since OpenGL 4.1 and OpenGL ES 3.0) with at
 * least one format -- some drivers support the calls and offer none. */
static int glx_binaries_supported(void)
{
    GLint maj = 0, mnr = 0, n = 0;
    int have = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &maj);
    glGetIntegerv(GL_MINOR_VERSION, &mnr);
#if defined(NV2A_GLES)
    have = maj >= 3;
#else
    have = maj > 4 || (maj == 4 && mnr >= 1);
    if (!have) {
        GLint k, ne = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &ne);
        for (k = 0; k < ne && !have; k++) {
            const char *e = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)k);
            if (e && !strcmp(e, "GL_ARB_get_program_binary")) have = 1;
        }
    }
#endif
    if (have) glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &n);
    return have && n > 0;
}

static void glx_cache_open(const char *dir, const char *tag)
{
    int i, n, queued = 0;
    const char *nobin = getenv("RECOMP_SHADER_BINARIES");
    if (!nv2a_sc_open(dir, tag)) return;
    /* RECOMP_SHADER_BINARIES=0: no program binaries, only the sources --
     * for a driver whose binaries misbehave, and to try the other path. */
    g_bin_ok = glx_binaries_supported() && !(nobin && !atoi(nobin));
    if (g_bin_ok) {
        char drv[768];
        snprintf(drv, sizeof drv, "%s|%s|%s", (const char *)glGetString(GL_VENDOR),
                 (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
        if (!nv2a_sc_bin_open(drv)) g_bin_ok = 0;
    }
    n = nv2a_sc_count();
    g_todo = n ? (int *)malloc((size_t)n * sizeof *g_todo) : NULL;
    for (i = 0; g_todo && i < n; i++) {
        uint64_t key; const char *v, *f;
        if (!nv2a_sc_entry(i, &key, &v, &f)) continue;
        if (g_bin_ok && nv2a_sc_bin_has(key)) continue;     /* loaded when asked for */
        g_todo[g_ntodo++] = i;
        queued++;
    }
    fprintf(stderr, "  [SHADERS] OpenGL: program binaries %s; %d programs to build on loading screens\n",
            g_bin_ok ? "kept" : (nobin && !atoi(nobin)) ? "off (RECOMP_SHADER_BINARIES=0)"
                                                       : "not available from this driver", queued);
}

/* Build queued programs while the time spent, plus what one usually takes,
 * stays inside the budget -- but always at least one, so that a driver slower
 * than the budget still gets through the queue, a program a frame. */
static int glx_cache_idle(double budget_ms)
{
    static double avg = 3.0;            /* ms a build takes, a running mean */
    double t0, t;
    int built = 0;
    if (g_itodo >= g_ntodo || g_nwarm >= MAX_WARM) return 0;
    t0 = glx_now_ms();
    while (g_itodo < g_ntodo && g_nwarm < MAX_WARM && (!built || glx_now_ms() - t0 + avg < budget_ms)) {
        uint64_t key; const char *v, *f;
        GLuint prog;
        int i = g_todo[g_itodo++], k, have = 0;
        if (!nv2a_sc_entry(i, &key, &v, &f)) continue;
        for (k = 0; k < g_nwarm; k++) if (g_warm[k].key == key) { have = 1; break; }
        if (have || asked(key)) continue;
        t = glx_now_ms();
        prog = glx_build(v, f, "a cached program");
        avg += 0.2 * (glx_now_ms() - t - avg);
        built++;
        if (!prog) continue;
        if (g_bin_ok) save_binary(key, prog);
        g_warm[g_nwarm].key = key; g_warm[g_nwarm].prog = prog; g_nwarm++;
        g_cst.idle_built++;
    }
    g_cst.idle_ms += glx_now_ms() - t0;
    return g_nwarm < MAX_WARM ? g_ntodo - g_itodo : 0;
}

static int prog_ok(uint32_t h) { return g_prog && h && h <= MAX_PROGS && g_prog[h - 1].used; }

static void glx_prog_free(uint32_t h)
{
    if (!prog_ok(h)) return;
    glDeleteProgram(g_prog[h - 1].prog);
    g_prog[h - 1].used = 0;
    g_prog[h - 1].prog = 0;
    if (g_cur_prog == h) g_cur_prog = 0;
}

static int glx_prog_uses(uint32_t h, int id)
{
    return prog_ok(h) && id >= 0 && id < NV2A_U_COUNT && g_prog[h - 1].loc[id] >= 0;
}

static void glx_prog_use(uint32_t h)
{
    if (!prog_ok(h)) return;
    glUseProgram(g_prog[h - 1].prog);
    g_cur_prog = h;
}

static void glx_uniform_f(int id, const float *v, int n)
{
    GLint loc;
    if (!prog_ok(g_cur_prog) || id < 0 || id >= NV2A_U_COUNT) return;
    loc = g_prog[g_cur_prog - 1].loc[id];
    if (loc < 0) return;
    switch (nv2a_uniform_desc[id].type) {
    case NV2A_UT_FLOAT: glUniform1fv(loc, n, v); break;
    case NV2A_UT_VEC2:  glUniform2fv(loc, n, v); break;
    case NV2A_UT_VEC3:  glUniform3fv(loc, n, v); break;
    case NV2A_UT_VEC4:  glUniform4fv(loc, n, v); break;
    case NV2A_UT_MAT2:  glUniformMatrix2fv(loc, n, GL_FALSE, v); break;
    default: break;
    }
}

static void glx_uniform_i(int id, int v)
{
    GLint loc;
    if (!prog_ok(g_cur_prog) || id < 0 || id >= NV2A_U_COUNT) return;
    loc = g_prog[g_cur_prog - 1].loc[id];
    if (loc >= 0) glUniform1i(loc, v);
}

/* Part of c[]: the constants that changed since this program last had them,
 * rather than all 192 every time any one did. */
static int glx_uniform_f_part(int id, int first, const float *v, int n)
{
    static int off = -1;
    GLint loc, size;
    /* RECOMP_GL_CONST_PART=0: always the whole file, as before. */
    if (off < 0) { const char *e = getenv("RECOMP_GL_CONST_PART"); off = (e && e[0] == '0') ? 1 : 0; }
    if (off) return 0;
    if (!prog_ok(g_cur_prog) || id != NV2A_U_C || first < 0 || n < 0) return 0;
    loc = g_prog[g_cur_prog - 1].loc[id];
    if (loc < 0) return 1;                  /* not read at all */
    size = g_prog[g_cur_prog - 1].csize;
    if (size <= 0) return 0;
    if (first >= size) return 1;            /* past what the program reads */
    if (first + n > size) n = size - first;
    if (n > 0) glUniform4fv(loc + first, n, v);
    return 1;
}

static void glx_texture(int unit, uint32_t tex)
{
    glActiveTexture((GLenum)(GL_TEXTURE0 + unit));
    glBindTexture(GL_TEXTURE_2D, (GLuint)tex);
}

/* ---- state --------------------------------------------------------------- */

/* Last state actually pushed to GL.
 *
 * A title issues thousands of small batches a frame and changes almost
 * nothing between them, so the honest cost of a draw here is not the
 * triangles, it is the hundred redundant GL calls around them: sixteen
 * attribute pointers, three kilobytes of constants and half a dozen state
 * toggles, per draw, all identical to the last one. Remembering what was set
 * turns that into a handful of calls. */
static struct {
    uint32_t layout;        /* g_vao's arrays: which are enabled, where each sits */
    int      layout_valid;
    uint32_t draw_layout;   /* the last draw's, whichever vertex array it used */
    int      draw_valid;
    uint32_t const_mask;    /* attributes currently set as constants */
    float    const_attr[16][4];   /* and the values they were set to */
    int      depth_test, depth_func, depth_mask;
    int      blend_enable, blend_src, blend_dst;
    int      valid;
} g_last;
static int g_last_cm_reset;        /* a clear forced the colour mask on */
static int g_last_stencil_reset;   /* a clear changed the stencil write mask */
static int g_last_clamp = -1;      /* GL_DEPTH_CLAMP as last set */

static GLenum gl_blend(uint32_t f)
{
    switch (f) {
    case 0x0000: return GL_ZERO;
    case 0x0001: return GL_ONE;
    case 0x0300: return GL_SRC_COLOR;
    case 0x0301: return GL_ONE_MINUS_SRC_COLOR;
    case 0x0302: return GL_SRC_ALPHA;
    case 0x0303: return GL_ONE_MINUS_SRC_ALPHA;
    case 0x0304: return GL_DST_ALPHA;
    case 0x0305: return GL_ONE_MINUS_DST_ALPHA;
    case 0x0306: return GL_DST_COLOR;
    case 0x0307: return GL_ONE_MINUS_DST_COLOR;
    case 0x0308: return GL_SRC_ALPHA_SATURATE;
    default:     return GL_ONE;
    }
}

static GLenum gl_depth_func(uint32_t f)
{
    switch (f) {
    case 0x0200: return GL_NEVER;   case 0x0201: return GL_LESS;
    case 0x0202: return GL_EQUAL;   case 0x0203: return GL_LEQUAL;
    case 0x0204: return GL_GREATER; case 0x0205: return GL_NOTEQUAL;
    case 0x0206: return GL_GEQUAL;  case 0x0207: return GL_ALWAYS;
    default:     return GL_LEQUAL;
    }
}

/* NV097_SET_STENCIL_OP_V_*: OpenGL's own values (INCRSAT and DECRSAT are
 * GL_INCR and GL_DECR, which saturate). Anything else keeps. */
static GLenum gl_stencil_op(uint32_t v)
{
    switch (v) {
    case 0x0000: case 0x1E00: case 0x1E01: case 0x1E02: case 0x1E03:
    case 0x150A: case 0x8507: case 0x8508:
        return (GLenum)v;
    default:
        return GL_KEEP;
    }
}

static void glx_state_invalidate(void)
{
    g_last.depth_test = g_last.depth_func = g_last.depth_mask = -1;
    g_last.blend_enable = g_last.blend_src = g_last.blend_dst = -1;
    g_last_cm_reset = 1;
    g_last_stencil_reset = 1;
    g_last_clamp = -1;
}

/* The per-draw state, still compared against what was last set rather than
 * set outright: these are the calls a title makes thousands of times a frame
 * with the same values, and OpenGL charges for each one. */
static void glx_state(const Nv2aRenderState *rs)
{
    if (rs->depth_test != g_last.depth_test || rs->depth_func != g_last.depth_func) {
        if (rs->depth_test) {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(gl_depth_func((uint32_t)rs->depth_func));
        } else glDisable(GL_DEPTH_TEST);
        g_last.depth_test = rs->depth_test;
        g_last.depth_func = rs->depth_func;
    }
    if (rs->depth_write != g_last.depth_mask) {
        glDepthMask(rs->depth_write ? GL_TRUE : GL_FALSE);
        g_last.depth_mask = rs->depth_write;
    }
    /* Past the far plane: clamp or cull, as the title asked -- with
     * RECOMP_GL_DEPTHCLAMP=1/0 to force it either way for the experiment
     * that matters: does the distant road come back when nothing beyond the
     * far plane is thrown away. */
    { static int force = -2;
      int clamp;
      if (force == -2) { const char *v = getenv("RECOMP_GL_DEPTHCLAMP");
                         force = v ? atoi(v) : -1; }
      /* A w-buffered draw must not be clipped on the program's z either:
       * its depth comes from w in the fragment stage, and the console's
       * range test is against w, which this title's clip range never
       * rejects. Clamp instead, and let the fragment shader overwrite. */
      clamp = force >= 0 ? force
            : ((rs->zclamp & 0xF0u) || rs->w_buffer) ? 1 : 0;
      if (clamp != g_last_clamp) {
#if defined(NV2A_GLES)
          /* OpenGL ES has depth clamping only as GL_EXT_depth_clamp (same
           * enum). Without it the draw is clipped as GL would anyway. */
          if (gles_has_depth_clamp()) {
              if (clamp) glEnable(GL_DEPTH_CLAMP_EXT); else glDisable(GL_DEPTH_CLAMP_EXT);
          }
#else
          if (clamp) glEnable(GL_DEPTH_CLAMP); else glDisable(GL_DEPTH_CLAMP);
#endif
          g_last_clamp = clamp;
      } }
    { static uint32_t last_cm = 0xFFFFFFFFu;
      uint32_t cm = rs->color_mask ? rs->color_mask : 0x01010101u;
      if (g_last_cm_reset) { last_cm = 0xFFFFFFFFu; g_last_cm_reset = 0; }
      if (cm != last_cm) {
          glColorMask((cm >> 16) & 1 ? GL_TRUE : GL_FALSE,
                      (cm >>  8) & 1 ? GL_TRUE : GL_FALSE,
                      (cm      ) & 1 ? GL_TRUE : GL_FALSE,
                      (cm >> 24) & 1 ? GL_TRUE : GL_FALSE);
          last_cm = cm;
      } }
    { static int nob = -1;
      if (nob < 0) nob = getenv("RECOMP_GL_NOBLEND") ? 1 : 0;
      if (nob) { glDisable(GL_BLEND); g_last.blend_enable = -1; } }
    if (rs->blend_enable != g_last.blend_enable
     || rs->blend_src != g_last.blend_src || rs->blend_dst != g_last.blend_dst) {
        if (rs->blend_enable) {
            glEnable(GL_BLEND);
            glBlendFunc(gl_blend((uint32_t)rs->blend_src),
                        gl_blend((uint32_t)rs->blend_dst));
        } else glDisable(GL_BLEND);
        g_last.blend_enable = rs->blend_enable;
        g_last.blend_src = rs->blend_src;
        g_last.blend_dst = rs->blend_dst;
    }
    /*
     * Stencil.
     *
     * JSRF draws its shadows with stencil volumes: each shadow volume twice
     * with colour writes off (blend ZERO/ONE), depth GEQUAL, incrementing
     * the stencil on one pass and decrementing it on the other; then one
     * full-screen black quad at alpha ~0.98 that the stencil test lets
     * through only where the count is non-zero. The surfaces carry an 8-bit
     * stencil (depth24/stencil8) and the state is applied here.
     */
    { static int valid; static Nv2aRenderState last;
      if (g_last_stencil_reset) { valid = 0; g_last_stencil_reset = 0; }
      if (!valid || rs->stencil_test != last.stencil_test
       || (rs->stencil_test
           && (rs->stencil_func != last.stencil_func || rs->stencil_ref != last.stencil_ref
            || rs->stencil_rmask != last.stencil_rmask || rs->stencil_wmask != last.stencil_wmask
            || rs->stencil_op[0] != last.stencil_op[0] || rs->stencil_op[1] != last.stencil_op[1]
            || rs->stencil_op[2] != last.stencil_op[2]))) {
          if (rs->stencil_test) {
              uint32_t f = rs->stencil_func;
              if (f < 0x200 || f > 0x207) f = 0x207;           /* ALWAYS */
              glEnable(GL_STENCIL_TEST);
              glStencilFunc((GLenum)f, (GLint)(rs->stencil_ref & 0xFFu),
                            (GLuint)(rs->stencil_rmask & 0xFFu));
              glStencilOp(gl_stencil_op(rs->stencil_op[0]),
                          gl_stencil_op(rs->stencil_op[1]),
                          gl_stencil_op(rs->stencil_op[2]));
              glStencilMask((GLuint)(rs->stencil_wmask & 0xFFu));
          } else {
              glDisable(GL_STENCIL_TEST);
          }
          last = *rs; valid = 1;
      } }

    g_last.valid = 1;

    /*
     * Culling. RECOMP_GL_NOCULL turns it off, because "geometry is missing"
     * and "geometry that should be hidden is not" look nothing alike, and
     * having both pictures separates them in one run; RECOMP_GL_CULLFLIP
     * turns the winding over.
     *
     * The registers hold OpenGL's own enumerants: 0x900 is GL_CW, 0x901
     * GL_CCW, 0x404 GL_FRONT, 0x405 GL_BACK, 0x408 GL_FRONT_AND_BACK. The
     * NV2A is a GeForce 3 and its driver spoke GL, so the values pass
     * straight through -- the winding included: the transform mirrors y, but
     * the console's own viewport transform has the same negative scale, so
     * the hardware sees the reflected winding too and the register already
     * accounts for it. Correcting again culled the visible side of every
     * surface, which left the city as the dark insides of its own buildings.
     */
    {
        static int cull = -1, flip = -1;
        if (cull < 0) { cull = getenv("RECOMP_GL_NOCULL") ? 0 : 1;
                        flip = getenv("RECOMP_GL_CULLFLIP") ? 1 : 0; }
        { static uint32_t seen_en[2], seen_cf[8], seen_ff[4]; static uint64_t n;
          static int on = -2;
          if (on == -2) on = getenv("RECOMP_GL_CULL_MAP") ? 1 : 0;
          if (on) {
              seen_en[rs->cull_enable ? 1 : 0]++;
              seen_cf[(rs->cull_face - 0x404) & 7]++;
              seen_ff[(rs->front_face - 0x900) & 3]++;
              if (++n % 50000 == 0)
                  fprintf(stderr, "  [GL] cull: off %u on %u | face F%u B%u "
                          "FB%u other%u | front CW%u CCW%u other %u/%u\n",
                          seen_en[0], seen_en[1], seen_cf[0], seen_cf[1],
                          seen_cf[4], seen_cf[2] + seen_cf[3] + seen_cf[5]
                          + seen_cf[6] + seen_cf[7],
                          seen_ff[0], seen_ff[1], seen_ff[2], seen_ff[3]);
          } }
        if (!cull || !rs->cull_enable) {
            glDisable(GL_CULL_FACE);
        } else {
            int ccw = (rs->front_face == 0x901);
            glEnable(GL_CULL_FACE);
            if (flip) ccw = !ccw;
            glFrontFace(ccw ? GL_CCW : GL_CW);
            glCullFace(rs->cull_face == 0x404 ? GL_FRONT
                     : rs->cull_face == 0x408 ? GL_FRONT_AND_BACK : GL_BACK);
        }
    }

    /* The window clip, as a scissor. The rectangle arrives top-down; GL's
     * run bottom-up, so the y is turned over against the target's height. */
    if (rs->scissor_enable && rt_ok(g_cur_rt)) {
        GLint ph = (GLint)g_rt[g_cur_rt - 1].ph;
        GLint gy = ph - (GLint)rs->scissor[3];
        if (gy < 0) gy = 0;
        glEnable(GL_SCISSOR_TEST);
        glScissor((GLint)rs->scissor[0], gy,
                  (GLsizei)(rs->scissor[2] - rs->scissor[0]),
                  (GLsizei)(rs->scissor[3] - rs->scissor[1]));
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
}

/* ---- vertices and draws -------------------------------------------------- */

/* The layout of a vertex: the stride and where each array-fed attribute
 * sits. The offsets and stride are a property of the layout, not of the
 * buffer, so a vertex array's pointers are re-specified only when it
 * changes. */
static uint32_t layout_of(uint32_t stride_floats, const int attr_at[16])
{
    uint32_t k = stride_floats * 2654435761u, i;
    for (i = 0; i < 16; i++)
        k = k * 17u + (uint32_t)(attr_at[i] + 1);
    return k;
}

/* The bound vertex array's attribute arrays, from the buffer bound to
 * GL_ARRAY_BUFFER. */
static void set_pointers(uint32_t stride_floats, const int attr_at[16])
{
    uint32_t i;
    for (i = 0; i < 16; i++) {
        if (attr_at[i] >= 0) {
            glEnableVertexAttribArray(i);
            glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE,
                (GLsizei)(stride_floats * sizeof(float)),
                (const void *)(uintptr_t)((uint32_t)attr_at[i] * sizeof(float)));
        } else {
            glDisableVertexAttribArray(i);
        }
    }
}

/* An attribute the program reads with no array behind it is one value for
 * the whole draw. GL has a per-attribute constant for exactly this, so it
 * costs one call rather than four floats in every vertex. Values are
 * compared per attribute: the sticky attributes move with every draw, and
 * comparing the whole table re-sent every constant. A draw laid out
 * differently from the last -- which arrays there are, and which attributes
 * are constants: the same program drawn twice with a different attribute
 * supplied as a constant is a different layout -- sends them all, because an
 * attribute read from an array leaves its constant undefined in older GL. */
static void set_consts(uint32_t draw_layout, uint32_t const_mask,
                       const float const_attr[16][4])
{
    uint32_t i;
    if (!g_last.draw_valid || g_last.draw_layout != draw_layout) {
        g_last.draw_layout = draw_layout;
        g_last.draw_valid = 1;
        g_last.const_mask = ~const_mask;   /* force the constants below */
    }
    if (const_mask) {
        int all = (g_last.const_mask != const_mask);
        for (i = 0; i < 16; i++)
            if ((const_mask & (1u << i))
                && (all || memcmp(g_last.const_attr[i], const_attr[i],
                                  sizeof const_attr[i]))) {
                glVertexAttrib4fv(i, const_attr[i]);
                memcpy(g_last.const_attr[i], const_attr[i], sizeof const_attr[i]);
            }
        g_last.const_mask = const_mask;
    }
}

static void glx_vertices(const float *v, uint32_t nverts, uint32_t stride_floats,
                         const int attr_at[16], uint32_t const_mask,
                         const float const_attr[16][4])
{
    uint32_t layout;

    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)nverts * stride_floats * sizeof(float),
                 v, GL_STREAM_DRAW);
    /* The buffer is re-created every draw; the pointers into it only change
     * with the layout. */
    layout = layout_of(stride_floats, attr_at);
    if (!g_last.layout_valid || g_last.layout != layout) {
        set_pointers(stride_floats, attr_at);
        g_last.layout = layout;
        g_last.layout_valid = 1;
    }
    set_consts(layout * 65599u + const_mask, const_mask, const_attr);
}

/* ---- kept vertex buffers ------------------------------------------------- *
 *
 * The vertex cache's buffers (nv2a_gl.c decides what goes in them and when
 * they are stale). Each has a vertex array of its own, so a draw from one
 * that is still laid out as last time is a bind and a draw: its pointers and
 * its index buffer are vertex-array state and stay put. They are filled
 * through GL_COPY_WRITE_BUFFER, which no vertex array records, so filling one
 * never disturbs the vertex array a draw is about to use.
 */
#define MAX_VBUFS 16384
static struct {
    GLuint   vao, vbo, ibo;
    uint32_t layout;
    uint8_t  used, laid_out, stream;
} *g_vb;
static uint32_t *g_vb_free, g_vb_nfree, g_vb_top;

static int vb_ok(uint32_t h) { return g_vb && h && h <= g_vb_top && g_vb[h - 1].used; }

static uint32_t glx_vbuf_new(int stream)
{
    uint32_t i;
    if (!g_vb) {
        g_vb = calloc(MAX_VBUFS, sizeof *g_vb);
        g_vb_free = calloc(MAX_VBUFS, sizeof *g_vb_free);
        if (!g_vb || !g_vb_free) { free(g_vb); free(g_vb_free); g_vb = NULL; g_vb_free = NULL; return 0; }
    }
    if (g_vb_nfree) i = g_vb_free[--g_vb_nfree];
    else if (g_vb_top < MAX_VBUFS) i = g_vb_top++;
    else return 0;
    memset(&g_vb[i], 0, sizeof g_vb[i]);
    glGenVertexArrays(1, &g_vb[i].vao);
    glGenBuffers(1, &g_vb[i].vbo);
    if (!g_vb[i].vao || !g_vb[i].vbo) {
        if (g_vb[i].vao) glDeleteVertexArrays(1, &g_vb[i].vao);
        if (g_vb[i].vbo) glDeleteBuffers(1, &g_vb[i].vbo);
        g_vb_free[g_vb_nfree++] = i;
        return 0;
    }
    g_vb[i].used = 1;
    g_vb[i].stream = (uint8_t)(stream ? 1 : 0);
    return i + 1;
}

static void glx_vbuf_free(uint32_t h)
{
    if (!vb_ok(h)) return;
    glDeleteVertexArrays(1, &g_vb[h - 1].vao);
    glDeleteBuffers(1, &g_vb[h - 1].vbo);
    if (g_vb[h - 1].ibo) glDeleteBuffers(1, &g_vb[h - 1].ibo);
    memset(&g_vb[h - 1], 0, sizeof g_vb[0]);
    g_vb_free[g_vb_nfree++] = h - 1;
}

static void glx_vbuf_fill(uint32_t h, const float *v, uint32_t vbytes,
                          const void *idx, uint32_t ibytes)
{
    if (!vb_ok(h)) return;
    /* The stream buffer is refilled for nearly every draw; the rest hold
     * what a level draws every frame. */
    if (v) {
        glBindBuffer(GL_COPY_WRITE_BUFFER, g_vb[h - 1].vbo);
        glBufferData(GL_COPY_WRITE_BUFFER, (GLsizeiptr)vbytes, v,
                     g_vb[h - 1].stream ? GL_STREAM_DRAW : GL_STATIC_DRAW);
    }
    if (idx) {
        if (!g_vb[h - 1].ibo) {
            glGenBuffers(1, &g_vb[h - 1].ibo);
            /* The index buffer is vertex-array state: attached once. */
            glBindVertexArray(g_vb[h - 1].vao);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_vb[h - 1].ibo);
        }
        glBindBuffer(GL_COPY_WRITE_BUFFER, g_vb[h - 1].ibo);
        glBufferData(GL_COPY_WRITE_BUFFER, (GLsizeiptr)ibytes, idx,
                     g_vb[h - 1].stream ? GL_STREAM_DRAW : GL_STATIC_DRAW);
    }
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
}

static void glx_vbuf_bind(uint32_t h, uint32_t stride_floats, const int attr_at[16],
                          uint32_t const_mask, const float const_attr[16][4])
{
    uint32_t layout;
    if (!vb_ok(h)) return;
    layout = layout_of(stride_floats, attr_at);
    glBindVertexArray(g_vb[h - 1].vao);
    if (!g_vb[h - 1].laid_out || g_vb[h - 1].layout != layout) {
        glBindBuffer(GL_ARRAY_BUFFER, g_vb[h - 1].vbo);
        set_pointers(stride_floats, attr_at);
        g_vb[h - 1].laid_out = 1;
        g_vb[h - 1].layout = layout;
    }
    set_consts(layout * 65599u + const_mask, const_mask, const_attr);
}

static void glx_draw_indexed(int prim, uint32_t count, int idx32);

static GLenum gl_prim_of(int p)
{
    switch (p) {
    case NV2A_PRIM_POINTS:         return GL_POINTS;
    case NV2A_PRIM_LINES:          return GL_LINES;
    case NV2A_PRIM_LINE_LOOP:      return GL_LINE_LOOP;
    case NV2A_PRIM_LINE_STRIP:     return GL_LINE_STRIP;
    case NV2A_PRIM_TRIANGLE_STRIP: return GL_TRIANGLE_STRIP;
    case NV2A_PRIM_TRIANGLE_FAN:   return GL_TRIANGLE_FAN;
    default:                       return GL_TRIANGLES;
    }
}

static void glx_draw(int prim, uint32_t count)
{
    glDrawArrays(gl_prim_of(prim), 0, (GLsizei)count);
}

static void glx_draw_indexed(int prim, uint32_t count, int idx32)
{
    glDrawElements(gl_prim_of(prim), (GLsizei)count,
                   idx32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, (const void *)0);
}

/* ---- clears -------------------------------------------------------------- */

static void glx_clear(unsigned mask, const float rgba[4], float depth,
                      uint32_t stencil, const uint32_t rect[4])
{
    GLbitfield bits = 0;

    if (mask & 1u) {
        glClearColor(rgba[0], rgba[1], rgba[2], rgba[3]);
        /* The console's clear picks its channels from the CLEAR_SURFACE mask
         * and ignores COLOR_MASK; GL's honours the write mask. Force it on
         * for the clear and let the next draw re-apply the title's. */
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        g_last_cm_reset = 1;
        bits |= GL_COLOR_BUFFER_BIT;
    }
    if (mask & 2u) {
#if defined(NV2A_GLES)
        glClearDepthf(depth);
#else
        glClearDepth((double)depth);
#endif
        glDepthMask(GL_TRUE);
        g_last.depth_mask = -1;   /* the clear forced it; re-apply next draw */
        bits |= GL_DEPTH_BUFFER_BIT;
    }
    if (mask & 4u) {
        glClearStencil((GLint)(stencil & 0xFFu));
        glStencilMask(0xFFu);
        g_last_stencil_reset = 1; /* the write mask; re-apply next draw */
        bits |= GL_STENCIL_BUFFER_BIT;
    }
    if (!bits) return;

    /* A clear is bounded by the clear rectangle, not by the surface: titles
     * clear sub-rectangles constantly -- a letterbox bar, a panel behind some
     * text -- and ignoring the rectangle wipes the whole frame every time.
     * It arrives top-down; GL's scissor runs bottom-up. */
    if (rect && rt_ok(g_cur_rt)) {
        GLint ph = (GLint)g_rt[g_cur_rt - 1].ph;
        GLint x = (GLint)rect[0];
        GLint y = ph - (GLint)rect[3];
        GLsizei w = (GLsizei)rect[2] - (GLsizei)rect[0];
        GLsizei h = (GLsizei)rect[3] - (GLsizei)rect[1];
        if (y < 0) { h += y; y = 0; }
        glEnable(GL_SCISSOR_TEST);
        glScissor(x, y, w > 0 ? w : 0, h > 0 ? h : 0);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
    glClear(bits);
    glDisable(GL_SCISSOR_TEST);
}

/* ---- the rest ------------------------------------------------------------ */

static int glx_init(void)
{
    glGenVertexArrays(1, &g_vao);
    glGenBuffers(1, &g_vbo);
    glBindVertexArray(g_vao);
    glx_state_invalidate();
    return 1;
}

/* The window is fed by nv2a_gl.c, which knows how the context was made. */
static void glx_present(uint32_t rt, uint32_t pw, uint32_t ph)
{
    (void)rt; (void)pw; (void)ph;
}

static void glx_report(void)
{
    if (g_copies)
        fprintf(stderr, "  [GL] %d copies of a surface sampled while drawn into\n", g_copies);
    if (nv2a_sc_is_open()) {
        int progs = 0, bins = 0, added = 0;
        nv2a_sc_stats(&progs, &bins, &added);
        fprintf(stderr, "  [SHADERS] programs asked for: %u built ahead (%u of them on loading screens, "
                "%.0f ms), %u from saved binaries%s, %u compiled then; %u binaries saved; on disk %d "
                "programs (%d new), %d binaries\n",
                g_cst.warm, g_cst.idle_built, g_cst.idle_ms, g_cst.bin,
                g_cst.bin_bad ? " (some refused: a driver update)" : "", g_cst.compiled,
                g_cst.saved, progs, added, bins);
    }
}

/* For nv2a_gl.c's GL-only paths (the SDL window's blit, the occlusion-query
 * probe): the framebuffer object behind a target. */
unsigned nv2a_gfx_gl_fbo(uint32_t rt)
{
    return rt_ok(rt) ? g_rt[rt - 1].fbo : 0;
}

static const Nv2aGfx g_gl = {
    "opengl", NV2A_LANG_GLSL, 1,
    glx_init,
    glx_target_new, glx_target_free, glx_target_bind, glx_target_texture,
    glx_target_snapshot, glx_target_read, glx_target_read_depth,
    glx_tex_new, glx_tex_free, glx_tex_read,
    glx_prog_new, glx_prog_free, glx_prog_uses, glx_prog_use,
    glx_uniform_f, glx_uniform_i,
    glx_texture, glx_state, glx_state_invalidate,
    glx_vertices, glx_draw, glx_clear, glx_present, glx_report,
    glx_vbuf_new, glx_vbuf_free, glx_vbuf_fill, glx_vbuf_bind, glx_draw_indexed,
    glx_uniform_f_part,
    NULL,                       /* pipeline_ms: the link covers it */
    glx_cache_open, glx_cache_idle,
};

const Nv2aGfx *nv2a_gfx_gl(void) { return &g_gl; }
