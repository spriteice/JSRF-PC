/* Run with: python tests/render_scale/run.py
 *
 * The internal resolution, src/nv2a_gl/nv2a_scale.c, as the engine builds it:
 *
 *   1. Each offered scale's name is the scale and the raster it makes of the
 *      Xbox's 640x480, 1x first.
 *   2. RECOMP_GL_SCALE's value is 1 when unset or not a number, a number as
 *      strtol reads it otherwise, and clamped to 1..NV2A_SCALE_ENV_MAX.
 */
#include <stdio.h>
#include <string.h>
#include "nv2a_scale.h"

static int s_failures;

#define CHECK(c) do { if (!(c)) { printf("render_scale: FAIL %s:%d: %s\n", \
                                         __FILE__, __LINE__, #c); s_failures++; } } while (0)

static void names(void)
{
    int i;
    for (i = 0; i < NV2A_SCALE_OFFERED; i++) {
        char want[32];
        snprintf(want, sizeof want, "%dx  %dx%d", i + 1, 640 * (i + 1), 480 * (i + 1));
        CHECK(nv2a_scale_names[i] && !strcmp(nv2a_scale_names[i], want));
    }
}

static void parse(void)
{
    CHECK(nv2a_scale_parse(NULL) == 1);
    CHECK(nv2a_scale_parse("") == 1);
    CHECK(nv2a_scale_parse("sharp") == 1);
    CHECK(nv2a_scale_parse("0") == 1);
    CHECK(nv2a_scale_parse("-3") == 1);
    CHECK(nv2a_scale_parse("1") == 1);
    CHECK(nv2a_scale_parse("3") == 3);
    CHECK(nv2a_scale_parse(" 2") == 2);
    CHECK(nv2a_scale_parse("0x4") == 4);
    CHECK(nv2a_scale_parse("4x") == 4);
    CHECK(nv2a_scale_parse("8") == NV2A_SCALE_ENV_MAX);
    CHECK(nv2a_scale_parse("9") == NV2A_SCALE_ENV_MAX);
    CHECK(nv2a_scale_parse("99999999999999999999") == NV2A_SCALE_ENV_MAX);
}

int main(void)
{
    names();
    parse();
    if (s_failures) {
        printf("render_scale: %d failed\n", s_failures);
        return 1;
    }
    printf("render_scale: all cases pass (%d scales offered, RECOMP_GL_SCALE up to %d)\n",
           NV2A_SCALE_OFFERED, NV2A_SCALE_ENV_MAX);
    return 0;
}
