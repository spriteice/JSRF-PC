# JSRF-PC changelog

What changed in each JSRF-PC version, newest first. Each version names the
upstream JSRF Unleashed version it is based on. Upstream's own changes are in
[CHANGELOG.md](CHANGELOG.md).

## b1 (beta) — 23 October 2026

Based on upstream JSRF Unleashed 0.5.4.

### Added

- **Developer menu:** In the in-game menu. Log to file turns the log on or off
  at once, Logging switches on the engine's diagnostic logs for bug reports, and
  Purge shaders restarts the game with an empty shader cache.

### Fixed

- **Shader cache after an update:** Shaders saved by an older version were
  never used again, so after an update the game paused to build them again
  during gameplay. The cache now keeps what each shader was made from, and from
  b1 on rebuilds them for the new version at start-up.

### Changed

- **New name:** With this fork focusing on PC builds, it is now renamed JSRF-PC.
- **Menu behaviour:** Adjusted to be more consistent, with the menus tidied up
  and put in a clearer order. A click outside the in-game menus now closes
  them, back to the bar, instead of opening the last menu again.
- **Version numbers:** Distinct from upstream, beta until 1.0.
