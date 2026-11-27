/*
 * Moving saves from one place to another (save_import.h).
 *
 * JSRF keeps one game save, always in the same folder (its name's), so an
 * import into a JSRF-PC that has been played replaces it: the plan says so,
 * the page's warning names it, and the dated backup is made before anything
 * is written (spriteice, 7:40pm on 2026-11-26). A graffiti design whose slot
 * is taken goes into a free slot instead, never over a design, and within
 * the slots whose header byte is known (JSRF_GRAFFITI_SLOTS_SAFE).
 */
#include "save_import.h"
#include "setup.h"
#include "setup_job.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void graffiti_name(unsigned slot, uint16_t name[11])
{
    char text[16];
    int i;
    snprintf(text, sizeof text, "GRAFFITI%03u", slot + 1);
    for (i = 0; i < 11; i++) name[i] = (uint8_t)text[i];
}

/* The folders the plan writes, so two saves don't go to one. */
typedef struct taken { char (*f)[JSRF_FOLDER_CHARS + 1]; int n; } taken;

static int is_taken(const taken *t, const char *folder)
{
    int i;
    for (i = 0; i < t->n; i++) if (!strcmp(t->f[i], folder)) return 1;
    return 0;
}

static void take(taken *t, const char *folder)
{
    memcpy(t->f[t->n++], folder, JSRF_FOLDER_CHARS + 1);
}

static int folder_there(const char *title, const char *folder)
{
    char p[JSRF_PATH_MAX];
    js_path(p, sizeof p, title, folder);
    return js_is_dir(p) || js_is_file(p);
}

int jsrf_plan_make(const jsrf_found *f, const int *chosen, const char *title, jsrf_plan *plan)
{
    taken t;
    int i, k;
    memset(plan, 0, sizeof *plan);
    plan->items = (jsrf_plan_item *)calloc((size_t)(f->nsaves ? f->nsaves : 1), sizeof *plan->items);
    t.f = (char (*)[JSRF_FOLDER_CHARS + 1])calloc((size_t)(f->nsaves + 1), JSRF_FOLDER_CHARS + 1);
    t.n = 0;
    if (!plan->items || !t.f) { free(plan->items); free(t.f); plan->items = NULL; return -1; }

    /* First the saves that go where their names say, then the designs whose
     * slot is taken, into what is left. */
    for (i = 0; i < f->nsaves; i++) {
        const jsrf_save *s = &f->saves[i];
        jsrf_plan_item *it;
        if (chosen && !chosen[i]) continue;
        it = &plan->items[plan->n++];
        it->save = i;
        it->action = JSRF_DO_SKIP;
        if (s->kind == JSRF_KIND_OTHER) {
            snprintf(it->note, sizeof it->note, "%s", s->why);
        } else if (s->kind == JSRF_KIND_GRAFFITI && !jsrf_graffiti_resigner) {
            snprintf(it->note, sizeof it->note, "Graffiti designs come across once the graffiti page is in.");
        } else if (is_taken(&t, s->folder)) {
            snprintf(it->note, sizeof it->note, "Another save chosen goes to the same folder, %s.", s->folder);
        } else if (!folder_there(title, s->folder)) {
            it->action = JSRF_DO_WRITE;
            take(&t, s->folder);
        } else if (s->kind == JSRF_KIND_GAME) {
            it->action = JSRF_DO_REPLACE;
            take(&t, s->folder);
            snprintf(it->note, sizeof it->note, "Replaces the save that is there now, after it is backed up.");
        } else {
            it->action = JSRF_DO_RESLOT;           /* a slot found below */
        }
    }
    for (k = 0; k < plan->n; k++) {
        jsrf_plan_item *it = &plan->items[k];
        unsigned slot;
        if (it->action != JSRF_DO_RESLOT) continue;
        it->action = JSRF_DO_SKIP;
        if (!jsrf_graffiti_reslotter) {
            snprintf(it->note, sizeof it->note, "A design is in its slot already.");
            continue;
        }
        for (slot = 0; slot < JSRF_GRAFFITI_SLOTS_SAFE; slot++) {
            uint16_t name[11];
            char folder[JSRF_FOLDER_CHARS + 1];
            graffiti_name(slot, name);
            jsrf_folder_name(name, 11, folder);
            if (is_taken(&t, folder) || folder_there(title, folder)) continue;
            take(&t, folder);
            it->action = JSRF_DO_RESLOT;
            it->slot = (int)slot;
            snprintf(it->note, sizeof it->note, "Its slot is taken, so it goes to GRAFFITI%03u.", slot + 1);
            break;
        }
        if (it->action == JSRF_DO_SKIP)
            snprintf(it->note, sizeof it->note, "Its slot is taken, and there is no free one in the first %d.",
                     JSRF_GRAFFITI_SLOTS_SAFE);
    }
    for (k = 0; k < plan->n; k++) {
        if (plan->items[k].action != JSRF_DO_SKIP) plan->writes++;
        if (plan->items[k].action == JSRF_DO_REPLACE) plan->replaces++;
    }
    free(t.f);
    return 0;
}

int jsrf_plan_run(jsrf_found *f, jsrf_plan *plan, const char *title, const char *backups,
                  const uint8_t key[JSRF_KEY_BYTES])
{
    char why[400];
    int k, bad = 0, r;
    plan->backup[0] = 0;
    plan->why[0] = 0;
    if (!plan->writes) return 0;
    r = jsrf_backup(title, backups, plan->backup, sizeof plan->backup, plan->why, sizeof plan->why);
    if (r == JSRF_FAILED) return -1;

    for (k = 0; k < plan->n; k++) {
        jsrf_plan_item *it = &plan->items[k];
        jsrf_save *s = &f->saves[it->save];
        it->result = JSRF_FAILED;
        if (it->action == JSRF_DO_SKIP) { it->result = JSRF_EXISTS; continue; }
        if (it->action == JSRF_DO_RESLOT) {
            jsrf_file *dat = jsrf_save_file(s, JSRF_GRAFFITI_FILE);
            uint16_t name[11];
            graffiti_name((unsigned)it->slot, name);
            if (!dat || jsrf_graffiti_reslotter(dat->data, dat->size, (unsigned)it->slot, key) != 0 ||
                jsrf_save_rename(s, name, 11) != 0) {
                snprintf(it->note, sizeof it->note, "It couldn't be moved to another slot.");
                bad = 1;
                continue;
            }
        } else if (jsrf_save_resign(s, key) != 0) {
            snprintf(it->note, sizeof it->note, "It couldn't be signed.");
            bad = 1;
            continue;
        }
        r = jsrf_save_write(title, s, it->action == JSRF_DO_REPLACE, why, sizeof why);
        it->result = r;
        if (r == JSRF_WROTE) snprintf(it->note, sizeof it->note, "Written, as %s.", s->folder);
        else if (r == JSRF_REPLACED) snprintf(it->note, sizeof it->note, "Written, in place of the save that was there.");
        else if (r == JSRF_EXISTS) snprintf(it->note, sizeof it->note, "Skipped: %s appeared there meanwhile.", s->folder);
        else { snprintf(it->note, sizeof it->note, "%s", why); bad = 1; }
    }
    for (k = 0; k < f->ntitle; k++)
        if (jsrf_title_file_write(title, &f->title[k], why, sizeof why) == JSRF_FAILED) bad = 1;
    return bad ? -1 : 0;
}

void jsrf_plan_free(jsrf_plan *plan)
{
    free(plan->items);
    plan->items = NULL;
    plan->n = 0;
}
