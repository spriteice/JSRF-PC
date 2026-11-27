/*
 * A graffiti design as the game saves one -- see graffiti_save.c. JSRF-PC
 * Tools' graffiti page (b3 row 35), and the saves page's re-sign of graffiti
 * saves brought from elsewhere (b3 row 36).
 */
#ifndef GRAFFITI_SAVE_H
#define GRAFFITI_SAVE_H

#include <stddef.h>
#include <stdint.h>

#include "jsrf_save.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The garage's own-designs tab: 10 pages of 7 across by 4 down, shared with
 * the designs painted in the game's editor. */
#define GRAFFITI_SLOTS        280
#define GRAFFITI_PAGE_SLOTS   28
#define GRAFFITI_COLUMNS      7
#define GRAFFITI_ROWS         4

/* RGB565's clear: the game draws no pixel of this colour. */
#define GRAFFITI_CLEAR        0xC618u

/* An 8-bit colour as the design's RGB565, rounded to nearest, and moved one
 * step of blue off GRAFFITI_CLEAR should it land there, so it stays solid. */
uint16_t graffiti_rgb565(uint8_t r, uint8_t g, uint8_t b);

/* save.dat's length for a size: the header, seven signatures and six blocks.
 * 0 for a size that isn't one. */
size_t graffiti_save_size(int size);

/* save.dat for a design: design is the size's exact pixels in RGBA
 * (graffiti_fit's), a pixel with alpha under 128 clear; slot from 0; signed
 * with key (jsrf_signature_key for JSRF-PC). out holds
 * graffiti_save_size(size) bytes. 0, or -1 for a size or slot that isn't
 * one. */
int graffiti_save_build(const uint8_t *design, int size, unsigned slot,
                        const uint8_t key[JSRF_KEY_BYTES], uint8_t *out);

/* A save.dat's seven signatures written again under key: 0, or -1 when it
 * isn't laid out as the game's are (its length for its size class, the
 * class, and "JSRF" at its fifth byte), and then nothing is changed. */
int jsrf_graffiti_resign(uint8_t *file, size_t n, const uint8_t key[JSRF_KEY_BYTES]);

/* A save.dat moved to another slot: its header's slot byte, then the same
 * re-sign. 0, or -1 as jsrf_graffiti_resign, or for a slot that isn't one. */
int jsrf_graffiti_reslot(uint8_t *file, size_t n, unsigned slot, const uint8_t key[JSRF_KEY_BYTES]);

/* 1 when every signature of a save.dat verifies under key, 0 when one
 * doesn't, -1 when it isn't laid out as the game's are. Reads only. */
int graffiti_save_verifies(const uint8_t *file, size_t n, const uint8_t key[JSRF_KEY_BYTES]);

/* A save.dat's size class, or -1 when it isn't laid out as the game's are. */
int graffiti_save_class(const uint8_t *file, size_t n);

/* ---- slots ---------------------------------------------------------------- */

/* A slot's save name, GRAFFITI001 for slot 0 to GRAFFITI280, in UTF-16 (11
 * units, then a 0) and in ASCII. */
void graffiti_slot_name16(unsigned slot, uint16_t name[12]);
void graffiti_slot_name(unsigned slot, char name[12]);
/* The save folder the game keeps a slot's design in: its name's hash. */
void graffiti_slot_folder(unsigned slot, char out[JSRF_FOLDER_CHARS + 1]);
/* Where a slot is in the garage: its page (0 to 9), row (0 to 3) and
 * column (0 to 6), each from 0. */
void graffiti_slot_place(unsigned slot, int *page, int *row, int *column);

/* Which slots hold a design in title (JSRF-PC's UDATA\5345000a): taken[s]
 * set to 1 for each slot whose folder is there, 0 for the rest. The number
 * taken. A title folder that isn't there has none. */
int graffiti_slots_taken(const char *title, uint8_t taken[GRAFFITI_SLOTS]);
/* The first slot from `from` on that isn't taken, or -1 when none is. */
int graffiti_next_free(const uint8_t taken[GRAFFITI_SLOTS], unsigned from);

/* ---- the files a design's save folder holds -------------------------------- */

/* The save icon the game gives a size, in its Media\Mark: SaveImageSS.xbx,
 * SaveImage_S.xbx, SaveImage_M.xbx, SaveImage_L.xbx or SaveImageXL.xbx. */
const char *graffiti_icon_file(int size);

/* That icon read from the game in game (the folder default.xbe is in), its
 * names matched without regard to case, into a malloc'ed *data. 0, or -1
 * when it isn't there. */
int graffiti_icon_read(const char *game, int size, uint8_t **data, size_t *n);

/* A design's whole save, ready for jsrf_save_write: save.dat
 * (graffiti_save_build's), SaveMeta.xbx naming its slot, and the icon as
 * SaveImage.xbx (copied; none when icon is NULL), then jsrf_save_check'ed.
 * 0, or -1 when memory runs out or the design can't be built (s is then
 * freed). */
int graffiti_save_make(jsrf_save *s, const uint8_t *design, int size, unsigned slot,
                       const uint8_t *icon, size_t icon_n, const uint8_t key[JSRF_KEY_BYTES]);

#ifdef __cplusplus
}
#endif

#endif
