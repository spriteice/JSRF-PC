/* libtiff's tiffconf.h for JSRF-PC Tools (launcher/tools/graffiti_image.c),
 * in place of the one its CMake writes from tiffconf.h.cmake.in: little-
 * endian hosts (x86-64 and ARM64, Linux and Windows), and only the codecs
 * that need no other library, with deflate through third_party/miniz. */
#ifndef _TIFFCONF_
#define _TIFFCONF_

#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>

#define TIFF_INT8_T int8_t
#define TIFF_INT16_T int16_t
#define TIFF_INT32_T int32_t
#define TIFF_INT64_T int64_t
#define TIFF_UINT8_T uint8_t
#define TIFF_UINT16_T uint16_t
#define TIFF_UINT32_T uint32_t
#define TIFF_UINT64_T uint64_t
#define TIFF_SSIZE_T ptrdiff_t

#define HAVE_IEEEFP 1
#define HOST_BIGENDIAN 0

#define CCITT_SUPPORT 1
#define LOGLUV_SUPPORT 1
#define LZW_SUPPORT 1
#define NEXT_SUPPORT 1
#define PACKBITS_SUPPORT 1
#define THUNDER_SUPPORT 1
#define ZIP_SUPPORT 1
#define STRIPCHOP_DEFAULT TIFF_STRIPCHOP
#define SUBIFD_SUPPORT 1
#define DEFAULT_EXTRASAMPLE_AS_ALPHA 1
#define CHECK_JPEG_YCBCR_SUBSAMPLING 1
#define MDI_SUPPORT 1

#endif
