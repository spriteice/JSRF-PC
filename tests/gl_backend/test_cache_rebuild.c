/* Run with: python tests/gl_backend/run.py
 *
 * The shader cache built again at start-up in the OpenGL backend
 * (cache_rebuild and cache_idle in src/nv2a_gl/nv2a_gfx_gl.c), driven the way
 * sdl_shader_cache in nv2a_gl.c drives it, in the context every test here
 * runs in (context.h). A first run, in a child process, draws N programs into
 * a fresh cache. The next, in this process, queues every one of them, with a
 * binary or not, builds exactly one a call, and saves nothing again: the
 * binaries file already holds them.
 */
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "context.h"
#include "nv2a_backend.h"
#include "nv2a_shader_cache.h"

#define N 12

static const Nv2aGfx *G;

static void check(int ok, const char *what)
{
    GLenum e = glGetError();
    if (!ok || e != GL_NO_ERROR) {
        fprintf(stderr, "FAIL %s%s (GL error 0x%x)\n", what, ok ? "" : ": wrong", e);
        exit(1);
    }
}

/* Program i's sources, each pair its own. */
static void sources(int i, char *v, char *f, size_t n)
{
    snprintf(v, n, NV2A_GLSL_VERSION "layout(location = 0) in vec4 v0;\n"
             "void main() { gl_Position = v0 * %d.0; }\n", i + 1);
    snprintf(f, n, NV2A_GLSL_VERSION "out vec4 frag;\n"
             "void main() { frag = vec4(%d.0 / 16.0); }\n", i);
}

/* fn on every file in dir; the sum of what it returns. */
static long each(const char *dir, long (*fn)(const char *path, const char *name))
{
    DIR *d = opendir(dir);
    struct dirent *e;
    char path[1200];
    long sum = 0;
    if (!d) return 0;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        sum += fn(path, e->d_name);
    }
    closedir(d);
    return sum;
}

static long binaries_bytes(const char *path, const char *name)
{
    static const char kind[] = "binaries-";
    struct stat st;
    return !strncmp(name, kind, sizeof kind - 1) && !stat(path, &st) ? (long)st.st_size : 0;
}

static long removed(const char *path, const char *name)
{
    (void)name;
    return remove(path) == 0;
}

/* A run of the game: the device up, and the cache open in dir. */
static void start(const char *dir)
{
    if (!gl_backend_context()) exit(1);
    G = nv2a_gfx_gl();
    check(G->init(), "init");
    G->cache_open(dir, "t");
    check(nv2a_sc_is_open(), "the cache opens");
    check(G->cache_rebuild != NULL, "the backend has cache_rebuild");
}

int main(void)
{
    char dir[] = "/tmp/gl-backend-cache-XXXXXX", v[512], f[512];
    int i, status, total, left, calls = 0, added = -1;
    long bytes;
    pid_t pid;

    check(mkdtemp(dir) != NULL, "a fresh folder");
    pid = fork();
    if (pid == 0) {
        start(dir);
        check(G->cache_rebuild() == 0, "an empty cache: nothing to build");
        for (i = 0; i < N; i++) {
            sources(i, v, f, sizeof v);
            check(G->prog_new(v, f, "test") != 0, "prog_new");
        }
        check(nv2a_sc_count() == N, "the first run keeps every program");
        exit(0);
    }
    check(pid > 0 && waitpid(pid, &status, 0) == pid && WIFEXITED(status) && !WEXITSTATUS(status),
          "the first run");
    bytes = each(dir, binaries_bytes);

    start(dir);
    total = G->cache_rebuild();
    check(total == N, "every program is queued");
    for (left = total; left > 0; calls++) {
        int was = left;
        left = G->cache_idle(0.0);
        check(left == was - 1, "one program a call");
    }
    check(calls == N, "every program is built");
    check(G->cache_idle(0.0) == 0, "nothing is left");
    for (i = 0; i < N; i++) {
        sources(i, v, f, sizeof v);
        check(G->prog_new(v, f, "test") != 0, "prog_new");
    }
    nv2a_sc_stats(NULL, NULL, &added);
    check(added == 0, "the game is handed what was built: nothing new");
    check(each(dir, binaries_bytes) == bytes, "the binaries file is as it was");
    printf("cache rebuild: %d programs in %d calls; binaries %ld bytes before and after\n",
           total, calls, bytes);

    each(dir, removed);
    rmdir(dir);
    printf("gl_backend: shader cache rebuild passes\n");
    return 0;
}
