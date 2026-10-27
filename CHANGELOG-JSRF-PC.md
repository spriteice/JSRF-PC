# JSRF-PC changelog

What changed in each JSRF-PC version, newest first. Upstream JSRF Unleashed's
own changes are in
[its changelog](https://github.com/JuMatt/JetSetRadioFuture-Unleashed/blob/main/CHANGELOG.md).

## b1.1-hotfix (beta) — 26 October 2026

Based on upstream JSRF Unleashed 0.5.6. A hotfix to b1.1.

### Added

- **Skip intro:** Display > Skip intro leaves out the logos and the graffiti
  notice at the start, and goes straight to the title screen. Off by default.

### Fixed

- **Opening logos:** The logos and the graffiti notice at the start went by in
  a few seconds, and a press skipped them. They now play as on the Xbox: about
  6 seconds a logo, and the graffiti notice stays up for at least 7 seconds.
- **Fades to black:** The logos, the title screen, and the changes between
  scenes cut to and from black instead of fading. They fade now, as on the
  Xbox, and Developer > Engine switches > RECOMP_GL_CURTAIN=skip leaves the
  fades out.

### Changed

- **Display menu:** Fullscreen, Widescreen (16:9), and Render scale move from
  Graphics to a new Display menu, between File and Graphics.

## b1.1 (beta) — 26 October 2026

Based on upstream JSRF Unleashed 0.5.6.

### Added

- **Restart:** File > Restart starts the game again, after asking, so the
  options marked (Restart) apply without closing it.
- **Engine switches:** Developer > Engine switches turns parts of the renderer
  off, or back to how they were, to compare one way with the other.
- **Game runs ahead of the GPU:** An experimental switch. With it on, the game
  prepares its next frames while the last ones are drawn, which made crowded
  places smoother in testing. _(Developer > Experimental, off by default while
  it's still being tested)_
- **Camera log:** Developer > Logging can log what the game's camera does, for
  bug reports about the camera.
- **CPU profile:** Developer > Logging can log where the game spends its time,
  for reports about slowdowns. _(Linux only)_

### Fixed

- **Slowdown in crowds:** The game could fall to 30 fps where a sound restarts
  many times a second, as in Shibuya Terminal's crowds. Sounds now restart as
  quickly as on the Xbox.
- **Stutter in busy places:** Every draw waited for the graphics driver,
  which made busy places such as 99th Street stutter. That wait is gone.
- **Flickering in the distance:** Distant buildings and windows flickered as
  the camera moved. Depth is now precise into the distance.
- **Graffiti on walls:** Graffiti could flicker, slip behind the wall it was
  sprayed on, or disappear entirely.
- **Graffiti menu icons:** Some icons in the Graffiti menu went missing. They
  all show now. _(Windows only)_
- **Jet boost:** Boosting drew a dark hexagon on the ground around the skater,
  other dark shapes, and a ghosted picture.
- **Free camera:** The view sometimes jumped up and looked down on the player,
  and talking to someone, as in The Garage, moved it. Neither happens now.
- **Shadows:** Characters, buildings, and bridges cast shadows. They were
  never drawn before. _(JuMatt, upstream 0.5.6)_
- **Shimmering textures:** Distant surfaces shimmered as the camera moved. They
  now use the game's own smaller copies of each texture.
  _(JuMatt, upstream 0.5.5)_
- **Reflections:** Reflective glass and panels, as on Dogenzaka Hill's
  footbridge, showed dark bars and odd stripes. They now reflect.
  _(JuMatt, upstream 0.5.5)_
- **Characters' shading:** Part of a character, such as an arm or the face,
  could take another material's shading. _(JuMatt, upstream 0.5.5)_
- **Memory after a restart:** Each restart, such as for a new render scale,
  kept the last run's memory until the game closed. _(Linux only)_

### Changed

- **Anti-aliasing:** SMAA takes the place of FXAA. If you had FXAA on, turn on
  Graphics > Anti-aliasing (SMAA). The settings file's fxaa= line is no longer
  read, so it can be deleted. _(JuMatt, upstream 0.5.5)_
- **Developer > Logging:** Each row is now named by the variable it sets, as
  in RECOMP_GL_PROFILE.
- **Shader cache:** The cache saved by b1 is invalidated, because the
  reflections fix changed what each shader is made from. Its shaders are
  dropped at the first start, so the game may experience shader stutter the
  first time you play each area.

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
