/* The sun's glare hidden behind walls (sun_flare.h). */
#include "sun_flare.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOTE_EVERY   2.0    /* seconds between lines while nothing changes */
#define NOTE_LEAST   0.25   /* and the least between any two */

_Static_assert(SUN_FLARE_WINDOW < 64 && SUN_FLARE_HIDE <= SUN_FLARE_WINDOW && SUN_FLARE_SHOW >= 1
               && SUN_FLARE_HIDE + SUN_FLARE_SHOW > SUN_FLARE_WINDOW,
               "the window a bit each in a uint64_t, and never both a hide and a show in it");

static int switch_on(const char *name)
{
    const char *e = getenv(name);
    return e && *e && strcmp(e, "0") != 0;
}

int sun_flare_fix_on(void)
{
    static int on = -1;
    if (on < 0) {
        on = switch_on("JSRF_SUN_FIX");
        if (on) fprintf(stderr, "[SUNFLARE] Sun flare fix on (JSRF_SUN_FIX): the flare's line tested along each piece\n");
    }
    return on;
}

int sun_flare_log_on(void)
{
    static int on = -1;
    if (on < 0) on = switch_on("JSRF_SUN_LOG");
    return on;
}

int sun_flare_steady(SunFlareHold *hold, uint32_t flare, int blocked)
{
    const uint64_t all = ((uint64_t)1 << SUN_FLARE_WINDOW) - 1;
    uint64_t bits;
    int n = 0;
    blocked = blocked != 0;
    if (hold->flare != flare) {
        hold->flare = flare;
        hold->shown = blocked;
        hold->recent = blocked ? all : 0;
        return blocked;
    }
    hold->recent = ((hold->recent << 1) | (uint64_t)blocked) & all;
    for (bits = hold->recent; bits; bits &= bits - 1) n++;
    if (!hold->shown && n >= SUN_FLARE_HIDE) hold->shown = 1;
    else if (hold->shown && SUN_FLARE_WINDOW - n >= SUN_FLARE_SHOW) hold->shown = 0;
    return hold->shown;
}

void sun_flare_ends(const float lo[3], const float hi[3], const float dir[3],
                    float a[3], float b[3])
{
    int k;
    for (k = 0; k < 3; k++) {
        a[k] = dir[k] < 0 ? hi[k] : lo[k];
        b[k] = dir[k] < 0 ? lo[k] : hi[k];
    }
}

static void piece_text(char *buf, size_t n, int piece)
{
    if (piece < 0) snprintf(buf, n, "clear");
    else           snprintf(buf, n, "blocked at piece %d", piece + 1);
}

int sun_flare_note(const SunFlareTest *test)
{
    static int      have;
    static double   last_t;
    static uint32_t last_flare;
    static int      last_game, last_real, held;
    int game = test->game >= 0, real = test->real >= 0, changed;
    char mirrored[8] = "", game_text[32], real_text[32];
    double len = sqrt((double)test->dir[0] * test->dir[0] + (double)test->dir[1] * test->dir[1]
                      + (double)test->dir[2] * test->dir[2]);
    int k;

    changed = !have || test->flare != last_flare || game != last_game || real != last_real;
    if (have && test->t - last_t < (changed ? NOTE_LEAST : NOTE_EVERY)) {
        if (changed) held++;
        return 0;
    }
    for (k = 0; k < 3; k++)
        if (test->dir[k] < 0) {
            size_t at = strlen(mirrored);
            snprintf(mirrored + at, sizeof mirrored - at, "%s%c", at ? " " : "", "xyz"[k]);
        }
    piece_text(game_text, sizeof game_text, test->game);
    piece_text(real_text, sizeof real_text, test->real);
    if (!(len > 0)) len = 1;
    fprintf(stderr, "[SUNFLARE] t=%.2f flare %08X state %d: eye (%.0f, %.0f, %.0f), sun's way "
            "(%.2f, %.2f, %.2f), game's line mirrored in %s; %d pieces, %d tested; "
            "game's line %s, real line %s; fix %s, so the glare shows %s%s\n",
            test->t, test->flare, test->state, test->eye[0], test->eye[1], test->eye[2],
            test->dir[0] / len, test->dir[1] / len, test->dir[2] / len,
            *mirrored ? mirrored : "none", test->pieces, test->tested, game_text, real_text,
            test->fix ? "on" : "off", test->shown ? "blocked" : "in sight",
            held ? " (changes held back since the last line)" : "");
    have = 1;
    last_t = test->t;
    last_flare = test->flare;
    last_game = game;
    last_real = real;
    held = 0;
    return 1;
}
