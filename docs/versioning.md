# Versioning

JSRF-PC releases are betas until 1.0.0. Every release says which upstream
JSRF Unleashed version it is based on, for example "b1, based on upstream
0.5.4".

## Release names

| Release | What it is | Git tag | Version shown |
|---|---|---|---|
| b1 | The first beta | `b1` | b1 |
| b1.1, b1.2, ... | Bug fixes to b1 | `b1.1`, `b1.2` | b1.1, b1.2 |
| b2 | The next feature release | `b2` | b2 |
| b2.1, ... | Bug fixes to b2 | `b2.1` | b2.1 |
| 1.0.0 | The final release | `1.0.0` | 1.0.0 |

- **No "v" in our tags.** Upstream tags its releases `v0.5.4`; ours have no
  "v", so the two never mix. This fork's older tags (`v0.5.3a`,
  `v0.5.3a-rc`) stay as they are, and upstream's tags are never changed.
- **One tag per release,** on the commit the release is built from.

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
  0.0.N.M and bN is 0.0.N.0: b1 is 0.0.1.0, b1.1 is 0.0.1.1, b2 is 0.0.2.0
  and b2.1 is 0.0.2.1. 1.0.0 is 1.0.0.0.

## GitHub releases

- **A full release,** not a pre-release.
- **Title:** `JSRF-PC b1 (beta)`. The final release drops "(beta)".
- **"Beta"** appears only in the release text and the changelog.
- **Files:** `JSRF-PC-b1-linux-x86_64.tar.gz` and
  `JSRF-PC-b1-windows-x86_64.zip`.
- **First line:** the upstream version it is based on: "JSRF-PC b1 (beta)
  is based on upstream JSRF Unleashed 0.5.4."

## Changelog

- **Our changelog is `CHANGELOG-JSRF-PC.md`,** newest entry first.
  `CHANGELOG.md` is upstream's and is left as it is.
- **An entry** starts `## b1 (beta) — <date>`, then "Based on upstream JSRF
  Unleashed 0.5.4."
- **It lists only what changed since the previous JSRF-PC version.** That
  includes upstream changes merged since then, each credited to its author.
