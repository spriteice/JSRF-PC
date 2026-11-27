# libwebp

https://chromium.googlesource.com/webm/libwebp, by Google and the authors
in `AUTHORS`: a BSD licence (`COPYING`), with Google's patent grant
(`PATENTS`), and NOTICE at the top of this repository.

JSRF-PC Tools' graffiti page reads WebP pictures with it, the first frame
of an animated one (`launcher/tools/graffiti_image.c`).

- Version **v1.6.0**, commit `4fa21912338357f89e4fd51cf2368325b59e9bd9`.
- Taken as they are, unmodified: `COPYING`, `PATENTS`, `AUTHORS`, and the
  `.c` and `.h` files of `src/dec`, `src/dsp`, `src/utils`, `src/demux` and
  `src/webp`, without the encoder's, which reading never calls:
  `dsp/cost*.c`, `dsp/enc*.c`, `dsp/lossless_enc*.c`, `dsp/ssim*.c`, and
  `utils/bit_writer_utils`, `utils/huffman_encode_utils` and
  `utils/quant_levels_utils` (`.c` and `.h`).
- Built from `launcher/tools/graffiti_libs.txt`, its `webp` group, with no
  configuration of its own.

To update: take the same files from the new tag, change the version and
commit above, check `graffiti_libs.txt` against the new file list, then run
`python3 tests/graffiti/run.py`.
