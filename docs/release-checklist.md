# Release checklist

Every JSRF-PC release goes through these steps, in this order. Steps 1 to 6
settle what ships, step 8 is the only one on the test devices, and nothing is
pushed or published before step 9. docs/versioning.md says how a release is
named, and docs/build-and-test-reference.md how it is built and tested.

## At a glance

1. Backlog closed.
2. Switches retired.
3. Main ready.
4. Known bugs current, and the gate.
5. Rules review, with the rule changes gathered since the last release.
6. README and docs.
7. Release build.
8. Test those packages.
9. Go-ahead.
10. Push and publish together.
11. Check the release.
12. Archive and move on.

A hotfix takes the gate and steps 7 to 12 only (If a release is broken).

The maintainer's shared folder, outside the repository, holds known-bugs.md
and `release-prep/`: each release's backlog, the release notes template with
the writing rules, and the files of past releases. Each backlog's last row,
Release, says which step the release has reached.

## The gate

No release ships with an open critical bug, hotfixes included. A bug is
critical when, on Linux or Windows, it stops the game starting, freezes,
hangs, or crashes it, loses saves or settings, or stops a player
progressing, however rarely it happens.

- A critical bug is fixed and tested before the release build (step 7).
  One found in testing (step 8) stops the release until it's fixed.
- The gate has no waiver. The maintainer can decide that a bug isn't
  critical, with the reason written in known-bugs.md, but not ship one that
  is.
- The one exception is a hotfix that fixes a critical bug. It can ship
  while other critical bugs from the release it replaces are still open,
  if it adds none and lists them as known issues.

## Freeze the scope

1. **Backlog closed:** every item in the release's backlog is done, or moved
   to a later one with the maintainer's OK.
2. **Switches retired:** every switch that turns a fix back to its old
   behaviour, from a release already out, is removed (docs/project-rules.md,
   Where code goes 5), before the review and the tests, so both cover it.
3. **Main ready:** the latest `origin/main`, with no unpushed commits from
   elsewhere and a clean tree. Experiments and developer switches are off by
   default, and nothing left from debugging is still on.
4. **Known bugs current:** known-bugs.md marks what this release fixed,
   adds anything new, and says whether each open bug is critical (The
   gate). Its open bugs become the release text's known issues. If one is
   critical, the release stops here until it's fixed. Every line of
   `tests/known-failures.txt`, `tests/asan-known.txt` and `tests/tsan.supp`
   names the release that is to fix its bug: a line that names this release
   or an earlier one is fixed now, its lines out, or moved to a later
   release with the maintainer's OK.

## Review

