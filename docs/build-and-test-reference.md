# Build and test reference

How test builds of JSRF-PC are made, checked, delivered, and reported. It is
written by the maintainer ("I", who never builds by hand and only runs the
builds) for whoever makes them ("you"), and goes with the project rules.

Where this file and the repo's own docs disagree, trust the repo's docs
(README.md, docs/building-on-linux.md, tools/README.md,
launcher/linux/package.sh, and launcher/windows/package.sh) and tell me
about the difference.

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
  has them. For each build it is put at the commit under test, detached,
  with the diff under test applied, and its tree's hash (`git add -A`, then
  `git write-tree`) must be the one the cloud had. It never has a branch of
  its own, and nothing is ever committed or pushed from it.
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
- **No playing:** you may run the packaged engine's own checks there
  (Compatibility checks, Windows: it starts), which don't start the game,
  and nothing else of the game.
- **One job at a time on each machine:** a build, a CI job or a repeated
  run never shares the PC with another, as load changes what timing tests
  find. The cloud's jobs run one after another too.

### When the PC is busy

One thread has the PC at a time. It claims it in the shared folder's
building-on-pc/PC-IN-USE.md before its Remote Control session starts, and
clears the claim when the session ends. While another thread has it, a
test build doesn't wait: it is made in the cloud, in the thread's sandbox.
A release build still waits for the PC, as it checks its generated code
against the kept copy (What is kept between builds).

- **In the sandbox:** the generated code (Building, 1), both packages
  (Building, 2 and 3), their compatibility checks but the Windows start
  check, and the comparison with the last tested packages in the shared
  folder's `test-builds/`. They use the same pinned build images, which
  the sandbox makes and keeps for as long as the thread lasts (What is
  kept between builds). TESTING.md names the machine it was built on
  (Test checklist).
- **Game files:** default.xbe is copied from my game folder into the
  sandbox. It and the generated code made from it stay there, are never
  sent anywhere else, and go when the thread ends.
- **What waits for the PC:** the work that needs Windows itself, under the
  thread's own claim once the PC is free: the Windows package's start
  check (Compatibility checks, Windows: it starts) and its setup for
  testing (Delivery package), and the Windows tests before a commit (CI).
  Repeated runs wait for it too, as their counts are compared on one
  machine (Repeated runs).
- **Delivery:** the Linux package is delivered as soon as its checks pass,
  and the Windows package once the PC is free. Work for Windows starts its
  testing only then (Test checklist, Order).
- **If the sandbox can't make its build images** either, because a server
  they download from is down (CI, below), the build and the CI jobs wait
  for whichever is back first, the server or the PC.
- **One job at a time:** the sandbox runs its build and its thread's CI
  jobs one after another (On my PC).

### What is kept between builds

