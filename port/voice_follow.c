/* The characters' voice lines kept on them as they move (voice_follow.h).
 *
 * A line is a character's when it was played from the bank in the
 * character's +0xC74, at the character's own position, +0xCA4: what the
 * character's voice calls pass (sub_000A1C50 and the others beside it). The
 * position must not be on the caller's stack, which is gone once it
 * returns. A line is let go when it no longer plays, or when the character
 * no longer has that bank, as when it is gone. A line not playing yet on
 * its first frame is given one more.
 *
 * Developer > Logging > RECOMP_APU_STATS, with the effects on: a line when
 * the first voice line is kept, and one when each is let go: how long it
 * was kept, and how far its speaker went from where it began, which is how
 * far behind it would have been left. Every line starts "[APU] effects:". */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "voice_follow.h"
#include "apu_gpfx.h"

#define VF_CHR_VOICE_BANK 0xC74u     /* a character's voice bank, set at 0x00084613 */
#define VF_CHR_POS        0xCA4u     /* where it is, three floats: where its lines are played */
#define VF_VEC3           12u        /* the bytes of three floats */
#define VF_NO_SOUND       0xFFFFFFFFu /* the handle of a play that played nothing */
#define VF_STACK_NEAR     0x10000u   /* this near the caller's stack: one of its locals */
#define VF_KEPT           16u        /* lines kept at once: a few for each character */
#define VF_FRAMES_PER_S   60.0       /* the game's frames, each one sound manager update */

typedef struct {
    uint32_t handle, where, bank;
    float from[3];                   /* where the speaker was when it began */
    float farthest;                  /* the farthest the speaker went from there */
    unsigned frames;                 /* the frames it was kept */
} VfLine;

static VfLine s_kept[VF_KEPT];
static unsigned s_n;
static int s_said;

static uint32_t rd32(const uint8_t *mem, uint32_t at)
{
    uint32_t v;
    memcpy(&v, mem + at, sizeof v);
    return v;
}

static void rdvec(const uint8_t *mem, uint32_t at, float v[3])
{
    memcpy(v, mem + at, VF_VEC3);
}

/* The bank the character at `where` has for its voice. */
static uint32_t voice_bank(const uint8_t *mem, uint32_t where)
{
    return rd32(mem, where - VF_CHR_POS + VF_CHR_VOICE_BANK);
}

void voice_follow_played(const uint8_t *mem, uint32_t ram, uint32_t handle, uint32_t bank,
                         uint32_t where, uint32_t esp)
{
    VfLine *l;
    unsigned i, longest = 0;

    if (!apu_gpfx_on() || !mem || handle == VF_NO_SOUND || where < VF_CHR_POS
        || ram < VF_VEC3 || where > ram - VF_VEC3)
        return;
    if (where + VF_STACK_NEAR > esp && where < esp + VF_STACK_NEAR)
        return;
    if (voice_bank(mem, where) != bank)
        return;
    for (i = 0; i < s_n && s_kept[i].handle != handle; i++)
        if (s_kept[i].frames > s_kept[longest].frames)
            longest = i;
    if (i == s_n) {
        if (s_n < VF_KEPT)
            s_n++;
        else
            i = longest;             /* full: in place of the one kept longest */
    }
    l = &s_kept[i];
    l->handle = handle;
    l->where = where;
    l->bank = bank;
    rdvec(mem, where, l->from);
    l->farthest = 0.0f;
    l->frames = 0;
    if (!s_said && apu_gpfx_timed()) {
        s_said = 1;
        fprintf(stderr, "[APU] effects: the characters' voice lines kept on them as they move,"
                " as the game keeps its looped one (RECOMP_APU_GPFX)\n");
        fflush(stderr);
    }
}

void voice_follow_frame(const uint8_t *mem, voice_follow_playing_fn playing,
                        voice_follow_move_fn move)
{
    unsigned i = 0;

    while (i < s_n) {
        VfLine *l = &s_kept[i];
        int gone = voice_bank(mem, l->where) != l->bank;
        int on = !gone && playing(l->handle);
        if (gone || (!on && l->frames)) {
            if (apu_gpfx_timed()) {
                fprintf(stderr, "[APU] effects: a voice line kept on its speaker for %.1f s,"
                        " who went up to %.0f from where it began%s\n",
                        (double)l->frames / VF_FRAMES_PER_S, (double)l->farthest,
                        gone ? "; its speaker gone" : "");
                fflush(stderr);
            }
            *l = s_kept[--s_n];
            continue;
        }
        if (on) {
            float now[3], d[3], dist;
            move(l->handle, l->where);
            rdvec(mem, l->where, now);
            d[0] = now[0] - l->from[0];
            d[1] = now[1] - l->from[1];
            d[2] = now[2] - l->from[2];
            dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (dist > l->farthest)
                l->farthest = dist;
        }
        l->frames++;
        i++;
    }
}
