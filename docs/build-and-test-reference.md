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

- **Source:** `main`. `upstream` is JuMatt's history up to 0.5.9, kept as a
  record and never merged (docs/upstream.md).
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
package only runs on the machine that built it. Test builds are made as the
release is, the way docs/building-on-linux.md's "A package for other
machines" says:

- The build image: `ubuntu:22.04` by digest, set up by
  `launcher/image.sh linux`.
- `launcher/linux/build.sh /work`, with no network: SDL 2.30.12 from source
  (`launcher/linux/sdl.sh`), the engine with the release flags, then
  `launcher/linux/package.sh`.

The build image is pinned. The recipe names the image, every package
installed in it with its version, and every option given to SDL's CMake,
and nothing else is installed. If something must be, add it to the recipe
first, and tell me. A package in the image can change what SDL builds with
no error: installing libibus-1.0-dev turned on IBus in the bundled SDL, and
`-DSDL_IBUS=OFF` keeps it off. sdl.sh stops if SDL's configuration isn't the
one recorded in `launcher/linux/sdl-config.h`.

### 3. Windows package

Cross-compiled on Linux, as the release is, the way
docs/building-on-linux.md's "Windows" says:

- The build image: `ubuntu:22.04` by digest, set up by
  `launcher/image.sh windows`.
- `launcher/windows/build.sh /work <version>`: llvm-mingw 20241217 (UCRT),
  SDL2 2.30.0 (the MinGW development package) and libepoxy 1.5.10, built
  with meson 1.12.1 (`launcher/windows/deps.sh`), then the engine with
  `launcher/windows/llvm-mingw.cmake`, then `launcher/windows/package.sh`.

Those versions are pinned, like the Linux image, and each download is
checked by its SHA-256.

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
  that has a run.py. gl_backend, gl_aa, gl_card, index_runs, psh_consts
  and big_batch need EGL and OpenGL headers. The last three also need GCC,
  and run only on Linux.
- Report what passed, and what didn't run and why.

## Compatibility checks

Run these on every build you deliver. Both build.sh scripts end by running
`launcher/check_package.py` on the package they made, and it checks any
other package too (`python3 launcher/check_package.py <package>`). Report
its output. If a check fails, fix it before delivering, except glibc
(below).

### Linux: glibc

