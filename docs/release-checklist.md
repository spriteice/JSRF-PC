# Release checklist

Every JSRF-PC release goes through these steps, in this order. Steps 1 to 6
settle what ships, step 8 is the only one on the test devices, and nothing is
pushed or published before step 9. docs/versioning.md says how a release is
named, and docs/build-and-test-reference.md how it is built and tested.

The maintainer's shared folder, outside the repository, holds known-bugs.md
and `release-prep/`: each release's backlog, the release notes template with
the writing rules, and the files of past releases. Each backlog's last row,
Release, says which step the release has reached.

## Freeze the scope

1. **Backlog closed:** every item in the release's backlog is done, or moved
   to a later one with the maintainer's OK.
2. **Upstream decided:** if upstream has released since the last merge, the
   maintainer decides whether to merge it now, as docs/upstream.md says, or
   leave it for the next release. A merge is the largest change a release
   takes, so it comes before the review and the tests.
3. **Main ready:** the latest `origin/main`, with no unpushed commits from
   elsewhere and a clean tree. Experiments and developer switches are off by
   default, and nothing left from debugging is still on.
4. **Known bugs current:** known-bugs.md marks what this release fixed and
   adds anything new. It becomes the release text's known issues.

## Review

5. **Rules review:** everything since the last release is checked against
   the project's rules. It looks for game files, generated code, or logs in
   the repo, session links in commit messages, band-aid fixes, address
   lists, sleeps, docs that break the writing rules, and upstream files
   changed with no entry in docs/upstream.md. It also searches the code in
   `port/` for the game function overrides and hooks added since the last
   release, in our own files too (which have no entry in docs/upstream.md),
   and flags any that look alike (the same kind of instruction, or the same
   symptom): those usually mean one recompiler or runtime bug, to fix there
   instead.

   Code findings are fixed now, each in its own commit, and doc findings in
   step 6. The review comes before the docs, as its fixes can change what
   the changelog says, and before the build, so that its fixes are tested.

## Docs

6. **README and docs:** brought up to date, and checked against the writing
   rules.
   - **README.md:** the features, the download links and latest version, the
     status and known issues, and the credits.
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

7. **Release build:** from the release commit, as
   docs/build-and-test-reference.md says, with the release's version given
   to both builds (`-DJSRF_VERSION=b1.1`, and the Windows package.sh's
   version argument). The generated code is made fresh (0 failed), every
   test passes, and both packages pass every compatibility check. The
   version reads as the release's name everywhere docs/versioning.md lists,
   with its Windows File version, and SHA256SUMS.txt covers both packages.
8. **Test those packages:** the test checklist on the Ally X, in Desktop
   Mode and Game Mode, and on the Windows PC. It covers the release's
   changes, the regression spots, a fresh start, and an upgrade over the
   previous release. If anything fails, it is fixed, the fix gets its own
   rules check, and the release is built again (step 7). Nothing is tagged
   yet, so no tag ever has to move.

## Publish

Only on the maintainer's say-so.

9. **Go-ahead:** the maintainer approves publishing, with every step of the
   test checklist passed on its device.
10. **Push and publish together:** push `main`, then publish the GitHub
    release straight away, as docs/versioning.md describes it, with the
    packages tested in step 8. Publishing creates the tag on the release
    commit. The README's download links don't work until the release
    exists, so the two go back to back.
11. **Check the release:** the tag is on the release commit, each file's
    SHA-256 matches the tested one, the README's download links work, and
    the release is marked Latest.

## Close out

12. **Archive and move on:** the release's folder in `release-prep/` keeps
    its release text, packages, SHA256SUMS.txt, and link. The backlog is
    marked shipped, and the roadmap points at the next release.

## If a release is broken

A published tag is never moved. The fix goes in the next bug fix release,
and until then the published release text lists the problem as a known
issue. The exception is a hotfix, on the maintainer's say-so: it ships the
fix straight away, as docs/versioning.md describes.
