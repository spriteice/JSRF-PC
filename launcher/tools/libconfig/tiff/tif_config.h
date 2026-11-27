/* libtiff's tif_config.h for JSRF-PC Tools, in place of the one its CMake
 * writes from tif_config.h.cmake.in (see tiffconf.h here). */
#ifndef _TIF_CONFIG_H_
#define _TIF_CONFIG_H_

#include "tiffconf.h"

#define HAVE_ASSERT_H 1
#define HAVE_FCNTL_H 1
#define HAVE_SYS_TYPES_H 1
#ifdef _WIN32
#define HAVE_IO_H 1
#define HAVE_SETMODE 1
#define USE_WIN32_FILEIO 1
#else
#define HAVE_UNISTD_H 1
#define HAVE_STRINGS_H 1
#define HAVE_MMAP 1
#define HAVE_FSEEKO 1
#endif

#define CHUNKY_STRIP_READ_SUPPORT 1
#define DEFER_STRILE_LOAD 1
#define STRIP_SIZE_DEFAULT 8192
#define TIFF_MAX_DIR_COUNT 1048576
#define SIZEOF_SIZE_T 8
#define WORDS_BIGENDIAN 0
#define TIFF_SIZE_FORMAT "zu"
#define TIFF_SSIZE_FORMAT PRId64

#define PACKAGE "LibTIFF Software"
#define PACKAGE_BUGREPORT "tiff@lists.osgeo.org"
#define PACKAGE_NAME "LibTIFF Software"
#define PACKAGE_TARNAME "tiff"
#define PACKAGE_URL ""

#endif
