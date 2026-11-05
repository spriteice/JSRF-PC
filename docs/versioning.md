# Versioning

JSRF-PC releases are betas until 1.0.

## Release names

- **b1:** The first beta.
- **b1.1, b1.2, and so on:** Bug fixes to b1.
- **b1.1-hotfix:** An urgent fix to a release, named after it with
  `-hotfix`. It is a release of its own, on its own tag: see
  [GitHub releases](#github-releases).
- **b2:** The next feature release, with b2.1 and so on for its bug fixes.
- **1.0:** The final release.

A release's git tag and the version it shows are both its name.

- **No "v" in our tags.** Upstream tags its releases `v0.5.4`; ours have no
  "v", so the two never mix. This fork's older tags (`v0.5.3a`,
  `v0.5.3a-rc`) stay as they are, and upstream's tags are never changed.
- **One tag per release,** on the commit the release is built from.
- **The release checklist before the tag:** every release goes through
  docs/release-checklist.md, which includes a check of everything since the
  last release against the project's rules. A hotfix, on the maintainer's
  say-so, skips its first six steps and takes the rest
  (docs/release-checklist.md, If a release is broken).

## The version string

- **Where it comes from:** the build takes it from the nearest release tag
  (`git describe` in `port/CMakeLists.txt`). Between releases it adds the
  number of commits since the tag and the commit, as in `b1-3-gabc1234`, and
  `-dirty` when there are uncommitted changes. A copy of the source without
  its git history passes the version with `-DJSRF_VERSION=b1`.
- **Where it shows:** the in-game menu (File > About), the first line of
  every log, the launchers' `--version`, and the Windows exe's Product
  version. It never says "beta".
- **Windows File version:** this field must be four numbers. bN.M is
  0.0.N.M and bN is 0.0.N.0: b1 is 0.0.1.0, b1.1 is 0.0.1.1, b2 is 0.0.2.0,
  and b2.1 is 0.0.2.1. 1.0 is 1.0.0.0. A hotfix keeps its release's
  numbers: b1.1-hotfix is 0.0.1.1.

## GitHub releases

- **A full release,** not a pre-release.
- **Title:** `JSRF-PC beta 1`, and `JSRF-PC beta 1.1` for b1.1. The final
  release is `JSRF-PC 1.0`.
- **"Beta"** appears only in the release title, the release text, and the
  changelog.
- **Files:** `JSRF-PC-b1-linux-x86_64.tar.gz` and
  `JSRF-PC-b1-windows-x86_64.zip`.
- **First line:** thanks JuMatt, with a link, for the original port,
  JSRF Unleashed.
- **Known issues:** the release text lists them, so testers don't report
  them again.
- **A hotfix** is a new release, on the hotfix's own tag, titled as in
  `JSRF-PC beta 1.1 hotfix`, with files named for it, as in
  `JSRF-PC-b1.1-hotfix-linux-x86_64.tar.gz`. The release it fixes stays as
  it is. (b1.1-hotfix itself, made before this rule, is an edit of b1.1's
  release.)
- **A published release's files are never swapped.** Launchers that list our
  releases, such as Quiver Launcher, check each download against the file
  first published, and refuse one that differs. A release that has to be
  made again uses its saved files, byte for byte, never rebuilt ones.

## Changelog

- **Our changelog is `CHANGELOG-JSRF-PC.md`,** newest entry first.
  `CHANGELOG.md` is upstream's and is left as it is.
- **An entry** starts `## b1 (beta) — <date>`.
- **It lists only what changed since the previous JSRF-PC version.** Until
  1 November 2026 that included upstream changes picked since then, each
  credited to its author; JSRF-PC takes none now (docs/upstream.md).
