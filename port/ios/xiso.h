/*
 * Unpacking an Xbox disc image (XDVDFS, "XISO") into a folder.
 *
 * Written for the iPhone, which cannot run another program such as
 * extract-xiso, and used by every app since: the format is small enough to
 * read here -- a volume descriptor 64 KB into the game partition, and
 * directories that are binary trees of entries pointing at sectors. Both
 * kinds of image people have are handled: an XISO (the game partition alone,
 * what extract-xiso and most tools write) and a full disc image with the
 * video partition in front of it. The Mac and Linux apps reach it through the
 * engine (--check-iso, --unpack-iso in port/main.c).
 */
#ifndef JSRF_XISO_H
#define JSRF_XISO_H

#include <stddef.h>
#include <stdint.h>

/* Called now and then while files are written: bytes so far, bytes in all.
 * Return non-zero to stop (the call then fails with "cancelled"). */
typedef int (*xiso_progress_fn)(void *ctx, uint64_t done, uint64_t total);

/* Unpack every file of the image at iso_path into dest_dir (created if
 * needed). 0 on success; -1 on failure, with a sentence for a person in err.
 * A failed or cancelled run leaves what it wrote: the caller unpacks into a
 * folder of its own and removes it. */
int xiso_extract(const char *iso_path, const char *dest_dir,
                 xiso_progress_fn progress, void *ctx,
                 char *err, size_t errlen);

/* 1 if the file looks like an Xbox disc image (either kind), else 0. */
int xiso_probe(const char *iso_path);

/* The first n bytes of the file (a path or "fd:N", like the calls above),
 * whatever it is: how many were read, or -1. */
long xiso_head(const char *path, unsigned char *buf, size_t n);

/* One file in the image's top folder, by name (case does not matter, as on
 * the disc), without unpacking anything: its first bufsize bytes or fewer
 * into buf (*got says how many) and its whole size into *size. 0 if it is
 * there; 1 if the image has no such file (or it is a folder); -1 if this is
 * not an Xbox disc image or it cannot be read, with a sentence in err. */
int xiso_read_file(const char *iso_path, const char *name,
                   unsigned char *buf, size_t bufsize, size_t *got, uint64_t *size,
                   char *err, size_t errlen);

#endif
