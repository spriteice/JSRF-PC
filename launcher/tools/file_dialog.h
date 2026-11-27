/*
 * The system's own dialogs for picking a folder or files -- see
 * file_dialog.c. The launchers' (IFileOpenDialog on Windows, with the
 * classic folder browser for an old Wine; zenity or kdialog on Linux), with
 * several files at once added, for JSRF-PC Tools. No library of its own.
 *
 * Paths are UTF-8. Each call blocks until the dialog closes.
 */
#ifndef JSRF_FILE_DIALOG_H
#define JSRF_FILE_DIALOG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JSRF_PICKED      1
#define JSRF_CANCELLED   0
#define JSRF_NO_DIALOG  (-1)   /* Linux with neither zenity nor kdialog: drop the files on the window */

/* One folder, into out. */
int jsrf_pick_folder(const char *title, char *out, size_t n);

/* Files: several when multi is 1, each handed to each(). patterns ("*.png",
 * NULL ended; NULL for any file) is what the dialog shows, under kind's
 * name. */
int jsrf_pick_files(const char *title, const char *kind, const char *const *patterns, int multi,
                    void (*each)(void *ctx, const char *path), void *ctx);

#ifdef __cplusplus
}
#endif

#endif
