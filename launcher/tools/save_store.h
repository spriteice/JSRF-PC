/*
 * JSRF-PC Tools' save manager -- see save_store.c. Whole copies of JSRF-PC's
 * saves (its UDATA\5345000a, the title folder), kept beside them:
 *
 *   save slots: <saves>\Save slots\1 to 8\5345000a, each with the name the
 *     player gave it and when it was saved, in about.txt beside 5345000a;
 *   backups: <saves>\Save backups\<YYYY-MM-DD HH.MM.SS>\5345000a, as
 *     jsrf_backup makes them before anything is replaced, each with a name
 *     the player (or the tool) may give it, in about.txt. The date stays.
 *
 * And save folders taken out of JSRF-PC's saves (the designs the player
 * deletes). Anything that changes JSRF-PC's saves backs them up first; a
 * folder being replaced is only swapped for the new one once that is
 * complete, and put back if the swap fails.
 *
 * Paths are UTF-8.
 */
#ifndef JSRF_SAVE_STORE_H
#define JSRF_SAVE_STORE_H

#include "save_sources.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JSRF_STORE_SLOTS   8
#define JSRF_STORE_NAME    96          /* bytes of a name kept, its 0 among them */
#define JSRF_STORE_ABOUT   "about.txt"

/* What a title folder holds. */
typedef struct jsrf_held_count {
    int game;                          /* 1: a game save (a folder with JSRFDATA.SAV) */
    int designs;                       /* graffiti designs (folders with save.dat) */
    int others;                        /* other save folders */
} jsrf_held_count;

/* A slot or a backup. */
typedef struct jsrf_held {
    char            dir[JSRF_PATH_MAX];    /* its folder: <slots>\3, <backups>\2026-11-27 09.41.12 */
    char            name[JSRF_STORE_NAME]; /* the name it was given, "" none */
    char            when[32];              /* "YYYY-MM-DD HH.MM.SS": saved (a slot), made (a backup); "" unknown */
    int             used;                  /* it holds a 5345000a */
    jsrf_held_count count;
} jsrf_held;

/* What title holds. A title folder that isn't there holds nothing. */
void jsrf_store_count(const char *title, jsrf_held_count *c);

/* A count in words: "Game save, 9 designs", "3 designs", "Game save",
 * "Nothing". */
void jsrf_store_count_text(const jsrf_held_count *c, char *out, size_t n);

/* "2026-11-27 09.41.12" as "27 Nov 2026, 9:41am"; anything else as it is. */
void jsrf_store_when_text(const char *when, char *out, size_t n);

/* The time now, as a backup's folder is named. */
void jsrf_store_now(char out[32]);

/* When a file was last written, the same way. 1, or 0 when it isn't there. */
int jsrf_store_file_when(const char *path, char out[32]);

/* The game save's file in title (JSRFDATA.SAV in the folder that has it,
 * matched without regard to case), into out. 1 found. */
int jsrf_store_game_file(const char *title, char *out, size_t n);

/* The 8 slots under slots (<saves>\Save slots), out[0] being slot 1. Each
 * one's dir is set, used or not. */
void jsrf_store_slots(const char *slots, jsrf_held out[JSRF_STORE_SLOTS]);

/* The backups under backups (<saves>\Save backups) that hold a 5345000a,
 * newest first, into *out (malloc'ed, the caller frees it). Their count, or
 * -1 when out of memory. Other folders there (an export's backups of an
 * emulator's saves, in a folder named after it) aren't listed. */
int jsrf_store_backups(const char *backups, jsrf_held **out);

/* A slot's or backup's name set (an empty one takes it away). Line breaks
 * and other control characters become spaces, and a name longer than
 * JSRF_STORE_NAME is cut at a whole character. 0, or -1. */
int jsrf_store_rename(const char *dir, const char *name);

/* JSRF-PC's saves (title) copied into slot i (0 to 7) under slots, named
 * name, in place of what the slot held, which goes only once the copy is
 * complete. JSRF_WROTE, or JSRF_FAILED with why. */
int jsrf_store_save_slot(const char *title, const char *slots, int i, const char *name, char *why, size_t n);

/* The saves in from (a slot's or backup's folder: its 5345000a) made
 * JSRF-PC's (title): title backed up first into backups, the backup named
 * label, then from's copied beside title and swapped in for it. made gets
 * the backup's folder, "" when title wasn't there to back up. JSRF_WROTE, or
 * JSRF_FAILED with why (title then is as it was). */
int jsrf_store_load(const char *from, const char *title, const char *backups, const char *label,
                    char *made, size_t mn, char *why, size_t n);

/* A slot or backup deleted, whole. 0, or -1. */
int jsrf_store_delete(const char *dir);

/* The save folders named in folders (each JSRF_FOLDER_CHARS hex digits)
 * taken out of title, after a backup of title into backups named label.
 * made as for jsrf_store_load. The number deleted, or -1 with why (the
 * backup failed, and nothing is deleted; or a folder couldn't be, and the
 * ones before it are). */
int jsrf_store_delete_saves(const char *title, const char *backups, const char *label,
                            const char (*folders)[JSRF_FOLDER_CHARS + 1], int nfolders,
                            char *made, size_t mn, char *why, size_t n);

/* A design's preview (save.dat's first block: 32 rows, 32 to 128 pixels
 * across) from its save.dat, as RGBA, the game's clear pixels clear, into
 * rgba (room for JSRF_PREVIEW_W * JSRF_PREVIEW_H pixels), its width and
 * height into w and h. Its size class (0 SS to 4 XL), or -1 when the file
 * isn't a design's. Reads only the file's start. */
#define JSRF_PREVIEW_W  128
#define JSRF_PREVIEW_H  32
int jsrf_design_preview(const char *save_dat, uint8_t *rgba, int *w, int *h);

#ifdef __cplusplus
}
#endif

#endif