5. **Rules review:** everything since the last release is checked against
   the project's rules. It looks for game files, generated code, or logs in
   the repo, session links in commit messages, band-aid fixes, address
   lists, sleeps, and docs that break the writing rules. It flags code that
   makes up in one layer for a bug in another, and any mechanism that had a
   second fix with no review of it (docs/project-rules.md, Where code goes 2
   and 7). It also searches the code in `port/` for the game function
   overrides and hooks added since the last release, and flags any that
   look alike (the same kind of instruction, or the same symptom): those
   usually mean one recompiler or runtime bug, to fix there instead.
   Every fixed bug has its regression test, or says why not; every
   override's comment says why it isn't fixed at the source; and the docs
   changed with their code (docs/project-rules.md, How it's written).

   The rule changes gathered under Rule changes in the release's backlog
   are made here too, in one commit with the maintainer's approval
   (docs/project-rules.md, Changing the rules).

   Code findings are fixed now, each in its own commit, and doc findings in
   step 6. The review comes before the docs, as its fixes can change what
   the changelog says, and before the build, so that its fixes are tested.

## Docs

6. **README and docs:** checked against the release and the writing rules.
   Each change updated its own docs when it was made, so this catches what
   was missed, and adds what only the release brings.
   - **README.md:** the features, the download links and latest version, the
     status and known issues, and the credits, with JuMatt's at the top.
   - **docs/:** anything the release changes, such as new keys or menu rows
     in docs/playing.md.
   - **Both packages' README.txt,** in `launcher/linux/` and
     `launcher/windows/`. They ship inside the packages, so they must be
     right before the build.
   - **The changelog entry** in CHANGELOG-JSRF-PC.md, as docs/versioning.md
     says. It goes in the commit the release is built from.
   - **The release text,** from the release notes template. Its lists are
     the changelog's, word for word, and its known issues come from step 4.

## Build and test

7. **Release build:** from the release commit, in the pinned build image,
   as docs/build-and-test-reference.md says, with the release's version given
   to every build (`-DJSRF_VERSION=b1.1`, and the version argument of the
   Windows package.sh, make_kit.sh and firststart/package.sh). The generated
   code is made fresh (0 failed), every test passes, and every package
   passes every check, including the comparison with the last tested
   package. The version reads as the release's name everywhere
   docs/versioning.md lists, with its Windows File version.

   From b2, the release ships the packages without the engine and a tools
   archive for each system (docs/build-and-test-reference.md, Packages
   without the engine), made in this order:
   - each system's kit, and its package and tools archive;
   - a first start of each package on the PC, in a new folder, with the
     game and the tools archive, and its `build/engine.stamp`'s hashes put
     in the kit (`launcher/kit/set_expected.py`); then each package made
     again from its kit;
   - a first start of each final package, in a new folder, whose engine and
     generated code both report `match`: every player's PC builds that
     engine, byte for byte.

   SHA256SUMS.txt covers the two packages and the two tools archives.

   A Debug build is made too, once a release: the Windows package from the
   same commit, with `-DCMAKE_BUILD_TYPE=Debug` after the version
   (`launcher/windows/build.sh /work b1.1-debug -DCMAKE_BUILD_TYPE=Debug`),
   which turns on the recompiler's register check and indirect-call
   records (port/CMakeLists.txt). It plays the idle run and, once they
   exist, the smoke set's runs on the PC, and its logs show no `[ABI]`
   report (docs/build-and-test-reference.md, Windows: the idle run, and The
   smoke set). It is never shipped or tested by hand.
8. **Test those packages:** the test checklist on the Ally X, in Desktop
   Mode and Game Mode, and on the Windows PC. It covers the areas the
   release's changes touch and, on both devices, a fresh install, an
   upgrade over the previous release and, once it exists, the smoke set
   (docs/build-and-test-reference.md, The smoke set). If anything fails,
   it is fixed, the fix gets its own rules check, and the release is built
   again (step 7). A critical bug found here stops the release (The gate).
   Nothing is tagged yet, so no tag ever has to move.

## Publish

Only on the maintainer's say-so.

9. **Go-ahead:** the maintainer approves publishing, with every step of the
   test checklist passed on its device and no open critical bug (The
   gate).
10. **Push and publish together:** push `main`, then publish the GitHub
    release straight away, as docs/versioning.md describes it, with the
    packages tested in step 8 and, from b2, the tools archives beside them. Publishing creates the tag on the release
    commit. The README's download links don't work until the release
    exists, so the two go back to back.
11. **Check the release:** the tag is on the release commit, each file's
    SHA-256 matches the tested one, the README's download links work, and
    the release is marked Latest.

## Close out

12. **Archive and move on:** the release's folder in `release-prep/` keeps
    its release text, packages, SHA256SUMS.txt, link, and a source backup:
    a copy of the repository to keep safe outside GitHub. The backlog is
    marked shipped, and the roadmap points at the next release.
    - **The backup** is made from a fresh `git clone --mirror` of the
      repository once the release is published, so it matches GitHub. It
      holds only what the repository holds: no game files, generated code,
      or logs.
    - **`JSRF-PC-b1.1-source.tar.gz`:** the release's files at its tag,
      with no history (`git archive --prefix=JSRF-PC-b1.1/ b1.1`). To build
      it, pass the version with `-DJSRF_VERSION=b1.1`, as
      docs/versioning.md says.
    - **`JSRF-PC-b1.1-repo.bundle`:** the whole repository: `main`,
      `upstream`, every tag, and all their history
      (`git bundle create JSRF-PC-b1.1-repo.bundle HEAD --branches --tags`).
      `git clone JSRF-PC-b1.1-repo.bundle` gives it back, ready to push to
      a new home.
    - **Checked before it's kept:** the bundle clones and passes
      `git fsck`, and the snapshot matches the tag's files. Both files go
      in SHA256SUMS.txt, and neither is uploaded to the GitHub release.
    - **Kept twice:** in the release's folder, and in `Source\<version>\`
      in the maintainer's Claude folder (docs/build-and-test-reference.md,
      Delivery package) with a SHA256SUMS.txt of its own, so a copy is on
      the maintainer's own disk. Any other source backup goes in `Source\`
      too.

## If a release is broken

A published tag is never moved. The fix goes in the next bug fix release,
and until then the published release text lists the problem as a known
issue. The exception is a hotfix, on the maintainer's say-so: it ships the
fix straight away, as a release of its own (docs/versioning.md). It carries
only its fix, so it skips steps 1 to 6, and takes the rest:

- **The gate,** with its exception for a hotfix (The gate).
- **Step 7:** the release build, from the hotfix's commit and with the
  hotfix's name as its version, and all of its checks.
- **Step 8:** its fix's test steps, and on both devices a fresh install and
  an upgrade over the release it fixes.
- **Steps 9 to 11:** the go-ahead, push and publish together, and the check
  of the release.
- **Step 12:** the archive, with the source backup.
