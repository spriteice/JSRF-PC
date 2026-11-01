# Upstream

JSRF-PC is built on
[JSRF Unleashed](https://github.com/JuMatt/JetSetRadioFuture-Unleashed), by
JuMatt, which is built on the
[xboxrecomp](https://github.com/sp00nznet/xboxrecomp) toolkit by Ned Heller
(sp00nznet). Since 1 November 2026 it's a standalone repository: it takes no
more merges or picks from upstream, and upstream's code is changed wherever
that's the right place, as ours is (docs/project-rules.md, Where code goes 4).

## Branches

- **`main`:** JSRF-PC.
- **`upstream`:** JuMatt's history up to 0.5.9 (`414b103`), kept unchanged as
  a record. It isn't merged into `main` or moved forward.

## Credit

- **The README** credits JuMatt at the top, and both JuMatt and xboxrecomp
  in its credits.
- **The changelog** credits each upstream change JSRF-PC took to its author,
  in the release that took it (CHANGELOG-JSRF-PC.md). `CHANGELOG.md` is
  upstream's own, kept as it is.
- **Docs** describe upstream's work respectfully, never as criticism.

## Why a change was made

Why JSRF-PC changed a piece of upstream's code is in the commits that
changed it. `git log -L` on a function, or `git log` on a file, lists them,
and docs/project-rules.md (Where code goes, 7) asks for that before a fix.
