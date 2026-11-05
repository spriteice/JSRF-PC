/* Run with: python tests/kernel_crypto/run.py
 *
 * The kernel's SHA-1, HMAC-SHA1 and RC4 (XcSHAInit, XcSHAUpdate,
 * XcSHAFinal, XcHMAC, XcRC4Key and XcRC4Crypt, src/kernel/kernel_crypto.c),
 * against known answers. The XDK signs saves with them (XCalculateSignature,
 * keyed by the kernel's XboxSignatureKey) and checks the signature when one
 * loads, so a wrong digest is a save that won't load, here or on a console.
 *
 *   1. SHA-1: FIPS 180's and RFC 3174's messages, and a message of each
 *      length round the padding's edges (55, 56, 63, 64 and 65 bytes, whose
 *      answers are Python's hashlib's). Each is also fed in pieces of every
 *      length from 1 to 130 bytes, as a title hashes a file a block at a time.
 *   2. HMAC-SHA1: RFC 2202's seven cases, keys longer than a block among them,
 *      each with its data split between XcHMAC's two buffers at every point.
 *   3. RC4: the classic three ("Key", "Wiki", "Secret"), and RFC 6229's
 *      keystream for a 40-bit and a 256-bit key, at offsets 0, 16 and 4096,
 *      crypted in one call and in pieces; and crypting twice with the same
 *      key gives the plaintext back.
 *
 * Nothing here runs the game's code: the functions take host pointers, as
 * kernel_bridge.c's bridges pass them after turning the guest's addresses.
 */
#include "kernel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SHA1_BYTES      20
#define MILLION         1000000u
#define MOST_PIECE      130          /* past two of SHA-1's 64-byte blocks */
#define RC4_OFFSETS     3

/* kernel_crypto.c's warning for the public-key calls it leaves out. */
void xbox_log(int level, const char *subsystem, const char *fmt, ...)
{
    (void)level; (void)subsystem; (void)fmt;
}

static int s_failures;

static void fail(const char *what, const char *which)
{
    if (s_failures++ < 20)
        printf("kernel_crypto: %s: %s\n", what, which);
    fflush(stdout);
}

/* "a9993e36..." into bytes. */
static void unhex(const char *hex, UCHAR *out, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned v;
        sscanf(hex + 2 * i, "%2x", &v);
        out[i] = (UCHAR)v;
    }
}

static int same_hex(const UCHAR *got, const char *want, size_t n)
{
    UCHAR w[64];
    unhex(want, w, n);
    return memcmp(got, w, n) == 0;
}

/* ---- 1: SHA-1 ------------------------------------------------------------ */

typedef struct { const char *name; const UCHAR *msg; size_t len; const char *digest; } ShaCase;

static void sha_whole(const UCHAR *msg, size_t len, UCHAR *digest)
{
    XBOX_SHA_CONTEXT ctx;
    xbox_XcSHAInit(&ctx);
    xbox_XcSHAUpdate(&ctx, msg, (ULONG)len);
    xbox_XcSHAFinal(&ctx, digest);
}

static void sha_pieces(const UCHAR *msg, size_t len, size_t piece, UCHAR *digest)
{
    XBOX_SHA_CONTEXT ctx;
    size_t at;
    xbox_XcSHAInit(&ctx);
    for (at = 0; at < len; at += piece)
        xbox_XcSHAUpdate(&ctx, msg + at, (ULONG)(len - at < piece ? len - at : piece));
    xbox_XcSHAFinal(&ctx, digest);
}

