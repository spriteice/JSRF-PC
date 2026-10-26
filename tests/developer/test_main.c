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

/* Two of Logging's switches, and two of Engine switches': one that is set
 * to 1 and one that is set to 0. A line each, "-" when it is not set. */
static void show(const char *when)
{
    static const char *const name[] = { "RECOMP_GL_PROFILE", "RECOMP_LOCK_WATCH",
                                        "RECOMP_GL_TEX_ONESAMPLER", "RECOMP_GL_TITLE_MIPS" };
    size_t i;
    for (i = 0; i < sizeof name / sizeof name[0]; i++) {
        const char *v = getenv(name[i]);
        fprintf(stderr, "%s: %s=%s\n", when, name[i], v ? v : "-");
    }
}

int main(void)
{
    developer_start();
    show("started");
    developer_restart();
    show("restarting");
    return 0;
}
