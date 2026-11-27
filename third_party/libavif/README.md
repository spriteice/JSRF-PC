# libavif

https://github.com/AOMediaCodec/libavif, by Joe Drago and its
contributors: a BSD licence (`LICENSE`, whose first two parts cover the
files here, and NOTICE at the top of this repository).

JSRF-PC Tools' graffiti page reads AVIF pictures with it, decoded by dav1d
(`third_party/dav1d`), the first frame of an animated one
(`launcher/tools/graffiti_image.c`).

- Version **v1.4.2**, commit `c5240fc79fe5c2407e10afd35f5505ef6333ea49`.
- Taken as they are, unmodified: `LICENSE`, `include/avif/*.h`, and from
  `src/`: `alpha.c`, `avif.c`, `codec_dav1d.c`, `colr.c`, `colrconvert.c`,
  `diag.c`, `exif.c`, `gainmap.c`, `io.c`, `mem.c`, `obu.c`,
  `properties.c`, `rawdata.c`, `read.c`, `reformat.c`,
  `reformat_libsharpyuv.c`, `reformat_libyuv.c`, `sampletransform.c`,
  `stream.c`, `utils.c` and `write.c`. The two `reformat_lib*` files build
  without those libraries, as libavif's own build does when they're off.
- Its `scale.c`, which needs libyuv, is left out, and
  `launcher/tools/libconfig/avif/scale_none.c` stands in for it: a picture
  read at its own size never scales, and one whose parts would need it is
  refused.
- Built from `launcher/tools/graffiti_libs.txt`, its `avif` group, with
  `AVIF_CODEC_DAV1D` the only codec.

To update: take the same files from the new tag, change the version and
commit above, check `scale_none.c` against the new `scale.c`'s functions
and `graffiti_libs.txt` against the new file list, then run
`python3 tests/graffiti/run.py`.
