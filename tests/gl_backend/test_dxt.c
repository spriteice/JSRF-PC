/* Run with: python tests/gl_backend/run.py
 *
 * DXT textures handed to the OpenGL backend as they are (tex_new_dxt in
 * src/nv2a_gl/nv2a_gfx_gl.c), through the device interface the way the front
 * half hands them over, on a headless EGL context (context.h). DXT1 with the
 * title's chain of four levels, DXT3 and DXT5 with one, and a DXT1 that isn't
 * a whole number of blocks: each level is read back from the driver and
 * compared with the front half's own decoder (d3d8_dxt_decode_block), which
 * is what it was before. The random blocks include DXT1's three-colour ones,
 * whose fourth colour is transparent. Colour may differ by rounding (a
 * driver widens 5 and 6 bits its own way); a transparent texel may not. A
 * chain the driver would have to make itself, and a format that isn't DXT1,
 * 3 or 5, are refused, so the front half decodes those.
 */
#include "context.h"
#include <stdlib.h>
#include <string.h>
#include "nv2a_backend.h"
#include "../../src/d3d/d3d8_swizzle.h"

static const Nv2aGfx *G;

static void check(int ok, const char *what)
{
    GLenum e = glGetError();
    if (!ok || e != GL_NO_ERROR) {
        fprintf(stderr, "FAIL %s%s (GL error 0x%x)\n", what, ok ? "" : ": wrong", e);
        exit(1);
    }
}

/* Level `l` of `tex`, w x h, against the front half's decode of `blocks`. */
static void same(uint32_t tex, int l, uint32_t w, uint32_t h, uint32_t fmt,
                 const uint8_t *blocks, const char *what)
{
    uint32_t bw = (w + 3) / 4, bb = d3d8_format_dxt_block_bytes(fmt), x, y, worst = 0;
    uint8_t *got = malloc((size_t)w * h * 4);
    char msg[128];
    glActiveTexture(GL_TEXTURE0 + 5);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetTexImage(GL_TEXTURE_2D, l, GL_RGBA, GL_UNSIGNED_BYTE, got);
    glBindTexture(GL_TEXTURE_2D, 0);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            uint32_t px[16], argb, c;
            const uint8_t *g = got + ((size_t)y * w + x) * 4;
            d3d8_dxt_decode_block(blocks + ((size_t)(y / 4) * bw + x / 4) * bb, fmt, px);
            argb = px[(y & 3) * 4 + (x & 3)];
            for (c = 0; c < 3; c++) {
                int want = (int)((argb >> (16 - 8 * c)) & 0xFF), d = abs(g[c] - want);
                if ((uint32_t)d > worst) worst = (uint32_t)d;
            }
            /* Alpha: exact where DXT1 says transparent and for DXT3's
             * 4 bits; DXT5's interpolated alpha may round. */
            if (abs(g[3] - (int)(argb >> 24)) > (fmt == 0x0F ? 1 : 0)) {
                snprintf(msg, sizeof msg, "%s, level %d: alpha at %u,%u is %u, decoded %u",
                         what, l, x, y, g[3], argb >> 24);
                check(0, msg);
            }
        }
    snprintf(msg, sizeof msg, "%s, level %d: colour within 3 of the decoder (%u at most)",
             what, l, worst);
    check(worst <= 3, msg);
    printf("ok: %s\n", msg);
    free(got);
}

int main(void)
{
    static uint8_t blocks[4096];
    Nv2aSampler chain = { 1, 1, 2, 1, 1, 1, 4 }, plain = { 1, 1, 0, 1, 1, 1, 0 };
    Nv2aSampler made = { 1, 1, 2, 1, 1, 1, 0 };
    uint32_t t, i, three = 0;
    GLint max = -1;

    if (!gl_backend_context()) return 1;
    G = nv2a_gfx_gl();
    check(G->init(), "init");
    check(G->tex_new_dxt != NULL, "the device has tex_new_dxt");

    srand(5);
    for (i = 0; i < sizeof blocks; i++) blocks[i] = (uint8_t)rand();
    for (i = 0; i < 7; i++) {          /* DXT1's blocks: half three-colour */
        uint8_t *b = blocks + i * 8;
        int lo = (b[0] | b[1] << 8) <= (b[2] | b[3] << 8);
        if ((i & 1) != lo) { uint8_t s0 = b[0], s1 = b[1]; b[0] = b[2]; b[1] = b[3]; b[2] = s0; b[3] = s1; }
        three += (b[0] | b[1] << 8) <= (b[2] | b[3] << 8);
    }
    check(three >= 3, "some DXT1 blocks are three-colour");

    /* DXT1, 8x8 and its chain: 4 blocks, then 1, 1 and 1. */
    t = G->tex_new_dxt(8, 8, 1, blocks, &chain);
    check(t != 0, "DXT1 with a chain of 4 made");
    glActiveTexture(GL_TEXTURE0 + 5);
    glBindTexture(GL_TEXTURE_2D, t);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, &max);
    glBindTexture(GL_TEXTURE_2D, 0);
    check(max == 3, "it samples levels 0 to 3");
    same(t, 0, 8, 8, 0x0C, blocks, "DXT1 8x8");
    same(t, 1, 4, 4, 0x0C, blocks + 32, "DXT1 8x8");
    same(t, 2, 2, 2, 0x0C, blocks + 40, "DXT1 8x8");
    same(t, 3, 1, 1, 0x0C, blocks + 48, "DXT1 8x8");
    G->tex_free(t);

    t = G->tex_new_dxt(16, 8, 3, blocks + 64, &plain);
    check(t != 0, "DXT3 made");
    same(t, 0, 16, 8, 0x0E, blocks + 64, "DXT3 16x8");
    G->tex_free(t);
    t = G->tex_new_dxt(16, 8, 5, blocks + 256, &plain);
    check(t != 0, "DXT5 made");
    same(t, 0, 16, 8, 0x0F, blocks + 256, "DXT5 16x8");
    G->tex_free(t);
    t = G->tex_new_dxt(6, 6, 1, blocks, &plain);
    check(t != 0, "DXT1 6x6 made");
    same(t, 0, 6, 6, 0x0C, blocks, "DXT1 6x6");
    G->tex_free(t);

    check(G->tex_new_dxt(8, 8, 1, blocks, &made) == 0,
          "a chain the driver would make itself is refused");
    check(G->tex_new_dxt(8, 8, 2, blocks, &plain) == 0, "DXT2 is refused");
    printf("gl_backend: DXT as they are passes\n");
    return 0;
}
