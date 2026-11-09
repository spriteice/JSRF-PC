/*
 * The launcher's setup: at the first start, the engine built on the player's
 * PC from their own copy of the game (both launchers: launcher/windows and
 * launcher/linux).
 *
 * The package carries no engine. When engine/jsrf_recomp.exe (bin/jsrf_recomp
 * on Linux) is missing or isn't the one this release's kit builds, the setup
 * opens a window and asks, in Aden's words:
 *
 *   No matching binary detected. Build now with local tools?   (a tools folder is there)
 *   No matching binary detected. Download tools now and build? (it isn't)
 *
 * Yes goes on, No closes the window. The tools are Python with capstone, and
 * Zig: from the tools archive (one folder, tools, unpacked beside the
 * launcher) or downloaded from their own sites (Python from GitHub, capstone
 * and Zig from PyPI), each checked by its SHA-256. Then the game's
 * default.xbe (from its folder, or read out of a disc image), and
 * kit/build_engine.py run with the tools' Python: translating, compiling and
 * linking, one progress bar. Last, the player is offered to delete the tools
 * and the generated code (never the engine), and the game starts.
 *
 * Everything the setup runs is in the tools or the kit, by its full path,
 * with an environment of its own: what the system has installed doesn't
 * matter. Downloads go through the system's own HTTPS (WinHTTP; on Linux,
 * libcurl.so.4). The tools' Python, a .tar.gz, is unpacked here (miniz),
 * and the wheels by build_engine.py with that Python: no programs of the
 * system's.
 *
 * Paths are UTF-8.
 */
#ifndef JSRF_SETUP_H
#define JSRF_SETUP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JSRF_SETUP_READY  0   /* the engine is there: start the game */
#define JSRF_SETUP_CLOSED 1   /* the player said No, or closed the window */
#define JSRF_SETUP_FAILED 2   /* it stopped, and said why */

typedef struct jsrf_setup {
    const char *root;   /* the launcher's folder */
    /* Where the game is: the launcher's own way of finding it (the folder it
     * remembers, or its dialog). 1 with a folder (with default.xbe in it) or
     * a disc image in path, 0 if the player cancelled. */
    int (*find_game)(void *ctx, char *path, size_t n);
    void *ctx;
    char game[4096];    /* out: what find_game gave, for the launcher to go on with */
} jsrf_setup;

/* 1 when the engine is there and is the one this release builds (or the one
 * built here, the player having said to play it anyway), else 0. */
int jsrf_setup_engine_ready(const char *root);

/* 1 when the setup must run first: the package has a kit (kit/kit.json), so
 * it builds its engine, and the engine isn't ready. A package with the
 * engine built in (a test build's) has no kit, and never runs the setup. */
int jsrf_setup_needed(const char *root);

/* The setup's window, start to end. */
int jsrf_setup_run(jsrf_setup *s);

/* ---- the parts, shared by the window and the tests (setup_core.c) ---- */

int js_sha256_file(const char *path, char hex[65]);
void js_sha256_hex(const void *data, size_t n, char hex[65]);
int js_is_file(const char *path);
int js_is_dir(const char *path);
int js_mkdirs(const char *path);
int js_rmtree(const char *path);
int js_write_text(const char *path, const char *text);
int js_read_kv(const char *path, const char *key, char *val, size_t n);
uint64_t js_tree_size(const char *path);


/* A .tar.gz unpacked into dest. */
int js_untar_gz(const char *tgz, const char *dest, char *err, size_t errlen);

/* A program run with only the environment given (NAME=value strings, NULL
 * ended), each line of its output handed to line(); its exit code, or -1
 * with a sentence in err if it couldn't be started. */
int js_run(const char *const *argv, const char *const *env, const char *cwd,
           void (*line)(void *ctx, const char *text), void *ctx, volatile int *cancel,
           char *err, size_t errlen);

/* The whole first start without a window, for the tests and for a PC without
 * a screen: answers given in the environment (JSRF_SETUP_ANSWER=yes|no,
 * JSRF_SETUP_TOOLS=keep|delete, JSRF_SETUP_MISMATCH=play|stop). */
int jsrf_setup_run_console(jsrf_setup *s);

#ifdef __cplusplus
}
#endif

#endif
