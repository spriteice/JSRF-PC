/*
 * JSRF-PC - is this the game the engine is made from?
 *
 * The engine is one build of Jet Set Radio Future translated ahead of time:
 * the standalone US disc's default.xbe (NTSC, version 1, built 28 January
 * 2002). That build's addresses are written all through the translated code
 * -- every function a table points at, every piece of data, the kernel
 * imports at 0x001C3F60. Another build of the game has its code and data at
 * other addresses: the European and Japanese releases, and the JSRF of the
 * Sega GT 2002 + JSRF combo discs (a later build, version 1.04 in
 * Cxbx-Reloaded's list, with glitches the standalone disc fixed), which is
 * the commonest US copy because it came with the console. The engine would
 * still load one, then run the standalone build's code against it, follow
 * pointers into the wrong bytes and fall over a few seconds in. A player's
 * 0.4.6 log on a Mac shows exactly that: one call to 0x0028448C (a pointer
 * read from where the standalone build keeps something else), then a crash
 * reading 0xFFFFF000 -- the value his default.xbe holds at 0x001C4048, the
 * standalone build's import of LaunchDataPage.
 *
 * So default.xbe's section table is compared with the standalone build's
 * before anything runs, and the player is told what the files are instead.
 *
 * Only the layout is compared, not the bytes: where each section is loaded,
 * how big it is and where it sits in the file. A US default.xbe with a patch
 * applied (the region, a widescreen hack) has the same layout and still runs:
 * the engine never executes the file's code anyway, only its own translation.
 */
#include "xbe_check.h"

#include "ios/xiso.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const struct {
    const char *name;
    uint32_t va, vsize, raw, rsize;
} k_usa[] = {
    { ".text",     0x00011000u, 0x0017BB30u, 0x00001000u, 0x0017BB30u },
    { "D3D",       0x0018CB40u, 0x000117F8u, 0x0017D000u, 0x0000E6B0u },
    { "DSOUND",    0x0019E340u, 0x0001C55Cu, 0x0018C000u, 0x0001C32Cu },
    { "MMATRIX",   0x001BA8A0u, 0x00001210u, 0x001A9000u, 0x00001210u },
    { "XGRPH",     0x001BBAC0u, 0x00000CFCu, 0x001AB000u, 0x00000CFCu },
    { "XPP",       0x001BC7C0u, 0x00007798u, 0x001AC000u, 0x00007798u },
    { ".rdata",    0x001C3F60u, 0x00027800u, 0x001B4000u, 0x000277F0u },
    { ".data",     0x001EB760u, 0x00092914u, 0x001DC000u, 0x00044574u },
    { "DOLBY",     0x0027E080u, 0x00006D98u, 0x00221000u, 0x00006D84u },
    { "$$XTIMAGE", 0x00284E20u, 0x00002800u, 0x00228000u, 0x00002800u },
    { "$$XSIMAGE", 0x00287620u, 0x00001000u, 0x0022B000u, 0x00001000u },
};
#define USA_SECTIONS   (sizeof k_usa / sizeof k_usa[0])
#define USA_BASE       0x00010000u
#define USA_TIMEDATE   0x3C558EA7u     /* 2002-01-28 */
#define USA_TITLE_ID   0x5345000Au     /* SE-010, Jet Set Radio Future */
#define USA_VERSION    1u

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* A Unix time as YYYY-MM-DD (no gmtime: the same on every platform). */
static void day_of(uint32_t t, char *out, size_t n)
{
    long z = (long)(t / 86400u) + 719468;
    long era = z / 146097, doe = z - era * 146097;
    long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long y = yoe + era * 400, doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long mp = (5 * doy + 2) / 153, d = doy - (153 * mp + 2) / 5 + 1;
    long m = mp < 10 ? mp + 3 : mp - 9;
    snprintf(out, n, "%04ld-%02ld-%02ld", y + (m <= 2), m, d);
}

static void region_of(uint32_t r, char *out, size_t n)
{
    out[0] = 0;
    if (r & 1u) snprintf(out + strlen(out), n - strlen(out), "%sNorth America", out[0] ? " + " : "");
    if (r & 2u) snprintf(out + strlen(out), n - strlen(out), "%sJapan", out[0] ? " + " : "");
    if (r & 4u) snprintf(out + strlen(out), n - strlen(out), "%sEurope and the rest of the world (PAL)",
                         out[0] ? " + " : "");
    if (!out[0]) snprintf(out, n, "none given (%08X)", (unsigned)r);
}

/* A title ID as the lists write it: two letters for the publisher and a
 * number (SE-010 is Sega's tenth), or hex if it is not that shape. */
