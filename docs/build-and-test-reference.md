# Build and test reference

How test builds of JSRF-PC are made, checked, delivered, and reported. It is
written by the maintainer ("I", who only runs the builds) for whoever makes
them ("you"), and goes with the project rules.

Where this file and the repo's own docs disagree, trust the repo's docs
(README.md, docs/building-on-linux.md, tools/README.md,
launcher/linux/package.sh, and launcher/windows/package.sh) and tell me
about the difference.

## Test devices

- **Primary:** ROG Xbox Ally X: x86_64, SteamOS 3.9.5, glibc 2.43, Z2
  Extreme with AMD Radeon Graphics, 24 GB shared memory.
- **Windows:** desktop PC: Windows 11, i7-10700K, 32 GB DDR4, RTX 2080 Ti
  with 11 GB video memory.
- **Occasionally:** Lenovo Yoga laptop: Windows 11, Ryzen 7 8840HS, Radeon
  780M, 16 GB shared memory.
- On the Ally X I test in Desktop Mode (KDE). Ask me whether the session is
  X11 or Wayland (`echo $XDG_SESSION_TYPE` on the device). Every build must
  also work in Game Mode (gamescope).
- I only run builds you deliver. I do not build, install packages, or
  change the devices.

## Building

### Before you start

- **Source:** `main`. `upstream` only tracks JuMatt's repository, and
  docs/upstream.md says how its changes are taken.
- **Tags:** fetch this repository's tags (`git fetch --tags origin`). The
  version comes from them, and without them it is `dev`.
- **Game files:** my own USA (NTSC-U) disc, the standalone one. Other
  regions and the Sega GT 2002 combo disc do not work. The build needs only
  its default.xbe.
- **Sandbox:** the network is limited (package registries and GitHub). If a
  tool or file can't be fetched, say so and ask me for it. Ask before
  installing system packages.

### 1. Generated code

Follow docs/building-on-linux.md, steps 2 to 4: capstone in a
virtual environment, default.xbe in `game_files/`, then the pipeline into
`port/recomp/gen`. Its last command must report `0 failed`. Use `python3`,
not `py -3`.

The generated code is large and derived from SEGA's binary. It stays in the
sandbox and is never committed.

### 2. Linux package

docs/building-on-linux.md's own build links the system's SDL2, so its
package only runs on the machine that built it. Test builds follow the
release recipe at the top of launcher/linux/package.sh instead:

- Ubuntu 22.04 (glibc 2.35), for example in a container, with clang and
  Ninja.
- SDL 2.30 built from source as the recipe says: shared, every backend
  loaded at run time, tests off.
- The engine with the recipe's CMake flags, and `CMAKE_PREFIX_PATH` pointing
  at that SDL.
- `launcher/linux/package.sh` on the engine and that SDL's
  `libSDL2-2.0.so.0`, with `SDL_LICENSE` set to SDL's licence file.

### 3. Windows package

Cross-compiled on Linux, as the top of launcher/windows/package.sh records:
llvm-mingw 20241217 (UCRT), SDL2 2.30.0 (the MinGW development package) and
libepoxy 1.5.10 (cross-built with the same toolchain), then CMake with
`launcher/windows/llvm-mingw.cmake`, then `launcher/windows/package.sh`.

### The version

- Each build has one version string, shown in File > About and on the log's
  first line. docs/versioning.md says how it is made.
- Both packages carry the same one. The Linux build takes it from git, and
  the Windows build is given it with `-DJSRF_VERSION=` and package.sh's
  version argument.
- A build with uncommitted changes gets its own with `-DJSRF_VERSION=`,
  naming the commit and the change (as in `ce20630-menu2`), so no two test
  builds share one.

### 4. Tests

- Tool tests: `python3 -m pytest -q tools`, with pytest, pefile, and numpy
  installed beside capstone.
- Engine tests: `python3 tests/<name>/run.py`, for each folder in `tests/`
  that has a run.py. gl_backend, gl_aa and gl_card need EGL and OpenGL
  headers.
- Report what passed, and what didn't run and why.

## Compatibility checks

Run these on every build you deliver.

### Linux: glibc

