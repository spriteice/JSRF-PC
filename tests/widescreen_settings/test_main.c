/* Run with: python tests/widescreen_settings/run.py
 *
 * The window's widescreen settings without the game: whether widescreen is on
 * at start (nv_window_pref_widescreen) and its shape (nv_window_pref_shape),
 * read through the engine's own settings code (taken from
 * src/nv2a_gl/nv2a_gl.c by run.py). Each case is its own run, with its own
 * settings file or none.
 *
 * With neither widescreen= nor aspect=, as on a new install, widescreen is on
 * at Match screen. A settings file from before aspect= keeps what F10 chose
 * then: widescreen= alone, on at 16:9 or off.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static void expect(int on, int shape)
{
    CHECK(nv_window_pref_widescreen() == on);
    CHECK(nv_window_pref_shape() == shape);
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    if (!strcmp(c, "none") || !strcmp(c, "other_keys")) expect(1, NV2A_WIDE_SHAPE_MATCH);
    else if (!strcmp(c, "old_wide") || !strcmp(c, "bad_shape")) expect(1, NV2A_WIDE_SHAPE_16_9);
    else if (!strcmp(c, "old_43")) expect(0, NV2A_WIDE_SHAPE_16_9);
    else if (!strcmp(c, "shape")) expect(1, NV2A_WIDE_SHAPE_21_9);
    else if (!strcmp(c, "shape_43")) expect(0, NV2A_WIDE_SHAPE_16_10);
    else CHECK(!"a case");
    printf("widescreen settings: %s\n", c);
    return 0;
}