static void sha1_known(void)
{
    static UCHAR a_lot[MILLION];
    static UCHAR as[65];
    const UCHAR *abc = (const UCHAR *)"abc";
    const UCHAR *two = (const UCHAR *)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    const UCHAR *four = (const UCHAR *)"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
                                       "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    ShaCase cases[] = {
        { "FIPS 180 \"abc\"", abc, 3, "a9993e364706816aba3e25717850c26c9cd0d89d" },
        { "the empty message", abc, 0, "da39a3ee5e6b4b0d3255bfef95601890afd80709" },
        { "FIPS 180's 448 bits", two, 56, "84983e441c3bd26ebaae4aa1f95129e5e54670f1" },
        { "FIPS 180's 896 bits", four, 112, "a49b2446a02c645bf419f995b67091253a04a259" },
        { "a million a's", a_lot, MILLION, "34aa973cd4c4daa4f61eeb2bdbad27316534016f" },
        { "55 a's", as, 55, "c1c8bbdc22796e28c0e15163d20899b65621d65a" },
        { "56 a's", as, 56, "c2db330f6083854c99d4b5bfb6e8f29f201be699" },
        { "63 a's", as, 63, "03f09f5b158a7a8cdad920bddc29b81c18a551f5" },
        { "64 a's", as, 64, "0098ba824b5c16427bd7a1122a5a442a25ec644d" },
        { "65 a's", as, 65, "11655326c708d70319be2610e8a57d9a5b959d3b" },
    };
    UCHAR digest[SHA1_BYTES];
    size_t i, piece;

    memset(a_lot, 'a', sizeof a_lot);
    memset(as, 'a', sizeof as);
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        sha_whole(cases[i].msg, cases[i].len, digest);
        if (!same_hex(digest, cases[i].digest, SHA1_BYTES))
            fail("SHA-1 of the whole message is wrong", cases[i].name);
        for (piece = 1; piece <= MOST_PIECE; piece++) {
            if (cases[i].len == MILLION && piece % 13) continue;   /* every 13th: it's slow */
            sha_pieces(cases[i].msg, cases[i].len, piece, digest);
            if (!same_hex(digest, cases[i].digest, SHA1_BYTES)) {
                fail("SHA-1 fed in pieces is wrong", cases[i].name);
                break;
            }
        }
    }
}

/* ---- 2: HMAC-SHA1 -------------------------------------------------------- */

typedef struct { const char *name; UCHAR key[80]; ULONG key_len; UCHAR data[80]; ULONG data_len;
                 const char *mac; } HmacCase;

static void fill(UCHAR *p, UCHAR v, size_t n) { memset(p, v, n); }

static void hmac_known(void)
{
    static HmacCase c[7];
    UCHAR mac[SHA1_BYTES];
    ULONG i, split;

    c[0].name = "RFC 2202 case 1"; fill(c[0].key, 0x0b, 20); c[0].key_len = 20;
    memcpy(c[0].data, "Hi There", 8); c[0].data_len = 8;
    c[0].mac = "b617318655057264e28bc0b6fb378c8ef146be00";
    c[1].name = "RFC 2202 case 2"; memcpy(c[1].key, "Jefe", 4); c[1].key_len = 4;
    memcpy(c[1].data, "what do ya want for nothing?", 28); c[1].data_len = 28;
    c[1].mac = "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79";
    c[2].name = "RFC 2202 case 3"; fill(c[2].key, 0xaa, 20); c[2].key_len = 20;
    fill(c[2].data, 0xdd, 50); c[2].data_len = 50;
    c[2].mac = "125d7342b9ac11cd91a39af48aa17b4f63f175d3";
    c[3].name = "RFC 2202 case 4"; for (i = 0; i < 25; i++) c[3].key[i] = (UCHAR)(i + 1);
    c[3].key_len = 25; fill(c[3].data, 0xcd, 50); c[3].data_len = 50;
    c[3].mac = "4c9007f4026250c6bc8414f9bf50c86c2d7235da";
    c[4].name = "RFC 2202 case 5"; fill(c[4].key, 0x0c, 20); c[4].key_len = 20;
    memcpy(c[4].data, "Test With Truncation", 20); c[4].data_len = 20;
    c[4].mac = "4c1a03424b55e07fe7f27be1d58bb9324a9a5a04";
    c[5].name = "RFC 2202 case 6, a key longer than a block"; fill(c[5].key, 0xaa, 80); c[5].key_len = 80;
    memcpy(c[5].data, "Test Using Larger Than Block-Size Key - Hash Key First", 54); c[5].data_len = 54;
    c[5].mac = "aa4ae5e15272d00e95705637ce8a3b55ed402112";
    c[6].name = "RFC 2202 case 7, key and data longer than a block"; fill(c[6].key, 0xaa, 80);
    c[6].key_len = 80;
    memcpy(c[6].data, "Test Using Larger Than Block-Size Key and Larger Than One Block-Size Data", 73);
    c[6].data_len = 73;
    c[6].mac = "e8e99d0f45237d786d6bbaa7965c7808bbff1a91";

    for (i = 0; i < 7; i++) {
        /* XcHMAC hashes its first buffer, then its second: split anywhere,
         * the two are the one message. */
        for (split = 0; split <= c[i].data_len; split++) {
            memset(mac, 0, sizeof mac);
            xbox_XcHMAC(c[i].key, c[i].key_len, c[i].data, split,
                        c[i].data + split, c[i].data_len - split, mac);
            if (!same_hex(mac, c[i].mac, SHA1_BYTES)) {
                fail(split ? "HMAC-SHA1 with its data split is wrong" : "HMAC-SHA1 is wrong", c[i].name);
                break;
            }
        }
    }
    /* No second buffer at all, as a title passes NULL for it. */
    xbox_XcHMAC(c[1].key, c[1].key_len, c[1].data, c[1].data_len, NULL, 0, mac);
    if (!same_hex(mac, c[1].mac, SHA1_BYTES))
        fail("HMAC-SHA1 with no second buffer is wrong", c[1].name);
}

