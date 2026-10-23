/*
 * The shader cache: every program a backend builds, remembered on disk, so the
 * next run can build it before the game asks for it.
 *
 * A program here is a pair of generated sources -- vertex and fragment, in the
 * backend's language -- and nothing else: the key is a hash of the two texts,
 * so a program is reused only for exactly the source it was built from, and a
 * change to the translator simply makes new keys. What is kept:
 *
 *   programs-<tag>.dat   each distinct source text once, and each program as
 *                        the pair of texts it was built from, in the order the
 *                        game first asked for them;
 *   recipes-<tag>.dat    the pipeline states a program was drawn with (Metal
 *                        makes one per program and blend state);
 *   binaries-<tag>-<driver>.dat   compiled programs, for a backend that can
 *                        save them (OpenGL's program binaries), valid only for
 *                        the driver that made them;
 *   states.dat           the NV2A state each program was translated from,
 *                        for every backend and translator, so that a new
 *                        translator can make its own programs from them.
 *
 * <tag> names the backend and the translator ("opengl-9f31c2a7"). Every file is
 * append-only records; a run that stopped half-way through writing one leaves a
 * damaged tail, which the next open drops.
 *
 * Thread-safe: Metal builds programs on a thread of its own while the game runs.
 */
#ifndef NV2A_SHADER_CACHE_H
#define NV2A_SHADER_CACHE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The key of a program: a hash of its two sources. */
uint64_t nv2a_sc_key(const char *vsrc, const char *fsrc);

/* Open the cache for `tag` in `dir` (created if needed) and load what is
 * there. 1 if it is open; 0 if it cannot be (no directory, no disk). */
int  nv2a_sc_open(const char *dir, const char *tag);
int  nv2a_sc_is_open(void);

/* The programs on disk, in the order they were first built. The strings stay
 * valid for the life of the process. */
int  nv2a_sc_count(void);
int  nv2a_sc_entry(int i, uint64_t *key, const char **vsrc, const char **fsrc);

/* Remember a program just built. Once per key; later calls do nothing. */
void nv2a_sc_add(uint64_t key, const char *vsrc, const char *fsrc);

/* Forget every program whose key is not one of keys[0..n-1], and write the
 * programs file again with only the rest. For a run that makes its programs
 * from their NV2A states at start-up, so that a program no state makes any
 * more -- an older translator's, or one kept before its state was -- is not
 * built again. Before any entry has been handed out. Returns how many went. */
int  nv2a_sc_keep(const uint64_t *keys, int n);

/* NV2A states: what a program was translated from, as the caller's bytes, of
 * a `kind` the caller defines and laid out as `layout` says. Each distinct one
 * is kept once. */
void nv2a_sc_state_add(uint32_t layout, uint32_t kind, const void *data, uint32_t len);
/* The states on disk, in the order they were first kept; -1 when there is no
 * states file, and the programs are best left as they are. The bytes stay
 * valid until nv2a_sc_state_keep drops them. */
int  nv2a_sc_state_count(void);
int  nv2a_sc_state(int i, uint32_t *layout, uint32_t *kind, const void **data, uint32_t *len);
/* Forget every state not laid out as `layout`, and write the states file again
 * with only the rest: for a run that reads no other layout, whose programs
 * nv2a_sc_keep forgets with them. Returns how many went. */
int  nv2a_sc_state_keep(uint32_t layout);

/* Pipeline recipes: what else a program needed to be drawn (a backend's own
 * 32-bit description of a blend state). Each pair is kept once. */
void nv2a_sc_recipe_add(uint64_t key, uint32_t recipe);
int  nv2a_sc_recipes(uint64_t key, uint32_t *out, int max);

/* Compiled programs, for the driver that `driver` identifies (vendor, renderer
 * and version, say). Open once, after nv2a_sc_open. */
int  nv2a_sc_bin_open(const char *driver);
/* Whether there is a saved binary for `key`. */
int  nv2a_sc_bin_has(uint64_t key);
/* The saved binary for `key`: a copy the caller frees, or NULL. */
void *nv2a_sc_bin_get(uint64_t key, uint32_t *len, uint32_t *format);
void nv2a_sc_bin_put(uint64_t key, const void *data, uint32_t len, uint32_t format);
/* A saved binary the driver refused (an update since): forget it. */
void nv2a_sc_bin_drop(uint64_t key);

/* Removes the files in the cache's folder that this run does not use: binaries
 * files other than the one it keeps -- an earlier driver's (on SteamOS a kernel
 * or Mesa update names a new one), another GPU's, an older engine's -- and this
 * backend's programs and recipes files for another translator. For a run that
 * makes its programs from their NV2A states and builds every one again at
 * start-up, which reads none of them. Binaries files stay when this run keeps
 * no binaries; states.dat, another backend's files and anything else in the
 * folder are never touched. Returns how many were removed. */
int  nv2a_sc_sweep(void);

/* Removes every file of the cache in `dir`: every backend's, translator's and
 * driver's programs, recipes and binaries, and states.dat -- so that the next
 * open starts from nothing (Developer > Purge shaders). Before the cache is
 * opened: an open cache is left alone. Anything else in the folder stays.
 * Returns how many were removed. */
int  nv2a_sc_purge(const char *dir);

/* For the report: programs and binaries on disk, and what this run added. */
void nv2a_sc_stats(int *programs, int *binaries, int *added);

#ifdef __cplusplus
}
#endif
#endif
