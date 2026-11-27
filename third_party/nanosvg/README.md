# NanoSVG

https://github.com/memononen/nanosvg, by Mikko Mononen and its
contributors: the zlib licence (`LICENSE.txt`, and NOTICE at the top of
this repository).

JSRF-PC Tools' graffiti page draws SVG pictures with it
(`launcher/tools/graffiti_image.c`).

- Commit `239e102ec2c691f2902e20ace2ed36ee4a35cfe6`: `src/nanosvg.h` and
  `src/nanosvgrast.h`, unmodified, with `LICENSE.txt`.
- Built once, in `launcher/tools/graffiti_libs.c`.

To update: replace the three files with those of the new commit, change the
commit above, then run `python3 tests/graffiti/run.py`.