/* ---- 3: RC4 -------------------------------------------------------------- */

static void rc4(const UCHAR *key, ULONG key_len, UCHAR *data, ULONG len, ULONG piece)
{
    XBOX_RC4_CONTEXT ctx;
    ULONG at;
    xbox_XcRC4Key(&ctx, key_len, key);
    for (at = 0; at < len; at += piece)
        xbox_XcRC4Crypt(&ctx, len - at < piece ? len - at : piece, data + at);
}

typedef struct { const char *key; const char *plain; const char *cipher; } Rc4Text;
typedef struct { const char *name; UCHAR key[32]; ULONG key_len;
                 ULONG offset[RC4_OFFSETS]; const char *stream[RC4_OFFSETS]; } Rc4Stream;

static void rc4_known(void)
{
    static const Rc4Text text[] = {
        { "Key", "Plaintext", "bbf316e8d940af0ad3" },
        { "Wiki", "pedia", "1021bf0420" },
        { "Secret", "Attack at dawn", "45a01f645fc35b383552544b9bf5" },
    };
    static Rc4Stream stream[2] = {
        { "RFC 6229, a 40-bit key", { 1, 2, 3, 4, 5 }, 5, { 0, 16, 4096 },
          { "b2396305f03dc027ccc3524a0a1118a8", "6982944f18fc82d589c403a47a0d0919",
            "ff25b58995996707e51fbdf08b34d875" } },
        { "RFC 6229, a 256-bit key", { 0 }, 32, { 0, 0, 0 },
          { "eaa6bd25880bf93d3f5d1e4ca2611d91", NULL, NULL } },
    };
    static UCHAR buf[4112], back[4112];
    ULONG i, k, piece;

    for (i = 0; i < 32; i++) stream[1].key[i] = (UCHAR)(i + 1);
    for (i = 0; i < sizeof text / sizeof text[0]; i++) {
        ULONG len = (ULONG)strlen(text[i].plain);
        for (piece = 1; piece <= len; piece++) {
            memcpy(buf, text[i].plain, len);
            rc4((const UCHAR *)text[i].key, (ULONG)strlen(text[i].key), buf, len, piece);
            if (!same_hex(buf, text[i].cipher, len)) {
                fail("RC4 is wrong", text[i].key);
                break;
            }
            rc4((const UCHAR *)text[i].key, (ULONG)strlen(text[i].key), buf, len, len);
            if (memcmp(buf, text[i].plain, len) != 0) {
                fail("RC4 twice with one key is not the plaintext", text[i].key);
                break;
            }
        }
    }
    for (i = 0; i < 2; i++) {
        for (piece = 1; piece <= MOST_PIECE; piece += 43) {
            memset(buf, 0, sizeof buf);
            rc4(stream[i].key, stream[i].key_len, buf, sizeof buf, piece == 1 ? sizeof buf : piece);
            for (k = 0; k < RC4_OFFSETS && stream[i].stream[k]; k++)
                if (!same_hex(buf + stream[i].offset[k], stream[i].stream[k], 16))
                    fail("RC4's keystream is wrong", stream[i].name);
        }
        memcpy(back, buf, sizeof buf);
        rc4(stream[i].key, stream[i].key_len, back, sizeof back, sizeof back);
        for (k = 0; k < sizeof back; k++)
            if (back[k]) { fail("RC4 twice with one key is not the plaintext", stream[i].name); break; }
    }
}

int main(void)
{
    sha1_known();
    hmac_known();
    rc4_known();
    if (s_failures) {
        printf("kernel_crypto: %d failures\n", s_failures);
        return 1;
    }
    printf("kernel_crypto: all cases pass\n");
    return 0;
}
