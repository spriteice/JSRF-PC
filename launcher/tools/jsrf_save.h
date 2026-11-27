/*
 * JSRF's saves, as JSRF-PC Tools reads and writes them -- see jsrf_save.c.
 *
 * The game keeps its saves in UDATA\5345000a, one folder a save: the game's
 * own (JSRFDATA.SAV) and each graffiti design (GRAFFITInnn, save.dat), each
 * with a SaveMeta.xbx that names it. XAPI signs each file with HMAC-SHA1
 * keyed by the kernel's XboxSignatureKey, roamable (no console's key mixed
 * in), and names each folder by a hash of the save's name with no key, the
 * same on every console and emulator. JSRF-PC's kernel gives 16 zero bytes
 * as that key (src/kernel/kernel_bridge.c), so a save signed anywhere else
 * is re-signed with them on its way in.
 *
 * Paths are UTF-8.
 */
#ifndef JSRF_SAVE_H
#define JSRF_SAVE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JSRF_TITLE_ID       "5345000a"
#define JSRF_KEY_BYTES      16
#define JSRF_SIG_BYTES      20
#define JSRF_FOLDER_CHARS   12            /* a save folder's name: 12 hex digits */
#define JSRF_NAME_UNITS     128           /* the most UTF-16 units of a save's name kept */
#define JSRF_GAME_FILE      "JSRFDATA.SAV"
#define JSRF_GAME_SIZE      41088u        /* 20 bytes of signature, then 41,068 starting "JSRF" */
#define JSRF_GRAFFITI_FILE  "save.dat"
#define JSRF_META_FILE      "SaveMeta.xbx"
#define JSRF_SAVE_FILES     8             /* the most files a save folder holds that are kept */
#define JSRF_FILE_MAX       (4u << 20)    /* the largest: an XL graffiti save is about 1.8 MB */

/* ---- signatures and names -------------------------------------------------- */

/* JSRF-PC's XboxSignatureKey (kernel ordinal 325): 16 zero bytes. */
extern const uint8_t jsrf_signature_key[JSRF_KEY_BYTES];

/* HMAC-SHA1 of data under key: src/kernel/kernel_crypto.c's XcHMAC, the one
 * the game signs with (kernel_crypto_host.c). */
void jsrf_hmac_sha1(const uint8_t *key, size_t kn, const void *data, size_t n, uint8_t out[JSRF_SIG_BYTES]);

/* HMAC-SHA1 of data under key, a title's XboxSignatureKey: the roamable
 * signature XAPI's XCalculateSignature gives on a console with that key. */
void jsrf_sign(const uint8_t key[JSRF_KEY_BYTES], const void *data, size_t n, uint8_t sig[JSRF_SIG_BYTES]);

/* A file whose first 20 bytes sign the rest, as JSRFDATA.SAV: 1 if they do
 * under key, 0 if not, -1 if it is shorter than a signature. */
int jsrf_file_verifies(const uint8_t key[JSRF_KEY_BYTES], const uint8_t *file, size_t n);
/* Its first 20 bytes rewritten to sign the rest under key. 0, or -1 if too
 * short. */
int jsrf_file_resign(const uint8_t key[JSRF_KEY_BYTES], uint8_t *file, size_t n);

/* Cxbx-Reloaded's XboxSignatureKey for the game in xbe (its default.xbe,
 * read whole or its first 64 KB): HMAC-SHA1 of the certificate's signature
 * key under Cxbx's certificate key, which is 16 zero bytes unless the player
 * gave Cxbx a keys.bin, cut to 16 bytes, as Cxbx's kernel makes it. 0, or
 * -1 when xbe isn't an XBE whose certificate is in what was given, or isn't
 * JSRF's (title 5345000a). Worked out from the player's own file at run
 * time: never kept. */
int jsrf_cxbx_key(const uint8_t *xbe, size_t n, uint8_t key[JSRF_KEY_BYTES]);

/* The folder XAPI keeps a save named name (UTF-16 units) in: for each unit
 * c, v = (v * 65536 + c) mod 0xFFFFFFFFFFC5, from 0, as 12 upper-case hex
 * digits, most significant first. */
void jsrf_folder_name(const uint16_t *name, size_t units, char out[JSRF_FOLDER_CHARS + 1]);

/* The name in a SaveMeta.xbx (UTF-16, little-endian, a byte order mark
 * first, lines "Key=value"): its Name line's value into name. Its length in
 * units, or -1 when there is none, or it is longer than cap. */
int jsrf_meta_name(const uint8_t *meta, size_t n, uint16_t *name, size_t cap);
/* A SaveMeta.xbx for a save named name, as XAPI writes one: the byte order
 * mark, "Name=", the name and CR LF. Its length, or 0 if out is too small. */
size_t jsrf_meta_build(const uint16_t *name, size_t units, uint8_t *out, size_t cap);

/* UTF-16 to UTF-8, for showing a name: a unit that isn't valid is shown as
 * U+FFFD. Always ends out with a 0. */
void jsrf_utf16_to_utf8(const uint16_t *s, size_t units, char *out, size_t cap);

