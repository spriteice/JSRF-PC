/* Run with: python tests/developer/run.py
 *
 * port/developer.c without the game or the menu, reading the engine's own
 * settings code (taken from src/nv2a_gl/nv2a_gl.c by run.py). run.py writes
 * the settings file and developer.cfg beside it, and checks the log this
 * writes: what developer_start put in the environment, that nothing reached
 * the log while Log to file was off, and that developer_restart takes out
 * what it put in and gives the log back.
 */
#include <stdio.h>
#include <stdlib.h>

void developer_start(void);
void developer_restart(void);

static void show(const char *when)
{
    const char *p = getenv("RECOMP_GL_PROFILE"), *w = getenv("RECOMP_LOCK_WATCH");
    fprintf(stderr, "%s: RECOMP_GL_PROFILE=%s RECOMP_LOCK_WATCH=%s\n", when, p ? p : "-", w ? w : "-");
}

int main(void)
{
    developer_start();
    show("started");
    developer_restart();
    show("restarting");
    return 0;
}
