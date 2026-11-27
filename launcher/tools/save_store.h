/*
 * JSRF-PC Tools' save manager -- see save_store.c. Whole copies of JSRF-PC's
 * saves (its UDATA\5345000a, the title folder), kept beside them:
 *
 *   save slots: <saves>\Save slots\1 to 8\5345000a, each with the name the
 *     player gave it, when it was saved, and where its saves came from, in
 *     about.txt beside 5345000a;
 *   backups: <saves>\Save backups\<YYYY-MM-DD HH.MM.SS>\5345000a, as
 *     jsrf_backup makes them before anything is replaced, each with a name
 *     the player (or the tool) may give it, and where its saves came from,
 *     in about.txt. The date stays;
 *   and where JSRF-PC's own saves came from, in <saves>\Save origin.txt.
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

/* Where a set of saves came from, told by its game save: the last step
 * that put that there, and where it was first brought in from. JSRF-PC's
 * own is kept in <saves>\Save origin.txt (JSRF_ORIGIN_FILE, beside Save
 * backups), a slot's and a backup's in its about.txt, which a backup gets
 * from JSRF-PC's as it is made (jsrf_backup). A field not known is "". */
#define JSRF_HOW_LOADED    "loaded"     /* JSRF-PC's, from a slot */
#define JSRF_HOW_RESTORED  "restored"   /* JSRF-PC's, from a backup */
#define JSRF_HOW_IMPORTED  "imported"   /* from an emulator, or a folder */
#define JSRF_HOW_SAVED     "saved"      /* a slot's, from JSRF-PC's */
#define JSRF_HOW_COPIED    "copied"     /* a slot's, from a backup */
typedef struct jsrf_origin {
    char how[16];
    char what[JSRF_STORE_NAME + 64];   /* from what: slot 2 ("Test"), the backup "Before ...", xbased */
    char at[32];                       /* when, as jsrf_store_now */
    char first[64];                    /* where the game save was first brought in from: Cxbx-Reloaded,
                                          xbased, a folder. "" when it began in JSRF-PC, or isn't known */
    char game[32];                     /* JSRF-PC's own: its game save's time just after, so a save the
                                          game made since is told apart */
} jsrf_origin;

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
    jsrf_origin     origin;
} jsrf_held;

/* What title holds. A title folder that isn't there holds nothing. */
void jsrf_store_count(const char *title, jsrf_held_count *c);

/* A count in words: "Game save, 9 designs", "3 designs", "Game save",
 * "Nothing". */
void jsrf_store_count_text(const jsrf_held_count *c, char *out, size_t n);

/* The same, with where its game save was first brought in from (first,
 * "" none): "Game save from xbased, 3 designs". */
void jsrf_store_holds_text(const jsrf_held_count *c, const char *first, char *out, size_t n);

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

/* The saves in from (a title folder: JSRF-PC's, or a backup's 5345000a)
 * copied into slot i (0 to 7) under slots, named name, with o as where they
 * came from (NULL: not known), in place of what the slot held, which goes
 * only once the copy is complete. JSRF_WROTE, or JSRF_FAILED with why. */
int jsrf_store_save_slot(const char *from, const char *slots, int i, const char *name, const jsrf_origin *o,
                         char *why, size_t n);

/* A slot filled another way (an import): begin makes a folder to fill
 * beside slot i, title getting the 5345000a to write into there (not made
 * yet); finish names it, gives it o, and swaps it in for what the slot held;
 * drop takes it away unused. begin 0, or -1 with why; finish JSRF_WROTE, or
 * JSRF_FAILED with why and the slot as it was. */
int jsrf_store_slot_begin(const char *slots, int i, char *title, size_t tn, char *why, size_t n);
int jsrf_store_slot_finish(const char *slots, int i, const char *name, const jsrf_origin *o, char *why, size_t n);
void jsrf_store_slot_drop(const char *slots, int i);

/* The saves in from (a slot's or backup's folder: its 5345000a) made
 * JSRF-PC's (title): title backed up first into backups, the backup named
 * label, then from's copied beside title and swapped in for it. made gets
 * the backup's folder, "" when title wasn't there to back up. JSRF_WROTE, or
 * JSRF_FAILED with why (title then is as it was). */
int jsrf_store_load(const char *from, const char *title, const char *backups, const char *label,
                    char *made, size_t mn, char *why, size_t n);

/* Where JSRF-PC's saves came from, read from file (<saves>\Save origin.txt):
 * 1 when there is one. */
int jsrf_origin_read(const char *file, jsrf_origin *o);

/* Set after a step that put a game save in title (JSRF-PC's saves): how,
 * what and first as given, the time now, and the game save's own time. 0,
 * or -1. */
int jsrf_origin_note(const char *file, const char *title, const char *how, const char *what, const char *first);

/* In words: "Loaded from slot 2 ("Test") on 27 Nov 2026, 10:59am. First
 * from xbased." "" when nothing is known. */
void jsrf_origin_text(const jsrf_origin *o, char *out, size_t n);

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
