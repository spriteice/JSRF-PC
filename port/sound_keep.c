/* sound_keep.c -- the sound's slots kept to the game's passes while a run is
 * recorded or played (sound_keep.h). */
#include "sound_keep.h"

#include <stdio.h>
#include <string.h>

/* Where each of the slot step's calls to the sample count (sub_0013A9A0)
 * returns to, in its order: the music's next track (while +0xA4 is set, in
 * the other of the two music ADXTs), the music (+0xBC), then +0xC0, +0xC8,
 * +0xCC and +0xC4. */
static const uint32_t k_site[SOUND_KEEP_SLOTS] = {
    0x00116C5Cu, 0x00116C93u, 0x00116CFBu, 0x00116D56u, 0x00116DB1u, 0x00116DFBu,
};
static const char *const k_name[SOUND_KEEP_SLOTS] = {
    "the music's next track", "the music", "speech (+0xC0)", "+0xC8", "+0xCC", "speech (+0xC4)",
};

static struct {
    int loading;                /* seen loading at the last pass */
    int done;                   /* its pass come: ready, or let go; not held again while it loads */
    uint32_t track, since;      /* the track it loads, and the pass it was first seen loading */
    int data_at;                /* the passes after that its data was first there, or -1 */
} s_slot[SOUND_KEEP_SLOTS];
static uint32_t s_pass;
static int s_on;                /* the passes kept to now; 0: off */
static int s_fresh;             /* a run started: afresh at the next pass (any thread) */
static SOUND_KEEP_STATS s_stats = { 0, 0, -1, -1, 0, 0.0, 0 };

int sound_keep_site(uint32_t from)
{
    int k;
    for (k = 0; k < SOUND_KEEP_SLOTS; k++)
        if (k_site[k] == from) return k;
    return -1;
}

const SOUND_KEEP_STATS *sound_keep_stats(void) { return &s_stats; }

void sound_keep_start(void) { __atomic_store_n(&s_fresh, 1, __ATOMIC_RELEASE); }

void sound_keep_say(void)
{
    const SOUND_KEEP_STATS *s = &s_stats;
    char more[160] = "";
    if (s->waited)
        snprintf(more, sizeof more, "; %d waited for at their pass (at most %.2f s)", s->waited, s->wait_most);
    else
        snprintf(more, sizeof more, "; none waited for");
    if (s->given_up)
        snprintf(more + strlen(more), sizeof more - strlen(more), "; %d not there in %.0f s, ready on their own time",
                 s->given_up, SOUND_KEEP_WAIT);
    if (!s->passes) return;
    if (s->data_least >= 0)
        fprintf(stderr, "[PLAYBACK] the sound kept to %d passes: %d slots ready on their pass, their data there %d"
                " to %d passes after they began loading%s\n", s->passes, s->ready, s->data_least, s->data_most,
                more);
    else
        fprintf(stderr, "[PLAYBACK] the sound kept to %d passes: %d slots ready on their pass%s\n", s->passes,
                s->ready, more);
}

void sound_keep_pass(int passes, const int loading[SOUND_KEEP_SLOTS], const uint32_t track[SOUND_KEEP_SLOTS])
{
    int k;
    if (passes < 0) passes = 0;
    if (__atomic_exchange_n(&s_fresh, 0, __ATOMIC_ACQ_REL)) s_on = -1;
    if (passes != s_on) {
        memset(s_slot, 0, sizeof s_slot);
        s_pass = 0;
        s_on = passes;
        if (passes) {
            memset(&s_stats, 0, sizeof s_stats);
            s_stats.passes = passes;
            s_stats.data_least = s_stats.data_most = -1;
        }
    }
    if (!s_on) return;
    s_pass++;
    for (k = 0; k < SOUND_KEEP_SLOTS; k++) {
        if (!loading[k]) {
            s_slot[k].loading = 0;
            continue;
        }
        if (!s_slot[k].loading || s_slot[k].track != track[k]) {
            s_slot[k].loading = 1;
            s_slot[k].done = 0;
            s_slot[k].track = track[k];
            s_slot[k].since = s_pass;
            s_slot[k].data_at = -1;
        }
    }
}

uint32_t sound_keep_count(int slot, const SOUND_KEEP_IO *io)
{
    uint32_t n = io->count(io->ctx), age;
    double from, waited = 0.0;
    if (!s_on || slot < 0 || slot >= SOUND_KEEP_SLOTS || !s_slot[slot].loading || s_slot[slot].done)
        return n;
    age = s_pass - s_slot[slot].since;
    if (n >= SOUND_KEEP_READY && s_slot[slot].data_at < 0) s_slot[slot].data_at = (int)age;
    if (age < (uint32_t)s_on) return n >= SOUND_KEEP_READY ? SOUND_KEEP_READY - 1u : n;

    /* Its pass: ready, once its data is there. The sound thread decodes it
     * while this one naps. */
    s_slot[slot].done = 1;
    if (n < SOUND_KEEP_READY) {
        from = io->now();
        while (n < SOUND_KEEP_READY && io->now() - from < SOUND_KEEP_WAIT) {
            io->nap(io->ctx);
            n = io->count(io->ctx);
        }
        waited = io->now() - from;
        if (n < SOUND_KEEP_READY) {
            s_stats.given_up++;
            fprintf(stderr, "[PLAYBACK] the sound: %s not there %.0f s after its pass (%u samples of %u):"
                    " ready on its own time\n", k_name[slot], SOUND_KEEP_WAIT, n, SOUND_KEEP_READY);
            return n;
        }
        s_slot[slot].data_at = (int)age;
        s_stats.waited++;
        if (waited > s_stats.wait_most) s_stats.wait_most = waited;
        fprintf(stderr, "[PLAYBACK] the sound: %s waited for at its pass, %u passes after it began loading:"
                " %.3f s\n", k_name[slot], age, waited);
    }
    s_stats.ready++;
    if (s_stats.data_least < 0 || s_slot[slot].data_at < s_stats.data_least)
        s_stats.data_least = s_slot[slot].data_at;
    if (s_slot[slot].data_at > s_stats.data_most) s_stats.data_most = s_slot[slot].data_at;
    return n;
}
