# dav1d

https://code.videolan.org/videolan/dav1d, by VideoLAN and the dav1d
authors (`THANKS.md`): a BSD licence (`COPYING`, and NOTICE at the top of
this repository).

libavif (`third_party/libavif`) decodes AVIF pictures with it, for
JSRF-PC Tools' graffiti page.

- Version **1.5.4**, commit `54706fc6bc0cdecab7e9593974a4039cc038fca7`.
- Taken as they are, unmodified: `COPYING`, `THANKS.md`, the `.c` and `.h`
  files of `src/` and `src/win32/`, and `include/common`, `include/compat`
  and `include/dav1d`. The assembly, the tools and the tests are left out.
- Configured by `launcher/tools/libconfig/dav1d` instead of its Meson
  build: `config.h`, plain C with no assembly on every processor, 8-bit and
  10/12-bit pictures, and `vcs_version.h`.
- Built from `launcher/tools/graffiti_libs.txt`, its `dav1d`, `dav1d8` and
  `dav1d16` groups: the `*_tmpl.c` files once for each bit depth, and
  `win32/thread.c` on Windows.

To update: take the same files from the new tag, change the version and
commit above and `vcs_version.h`, compare the new `meson.build`'s config
entries with `config.h`, check `graffiti_libs.txt` against the new file
list, then run `python3 tests/graffiti/run.py`.