static void title_of(uint32_t t, char *out, size_t n)
{
    unsigned a = (t >> 24) & 0xFFu, b = (t >> 16) & 0xFFu;
    if (a >= 'A' && a <= 'Z' && b >= 'A' && b <= 'Z')
        snprintf(out, n, "%c%c-%03u", (char)a, (char)b, (unsigned)(t & 0xFFFFu));
    else
        snprintf(out, n, "%08X", (unsigned)t);
}

static int has_word(const char *s, const char *w)
{
    size_t i, j, n = strlen(w);
    for (i = 0; s[i]; i++) {
        for (j = 0; j < n; j++) {
            char a = s[i + j], b = w[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (a != b) break;
        }
        if (j == n) return 1;
    }
    return 0;
}

/* How the last default.xbe that was not the standalone build's differs from it, in
 * the terms of its section table -- for the log, not for the player. */
static char s_detail[240];
const char *jsrf_xbe_detail(void) { return s_detail; }

int jsrf_xbe_check(const unsigned char *x, size_t n, char *why, size_t whyn)
{
    uint32_t base, td, cert, nsec, secs, title = 0, region = 0, version = 0;
    char name[41] = "", when[24], usa_when[24], reg[96], where[160] = "", tid[16];
    size_t i, c;
    int same = 1;

    if (why && whyn) why[0] = 0;
    if (n < 0x178 || memcmp(x, "XBEH", 4)) {
        if (why) snprintf(why, whyn, "Their default.xbe is not an Xbox program: the file is damaged, "
                                     "or it is not the game's.");
        snprintf(s_detail, sizeof s_detail, "default.xbe: %u bytes, no XBEH header", (unsigned)n);
        return JSRF_XBE_OTHER;
    }
    base = rd32(x + 0x104);
    td   = rd32(x + 0x114);
    cert = rd32(x + 0x118);
    nsec = rd32(x + 0x11C);
    secs = rd32(x + 0x120);

    if (cert >= base && (size_t)(cert - base) + 0xB0 <= n) {
        const unsigned char *ct = x + (cert - base);
        title   = rd32(ct + 0x08);
        region  = rd32(ct + 0xA0);
        version = rd32(ct + 0xAC);
        for (c = 0; c < 40; c++) {
            unsigned ch = (unsigned)ct[0x0C + 2 * c] | (unsigned)ct[0x0D + 2 * c] << 8;
            if (!ch) break;
            name[c] = ch >= 0x20 && ch < 0x7F ? (char)ch : '?';
        }
        name[c] = 0;
    }

    if (base != USA_BASE || nsec != USA_SECTIONS || secs < base) {
        same = 0;
        snprintf(where, sizeof where, "%u sections, loaded at 0x%08X", (unsigned)nsec, (unsigned)base);
    }
    for (i = 0; same && i < USA_SECTIONS; i++) {
        size_t o = (size_t)(secs - base) + i * 56;
        if (o + 56 > n) {
            same = 0;
            snprintf(where, sizeof where, "a section table that does not fit in its header");
            break;
        }
        if (rd32(x + o + 4) != k_usa[i].va || rd32(x + o + 8) != k_usa[i].vsize ||
            rd32(x + o + 12) != k_usa[i].raw || rd32(x + o + 16) != k_usa[i].rsize) {
            same = 0;
            snprintf(where, sizeof where,
                     "its section %u at 0x%08X, %u bytes, where the standalone US build has %s at 0x%08X, %u bytes",
                     (unsigned)(i + 1), (unsigned)rd32(x + o + 4), (unsigned)rd32(x + o + 8),
                     k_usa[i].name, (unsigned)k_usa[i].va, (unsigned)k_usa[i].vsize);
        }
    }
    if (same) return JSRF_XBE_OK;
    title_of(title, tid, sizeof tid);
    snprintf(s_detail, sizeof s_detail, "default.xbe (%s, version %u): %s", tid, (unsigned)version, where);

    if (why && whyn) {
        day_of(td, when, sizeof when);
        day_of(USA_TIMEDATE, usa_when, sizeof usa_when);
        region_of(region, reg, sizeof reg);
        if (title != USA_TITLE_ID && (has_word(name, "jsrf") || has_word(name, "jet set")))
            snprintf(why, whyn,
                     "This is a disc with more than Jet Set Radio Future on it: its default.xbe is "
                     "\"%s\" (%s, built %s), not the game's own.", name, tid, when);
        else if (title != USA_TITLE_ID)
            snprintf(why, whyn,
                     "These are another game's files, not Jet Set Radio Future's: their default.xbe "
                     "is \"%s\" (%s, built %s).", name[0] ? name : "?", tid, when);
        else if ((region & 7u) == 1u)
            snprintf(why, whyn,
                     "These are Jet Set Radio Future's files for North America, but not the build JSRF-PC "
                     "is made from: their default.xbe is version %u, built %s, where the "
                     "standalone US disc's is version %u, built %s. The JSRF on the Sega GT 2002 + JSRF "
                     "combo disc (the copy that came with the Xbox) is a build like this.",
                     (unsigned)version, when, USA_VERSION, usa_when);
        else if ((region & 7u) == 2u)
            snprintf(why, whyn,
                     "These are the files of a Japanese-region build of Jet Set Radio Future (version "
                     "%u, built %s), not of the standalone US disc.", (unsigned)version, when);
        else if ((region & 7u) == 4u)
            snprintf(why, whyn,
                     "These are the files of a European (PAL) build of Jet Set Radio Future "
                     "(version %u, built %s), not of the standalone US disc.", (unsigned)version, when);
        else
            snprintf(why, whyn,
                     "These are the files of another build of Jet Set Radio Future (region %s, version "
                     "%u, built %s), not of the standalone US disc.", reg, (unsigned)version, when);
    }
    return JSRF_XBE_OTHER;
}

int jsrf_xbe_check_folder(const char *game_dir, char *why, size_t whyn)
{
    char path[4096];
    unsigned char head[16384];
    size_t got;
    FILE *f;

    snprintf(path, sizeof path, "%s/default.xbe", game_dir);
    f = fopen(path, "rb");
    if (!f) {
        if (why) snprintf(why, whyn, "There is no default.xbe in %s, so it is not the game's folder: "
                                     "the game's has default.xbe and Media in it.", game_dir);
        return JSRF_XBE_MISSING;
    }
    got = fread(head, 1, sizeof head, f);
    fclose(f);
    return jsrf_xbe_check(head, got, why, whyn);
}

/* What a file that is not an Xbox disc image is, from its first bytes, for a
 * message that says what to do with it -- or NULL. */
static const char *kind_of(const char *path)
{
    static const struct { const char *magic; size_t n; const char *what; } k[] = {
        { "PK\x03\x04", 4, "a zip archive" },
        { "7z\xBC\xAF\x27\x1C", 6, "a 7-Zip archive" },
        { "Rar!\x1A\x07", 6, "a RAR archive" },
        { "\x1F\x8B", 2, "a gzip archive" },
        { "BZh", 3, "a bzip2 archive" },
        { "\xFD" "7zXZ", 6, "an xz archive" },
        { "CISO", 4, "a compressed disc image (CSO)" },
    };
    unsigned char h[8] = { 0 };
    long got = xiso_head(path, h, sizeof h);
    size_t i;
    for (i = 0; i < sizeof k / sizeof k[0]; i++)
        if (got >= (long)k[i].n && !memcmp(h, k[i].magic, k[i].n)) return k[i].what;
    return NULL;
}

int jsrf_xbe_check_iso(const char *iso_path, char *why, size_t whyn)
{
    unsigned char head[16384];
    size_t got = 0;
    uint64_t size = 0;
    char err[256];
    int r;

    if (why && whyn) why[0] = 0;
    r = xiso_read_file(iso_path, "default.xbe", head, sizeof head, &got, &size, err, sizeof err);
    if (r < 0) {
        const char *kind = xiso_probe(iso_path) ? NULL : kind_of(iso_path);
        if (!why) return JSRF_XBE_NOT_IMAGE;
        if (kind && !strcmp(kind, "a compressed disc image (CSO)"))
            snprintf(why, whyn, "That file is a compressed Xbox disc image (CSO), which JSRF-PC "
                                "cannot read: turn it back into a plain .iso first, or pick the folder "
                                "of the game's extracted files.");
        else if (kind)
            snprintf(why, whyn, "That file is %s, not a disc image: unpack it first, then pick the "
                                ".iso that comes out of it (or the folder of the game's files).", kind);
        else if (!strcmp(err, "That file is not an Xbox disc image."))
            snprintf(why, whyn, "That file is not an Xbox disc image. Pick an image of the game's disc "
                                "(an .iso), or the folder its files were extracted into.");
        else {
            if (err[0] >= 'A' && err[0] <= 'Z' && err[1] >= 'a' && err[1] <= 'z')
                err[0] = (char)(err[0] - 'A' + 'a');     /* it follows a colon */
            snprintf(why, whyn, "That disc image cannot be read: %s", err);
        }
        return JSRF_XBE_NOT_IMAGE;
    }
    if (r > 0) {
        if (why) snprintf(why, whyn, "That Xbox disc image has no default.xbe in it, so it is not "
                                     "a game's disc.");
        return JSRF_XBE_MISSING;
    }
    return jsrf_xbe_check(head, got, why, whyn);
}