The Ally X has glibc 2.43, so a binary that needs anything newer won't
start. The published floor is glibc 2.34 (the package's README.txt). Stay
within it so other distributions keep working.

check_package.py checks that the engine and the bundled SDL2 need glibc
2.34 at most, and nothing else from the system but libc, libm, libGL.so.1
and SDL2 (the engine) or libc and libm (SDL2). It also checks that the
engine doesn't need libstdc++: the menu's C++ runtime is linked in.

Also report:

- `ldd bin/jsrf_recomp` with `LD_LIBRARY_PATH=lib`, in the build image,
  noting any library that is neither bundled nor expected on SteamOS.
- The build image's glibc (`ldd --version`, which build.sh prints).

If the newest version is above 2.34, name the symbol responsible
(`objdump -T <binary> | grep GLIBC_<version>`) and tell me before
delivering. If it is above 2.43, do not deliver; fix it first.

### Windows: DLLs

check_package.py checks that each program and DLL imports only the DLLs
bundled beside it (SDL2, libepoxy-0 and libwinpthread-1 in `engine/`) or
ones that are part of Windows 10 and 11. package.sh prints the DLLs the
launcher and the engine import. Report the list.

### Linux and Windows: package contents

check_package.py checks that each package holds exactly the files it lists:
the launcher, the engine and its bundled libraries, README.txt, VERSION, and
`licences/` with a licence for each of them, plus the icon on Linux. So no
game files, logs, source, or anything left over from debugging. A file added
to a package goes on that list in the same commit. It also checks that text
files have the platform's line endings once: LF on Linux, CRLF on Windows,
never `\r\r\n`.

### Linux and Windows: against the last tested package

Compare each package with the last one I tested: the files it holds, and
each file's size and SHA-256. Every difference comes from the change or the
version, or is explained in TESTING.md before delivery. A library built here
differs in its build ID and build folder each time, so for those compare
what it exports and needs (`objdump -T` and `objdump -p`) and its size. A
bundled library that changed when its source and recipe didn't means the
build image changed: find why before delivering.

## Delivery package

One folder per build, named `<date>-<commit>-<topic>` (as in
`2026-10-23-ce20630-menu-cleanup`), holding:

- `JSRF-PC-<version>-linux-x86_64.tar.gz` and
  `JSRF-PC-<version>-windows-x86_64.zip`: the two package.sh outputs,
  renamed with the version, as releases are (docs/versioning.md).
- `SHA256SUMS.txt` for both.
- `TESTING.md`: the test checklist below.
- `<folder>.html`: the test list, made from TESTING.md (Test list, below).
- `REPORT.md`: the change report below, when the build carries a change.
- The full diff against the commit it was built from, when the tree has
  uncommitted changes.

Never include game files, generated code, or logs in what you send me,
except inside the packaged build.

My Claude folder, on the Windows PC, has `Testing\` for test lists, with
their results in `Testing\Results\`, and `Source\` for source backups
(docs/release-checklist.md, step 12). The shared folder's
test-lists/README.md has its path.

## Test checklist (TESTING.md)

- **What it is:** the change in one or two lines, the version string, and
  the packages with their SHA-256.
- **Built from:** the commit, whether the tree had uncommitted changes, the
  generated code (functions translated, 0 failed), the toolchains, and the
  build image with its packages.
- **Checks done here:** the compatibility checks, the comparison with the
  last tested package, the tests, and anything run in the sandbox.
  Everything else is unverified.
- **What to test:** numbered steps for the areas the change's code touches,
  and nothing else, each saying exactly what to do and on which device,
  what I should see if it works, and what I'd see if it doesn't.
- **Where:** Desktop Mode, Game Mode, or both on the Ally X, and the Windows
  PC.
- **Order:** one platform at a time, starting with the work's target
  platform. That is Linux (the Ally X) unless the work is for Windows, such
  as a Windows bug. Its steps come first, and testing stays on it, through
  any fixes and new builds, until every step there passes. Only then does
  testing move to the other platform, on the same build, as each build
  carries both packages. After any fix, the steps already passed that it
  could affect are run again on the new build.
- **Release builds:** also a fresh install and an upgrade, on both
  platforms: the Ally X and the Windows PC. The fresh install is the
  package extracted to a new folder, with the settings, cache, and log
  folders renamed first and put back after (not deleted: a disc image's
  saves are in them). The upgrade is the new build started over the
  previous release's settings and saves, which must carry over unchanged.
  docs/release-checklist.md has the rest of a release's steps.
- **What to send back:** the test list's results, the full last-run.log
  from each run, and screenshots of anything visual. The package's
  README.txt says where the log is kept ("Where things are kept", and
  `portable/logs/` in portable mode).

## Test list

I mark TESTING.md's steps in a page made from it, on the Windows PC, for
every device's testing, and you read my answers from my Claude folder.

- **Made** with `make_test_list.py` in the shared folder's `test-lists/`,
  from TESTING.md as it is delivered: one page that works offline in any
  browser. Each `- [ ]` step gets Pass, Fail, Skip and a notes box, and an
  empty label line in a step, such as `Browser:`, gets a box to fill.
- **Delivered:** `<folder>.html` is copied from the build's folder to
  `Testing\`, where I open it. If TESTING.md changes, the list is made
  and copied again under the same name: my answers carry over by step, and
  the page says the list changed.
- **Read back** from `Testing\Results\<folder>.results.json`, which the
  page's Save results writes, with test-lists' `read_results.py`. It
  checks that the results are for that list and that version of its text.
  If the file can't reach `Testing\Results\`, I paste the same text into
  the chat (More > Copy results).
- **My results count as my message** (project rules, Workflow 5): a step I
  marked Fail, Skip, or nothing hasn't passed, and one I marked Pass still
  has its log read in full. Record them in TESTING.md's notes, as before.

## Change report (REPORT.md)

- **Root cause,** what went wrong and the model behind it, with your
  confidence and the evidence.
- **Diagnosis:** a link to the written diagnosis, and when I agreed it.
- **History:** earlier fixes in the same code, and which of them this
  replaces or removes.
- **Files touched,** with lines added and removed.
- **Why it fixes the root,** whether the same model can cause other bugs,
  and which existing helpers it reuses.
- **Regression test:** the test, and that it fails without the fix. Or why
  there isn't one, and what catches the bug again instead.
- **Overrides:** for each game function override or hook added or changed,
  why it isn't fixed at the source.
- **Docs** updated in the change.
- **New functions,** each with why no existing one does the job.
- **Platform impact:** Linux and Windows, saying which were built or
  verified and which were not.
- **Gamescope (Game Mode) risk,** if any.
- **What to test:** the checklist.
- **Unverified:** everything not proven here.
- **Proposed commit message.**

## Optional habit

Keep a local, untracked BRINGUP.md in the sandbox listing the commands run
and the errors seen, so a long session can be resumed.
