/* zlib's interface for libtiff's deflate codec (tif_zip.c), from
 * third_party/miniz. miniz gives zlib's names too, but not quite zlib's
 * types: its allocator takes size_t where zlib's, and libtiff's, take
 * unsigned int, so calling libtiff's through miniz's pointer is undefined
 * (and refused by newer compilers). So this is a z_stream of zlib's own
 * shape around miniz's, copied to it and back on each call, and miniz is
 * given an allocator that calls libtiff's with the sizes converted. Only
 * what tif_zip.c uses is here; libtiff itself is unmodified. */
#ifndef JSRF_TIFF_ZLIB_H
#define JSRF_TIFF_ZLIB_H

#include <limits.h>
#include <stddef.h>
#include <string.h>

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "miniz.h"

typedef unsigned char Byte;
typedef Byte Bytef;
typedef unsigned int uInt;
typedef mz_ulong uLong;
typedef void *voidpf;
typedef voidpf (*alloc_func)(voidpf opaque, uInt items, uInt size);
typedef void (*free_func)(voidpf opaque, voidpf address);

typedef struct z_stream_s {
    Bytef *next_in;
    uInt avail_in;
    uLong total_in;
    Bytef *next_out;
    uInt avail_out;
    uLong total_out;
    char *msg;
    alloc_func zalloc;      /* NULL for miniz's own, as zlib's Z_NULL */
    free_func zfree;
    voidpf opaque;
    int data_type;          /* kept for zlib's sake; miniz doesn't use it */
    mz_stream mz;           /* miniz's stream, which does the work */
} z_stream;
typedef z_stream *z_streamp;

#define Z_NULL 0
#define Z_NO_FLUSH MZ_NO_FLUSH
#define Z_PARTIAL_FLUSH MZ_PARTIAL_FLUSH
#define Z_SYNC_FLUSH MZ_SYNC_FLUSH
#define Z_FULL_FLUSH MZ_FULL_FLUSH
#define Z_FINISH MZ_FINISH
#define Z_OK MZ_OK
#define Z_STREAM_END MZ_STREAM_END
#define Z_NEED_DICT MZ_NEED_DICT
#define Z_STREAM_ERROR MZ_STREAM_ERROR
#define Z_DATA_ERROR MZ_DATA_ERROR
#define Z_MEM_ERROR MZ_MEM_ERROR
#define Z_BUF_ERROR MZ_BUF_ERROR
#define Z_NO_COMPRESSION MZ_NO_COMPRESSION
#define Z_BEST_SPEED MZ_BEST_SPEED
#define Z_BEST_COMPRESSION MZ_BEST_COMPRESSION
#define Z_DEFAULT_COMPRESSION MZ_DEFAULT_COMPRESSION
#define Z_DEFAULT_STRATEGY MZ_DEFAULT_STRATEGY
#define Z_DEFLATED MZ_DEFLATED
#define Z_BINARY 0

static void *jsrf_z_alloc(void *opaque, size_t items, size_t size)
{
    z_stream *z = (z_stream *)opaque;
    if (items > UINT_MAX || size > UINT_MAX)
        return NULL;
    return z->zalloc(z->opaque, (uInt)items, (uInt)size);
}

static void jsrf_z_free(void *opaque, void *address)
{
    z_stream *z = (z_stream *)opaque;
    z->zfree(z->opaque, address);
}

/* Before a call: the stream's buffers, and at the start its allocator. */
static void jsrf_z_in(z_stream *z, int start)
{
    if (start) {
        memset(&z->mz, 0, sizeof z->mz);
        if (z->zalloc && z->zfree) {
            z->mz.zalloc = jsrf_z_alloc;
            z->mz.zfree = jsrf_z_free;
            z->mz.opaque = z;
        }
        z->total_in = z->total_out = 0;
    }
    z->mz.next_in = z->next_in;
    z->mz.avail_in = z->avail_in;
    z->mz.total_in = z->total_in;
    z->mz.next_out = z->next_out;
    z->mz.avail_out = z->avail_out;
    z->mz.total_out = z->total_out;
}

/* After: how far it got, and its message. */
static int jsrf_z_out(z_stream *z, int status)
{
    z->next_in = (Bytef *)z->mz.next_in;
    z->avail_in = z->mz.avail_in;
    z->total_in = z->mz.total_in;
    z->next_out = z->mz.next_out;
    z->avail_out = z->mz.avail_out;
    z->total_out = z->mz.total_out;
    z->msg = z->mz.msg ? z->mz.msg : (status < 0 ? (char *)mz_error(status) : NULL);
    return status;
}

static inline int inflateInit(z_streamp z) { jsrf_z_in(z, 1); return jsrf_z_out(z, mz_inflateInit(&z->mz)); }
static inline int inflateReset(z_streamp z) { jsrf_z_in(z, 0); return jsrf_z_out(z, mz_inflateReset(&z->mz)); }
static inline int inflate(z_streamp z, int flush) { jsrf_z_in(z, 0); return jsrf_z_out(z, mz_inflate(&z->mz, flush)); }
static inline int inflateEnd(z_streamp z) { return mz_inflateEnd(&z->mz); }
static inline int deflateInit(z_streamp z, int level) { jsrf_z_in(z, 1); return jsrf_z_out(z, mz_deflateInit(&z->mz, level)); }
static inline int deflateReset(z_streamp z) { jsrf_z_in(z, 0); return jsrf_z_out(z, mz_deflateReset(&z->mz)); }
static inline int deflate(z_streamp z, int flush) { jsrf_z_in(z, 0); return jsrf_z_out(z, mz_deflate(&z->mz, flush)); }
static inline int deflateEnd(z_streamp z) { return mz_deflateEnd(&z->mz); }
/* Changing the level of a compressor already started: miniz can't, and only
 * writing a TIFF reaches it, which JSRF-PC Tools never does. */
static inline int deflateParams(z_streamp z, int level, int strategy)
{
    (void)z; (void)level; (void)strategy;
    return Z_STREAM_ERROR;
}

#endif
