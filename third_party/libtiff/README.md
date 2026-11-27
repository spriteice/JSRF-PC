# LibTIFF

https://gitlab.com/libtiff/libtiff, by Sam Leffler, Silicon Graphics and
its contributors: the LibTIFF licence, and the University of California's
for the LZW codec (`LICENSE.md`, and NOTICE at the top of this repository).

JSRF-PC Tools' graffiti page reads TIFF pictures with it, the first page
of one with several (`launcher/tools/graffiti_image.c`).

- Version **v4.7.2**, commit `d01a94be176f5f6a87f7ee1c0b32e65416aa2b4d`.
- Taken as they are, unmodified: `LICENSE.md` and the `.c` and `.h` files
  of `libtiff/`, without `mkg3states.c` and `mkspans.c`, which make tables
  and aren't the library, and the `.in` templates.
- Configured by `launcher/tools/libconfig/tiff` instead of the templates:
  `tiffconf.h`, `tif_config.h` and `tiffvers.h`, with the codecs that need
  no other library (uncompressed, PackBits, LZW, deflate, CCITT fax,
  LogLuv, NeXT and ThunderScan; not JPEG, JBIG, LERC, LZMA, WebP or ZSTD
  inside a TIFF), and `zlib.h`, which gives the deflate codec zlib's
  interface from `third_party/miniz`.
- Built from `launcher/tools/graffiti_libs.txt`, its `tiff` group:
  `tif_unix.c` on Linux, `tif_win32.c` on Windows.

To update: take the same files from the new tag, change the version and
commit above and `tiffvers.h`, compare the new `tif_config.h.cmake.in` and
`tiffconf.h.cmake.in` with ours, check `graffiti_libs.txt` against the new
file list, then run `python3 tests/graffiti/run.py`.
