# Build and test reference

How test builds of JSRF-PC are made, checked, delivered, and reported. It is
written by the maintainer ("I", who never builds by hand and only runs the
builds) for whoever makes them ("you"), and goes with the project rules.

Where this file and the repo's own docs (README.md,
docs/building-on-linux.md, tools/README.md, launcher/linux/package.sh, and
launcher/windows/package.sh) disagree, stop and tell me. Don't pick one.

## At a glance: a test build

In order, with where each step is said in full:

1. **Where:** claim the PC in building-on-pc/PC-IN-USE.md, or build in the
   sandbox while another thread has it (Where the work runs).
2. **The tree:** the commit under test with its diff, and the same tree
   hash on both machines (On my PC).
3. **Build:** the generated code with 0 failed, then both packages in
   their pinned images (Building).
4. **Check:** the tests, the compatibility checks, and the comparison with
   the last tested packages (Building 4, Compatibility checks).
5. **Deliver:** the folder in `test-builds/`, with TESTING.md, its test
   list, and REPORT.md for a change (Delivery package, Test checklist,
   Test list).
6. **Read back:** my results and every log in full, and every step still
   untested, per device (Test list; project rules, Workflow 5).

## Test devices

- **Primary:** ROG Xbox Ally X: x86_64, SteamOS 3.9.5, glibc 2.43, Z2
  Extreme with AMD Radeon Graphics, 24 GB shared memory.
- **Windows:** desktop PC: Windows 11, i7-10700K, 32 GB DDR4, RTX 2080 Ti
  with 11 GB video memory. It is also where test builds are made (Where
  the work runs, below).
- **Occasionally:** Lenovo Yoga laptop: Windows 11, Ryzen 7 8840HS, Radeon
  780M, 16 GB shared memory. When it's free, an extra Windows test device
  for AMD's Windows driver, where known bug 4 shows. Nothing is built on
  it, and no test waits for it.
- On the Ally X I test in Desktop Mode (KDE). Ask me whether the session is
  X11 or Wayland (`echo $XDG_SESSION_TYPE` on the device). Every build must
  also work in Game Mode (gamescope).
- I only run builds you deliver. I don't build by hand: you build on the
  Windows PC yourself, or in the cloud while another thread has it (When
  the PC is busy, below). Nothing on the devices is installed or changed but
  what "On my PC" (below) lists, unless I say so.

## Where the work runs

Two machines share the work, at the same time:

- **The cloud,** a thread's sandbox: the code, the diagnosis, the CI jobs
  in the build image (CI, below), test builds while another thread has the
  PC (When the PC is busy, below), the delivery folder in the shared
  folder, and every commit and push.
