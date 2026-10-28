/* Run with: python tests/tex_decode/run.py
 *
 * decode_level decodes every format but DXT with a loop for each format
 * (decode_rows), where it used to ask decode_texel which format it was at
 * every texel. The picture must not change: every format it decodes,
 * swizzled at several shapes and linear with and without a pitch, with a
 * bump source's bias and without, comes out byte for byte as one texel at a
 * time gives it. DXT1, 3 and 5 are checked against d3d8_dxt_decode_texel. It
 * says how long each way takes too, which is not checked.
 */
static int fails;

/* Before: one texel at a time, as decode_level did. */
static void one_at_a_time(uint32_t color, const uint8_t *src, uint32_t w, uint32_t h,
                          uint32_t pitch, uint8_t *rgba)
{
    uint32_t lin = fmt_is_swz(color) ? swz_to_linear(color) : color;
    uint32_t bpp = linear_bpp(lin), mx = 0, my = 0, x, y;
    int swz = fmt_is_swz(color);
    if (swz) xbox_swizzle_masks(w, h, &mx, &my);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            const uint8_t *p;
            uint32_t argb = 0;
            if (fmt_is_dxt(color)) {
                d3d8_dxt_decode_texel(src, color, x, y, w, &argb);
            } else {
                if (swz)
                    p = src + (size_t)(swizzle_deposit(x, mx) | swizzle_deposit(y, my)) * bpp;
                else
                    p = src + (size_t)y * (pitch ? pitch : w * bpp) + (size_t)x * bpp;
                argb = decode_texel(lin, p);
            }
            if (g_upload_bias_bg) argb ^= 0x00008080u;
            rgba[((size_t)y * w + x) * 4 + 0] = (uint8_t)(argb >> 16);
            rgba[((size_t)y * w + x) * 4 + 1] = (uint8_t)(argb >> 8);
            rgba[((size_t)y * w + x) * 4 + 2] = (uint8_t)argb;
            rgba[((size_t)y * w + x) * 4 + 3] = (uint8_t)(argb >> 24);
        }
}

static void same(uint32_t color, uint32_t w, uint32_t h, uint32_t pitch, const uint8_t *src,
                 uint8_t *a, uint8_t *b)
{
    size_t n = (size_t)w * h * 4, i;
    int ok;
    memset(a, 0xAA, n);
    memset(b, 0x55, n);
    ok = decode_level(color, src, w, h, pitch, a);
    one_at_a_time(color, src, w, h, pitch, b);
    for (i = 0; ok && i < n; i++)
        if (a[i] != b[i]) { ok = 0; break; }
    if (!ok) {
        fails++;
        printf("FAIL: format 0x%02X %ux%u pitch %u bias %d: byte %zu is %u, one at a time %u\n",
               color, w, h, pitch, g_upload_bias_bg, i, i < n ? a[i] : 0, i < n ? b[i] : 0);
    }
}

int main(void)
{
    static const uint32_t swizzled[] = { 0x00, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x19, 0x1A };
    static const uint32_t linear[] = { 0x10, 0x11, 0x12, 0x13, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,
                                       0x3F, 0x40, 0x41 };
    static const uint32_t dxt[] = { 0x0C, 0x0E, 0x0F };
    static const uint32_t shapes[][2] = { { 64, 64 }, { 128, 32 }, { 16, 64 }, { 1, 1 },
                                          { 2, 1 }, { 4, 2 }, { 8, 512 } };
    enum { MAX = 1024 * 1024 * 4 };
    uint8_t *src = malloc(MAX), *a = malloc(MAX), *b = malloc(MAX);
    size_t i;
    unsigned k, s, bias, checked = 0;

    srand(7);
    for (i = 0; i < MAX; i++) src[i] = (uint8_t)rand();
    for (bias = 0; bias < 2; bias++) {
        g_upload_bias_bg = (int)bias;
        for (k = 0; k < sizeof swizzled / sizeof *swizzled; k++)
            for (s = 0; s < sizeof shapes / sizeof *shapes; s++, checked++)
                same(swizzled[k], shapes[s][0], shapes[s][1], 0, src, a, b);
        for (k = 0; k < sizeof linear / sizeof *linear; k++) {
            uint32_t bpp = linear_bpp(linear[k]);
            same(linear[k], 37, 13, 0, src, a, b);
            same(linear[k], 37, 13, 37 * bpp + 12, src, a, b);
            same(linear[k], 640, 480, 640 * bpp, src, a, b);
            checked += 3;
        }
        for (k = 0; k < sizeof dxt / sizeof *dxt; k++)
            for (s = 0; s < sizeof shapes / sizeof *shapes; s++, checked++)
                same(dxt[k], shapes[s][0], shapes[s][1], 0, src, a, b);
    }
    printf("%u levels compared: %s\n", checked, fails ? "some differ" : "all the same");
    if (decode_level(0x3A, src, 4, 4, 0, a)) {
        fails++;
        printf("FAIL: a format with no decoder (0x3A) decoded\n");
    }

    printf(fails ? "tex_decode: %d case(s) FAIL\n" : "tex_decode: all cases pass\n", fails);
    free(src); free(a); free(b);
    return fails ? 1 : 0;
}
