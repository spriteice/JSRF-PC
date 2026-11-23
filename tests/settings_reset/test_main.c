/* Run with: python3 tests/settings_reset/run.py
 *
 * File > Presets > Reset's start without the game: what port/main.c calls
 * first (nv_window_reset_at_start, which calls nv_window_settings_reset),
 * taken from src/nv2a_gl/nv2a_gl.c by run.py with the settings file code
 * they use. run.py writes the settings file and the files beside it, and
 * runs this as the new start runs, told to reset or not; this then says
 * whether a later start (File > Restart's) would be told so too.
 */
#include <stdio.h>
#include <stdlib.h>

void nv_window_reset_at_start(void);

int main(void)
{
    const char *e;
    nv_window_reset_at_start();
    e = getenv("RECOMP_RESET_SETTINGS");
    fprintf(stderr, "after: RECOMP_RESET_SETTINGS=%s\n", e && *e ? e : "-");
    return 0;
}