- **My Windows PC,** through a Remote Control session, in `Building\` in my
  Claude folder: the generated code, both packages and their checks, the
  comparison with the last tested package, the Windows tests, repeated
  runs of a test, and the rest of CI when a server it downloads from is
  down (CI, Repeated runs).

So the PC builds while the cloud runs CI, and neither waits for the other.
The shared folder's building-on-pc/README.md says how the tree and the
files move between them, and what the PC has installed.

### On my PC

- **Set up once:** Docker, through WSL 2, for the build image, and Git and
  Python for the checkout and the Windows tests, each at the version
  building-on-pc/README.md lists. Anything else, or another version, is
  asked for first.
- **The checkout:** `Building\src`, a clone of this repository that is only
  built from, with `core.autocrlf` off so the files are as the repository
  has them. It is never reset or cleaned, as the PC's permission check
  refuses that: each run makes its own detached worktree of it, in the
  run's folder (`git worktree add --detach <folder>\src <commit>`), with
  the diff under test applied by `git apply --index`, and that tree's hash
  (`git write-tree`) must be the one the cloud had. `git apply --index`
  gives each file the diff's mode; `git add -A` wouldn't, as
  `core.filemode` is off there. Neither has a branch of its own, and
  nothing is ever committed or pushed from either.
- **Game files:** the generated code is made on the PC, from my own
  default.xbe there, and stays in `Building\`. It never leaves the PC, and
  generated code made in the cloud never goes there. default.xbe leaves
  the PC only for a build in the cloud (When the PC is busy, below).
- **What is written:** only `Building\`, which holds the build
  distribution's disk and so Docker's storage, and what WSL itself keeps
  about that distribution (building-on-pc/README.md lists it). My
  settings, saves and other folders are never changed, and neither is
  my portable folder in `Building\`: each build gets a copy of it
  (Delivery package).
- **No playing by hand:** you may run the packaged engine's own checks
  there (Compatibility checks, Windows: it starts), which don't start the
  game, and its runs with nobody at the pad: the idle run (Compatibility
  checks, Windows: the idle run) and, once they exist, the smoke set's
  saved runs (The smoke set). Nothing else of the game.
- **A step the permission check refuses:** the session stops there and
  reports it. It never runs that command again, or another to the same
  end. The thread then asks me on a card whether to accept it in the
  Claude app or to change the step.
- **One job at a time on each machine:** a build, a CI job or a repeated
  run never shares the PC with another, as load changes what timing tests
  find. The cloud's jobs run one after another too.

### When the PC is busy

One thread has the PC at a time. It claims it in the shared folder's
building-on-pc/PC-IN-USE.md before its Remote Control session starts, and
clears the claim when the session ends, moving its entry to the top of
building-on-pc/PC-HISTORY.md. While another thread has it, a
test build doesn't wait: it is made in the cloud, in the thread's sandbox.
A release build still waits for the PC, as it checks its generated code
against the kept copy (What is kept between builds).

- **In the sandbox:** the generated code (Building, 1), both packages
  (Building, 2 and 3), their compatibility checks but the Windows start
  check, and the comparison with the last tested packages in the shared
  folder's `test-builds/`. They use the same published build images,
  which the sandbox pulls and keeps for as long as the thread lasts (What
  is kept between builds). TESTING.md names the machine it was built on
  (Test checklist).
- **Game files:** default.xbe is copied from my game folder into the
  sandbox. It and the generated code made from it stay there, are never
  sent anywhere else, and go when the thread ends.
- **What waits for the PC:** the work that needs Windows itself, under the
  thread's own claim once the PC is free: the Windows package's start
  check and idle run (Compatibility checks, Windows: it starts, and
  Windows: the idle run) and its setup for testing (Delivery package),
  and the Windows tests before a commit (CI).
  Repeated runs wait for it too, as their counts are compared on one
  machine (Repeated runs).
- **Delivery:** the Linux package is delivered as soon as its checks pass,
  and the Windows package once the PC is free. Work for Windows starts its
  testing only then (Test checklist, Order).
- **If the sandbox can't get its build images** either (CI, When a
  server is down), the build and the CI jobs wait for whichever is back
  first, the server or the PC.
- **One job at a time:** the sandbox runs its build and its thread's CI
  jobs one after another (On my PC).

### What is kept between builds

In `Building\cache\` and Docker's storage, so a build makes only what
changed:

- the build images: `linux` and `windows` for the packages, and `linux
  tests` for CI. `launcher/image.sh` makes them, and CI's first job
  publishes them on GitHub's registry (`ghcr.io/spriteice/jsrf-pc-build`)
  on a push to `main` that finds them missing, before its other jobs
  start, each named for its groups and image.sh's SHA-256 and never
  replaced. The package is public, so they're pulled without signing in.
  `launcher/images.sh get` uses the image Docker has, or pulls it, or,
  while it isn't published yet, makes it: from Ubuntu's snapshot archive,
  or the same versions from Ubuntu's own archive when the snapshot fails,
  while that archive still has them. So a new image.sh names new images;
- the conformance image, for CI only: `launcher/image-386.sh` makes it
  from Alpine for i386 (linux/386), and it is published and got the same
  way, named for that file's SHA-256. Alpine's archive keeps only each
  package's newest version, so making it from scratch again needs the
  file's versions brought up to date, which names a new image;
- `launcher/windows/deps.sh`'s downloads and SDL's source, each still
  checked by its SHA-256 every time it is used;
- the generated code, tagged with default.xbe's SHA-256 and one hash of
  what makes it: every tracked file in `tools/` and `port/recomp_manual.c`,
  and the versions of Python and capstone that ran the pipeline
  (docs/building-on-linux.md installs capstone 5.0.9, the version CI
  tests the tools with). It is used
  only when both match, and made again otherwise;
- the last tested packages, for the comparison (Compatibility checks).

Nothing else is kept. Every build of SDL and the engine, and every test,
runs from scratch, as on GitHub and as the release is built. A release
build makes its generated code fresh (docs/release-checklist.md, step 7):
if that differs from the kept copy with the same tags, tell me before
going on, as the pipeline then isn't repeatable.

When the generated code is made again because the hash of what makes it
changed, compare it with the kept copy the change's base used:
`python3 tools/gen_diff.py <kept> <new>` (tools/README.md). It
counts the functions that are the same, changed, changed only in their
comments, added and removed, and lists the addresses of those changed,
added and removed, never their code or names, so its report goes in
TESTING.md (Test checklist, Built from). Each function it lists is
accounted for there before delivery, as the package comparison's
differences are: by what the change does, one reason covering as many
functions as it explains, or by what you found when you looked. A change
that shouldn't alter the code, such as a new tool or test in `tools/`,
lists none. The sandbox keeps no generated code between threads, so there
the kept copy is made from the base commit first.

The cloud's sandbox keeps its build images the same way, for as long as
the thread lasts. A build image made there or on the PC, while one isn't
published, is never published itself.

## Building

### Before you start

- **Source:** `main`. `upstream` is JuMatt's history up to 0.5.9, kept as a
  record and never merged (docs/upstream.md).
- **Tags:** fetch this repository's tags (`git fetch --tags origin`). The
  version comes from them, and without them it is `dev`.
- **Game files:** my own USA (NTSC-U) disc, the standalone one. Other
  regions and the Sega GT 2002 combo disc do not work. The build needs only
  its default.xbe.
- **Sandbox:** the cloud's network is limited (package registries and
  GitHub). If a tool or file can't be fetched, say so and ask me for it.
  Ask before installing system packages, there or on the PC. A Python
  virtual environment, in a container or the thread's own folder, isn't
  one (Building, 4).

### 1. Generated code

Follow docs/building-on-linux.md, steps 2 to 4: capstone in a
virtual environment, default.xbe in `game_files/`, then the pipeline into
`port/recomp/gen`. Its last command must report `0 failed`. Use `python3`,
not `py -3`.

The generated code is large and derived from SEGA's binary. It stays on the
machine that made it, in `Building\` on the PC (or the sandbox), and is
never committed or copied anywhere else.

### 2. Linux package

docs/building-on-linux.md's own build links the system's SDL2, so its
package only runs on the machine that built it. Test builds are made as the
release is, the way docs/building-on-linux.md's "A package for other
machines" says:

- The build image: `ubuntu:22.04` by digest, set up by
  `launcher/image.sh linux`, and pulled with `launcher/images.sh get
  linux` (What is kept between builds).
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
  `launcher/image.sh windows`, and pulled with `launcher/images.sh get
  windows`.
- `launcher/windows/build.sh /work <version>`: llvm-mingw 20241217 (UCRT),
  SDL2 2.30.0 (the MinGW development package) and libepoxy 1.5.10, built
  with meson 1.12.1 (`launcher/windows/deps.sh`), then the engine with
  `launcher/windows/llvm-mingw.cmake`, then `launcher/windows/package.sh`.

Those versions are pinned, like the Linux image, and each download is
checked by its SHA-256. A download is tried 5 times, with pauses of 10 to
40 s, as GitHub's release downloads time out now and then; so are SDL's
source and llvm-mingw for CI (`.github/ci/fetch.py`).

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
  installed beside capstone (the versions in `.github/ci/tools-requirements.txt`).
  In the sandbox they run in the `linux tests` image, with the engine
  tests, as CI's GCC job runs them:
  `.github/ci/docker.sh "linux tests" .github/ci/linux.sh gcc`. A runner of
  the thread's own never stands in for them.
- Engine tests: `python3 tests/run_all.py`, which runs
  `tests/<name>/run.py` for each folder in `tests/` that has one, or only
  the tests named. gl_backend, gl_aa, gl_card, index_runs, psh_consts and
  big_batch need EGL and OpenGL headers, and run only on Linux. The last
  three also need GCC.
- Report what passed, and what didn't run and why.

## CI

GitHub runs `.github/workflows/ci.yml` on every push and pull request, with
none of the game's files. The engine is built against an empty
generated-code folder, and `.github/ci/link_check.py` checks that its link
lacks only what the generated code defines, then stubs that out so the rest
links and can be packaged. The engine is never run. Its first job, Build
image, publishes any build image not yet published for this image.sh, on a
push to `main`, and the jobs in those images start after it (What is kept
between builds).

| Job | What it checks |
|---|---|
| Build image | Nothing: on a push to `main`, it publishes each build image not yet published (`launcher/images.sh publish`). |
| Linux, GCC | The engine and the runtime built with GCC, and the link; the Debug build too, as far as the link, with the recompiler's register check and indirect-call records (`port/CMakeLists.txt`); the engine tests; the tool tests, and CI's own (`.github/ci/test_ci.py`); both builds' warnings (below); and, for information only, the engine tests' line coverage of `src/` and `port/` (below). |
| Linux, clang, and the package | The Linux package, made by `launcher/linux/build.sh` as the release is, and the package check; the engine tests built with clang; the build's warnings. |
| Thread sanitizer | The engine tests that start threads, built with clang's ThreadSanitizer (`tests/run_all.py --tsan`). |
| Address and undefined-behaviour sanitizers | Every engine test, built with GCC's AddressSanitizer, its leak check included, and UndefinedBehaviorSanitizer (`tests/run_all.py --asan`). |
| Recompiler conformance tests | The recompiler's lifter against the CPU: each snippet in `tools/conformance` assembled by GCC and run, then lifted and its C run beside it, and the two compared (`python3 -m tools.conformance`), in the 32-bit conformance image (`launcher/image-386.sh`, linux/386). The corpus phase, which needs MSVC, is skipped. |
| Windows build and package | The Windows package, made by `launcher/windows/build.sh` as the release is, and the package check; the build's warnings. |
| Windows tests | The engine tests that run on Windows, on GitHub's Windows Server 2022, built with llvm-mingw. Before a commit, on my Windows PC. |

- **Known bugs:** a test that fails today in a known way is listed in
  `tests/known-failures.txt`, a race the thread sanitizer finds today in
  `tests/tsan.supp`, and what the address and undefined-behaviour
  sanitizers find today in `tests/asan-known.txt`, each with its known bug,
  the change that removes it, and the release that is to fix it (or, in
  tsan.supp, that it is there by design), which `tests/run_all.py` checks
  every line has. Such a failure is reported on the run and doesn't fail
  it. Anything else does. The fix for a bug takes its lines out, and each
  release checks them (docs/release-checklist.md, step 4). Each entry in
  tsan.supp also says how many reports it may hide in each test ("hides at
  most", about twice the most seen in 30 runs, as the count varies with
  timing), and a test in which one hides more, or any in a test it doesn't
  name, fails: so an entry can't quietly hide a new race in another test,
  or many more of the same. An entry names the narrowest thing that covers
  its bug: the register path's two (`recomp_mmio_read` and `_write`) are
  `race_top:` entries, which hide only the game's own access in them.
- **Warnings:** our code (all but `third_party/` and the generated code) is
  built with `-Wall -Wextra`, and each build job counts the warnings in it,
  once a place (`.github/ci/count_warnings.py`), against its line in
  `.github/ci/warnings.txt`. A count above the line fails the job. A count
  below it passes, and the job's log says the line can come down: a later
  change to CI lowers it, so a code commit never has to change `.github/`.
  At 0, any warning fails the job, as `-Werror` would. The job's log lists
  every warning it counted. These are the static checks of the project
  rules (Workflow 4).
- **Coverage:** the GCC job runs the engine tests again built with GCC's
  line counts (`tests/run_all.py --coverage`), and its log ends with each
  file's lines reached, the files of `src/` and `port/` no test builds,
  and the totals (`.github/ci/coverage.py`). It is for information only:
  it fails nothing, and nothing keeps it. A test that builds a copy of a
  file's text, as `tests/nv2a_queue` does, counts towards the copy.
- **Before a commit,** every job runs on the commit's exact tree, except
  for a commit that changes only Markdown files, whose CI is GitHub's run
  on its push, and for one moved since its run onto commits that change
  only Markdown files, whose run stands (project rules, Before every
  commit, 6, and Several items at once). Both hold only while no job or
  test reads a Markdown file, so a change that makes one read it names
  that file here:
  - **In the cloud:** every job but Build image and the Windows tests is
    a script in `.github/ci`, run in the build image by
    `.github/ci/docker.sh`, so the same commands run there as on GitHub:
    - `.github/ci/docker.sh "linux tests" .github/ci/linux.sh gcc`
    - `.github/ci/docker.sh "linux tests" .github/ci/linux.sh clang`
    - `.github/ci/docker.sh "linux tests" .github/ci/tsan.sh`
    - `.github/ci/docker.sh "linux tests" .github/ci/asan.sh`
    - `.github/ci/docker.sh conformance .github/ci/conformance.sh`
    - `.github/ci/docker.sh windows .github/ci/windows.sh`

    Each job runs in its published build image, pulled or kept (What is
    kept between builds). Behind the sandbox's proxy, `CI_DOCKER_ARGS`
    gives Docker what it needs, and `CI_BASE` a base image for a build
    image made there (the top of docker.sh). The shared folder's
    cloud-builds/SANDBOX.md has the recipe, with a script that starts
    Docker and sets both. The Windows job's downloads may be
    kept between runs too; each job's builds and tests still run from
    scratch.
  - **On my Windows PC:** the Windows tests, as GitHub runs them: the
    Python version in ci.yml, llvm-mingw from `.github/ci/llvm_mingw.py`
    (in `Building\cache\llvm-mingw-tests`, where a zip with its SHA-256
    isn't downloaded again, nor a copy unpacked whole from it unpacked
    again), then `python tests/run_all.py`, in the run's own worktree (On
    my PC, The checkout), with TEMP and TMP set to an empty `tmp\` in the run's
    folder, so the tests' own temporary folders are made there too, and
    the run shows it left nothing behind (On my PC, What is written). The
    PC runs Windows 11, not GitHub's Windows Server 2022, so GitHub's run on
    the push is still the one that counts. A pre-approved thread reads
    that run when it ends (Review pack).
  - **When a server is down:** if the cloud can't get its build images
    because a server is down (GitHub's registry, or, for an image not
    published yet, both of Ubuntu's archives), its jobs run on my Windows
    PC instead, from the images the PC keeps: each job's script in the
    image of its groups, as docker.sh runs it, in a checkout with nothing
    but the commit's tree, and the Windows job with its downloads kept.
    Say so in the review pack.
  - The two machines run their jobs at the same time, and each runs its
    own one after another.

### Repeated runs

A known failure that comes and goes, such as known bugs 19 and 22, or a
race the sanitizer finds only sometimes (bug 20), is measured by running
its test many times: at least 20 runs, reporting how many failed and how.
A fix for one is shown by the same count with and without it, on the same
machine. Repeated runs go on the PC, in the build image, one test at a
time, and alone on the machine (Where the work runs, On my PC).

A new or changed test that starts threads or measures time is run the
same way before its commit (project rules, How it's written): 20 runs in a
row on Linux, in the build image, and 20 on Windows if it runs there, and it
passes every one. A test whose commit is checked while another thread has
the PC runs its Linux runs in the sandbox's build image, alone there, and
its Windows runs once the PC is free. The review pack gives both counts.

### The same engine

A change to what the game runs that leaves the engine the same, byte for
byte, needs no test build (project rules, What the game runs). Its review
pack shows that with the engine built from the base tree and from the
change's tree, each checkout mounted at the same path, in each way it's
built:

- with GCC, in the `linux tests` image, as `.github/ci/linux.sh gcc`
  configures it;
- with clang and the options `launcher/linux/build.sh` gives, as the Linux
  package is built;
- as the Windows package, by `launcher/windows/build.sh` with llvm-mingw
  in the `windows` image.

Every object and static library is compared by SHA-256, and every one must
be the same. A linked program isn't compared, as its link's timestamp
differs on every build. No build here has the generated code, so a change
to what makes it (the parts of `tools/` the pipeline runs, and
`port/recomp_manual.c`) can't be shown this way. The shared folder's
rules-review/code-quality-2026-11-05/ci/masks/evidence/engine-objects-before-and-after.txt
is an example.

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

### Windows: it starts

On the PC, from the package as delivered, `engine\jsrf_recomp.exe
--version` prints the build's version, which shows that Windows loads it
and every DLL it needs, and `engine\jsrf_recomp.exe --check-game` on my
game folder prints `ok`. Neither starts the game. Report both outputs.

### Windows: the idle run

On the PC, the package as delivered then plays the game's title on its
own, with nobody at the pad, to show that the build gets that far and stays
up:

- **How:** with the shared folder's building-on-pc/idle-run/ script (its
  README says how), on a second copy of my portable folder, made as for
  testing (Delivery package), so the copy I test with isn't touched. The
  game shows its title and the scene behind it, and about 3 minutes 40
  seconds in (the game's frame 13415) goes back to the intro, loading the
  title again. The run ends there, and the game is closed through its
  window, not killed. input-playback/PHASE-0.md's six idle runs are what
  it looks like.
- **Passes** when its log, read in full on the PC, has the build's version
  on its first line, shows the title loaded at the start and again after
  the timeout (two `[LOAD]` lines for mission 0000005A's stage 00010000),
  and has no crash or freeze report (`[CRASH]` or `[FROZEN]`). The script
  checks those lines and says which it found. Report it with the other
  checks in TESTING.md.
- **If it fails,** the build isn't delivered until it's fixed, or until
  I've seen why and said to go on.

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
`2026-10-23-ce20630-menu-cleanup`). It is made in `Building\` on the PC,
or in the sandbox while another thread has the PC (When the PC is busy),
and kept in the shared folder's `test-builds/`, which stays the record,
holding:

- `JSRF-PC-<version>-linux-x86_64.tar.gz` and
  `JSRF-PC-<version>-windows-x86_64.zip`: the two package.sh outputs,
  renamed with the version, as releases are (docs/versioning.md).
- `SHA256SUMS.txt` for both.
- `TESTING.md`: the test checklist below.
- `<folder>.html`: the test list, made from TESTING.md (Test list, below).
- `REPORT.md`: the change report below, when the build carries a change.
- The full diff against the commit it was built from, when the tree has
  uncommitted changes.

Each package reaches its device the way I fetch it, and its SHA-256 is
checked against SHA256SUMS.txt after every copy:

- **Linux:** the package is attached to your message in the thread, as a
  file I download in the browser on the Ally X. My Linux logs come back
  the same way, attached to my message.
- **Windows:** the package stays on the PC, extracted in
  `Building\<folder>\`, and I run it from there. Before I test, my
  portable folder in `Building\` is copied next to its JSRF-PC.exe,
  without its logs. In that copy's developer.cfg the switches TESTING.md
  names are set, and every other one goes back to its default, so a
  switch left on from an earlier test can't change this one. The build
  then starts in portable mode, with my settings and saves, ready to
  test. Progress I make there stays in that build's copy. Its test list
  goes to `Testing\` (Test list, below). A package made in the cloud is
  copied there from `test-builds/` once the PC is free, then set up the
  same way.

Never include game files, generated code, or logs in what you send me,
except inside the packaged build.

My Claude folder, on the Windows PC, has `Building\` for builds (Where the
work runs), `Testing\` for test lists, with their results in
`Testing\Results\`, and `Source\` for source backups
(docs/release-checklist.md, step 12). The shared folder's
test-lists/README.md has its path.

## Test checklist (TESTING.md)

- **What it is:** the change in one or two lines, the version string, and
  the packages with their SHA-256.
- **Built from:** the commit, whether the tree had uncommitted changes and
  its tree's hash, the machine it was built on, the generated code
  (functions translated, 0 failed, whether it was the kept copy, and
  gen_diff's report when it was made again for a change to what makes it:
  What is kept between builds), the toolchains, and the build images, by
  name and digest, with their packages.
- **Checks done here:** the compatibility checks, the comparison with the
  last tested package, the tests, and anything run in the sandbox or on
  the PC. Everything else is unverified.
- **What to test:** numbered steps for the areas the change's code touches,
  and nothing else, each saying exactly what to do and on which device,
  what I should see if it works, and what I'd see if it doesn't. A change
  to a shared layer also has the smoke set's runs, once they exist (The
  smoke set).
- **Where:** Desktop Mode, Game Mode, or both on the Ally X, and the Windows
  PC. A step for the laptop is marked optional, and only for a change that
  could act differently on AMD's Windows driver.
- **Switches:** each logging switch, and any other Developer switch, the
  test needs on each device, with its value. On the PC they are set for me
  (Delivery package); on the Ally X the step says which to tick.
- **Saved runs:** in a pre-approved thread (project rules, Pre-approved
  threads), each step names the saved run that tests it, from the shared
  folder's `input-playback/RUNS.md`, or says that I test it by hand.
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
  `portable/logs/` in portable mode). Linux logs come attached to my
  message in the thread. Windows logs are read on the PC, in the build's
  own `portable\logs\`, so I don't need to send them.

## The smoke set

A fixed set of saved runs (input playback's recordings of my play, which
play back with nobody at the pad), a few minutes each, that goes through
the game end to end: the start to the title, a new game and the tutorial,
one stage in each area, a save and a load, File > Restart, and the garage's
graffiti picker. It finds what a change's own steps can't: a shared layer
breaking a stage nobody was looking at.

- **When:** every test build of a change to a shared layer, and every
  release (docs/release-checklist.md, step 8). A shared layer is any of
  the layers but `port/` in the project rules (Where code goes, 2): the
  recompiler and the pipeline, the kernel, the device models, the renderer
  and the platform layer, and the build itself (the CMake files, the build
  images, the flags). When unsure, it is one.
- **Where:** on the PC and on the Ally X, with nobody at the pad, as
  input-playback/RUNS.md says. Each run is a step in TESTING.md, and it
  passes when its log, read in full, shows what the step says I'd see.
- **Until saved runs exist** (RUNS.md lists them once input playback is
  built), there is no smoke set and nothing waits for it. Once it exists,
  a new feature's own saved run joins it (project rules, How it's
  written).

## Batches

One test build can carry several ready items, so I test them in one go.

- **What goes in one:** items with the same target platform (Test
  checklist, Order) that don't change the same code. An item whose fix
  could change what another's steps test goes in a build of its own.
- **TESTING.md** has one section for each item, naming its backlog row,
  with its own numbered steps, and one test list is made from it. REPORT.md
  has one section for each item too.
- **A failed step** belongs to its item, which is fixed and built again
  with the rest, and the steps it could affect are run again (Test
  checklist, Order). If it can't be told which item a failure comes from,
  the next build leaves out the items it could be.
- **The commits** are still one for each item (project rules, How it's
  written 2), proposed in turn once that item's steps have passed, in the
  order the build applied them, so the last commit's tree is the one I
  tested. Each has its own CI run on its own tree.

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
  In a pre-approved thread, they are what lets a commit through (project
  rules, Pre-approved threads).

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

## Review pack

Everything I need to approve one commit, in one file: `REVIEW.md`, in a
folder for that commit in the work's folder in the shared folder, with
what it links to beside it. In this order:

- **What I'm asking:** the commit, and the `origin/main` it goes on,
  checked just before writing. The ask itself goes on a card in the
  thread (project rules, at the top).
- **Testing:** each step of the test list, with its device and my result,
  and every step still untested, with the device it needs (project rules,
  Workflow 5).
- **The six checks** (project rules, Before every commit), each with its
  result: what's staged; the diff, with its SHA-256 and whether it is the
  test build's byte for byte (or, in a batch, its item's part of it); the
  commit message; the root cause, its model and the regression test, or
  for a feature where it lives; each added or edited document, rendered
  whole; and each CI job with its log, the Windows tests' on the PC among
  them.

Nothing it needs is left to another message or file I haven't been
pointed to.

In a pre-approved thread (project rules, Pre-approved threads), the pack
is the commit's record, written the same way before the commit. What I'm
asking becomes what is committed and pushed, on which `origin/main`, and
under which pre-approval (my words and their time). If `origin/main` moved
after its tests, the pack also gives the commits it now goes onto, the
check both ways and its answer (project rules, Pre-approved threads,
Alongside other threads), and what ran again on the new tree. GitHub's run
on the push is added when it ends.

## Optional habit

Keep a local, untracked BRINGUP.md in the sandbox listing the commands run
and the errors seen, so a long session can be resumed.
