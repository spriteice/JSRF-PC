/*
 * Reading an Xbox hard disk image, and the FATX filesystems on it -- see
 * xbox_disk.c.
 *
 * For importing saves from an emulator that keeps the console's whole disk
 * as one image file (xemu: qcow2, or raw). Read only: the image is never
 * written, and nothing in it is trusted.
 */
#ifndef XBOX_DISK_H
#define XBOX_DISK_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* A retail disk's E: partition (Partition1), where UDATA and TDATA are: its
 * first sector and its length in sectors, as src/kernel/kernel_path.c's
 * partition table gives them. */
#define XBOX_DISK_SECTOR          512ull
#define XBOX_DISK_E_FIRST_SECTOR  0x0055F400ull
#define XBOX_DISK_E_SECTORS       0x00465400ull
#define XBOX_DISK_E_OFFSET        (XBOX_DISK_E_FIRST_SECTOR * XBOX_DISK_SECTOR)
#define XBOX_DISK_E_LENGTH        (XBOX_DISK_E_SECTORS * XBOX_DISK_SECTOR)

/* ---- the image ------------------------------------------------------------ */

typedef struct xbox_disk xbox_disk;

/* Opens the image in f (opened for reading, in binary, by the caller, who
 * closes it after xbox_disk_close): qcow2 when it starts with qcow2's magic,
 * and otherwise a raw image. NULL, with why in err, for a qcow2 image this
 * reader doesn't handle (a backing file, encryption, an external data file,
 * extended L2 entries, a compression other than deflate). */
xbox_disk *xbox_disk_open(FILE *f, char *err, size_t errn);

/* The disk's length in bytes, as the guest sees it. */
uint64_t xbox_disk_size(const xbox_disk *d);

/* 1 if the image is qcow2, 0 if raw. */
int xbox_disk_is_qcow2(const xbox_disk *d);

/* Reads n bytes at off into buf. Clusters an image never wrote read as
 * zeros. 0, or -1 when the range is past the disk's end or the image is
 * damaged there. */
int xbox_disk_read(xbox_disk *d, uint64_t off, void *buf, size_t n);

void xbox_disk_close(xbox_disk *d);

/* ---- FATX ------------------------------------------------------------------ */

#define FATX_NAME_MAX   42
#define FATX_ATTR_DIR   0x10

typedef struct fatx fatx;

typedef struct fatx_entry {
    char     name[FATX_NAME_MAX + 1];
    uint8_t  attr;
    uint32_t first;     /* the first cluster: 0 for an empty file */
    uint32_t size;      /* bytes, for a file */
    uint32_t written;   /* the last write: FAT date in the high half, time in the low */
} fatx_entry;

/* The FATX filesystem at base, length bytes long, on d. NULL, with why in
 * err, when there is none there or its numbers don't fit the partition. */
fatx *fatx_open(xbox_disk *d, uint64_t base, uint64_t length, char *err, size_t errn);

/* The root directory, as an entry fatx_list takes. */
fatx_entry fatx_root(const fatx *f);

/* The directory dir's entries, deleted ones left out, into *out (malloc'ed,
 * the caller frees it). Their count, or -1 when the directory is damaged or
 * isn't one. A name that could leave a folder it is copied into (".", "..",
 * a slash or a backslash, a control character) makes the directory damaged. */
int fatx_list(fatx *f, const fatx_entry *dir, fatx_entry **out);

/* The entry at path, its parts split by '/' or '\\' and matched without
 * regard to case, as FATX does; "" is the root. 1 found, 0 not there, -1
 * damaged on the way. */
int fatx_find(fatx *f, const char *path, fatx_entry *out);

/* A file's contents, whole, into *data (malloc'ed, the caller frees it; NULL
 * for an empty file). 0, or -1 when its clusters don't hold its size, or
 * it is larger than FATX_FILE_MAX. */
#define FATX_FILE_MAX   (64u * 1024 * 1024)
int fatx_read(fatx *f, const fatx_entry *file, uint8_t **data);

void fatx_close(fatx *f);

#endif
