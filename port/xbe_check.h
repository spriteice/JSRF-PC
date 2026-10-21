/*
 * JSRF Unleashed - is this the game the engine is made from?
 *
 * The engine runs one build of Jet Set Radio Future: the standalone US disc's
 * default.xbe. See xbe_check.c.
 */
#ifndef JSRF_XBE_CHECK_H
#define JSRF_XBE_CHECK_H

#include <stddef.h>

#define JSRF_XBE_OK         0   /* the standalone US disc's program (or a patched copy of it) */
#define JSRF_XBE_MISSING    2   /* no default.xbe, or it cannot be read */
#define JSRF_XBE_OTHER      3   /* another program: another build of the game, or another game */
#define JSRF_XBE_NOT_IMAGE  4   /* a file that is not an Xbox disc image (jsrf_xbe_check_iso) */

/* x, n: the start of default.xbe -- its header and section table, the first
 * 4 KB or the whole file. Returns one of the codes above; for anything but
 * JSRF_XBE_OK, why gets a sentence or two saying what the file is. */
int jsrf_xbe_check(const unsigned char *x, size_t n, char *why, size_t whyn);

/* The same for <game folder>/default.xbe, read from disk. */
int jsrf_xbe_check_folder(const char *game_dir, char *why, size_t whyn);

/* The same for a disc image, before unpacking it: whether it is an Xbox disc
 * image at all (or a zip, a compressed image...), then its default.xbe, read
 * from inside it (port/ios/xiso.c). */
int jsrf_xbe_check_iso(const char *iso_path, char *why, size_t whyn);

/* After a JSRF_XBE_OTHER: how that file's section table differs from the
 * standalone US build's, for a log (the player's message leaves it out). */
const char *jsrf_xbe_detail(void);

/* What a player is told when the code is JSRF_XBE_OTHER, after "why". */
#define JSRF_XBE_OTHER_ADVICE \
    "JSRF Unleashed is translated from one build of the game, address for address: the " \
    "standalone US disc of Jet Set Radio Future (NTSC, 2002). It runs only that disc's files. " \
    "The European and Japanese releases and the Sega GT 2002 + JSRF combo discs are built " \
    "differently, and the game would crash a few seconds in. Use the files of a standalone " \
    "US disc."

/* The heading over any of these messages, in every app. */
#define JSRF_XBE_HEADING "These files won't work with JSRF Unleashed"

#endif