In `Building\cache\` and Docker's storage, so a build makes only what
changed:

- the build images: `linux` and `windows` for the packages, and `linux
  tests` for CI, each tagged with image.sh's SHA-256 and made again when
  it changes;
- `launcher/windows/deps.sh`'s downloads and SDL's source, each still
  checked by its SHA-256 every time it is used;
- the generated code, tagged with default.xbe's SHA-256 and one hash of
  what makes it: every tracked file in `tools/` and `port/recomp_manual.c`,
  and the versions of Python and capstone that ran the pipeline
  (docs/building-on-linux.md installs the newest capstone). It is used
  only when both match, and made again otherwise;
- the last tested packages, for the comparison (Compatibility checks).

Nothing else is kept. Every build of SDL and the engine, and every test,
runs from scratch, as on GitHub and as the release is built. A release
build makes its generated code fresh (docs/release-checklist.md, step 7):
if that differs from the kept copy with the same tags, tell me before
going on, as the pipeline then isn't repeatable.

The cloud's sandbox keeps its build images the same way, for as long as
the thread lasts.

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
  Ask before installing system packages, there or on the PC.

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
  installed beside capstone (the versions in `.github/ci/tools-requirements.txt`).
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
links and can be packaged. The engine is never run.

| Job | What it checks |
|---|---|
| Linux, GCC | The engine and the runtime built with GCC, and the link; the engine tests; the tool tests. |
| Linux, clang, and the package | The Linux package, made by `launcher/linux/build.sh` as the release is, and the package check; the engine tests built with clang. |
| Thread sanitizer | The engine tests that start threads, built with clang's ThreadSanitizer (`tests/run_all.py --tsan`). |
| Windows build and package | The Windows package, made by `launcher/windows/build.sh` as the release is, and the package check. |
| Windows tests | The engine tests that run on Windows, on GitHub's Windows Server 2022, built with llvm-mingw. Before a commit, on my Windows PC. |

- **Known bugs:** a test that fails today in a known way is listed in
  `tests/known-failures.txt`, and a race the sanitizer finds today in
  `tests/tsan.supp`, each with its known bug and the change that removes
  it. Such a failure is reported on the run and doesn't fail it. Anything
  else does. The fix for a bug takes its lines out.
- **Before a commit,** every job runs on the commit's exact tree:
  - **In the cloud:** every job but the Windows tests is a script in
    `.github/ci`, run in the build image by `.github/ci/docker.sh`, so the
    same commands run there as on GitHub:
    - `.github/ci/docker.sh "linux tests" .github/ci/linux.sh gcc`
    - `.github/ci/docker.sh "linux tests" .github/ci/linux.sh clang`
    - `.github/ci/docker.sh "linux tests" .github/ci/tsan.sh`
    - `.github/ci/docker.sh windows .github/ci/windows.sh`

    Behind the sandbox's proxy, `CI_BASE` and `CI_DOCKER_ARGS` give Docker
    what it needs (the top of docker.sh). The build images and the Windows
    job's downloads may be kept between runs (Where the work runs, What is
    kept between builds); each job's builds and tests still run from
    scratch.
  - **On my Windows PC:** the Windows tests, as GitHub runs them: the
    Python version in ci.yml, llvm-mingw from `.github/ci/llvm_mingw.py`,
    then `python tests/run_all.py`, in the checkout in `Building\`. The PC
    runs Windows 11, not GitHub's Windows Server 2022, so GitHub's run on
    the push is still the one that counts. A pre-approved thread reads
    that run when it ends (Review pack).
  - **When a server is down:** if the cloud can't make its build images
    because a server they download from is down (Ubuntu's snapshot
    archive, which is often slow), its four jobs run on my Windows PC
    instead, from the images the PC keeps: each job's script in the image
    of its groups, as docker.sh runs it but without making the image
    again, in a checkout with nothing but the commit's tree, and the
    Windows job with its downloads kept. Say so in the review pack.
  - The two machines run their jobs at the same time, and each runs its
    own one after another.

### Repeated runs

A known failure that comes and goes, such as known bugs 19 and 22, or a
race the sanitizer finds only sometimes (bug 20), is measured by running
its test many times: at least 20 runs, reporting how many failed and how.
A fix for one is shown by the same count with and without it, on the same
machine. Repeated runs go on the PC, in the build image, one test at a
time, and alone on the machine (Where the work runs, On my PC).

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
  (functions translated, 0 failed, and whether it was the kept copy), the
  toolchains, and the build image with its packages.
- **Checks done here:** the compatibility checks, the comparison with the
  last tested package, the tests, and anything run in the sandbox or on
  the PC. Everything else is unverified.
- **What to test:** numbered steps for the areas the change's code touches,
  and nothing else, each saying exactly what to do and on which device,
  what I should see if it works, and what I'd see if it doesn't.
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
  checked just before writing.
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
under which pre-approval (my words and their time). GitHub's run on the
push is added when it ends.

## Optional habit

Keep a local, untracked BRINGUP.md in the sandbox listing the commands run
and the errors seen, so a long session can be resumed.
