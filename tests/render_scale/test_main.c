/* Run with: python tests/render_scale/run.py
 *
 * The internal resolution, src/nv2a_gl/nv2a_scale.c, as the engine builds it:
 *
 *   1. There are six scales, and each one's name is the scale and the raster
 *      it makes of the Xbox's 640x480, 1x first.
 *   2. RECOMP_GL_SCALE's value is 1 when unset or not a number, a number as
 *      strtol reads it otherwise, and clamped to 1..NV2A_SCALE_MAX: 1x to 6x.
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
    CHECK(NV2A_SCALE_MAX == 6);
    CHECK(sizeof nv2a_scale_names / sizeof nv2a_scale_names[0] == NV2A_SCALE_MAX);
    CHECK(NV2A_SCALE_FRAME_W == 640 && NV2A_SCALE_FRAME_H == 480);
    for (i = 0; i < NV2A_SCALE_MAX; i++) {
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
    CHECK(nv2a_scale_parse("5") == 5);
    CHECK(nv2a_scale_parse("6") == 6);
    CHECK(nv2a_scale_parse("7") == 6);
    CHECK(nv2a_scale_parse("8") == 6);
    CHECK(nv2a_scale_parse("99999999999999999999") == 6);
}

int main(void)
{
    names();
    parse();
    if (s_failures) {
        printf("render_scale: %d failed\n", s_failures);
        return 1;
    }
    printf("render_scale: all cases pass (1x to %dx)\n", NV2A_SCALE_MAX);
    return 0;
}
