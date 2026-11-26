# JSRF-PC changelog

What changed in each JSRF-PC version, newest first. Upstream JSRF Unleashed's
own changes are in
[its changelog](https://github.com/JuMatt/JetSetRadioFuture-Unleashed/blob/main/CHANGELOG.md).

## b2.2-hotfix (beta) — 26 November 2026

A hotfix to b2.2.

### Added

- **Random graffiti (experimental):** Developer > Experimental > Random
  graffiti sprays designs drawn at random from the ones your save has
  unlocked, up to 13 of each size and a different one at each spot, in place
  of the five chosen in the garage. They are drawn again each time an area
  loads, and at each graffiti soul you pick up. Off by default, and it needs a
  restart.
- **Sun flare fix (experimental):** Developer > Experimental > Sun flare fix
  hides the sun's glare and rays behind walls and slopes they showed over,
  where the game's own check misses them. The glare goes about a second after
  the sun is hidden, and comes back a moment after it clears. Off by default,
  and it needs a restart; Developer > Logging gains JSRF_SUN_LOG, which logs
  the check.

### Fixed

- **Button prompts in the tutorial:** In every Input > Button prompts style
  but Default, Corn's and Gum's lines and the tutorial's text on the screen
  showed the Xbox's buttons. They now show the style's pictures, as every talk
  and mission message that names a control does: a button's letter, a trigger,
  and the left thumbstick, each coming in as the talk types it out.
- **Skipping Beat's runs:** With Skip cutscenes on, Beat's three runs in
  Dogenzaka Hill, where the camera follows him to his place, couldn't be
  skipped. Every preview where the camera follows a rival now ends at a second
  press of A, the way it ends when it plays out, so a scene after the rival
  arrives still plays, and two presses skip it too.
- **Pink lines in 99th Street's opening:** With Far buildings on, thin pink
  lines crossed the close-up of the dragon in 99th Street's opening scene. Far
  buildings now draws nothing extra while that scene plays, and the street
  keeps its far buildings the rest of the time.
- **Scenery in the air on Kibogaoka Hill:** With Far buildings at 3× or All, a
  piece of the bowl's west side hung in the air, seen from the east of the
  map. Far buildings now leaves three pieces of that side out from there.
- **The speed trail with See through fix:** With See through fix on, b2.2 drew
  the trail behind your skater pale and see-through. It is drawn as on the
  Xbox again: shadowed where your skater is, and solid enough to hide the
  dash's burst behind it, while your skater, faded when the camera is close,
  still shows through it. The crowd, the spray paint and its arrows while you
  paint a wall, the police's mesh barricade, and the edges of cars are drawn
  as on the Xbox too; the crowd still shows through glass.
- **A crash after a skipped scene:** With Skip cutscenes on, a scene that
  ended just as "Press A again to skip" first showed could crash the game a
  moment later, most likely in the garage's first scene after the tutorial. It
  was found in the code before anyone met it, and is fixed.
- **Mid-air starts from Developer > Missions:** A rival's preview started from
  Developer > Missions dropped you in mid-air in the middle of the map once it
  ended. You now start where the preview starts, by the rival, or else at the
  level's start.

### Changed

- **Modern preset:** File > Presets > Modern now ticks Half pipe cam, so a
  jump in a half pipe plays the game's own shot. With it unticked, the free
  camera can go through a half pipe's wall, a known issue. If you chose Modern
  in b2.2, it stays unticked until you tick Input > Behaviour > Camera > Half
  pipe cam, or choose Modern again.

## b2.2 (beta) — 23 November 2026

### Added

- **Controller and keyboard layouts:** Input > Controller layout and Input >
  Keyboard layout change which button, key, or mouse button presses each of
  the game's controls, with two keys for each control on the keyboard.
- **Button prompts:** Input > Button prompts > Style draws the game's button
  icons as your controller's, in Xbox, PlayStation, or Nintendo style, or as
  keys, following your layouts. Auto follows the controller or keyboard you
  last used; Default, which a new install starts with, keeps the game's own.
- **Mouse camera:** The mouse turns the free camera, as the right stick does,
  with its own sensitivity in Input > Free camera.
- **Camera shots:** Input > Behaviour > Camera has a tick for each of the
  game's own camera shots, Graffiti cam, Half pipe cam, and Loop cam, with the
  free camera on or off. After a shot, the free camera glides back to where
  you had it.
- **No skater fade:** Input > Behaviour > Camera > No skater fade keeps the
  camera far enough from your skater to show them whole, seeing through a
  wall that is nearer, where they would otherwise fade out. Off by default.
- **Keyboard moves:** Input > Behaviour > Keyboard moves sets whether W A S D
  move from the way the camera faces, the default, or from the way the skater
  faces.
- **Stick shape:** Input > Behaviour > Stick shape sets how far the sticks
  reach on a diagonal: Square, the default, as the Xbox controller's do, or
  Round, as your controller gives them.
- **Skip cutscenes:** Gameplay > Skips > Skip cutscenes ends a story scene, or
  a rival's preview, at a second press of A while "Press A again to skip"
  shows. Off by default. Thanks to Miggas and mstan for the idea.
- **Graffiti soul indicator:** Gameplay > Graffiti souls > Visual indicator
  shows the soul on the HUD while one of an area's soul challenges is under
  way and on track, and turns it red when the challenge fails. Off by
  default.
- **Mute:** Audio > Mute silences the game's sound at once.
- **Start minimized:** `--minimized` starts the game minimized, in a window.
- **Photo mode:** Developer > Missions > Photo mode holds the world still,
  hides the HUD, and gives you a camera that flies anywhere.
- **Missions:** Developer > Missions restarts the game at any story mission,
  as any of its characters, with save files kept separate from your own. It
  is not intended for playthroughs, as some missions can start bugged.
- **Input playback:** Developer > Playback records what you press and plays
  it back, for testing. `--play` plays a recording and closes the game at its
  end.
- **More logs for bug reports:** Developer > Logging gains JSRF_EVT_LOG,
  JSRF_FLOW_LOG, JSRF_TALK_LOG, and RECOMP_INPUT_LATENCY, and Developer > Far
  buildings test finds which part of a map Far buildings draws out of place.

### Fixed

- **Controller vibration:** Vibration played on the wrong motor and stopped
  after a second. It now plays on the correct motors, for as long as the game
  asks.
- **The ending's skater:** On AMD processors, the skater in the ending went
  off course near its end. The game takes approximate reciprocals and
  reciprocal square roots with two processor instructions, rcpss and rsqrtss,
  which AMD's processors answer slightly differently from the Xbox's Intel
  one, so the ending's recorded run drifted from the Xbox's. JSRF-PC now works
  out the Xbox's answers itself, so they skate it as on the Xbox, on every
  processor.
- **Garage Graffiti menu icons:** On AMD processors, the left and right icons
  in the garage's Graffiti menu stopped working when selected again. The same
  two instructions were the cause: AMD's answers put the icons at the same
  depth as the squares behind them. They now work, by the same fix as above.
- **Music info in the final level:** The pause menu showed no song in the
  final level. It now names it.
- **Flag chases in widescreen:** The word FLAG in a flag chase stayed where
  4:3 puts it, away from the edge. It now sits at the screen's left edge.
- **Graffiti editor in widescreen:** The bar of static across the graffiti
  editor was shorter than the help line scrolling along it. It now crosses
  the whole screen.
- **Free camera off the map:** With the free camera on, your skater or the
  whole level could vanish, as when falling from a tower. Both now stay
  drawn.
- **Floating buildings with Far buildings:** Buildings, cars, and scenery
  could hang in the sky or stand out of place on Dogenzaka Hill, Hikage
  Street, Kibogaoka Hill, and Chuo Street. Far buildings now leaves them out
  where they don't belong.
- **Patches of light with Far shadows:** With Far shadows at All, pale
  patches showed along Chuo Street's long halfpipe and on a walkway on
  Kibogaoka Hill. They are gone.

### Changed

- **New default settings:** A new install now starts at File > Presets >
  Performance: Draw distance, Far buildings, and Far shadows at 1×, no
  anti-aliasing or anisotropic filtering, Upscaling's Textures and Graffiti
  off, and Default cam fix off. Settings you chose stay as they are, and any
  you never changed take the new defaults.
- **Presets:** File > Presets is now Xbox, Modern, Performance, Balanced, and
  Quality, with Reset below them, which puts every setting back to how a new
  install has it. Performance, Balanced, and Quality change only visuals, and
  apply with no restart.
- **Resolution:** Display > Resolution replaces Render scale: Default (the
  Xbox's 640×480), 720p, 900p, 1080p, 1440p, 1600p, 2160p, or Match screen,
  your screen's own and the default. It changes at once, with no restart,
  from the menu or F9, and your old render scale carries over.
- **Keyboard defaults:** The keys have new defaults: Left Shift for B, C for
  X, V for Y, T for White, Left and Right Ctrl for the stick clicks, and U, H,
  J, and K for the d-pad. The mouse's left, middle, and right buttons are A,
  B, and the R trigger.
- **Menus:** Controller is now Input, with Controller layout, Keyboard
  layout, Button prompts, Free camera, and Behaviour, where Default cam fix
  now is. Skips moved from Display to a new Gameplay menu. Your settings for
  them stay as they were.
- **See through fix:** Faded characters, such as your skater close to the
  camera, now let what is behind them show through too, tinted by them.
- **Free camera deadzone:** 10% by default, from 15%. A deadzone you set
  stays as it is.

## b2.1-hotfix (beta) — 14 November 2026

A hotfix to b2.1.

### Fixed

- **Buildings in Dogenzaka Hill's sky:** With Far buildings on, the top of
  Dogenzaka Hill showed in the sky over its bottom. At 3×, the new default,
  it no longer does.
- **Patches on characters with Textures upscaling:** With Graphics >
  Upscaling > Textures on, the characters' light and shadow could break into
  patches, triangle by triangle. They now keep the game's own tones, and the
  rest of their textures are still upscaled.
- **Chuo Street's halfpipe:** The bright patches along it with Far shadows on
  are gone at 3×, the new default.

### Changed

- **Presets:** Each preset in File > Presets now sets every setting but
  Developer's. It also puts the game full screen, Upscaling's Cleanup as a
  new install has it, and the free camera's own settings back to their
  defaults.
- **Far buildings and Far shadows:** Each is now a choice of how far ahead to
  draw, in Display > Building distance: Off, 1×, 2×, 3× (the default), or
  All, which is b2.1's Far buildings. Far shadows never reaches further than
  Far buildings, and your b2.1 setting carries over, on as 3×.
- **Draw copies:** Developer > Engine switches > JSRF_DRAW_COPIES, which did
  nothing, is gone.

## b2.1 (beta) — 14 November 2026

### Added

- **Upscaling:** Graphics > Upscaling draws the menus and HUD, the 3D world's
  textures, and the graffiti tags from sharper copies made with Super-xBR, at
  2×, 4× (the default), or 8×, tuned by Sharpness, Edge strength, and Cleanup.
  The copies are made on the fly, so minor hitching may occur at higher scales.
- **See through fix:** Characters and moving objects now show through glass
  and water, tinted by them, where the Xbox hides them. It is in Display >
  Enhancements, on by default.
- **HUD scale:** Display > HUD draws the HUD at 0.5× to 1.5× its size, each
  piece keeping its place at the screen's edge, and Disable HUD hides it.
- **Presets:** File > Presets sets many settings at once: Xbox mode, the game
  as the Xbox runs it; JSRF-PC default, the settings a new install starts
  with; or Max quality, with the graphics at their most.
- **Far shadows:** Display > Building distance > Far shadows draws the
  shadows of the buildings Far buildings adds, so they don't pop in as you
  come near, at the cost of minor glitches. On by default.
- **Default cam fix:** Controller > Default cam fix turns b2's fix for the
  game's camera clipping through walls on or off. On by default; off, the
  camera is as on the Xbox.
- **Menu hints:** A line along the bottom of the in-game menu says what the
  row you're on does, beside the buttons to press.

### Fixed

- **Graffiti camera:** With the free camera off, tagging a parked bus, a
  truck, or a column could turn the camera first-person. The game frames the
  shot itself again, as on the Xbox.
- **Garage menu arrows:** In widescreen, the yellow arrows along the bottom of
  the garage's System and Graffiti menus could overlap. They now stay in
  line.
- **Black screen before the Sega logo:** On a busy PC, the game could stop for
  good at a black screen before the Sega logo. _(Windows)_
- **First-start progress bar:** The bar could jump back and forth while the
  engine was built. It now only moves forward, at your PC's own pace.
- **Log size with Far buildings:** With Far buildings on, `last-run.log` could
  grow by over 100 MB in one street. It now stays small.

### Changed

- **Menu look:** The in-game menu and the first start's window use the
  original Xbox's greens, with rounder, roomier rows. The menu dims the game
  behind it, and fades in and out.
- **Faster first start:** Building the engine at the first start is about
  twice as fast on a desktop PC, about 2 minutes, and takes about 2.5 minutes
  on a ROG Xbox Ally X.
- **Menu rows moved:** Skip intro is now in Display > Skips, Music info in
  pause in Display > Enhancements, and Far buildings in Display > Building
  distance. Your settings for them stay as they were.

## b2 (beta) — 9 November 2026

### Added

- **Render scale up to 6×:** Display > Render scale and F9 now go up to 6×,
  3840×2880, for 4K screens. When the picture is drawn at more than twice the
  size it is shown at, it is now shrunk in steps, so none of its detail is
  lost.
- **True widescreen:** Display > Aspect ratio offers 4:3, 16:10, 16:9, 21:9,
  and Match screen, your screen's own shape. The picture is drawn as wide as
  the shape, so neither axis is stretched any more with widescreen enabled.
  F10 switches between 4:3 and Match screen.
- **Widescreen HUD and menus:** In widescreen, the HUD keeps its shape and
  sits at the screen's edges. The pause menus, the race previews, the garage's
  menus, the loading screens, the title, the GG notebook, the result screens,
  and VS mode's setup screen are laid out across the screen.
- **Draw distance:** Display > Draw distance draws crowds, spray cans, props,
  traffic, the police, and the race's checkpoints further away: 1× is the
  game's own, then 2× to 4×, or Unlimited. **Above 2× can introduce severe
  slowdowns on weaker systems.**
- **Far buildings:** Display > Far buildings draws every part of the map the
  game ever shows, so the city's buildings don't pop in as you skate.
- **Music info in pause:** Display > Music info in pause shows the song
  playing, its number, title, and artist, in both pause menus.

### Fixed

- **Jet boost in widescreen:** The boost's haze now covers the whole screen,
  with no black bars at the sides.
- **Default camera clipping in geometry:** The game's default camera no
  longer shows what is behind a wall beside it, or behind a half pipe's curve,
  which happened most in widescreen. Near a wall, the view now moves a little
  to the side or in. These fixes have come from this repo's free camera code.
- **Rare crashes:** The engine could misread the game's own timer and
  interrupt objects when the game cancelled or disconnected one, which could
  crash it or change the game's memory. It reads them as the Xbox does now.
  Two checks left from early development, which ended the game, now write a
  line to the log and let it carry on.

### Changed

- **The first start builds the engine:** The release package no longer
  includes the engine, only the launcher. The first time you run it, the
  launcher builds the engine from your own copy of the game. It takes 2-5
  minutes depending on your system and whether you downloaded the tools
  beforehand. You also have the option to let the launcher download the tools
  automatically for you. Then it offers to delete the tools and the code made
  from your game, keeping only the necessary files to run the game.
- **Default settings:** A new install, and any setting you never changed, now
  starts full screen at 3× and your screen's own shape, with 2× draw
  distance, Far buildings, Music info in pause, SMAA, 8× anisotropic
  filtering, and a 15% deadzone for the free camera. A setting you changed
  stays as you set it.
- **Linux launcher:** `jsrf-pc` is now a program, as the Windows launcher is,
  rather than a script. It works as before. _(Linux)_
- **Licence:** JSRF-PC's own code is now under the GNU General Public
  License, version 2 or later. The code from JSRF Unleashed and xboxrecomp
  stays under the MIT licence, and so do the releases up to b1.4.

## b1.4 (beta) — 4 November 2026

### Added

- **Heap guard:** Developer > Logging > RECOMP_HEAP_GUARD logs the game's
  mistakes with the memory it releases, and where in the game's code each one
  is: a block released twice, used after its release, or released through the
  wrong call. It is there to find hangs and crashes like the one on Rokkaku-dai
  Heights.

### Fixed

- **Freeze at the first start:** The first start after installing or
  updating could freeze on its first frame, before the title. Two parts of the
  game could run at the same instant, where the Xbox runs one at a time, and
  the signal for the first frame was lost. They take turns now. _(Windows)_
- **Hang entering Rokkaku-dai Heights:** Rarely, entering Rokkaku-dai Heights
  from The Garage didn't finish loading. The game released a character's file
  twice, and the second time freed the next mission's file. It is released
  once now.
- **Graffiti loading:** The game made the same mistake with its graffiti
  designs, as it built them after a save loaded, which could free a file still
  in use.
- **Lost frame signals:** Under heavy load, the signal for a new frame could
  be lost, and the frame shown late. The graphics chip's signals now clear as
  the Xbox's do, so none is lost.

### Changed

- **Launcher:** The launcher now only handles the game's folders and your
  settings, with no engine switches. The engine's own defaults are the game's
  working setup.
- **Engine switches:** Developer > Engine switches loses the seven rows that
  put a fix back the way it was before: the fades to black, the shadows,
  depth, and three for textures. It keeps the rows that turn part of the
  engine off or change how it works. A developer.cfg with the old rows ticked
  still loads, without them. 27 more switches like them, which only an
  environment variable could set, are gone too.

## b1.3 (beta) — 1 November 2026

### Added

- **Audio effects:** A new Audio menu, with Audio effects: our own versions of
  the Xbox's sound effects, 3D sound and reverb. Sounds come from where they are
  around you, and places such as the sewers echo. They may cause crashes: if the
  game crashes, compare its behaviour with Audio effects off. Thanks to
  AdriM1985 for pointing out that they were missing.
- **R3 for the free camera:** Clicking the right stick turns the free camera
  on and off.
- **Auto centre:** Controller > Auto centre eases the free camera back behind
  the player once the right stick is let go: Mild, Medium, High, or
  Aggressive. Off by default.
- **Sound stream logging:** Developer > Logging > JSRF_ADX_ERR_LOG logs when
  one of the game's sound streams runs dry and how it recovers, to help find
  "disc can't be read" errors.

### Fixed

- **"Disc can't be read" on Rokkaku-dai Heights:** The game could stop with
  its disc error while a looping sound started over. Its threads now wait
  their turn around the sound library's lock, as on the Xbox. Thanks to
  AdriM1985 for the report.
- **Scenes and results cut short:** "Cleared" scenes, as after copying Combo's
  line, ended early, and Beat's "You Win" screen after his race was cut short.
  Both play in full now. Thanks to AdriM1985 for the report.
- **Freezes between shots:** Where the Xbox cuts to black for a moment between a
  scene's shots, as in Professor K's introduction, the picture froze instead,
  which looked like a hang. It cuts to black now, the same as the original
  hardware. Thanks to AdriM1985 for the report.
- **Hang or crash after Restart:** After File > Restart, the game could hang
  on its loading screen or crash as it started, as two of its threads could
  be given the same memory.
- **Occasional late frames:** Now and then the game missed the signal for a
  new frame, and that frame was shown late. It no longer misses it.
- **Clicks and stuck sounds:** A sound could be mixed while the game was
  changing it, which could cause clicks, and sounds that stuck or went
  missing. The mix now waits for the game's change to finish, as xemu does.
- **Free camera refinement:** It no longer takes over scenes that follow another
  skater, and pausing keeps the view where you left it. Near walls, half pipes,
  and bumps, the view no longer cuts into them, snaps onto the player, or turns
  over, and it comes back out smoothly.

### Changed

- **Developer menus in order:** Developer > Logging and Engine switches list
  their rows in alphabetical order. What you ticked stays ticked.

## b1.2 (beta) — 29 October 2026

### Added

- **Performance menu:** Developer > Performance has what the engine does for
  speed, each on and ticked by default, to untick and compare: GPU queue, Sleep
  while waiting for the GPU, Compressed textures direct, and on Linux, Use P
  cores and Display pacing.
- **Use P cores:** On a processor with two kinds of cores, as the ROG Xbox Ally
  X has, the game's two busiest threads run on the fast ones. It leaves the
  clocks and the power profile as they are. _(Linux)_
- **Sound logging:** Developer > Logging > RECOMP_APU_CHANNELS logs when one
  side of the sound goes quiet, with what each voice was playing at the time,
  to help find the bug where sound comes from one side only.
- **xemu's sound:** Developer > Engine switches > RECOMP_APU_MIX=xemu plays the
  sound as xemu does, to compare when testing. The game's own sound stays the
  default.

### Fixed

- **Uneven frame pacing:** The game's frames drifted against the display's
  refreshes, so sometimes one showed twice and the next not at all, which felt
  choppy and sluggish in Game Mode. Frames are now timed by the display's
  refreshes. _(Linux)_
- **Stutter entering a level:** Entering a level held one frame for up to a
  fifth of a second while its textures were made. Textures are now made several
  times faster, and entering a level takes under 30 ms.
- **Slowdown during certain effects:** The spray can's effect and the sewer's
  splashing water made new shaders as they faded, which slowed the game down.
  They make none now.
- **Busy menus:** In the menus and The Garage, the game spun while it waited
  for the graphics, using up to two thirds of a CPU core. It now sleeps while
  it waits, and the menus respond better.
- **Missing scenery on Kibogaoka Hill and in the Fortified Residential Zone:**
  Rails, rafters, walkways, and stacked houses weren't drawn. _(JuMatt,
  upstream 0.5.7)_

### Changed

- **GPU queue on:** The game prepares its next frames while the last ones are
  drawn, as b1.1's experiment did, now by default. It moves from Developer >
  Experimental to Developer > Performance > GPU queue. If you turned the
  experiment off again in b1.1, the queue stays off: tick GPU queue to turn it
  on.
- **CPU profile:** Developer > Logging's RECOMP_CPU_PROFILE now works on
  Windows too.
- **GPU profile:** Developer > Logging's RECOMP_GL_PROFILE also logs the
  graphics card's own time for each frame.
- **Timeline log:** A diagnostic log that was always on is now off, which
  nearly halves a run's log. Developer > Logging > RECOMP_GL_TIMELINE turns it
  on.
- **Shader cache:** The shaders changed, so the first start after updating
  shows "Building shaders" for longer than usual, once. The cache is kept.

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

Based on upstream JSRF Unleashed 0.5.4. It also lists the changes first
released as 0.5.3a and its release candidate, on 22 October 2026, before the
beta numbering.

### Added

- **Developer menu:** In the in-game menu. Log to file turns the log on or off
  at once, Logging switches on the engine's diagnostic logs for bug reports, and
  Purge shaders restarts the game with an empty shader cache.
- **Portable mode:** A folder named `portable` next to the launcher keeps the
  settings, saves, logs, and cache inside it.
- **In-game menu:** F1, or Back on a controller. On Windows it replaces the
  Video menu bar above the window; F9 to F11 work as before.
- **Free camera:** F8, or `freecam=1` in the settings file, gives the right
  stick to the camera, which turns around the player. The L trigger puts it
  back behind the player. Sensitivity, inversion, and deadzone are settings.
- **Anti-aliasing:** MSAA (2×, 4×, or 8×, up to what your graphics card
  supports) and FXAA, in the in-game menu's Graphics menu. They can be on
  together, a change takes effect straight away, and both are off by default.
  MSAA is costly; FXAA is cheap but softens the HUD a little.
- **Anisotropic filtering:** 2× to 16×, in the same menu, keeps textures sharp
  on surfaces seen at an angle.
- **Full screen is remembered:** F11, Alt+Return, or the menu's Full screen
  setting is kept for the next start, like widescreen and the scale.

### Fixed

- **Shader cache after an update:** Shaders saved by an older version were
  never used again, so after an update the game paused to build them again
  during gameplay. The cache now keeps what each shader was made from, and from
  b1 on rebuilds them for the new version at start-up.
- **Builds from source boot, save, and play:** Built with the repository's own
  tools alone, the game had a black screen at boot, a hang at the save menu,
  audio stutter, and two missing functions. Upstream's own downloads didn't
  have these problems. The tools also find one more of the game's methods than
  before.
- **Frame pacing:** The GPU thread's short waits last as long as asked,
  instead of up to 15 ms longer. _(Windows)_
- **Kernel calls:** Two kernel calls the game makes are now handled
  (NtProtectVirtualMemory and KeSetDisableBoostThread), and two others no
  longer leave the stack off by 4 bytes.
- **Threads:** The game's threads hand over to each other straight away,
  instead of waiting a millisecond or more each time (up to 15.6 ms on
  Windows).
- **A clock:** A clock the audio and graphics code read no longer overflows,
  which it did after 9 seconds of computer uptime on Linux and 15 minutes on
  Windows.
- **Freeing memory:** Freeing game memory no longer fails thousands of times a
  minute, each failure logged and reported to the game. _(Windows)_
- **Reading files:** Two threads reading the same file can no longer get each
  other's data. _(Linux)_
- **The graffiti cache:** Its second release of the same memory on every scene
  change now fails quietly, as on the console.

### Changed

- **New name:** With this fork focusing on PC builds, it is now renamed JSRF-PC.
- **Menu behaviour:** Adjusted to be more consistent, with the menus tidied up
  and put in a clearer order. A click outside the in-game menus now closes
  them, back to the bar, instead of opening the last menu again.
- **Version numbers:** Distinct from upstream, beta until 1.0.
- **Shader cache:** Every cached shader is now compiled on each launch, behind
  a progress bar, and Graphics > Shader Cache turns it on or off (on by
  default). Cache files left by other drivers or versions are removed.
- **Release builds:** Smaller and faster, with quieter logs: debugging checks
  at every function call are now only in debug builds.