/* ---- a save, read whole ----------------------------------------------------- */

#define JSRF_KIND_GAME      1   /* JSRFDATA.SAV: the game's progress */
#define JSRF_KIND_GRAFFITI  2   /* GRAFFITInnn's save.dat: a design */
#define JSRF_KIND_OTHER     0   /* neither: never imported */

typedef struct jsrf_file {
    char     name[64];
    uint8_t *data;              /* malloc'ed; NULL for an empty file */
    size_t   size;
} jsrf_file;

typedef struct jsrf_save {
    char      from[256];        /* its folder's name where it was found */
    int       nfiles;
    jsrf_file files[JSRF_SAVE_FILES];
    /* jsrf_save_check's */
    int       kind;
    uint16_t  name16[JSRF_NAME_UNITS];
    int       units;
    char      name[JSRF_NAME_UNITS * 3 + 1];   /* the name, UTF-8 */
    char      folder[JSRF_FOLDER_CHARS + 1];   /* the folder its name gives */
    int       signed_here;      /* 1: every signature verifies with JSRF-PC's key as found */
    char      why[200];         /* when kind is OTHER: why it isn't imported */
} jsrf_save;

/* A file of the save by name, without regard to case, as FATX matches. */
jsrf_file *jsrf_save_file(jsrf_save *s, const char *name);

/* Adds a file, the data taken over (freed with the save). 0, or -1 when the
 * save already holds JSRF_SAVE_FILES (the data is freed). */
int jsrf_save_add(jsrf_save *s, const char *name, uint8_t *data, size_t size);

/* What the save is (kind, name, folder, signed_here), and why when it isn't
 * one to import (kind OTHER). Returns kind. */
int jsrf_save_check(jsrf_save *s);

/* Re-signs a graffiti save's save.dat under key: graffiti_save.c's (b3 row
 * 35). 0, or -1 when the file isn't laid out as the game's are. */
typedef int (*jsrf_graffiti_resign_fn)(uint8_t *file, size_t n, const uint8_t key[JSRF_KEY_BYTES]);
extern jsrf_graffiti_resign_fn jsrf_graffiti_resigner;

/* Moves a graffiti save's save.dat to slot (0 for GRAFFITI001) and re-signs
 * it under key: graffiti_save.c's (b3 row 35). 0, or -1 as above. */
typedef int (*jsrf_graffiti_reslot_fn)(uint8_t *file, size_t n, unsigned slot, const uint8_t key[JSRF_KEY_BYTES]);
extern jsrf_graffiti_reslot_fn jsrf_graffiti_reslotter;

/* The first graffiti slot moves are kept below: the header's slot byte past
 * it isn't known yet (custom-graffiti/FORMAT.md). */
#define JSRF_GRAFFITI_SLOTS_SAFE  228

/* The save given a new name: its SaveMeta.xbx written for it, and checked
 * again (its folder follows the name). 0, or -1. */
int jsrf_save_rename(jsrf_save *s, const uint16_t *name, size_t units);

/* Every signature in the save rewritten under key (jsrf_signature_key for
 * JSRF-PC). 0, or -1 when it can't be (a graffiti save before the graffiti
 * page gives its re-sign). */
int jsrf_save_resign(jsrf_save *s, const uint8_t key[JSRF_KEY_BYTES]);

void jsrf_save_free(jsrf_save *s);

/* ---- writing ------------------------------------------------------------------ */

#define JSRF_WROTE     0
#define JSRF_EXISTS    1        /* a save folder of that name is there: nothing written */
#define JSRF_REPLACED  2        /* it was there, and was replaced, as asked */
#define JSRF_FAILED    (-1)

/* 1 when title already has a save folder of the save's name (s->folder). */
int jsrf_save_exists(const char *title, const jsrf_save *s);

/* The save written into title (JSRF-PC's UDATA\5345000a, made if it isn't
 * there), in the folder its name gives (s->folder). When that folder is
 * there, nothing is written (JSRF_EXISTS), unless replace is 1: then the
 * save is written beside it first, and only then swapped in, the old one
 * removed last (the caller backs it up before, with jsrf_backup). A folder
 * this makes and can't finish is removed again, and a swap that fails
 * puts the old folder back. why gets a sentence when it fails. */
int jsrf_save_write(const char *title, const jsrf_save *s, int replace, char *why, size_t n);

/* A file of the title's own (TitleMeta.xbx, TitleImage.xbx, SaveImage.xbx)
 * written into title only if it has none of that name. */
int jsrf_title_file_write(const char *title, const jsrf_file *f, char *why, size_t n);

/* A dated copy of title made under backups: backups\<YYYY-MM-DD HH.MM.SS>\
 * 5345000a, the time the local one, " (2)" and on added when that is taken.
 * Never over anything. The folder made, in made. JSRF_WROTE, JSRF_EXISTS
 * when there is no title folder yet (nothing to back up), or JSRF_FAILED
 * (what was made is removed). */
int jsrf_backup(const char *title, const char *backups, char *made, size_t mn, char *why, size_t n);

#ifdef __cplusplus
}
#endif

#endif
