/*
 * Where other emulators keep JSRF's saves, and reading them out -- see
 * save_sources.c.
 *
 * Cxbx-Reloaded and xbased keep the console's E: drive as host folders
 * (EmuDisk\Partition1 and hdd\Partition1); xemu keeps the whole disk as one
 * image (xbox_disk.c reads it); a real Xbox's saves come as a folder copied
 * off it. Every source is read only, and nothing in it is trusted.
 *
 * Paths are UTF-8.
 */
#ifndef JSRF_SAVE_SOURCES_H
#define JSRF_SAVE_SOURCES_H

#include "jsrf_save.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JSRF_SRC_CXBX     1
#define JSRF_SRC_XBASED   2
#define JSRF_SRC_XEMU     3

#define JSRF_PATH_MAX     4096
#define JSRF_TITLE_FILES  4            /* TitleMeta.xbx, TitleImage.xbx, SaveImage.xbx */

/* The saves read out of one place. */
typedef struct jsrf_found {
    char       where[JSRF_PATH_MAX];   /* the title folder, or the image */
    int        nsaves;
    jsrf_save *saves;                  /* malloc'ed; each checked (jsrf_save_check) */
    int        ntitle;
    jsrf_file  title[JSRF_TITLE_FILES];/* the title's own files, kept to copy if JSRF-PC has none */
    char       why[400];               /* when nothing could be read: why */
} jsrf_found;

void jsrf_found_free(jsrf_found *f);

/* An environment variable, in UTF-8 (Windows' own, not its code page's). 1
 * when it is set and not empty. */
int jsrf_env(const char *name, char *out, size_t n);

/* The places source usually keeps JSRF's saves that are there on this
 * computer, into paths: the title folder (5345000a) for Cxbx-Reloaded and
 * xbased, the disk image xemu.toml names for xemu. Their count. A source
 * kept somewhere else (a portable copy, a folder of its own) is picked by
 * the player instead. */
int jsrf_usual_places(int source, char (*paths)[JSRF_PATH_MAX], int max);

/* xemu.toml's hdd_path, in its [sys.files] table, into out: a path given
 * relative is taken from dir, xemu.toml's folder. 1 found, 0 not. */
int jsrf_xemu_hdd_path(const char *toml, size_t n, const char *dir, char *out, size_t on);

/* In a folder the player picked: JSRF's title folder, the folder itself or
 * where the usual layouts put it under it (5345000a; UDATA\5345000a; a
 * drive's Partition1\UDATA\5345000a; Cxbx-Reloaded's EmuDisk\...; xbased's
 * hdd\...). 1 with it in dir, 2 when picked is one save's own folder (it
 * has a SaveMeta.xbx), 0 when there is none. */
int jsrf_find_title_dir(const char *picked, char *dir, size_t n);

/* Every save in a title folder on this computer. 0, or -1 with why in
 * out->why. */
int jsrf_read_title_dir(const char *dir, jsrf_found *out);

/* One save's own folder. */
int jsrf_read_save_dir(const char *dir, jsrf_found *out);

/* Every save in an Xbox disk image (xemu's qcow2, or raw), its E: drive's
 * UDATA\5345000a. */
int jsrf_read_image(const char *image, jsrf_found *out);

/* What the player picked or dropped, whatever it is: a folder (as
 * jsrf_find_title_dir finds in it), a disk image, or an xemu.toml (the
 * image it names). */
int jsrf_read_source(const char *path, jsrf_found *out);

/* Where an export to source (Cxbx-Reloaded or xbased) goes, from the
 * emulator's folder the player picked or a usual place: its title folder if
 * it has one, else where it would be, under the E: drive's folder it has
 * (EmuDisk\Partition1 or hdd\Partition1), which jsrf_save_write makes. 1
 * with it in title, 0 when picked isn't that emulator's folder. */
int jsrf_export_title(int source, const char *picked, char *title, size_t n);

/* The emulator's folder an export goes to without asking, when there is
 * one: Cxbx-Reloaded's or xbased's data folder in its usual place (under
 * Wine, a prefix's Cxbx-Reloaded title folder), for jsrf_export_title. 1
 * found. */
int jsrf_usual_export(int source, char *folder, size_t n);

/* The start of the game's default.xbe (its first 64 KB, where its
 * certificate is), from game, its folder or its disc image, for
 * jsrf_cxbx_key. 0, or -1 with why. Read only, and kept only as long as the
 * caller keeps buf. */
#define JSRF_XBE_HEAD  65536u
int jsrf_read_xbe_head(const char *game, uint8_t *buf, size_t *got, char *why, size_t wn);

#ifdef __cplusplus
}
#endif

#endif
