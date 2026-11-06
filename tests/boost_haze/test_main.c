/* Run with: python tests/boost_haze/run.py
 *
 * wide_haze_corners (port/recomp_manual.c), cut by text, against a guest
 * memory that holds the haze's four vertices where the game keeps them. The
 * game's part is played here: put() is the game placing the corners (as
 * sub_000A2960 does on a boost's first frame, and sub_000A4D80 when the
 * camera is made), and each call of wide_haze_corners is the end of one of
 * those functions.
 *
 *   1. In 4:3 nothing changes, and nothing is logged.
 *   2. Widened, the corners' x is divided by the squeeze; y, z and the
 *      texture coordinates stay.
 *   3. Called again without a new placing, they don't move again.
 *   4. A new placing is widened from what the game put, not from before.
 *   5. Another shape: from what the game put, by the new squeeze.
 *   6. Widescreen off: back to what the game put.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *s_guest;
static int s_fail;

static float *vert(int i) { return (float *)(s_guest + HAZE_VERTS + (uint32_t)i * HAZE_STRIDE); }

/* The game's corners at depth 2.45 under its 60-degree camera: x +-1.4145,
 * y +-1.0609, z 2.45, then the two pairs of texture coordinates. */
static void put(float xs)
{
    static const float cx[4] = { -1.0f, 1.0f, -1.0f, 1.0f }, cy[4] = { 1.0f, 1.0f, -1.0f, -1.0f };
    int i, k;
    for (i = 0; i < 4; i++) {
        float *v = vert(i);
        v[0] = cx[i] * xs; v[1] = cy[i] * 1.0609f; v[2] = 2.45f;
        for (k = 3; k < 7; k++) v[k] = 10.0f * (float)i + (float)k;
    }
}

static void want(float xs, const char *what)
{
    static const float cx[4] = { -1.0f, 1.0f, -1.0f, 1.0f }, cy[4] = { 1.0f, 1.0f, -1.0f, -1.0f };
    int i, k;
    for (i = 0; i < 4; i++) {
        const float *v = vert(i);
        int ok = fabsf(v[0] - cx[i] * xs) < 1e-5f && v[1] == cy[i] * 1.0609f && v[2] == 2.45f;
        for (k = 3; k < 7; k++) ok = ok && v[k] == 10.0f * (float)i + (float)k;
        if (!ok) {
            fprintf(stderr, "FAIL %s: vertex %d is %.5f %.5f %.5f, x should be %.5f\n",
                    what, i, v[0], v[1], v[2], cx[i] * xs);
            s_fail++;
        }
    }
}

static void logged(unsigned n, const char *what)
{
    if (s_logged != n) {
        fprintf(stderr, "FAIL %s: %u log lines, not %u\n", what, s_logged, n);
        s_fail++;
    }
}

int main(void)
{
    const float x = 1.4145f, s169 = 0.75f, s219 = (4.0f / 3.0f) / (21.0f / 9.0f);
    s_guest = calloc(1, HAZE_VERTS + 4 * HAZE_STRIDE + 16);
    if (!s_guest) return 1;
    g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)s_guest;

    /* 1. 4:3. */
    put(x);
    wide_haze_corners("made");
    want(x, "4:3, made");
    put(x);
    wide_haze_corners("boost");
    want(x, "4:3, a boost");
    logged(0, "4:3");

    /* 2. 16:9: the quad as wide as the widened view. */
    s_wide_on = 1; s_squeeze = s169;
    put(x);
    wide_haze_corners("boost");
    want(x / s169, "16:9, a boost");
    logged(1, "16:9, a boost");

    /* 3. The frames after: the game draws from them and doesn't place them. */
    wide_haze_corners("frame");
    wide_haze_corners("frame");
    want(x / s169, "16:9, frames after");
    logged(1, "16:9, frames after");

    /* 4. The next boost places them again. */
    put(x);
    wide_haze_corners("boost");
    want(x / s169, "16:9, the next boost");
    logged(2, "16:9, the next boost");

    /* 5. 21:9 in the middle of a boost. */
    s_squeeze = s219;
    wide_haze_corners("frame");
    want(x / s219, "21:9, mid-boost");
    logged(3, "21:9, mid-boost");

    /* 6. Widescreen off, then a boost placing them in 4:3. */
    s_wide_on = 0;
    wide_haze_corners("frame");
    want(x, "off, mid-boost");
    put(x);
    wide_haze_corners("boost");
    want(x, "off, a boost");
    logged(4, "off");

    free(s_guest);
    if (s_fail) {
        fprintf(stderr, "boost_haze: %d failed\n", s_fail);
        return 1;
    }
    printf("boost_haze: all cases pass\n");
    return 0;
}