The Ally X has glibc 2.43, so a binary that needs anything newer won't
start. The published floor is glibc 2.34 (the package's README.txt). Stay
within it so other distributions keep working.

package.sh prints the libraries the engine needs and its newest glibc
symbol. In the extracted package, check the bundled SDL2 too, and that
libstdc++ is linked in rather than needed:

```sh
objdump -T bin/jsrf_recomp | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1
objdump -T lib/libSDL2-2.0.so.0 | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1
objdump -T bin/jsrf_recomp | grep -c 'GLIBCXX\|CXXABI'    # must be 0
```

If binutils is missing, install it or use `readelf -V`, and say which you
used.

Also report:

- `ldd bin/jsrf_recomp` with `LD_LIBRARY_PATH=lib`, in the build image,
  noting any library that is neither bundled nor expected on SteamOS.
- The build image's glibc (`ldd --version`).

If the newest version is above 2.34, name the symbol responsible
(`objdump -T <binary> | grep GLIBC_<version>`) and tell me before
delivering. If it is above 2.43, do not deliver; fix it first.

### Windows: DLLs

package.sh prints the DLLs the launcher and the engine import. Each must be
bundled in `engine/` (SDL2, libepoxy-0, libwinpthread-1) or be part of
Windows. Report the list.

### Linux and Windows: package contents

List what is in each package (`tar tzf` and `unzip -l`) and report it. It
holds only the launcher, the engine and its bundled libraries, README.txt,
VERSION, and `licences/` with a licence for each of them, plus the icon on
Linux. No game files, logs, source, or anything left over from debugging.

## Delivery package

One folder per build, named `<date>-<commit>-<topic>` (as in
`2026-10-23-ce20630-menu-cleanup`), holding:

- `JSRF-PC-<version>-linux-x86_64.tar.gz` and
  `JSRF-PC-<version>-windows-x86_64.zip`: the two package.sh outputs,
  renamed with the version, as releases are (docs/versioning.md).
- `SHA256SUMS.txt` for both.
- `TESTING.md`: the test checklist below.
- `REPORT.md`: the change report below, when the build carries a change.
- The full diff against the commit it was built from, when the tree has
  uncommitted changes.

Never include game files, generated code, or logs in what you send me,
except inside the packaged build.

## Test checklist (TESTING.md)

- **What it is:** the change in one or two lines, the version string, and
  the packages with their SHA-256.
- **Built from:** the commit, whether the tree had uncommitted changes, the
  generated code (functions translated, 0 failed), and the toolchains.
- **Checks done here:** the compatibility checks, the tests, and anything
  run in the sandbox. Everything else is unverified.
- **What to test:** numbered steps, each saying exactly what to do and on
  which device, what I should see if it works, and what I'd see if it
  doesn't.
- **Where:** Desktop Mode, Game Mode, or both on the Ally X, and the Windows
  PC.
- **Order:** one platform at a time, starting with the work's target
  platform. That is Linux (the Ally X) unless the work is for Windows, such
  as a Windows bug. Its steps come first, and testing stays on it, through
  any fixes and new builds, until every step there passes. Only then does
  testing move to the other platform, on the same build, as each build
  carries both packages. After any fix, the steps already passed that it
  could affect are run again on the new build.
- **Regression spots:** title screen, menus, starting a new game, tutorial,
  skating, graffiti.
- **Release builds:** also a fresh start, with the settings, cache, and log
  folders renamed first and put back after (not deleted: a disc image's
  saves are in them). And an upgrade, starting the new build over the
  previous release's settings and saves, which must carry over unchanged.
  docs/release-checklist.md has the rest of a release's steps.
- **What to send back:** the full last-run.log from each run, and
  screenshots of anything visual. The package's README.txt says where the
  log is kept ("Where things are kept", and `portable/logs/` in portable
  mode).

## Change report (REPORT.md)

- **Root cause,** with your confidence and the evidence.
- **Files touched,** with lines added and removed.
- **Upstream files:** those of them upstream has, and their entry in
  docs/upstream.md.
- **Why it is the minimal root-cause fix,** and which existing helpers it
  reuses.
- **New functions,** each with why no existing one does the job.
- **Platform impact:** Linux, Windows, macOS, iOS, and Android, saying which
  were built or verified and which were not.
- **Gamescope (Game Mode) risk,** if any.
- **What to test:** the checklist.
- **Unverified:** everything not proven here.
- **Proposed commit message.**

## Optional habit

Keep a local, untracked BRINGUP.md in the sandbox listing the commands run
and the errors seen, so a long session can be resumed.
