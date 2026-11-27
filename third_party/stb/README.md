# stb

https://github.com/nothings/stb, by Sean Barrett and its contributors:
public domain, or the MIT licence, as one chooses (`LICENSE`, and NOTICE at
the top of this repository). JSRF-PC takes them under the MIT licence.

JSRF-PC Tools' graffiti page reads JPEG and PNG pictures with `stb_image.h`
and makes the wall texture's DXT3 colour blocks with `stb_dxt.h`
(`launcher/tools/graffiti_image.c`, `launcher/tools/graffiti_save.c`).

- Commit `6b5799bae8a1fe3792691ef4b72ae26434c3e799`: `stb_image.h` v2.34
  and `stb_dxt.h` v1.13, unmodified, with the repository's `LICENSE`.
- Built once, in `launcher/tools/graffiti_libs.c`, which sets their
  options: JPEG and PNG only, and no stdio.

To update: replace both files and `LICENSE` with those of the new commit,
change the commit and versions above, then run
`python3 tests/graffiti/run.py`.
