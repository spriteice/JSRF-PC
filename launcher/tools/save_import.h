/*
 * Moving saves from one place to another, as the saves page does it -- see
 * save_import.c. An import brings saves into JSRF-PC; an export sends
 * JSRF-PC's to xbased or Cxbx-Reloaded. Both are a plan first, which the
 * page shows (and the warning names what it replaces), then the run: a
 * dated backup of where they go, then each save re-signed for it and
 * written.
 *
 * Paths are UTF-8.
 */
#ifndef JSRF_SAVE_IMPORT_H
#define JSRF_SAVE_IMPORT_H

#include "save_sources.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JSRF_DO_SKIP     0      /* not written: why in note */
#define JSRF_DO_WRITE    1      /* into a folder of its own */
#define JSRF_DO_REPLACE  2      /* in place of the save of that name there, once it is backed up */
#define JSRF_DO_RESLOT   3      /* a design whose slot is taken there, into a free one */

typedef struct jsrf_plan_item {
    int  save;                  /* its index in the jsrf_found */
    int  action;
    int  slot;                  /* for RESLOT: the slot it goes to (0 for GRAFFITI001) */
    char note[400];             /* why it is skipped, or what happens to it; after the run, what did */
    int  result;                /* after the run: a JSRF_WROTE and so on, or JSRF_FAILED */
} jsrf_plan_item;

typedef struct jsrf_plan {
    int             n;
    jsrf_plan_item *items;      /* malloc'ed */
    int             writes;     /* items that write */
    int             replaces;   /* of which replace a save */
    char            backup[JSRF_PATH_MAX];   /* after the run: the backup made, if any */
    char            why[400];   /* after the run: why it stopped, if it did */
} jsrf_plan;

/* What would happen to each chosen save of f (chosen[i] nonzero; NULL for
 * all) in title, the title folder it goes to. 0, or -1 out of memory. */
int jsrf_plan_make(const jsrf_found *f, const int *chosen, const char *title, jsrf_plan *plan);

/* The plan carried out: backups gets the dated backup of title first (when
 * title is there and something is written), and nothing is written if that
 * fails. Then each save is re-signed under key and written, and title's own
 * files are copied from f where title has none. f's saves are changed (they
 * are re-signed). 0 when every write worked, -1 when any failed (each
 * item's note says), or the backup did (plan->why). */
int jsrf_plan_run(jsrf_found *f, jsrf_plan *plan, const char *title, const char *backups,
                  const uint8_t key[JSRF_KEY_BYTES]);

void jsrf_plan_free(jsrf_plan *plan);

#ifdef __cplusplus
}
#endif

#endif
