# Playing JSRF-PC

The controls, the in-game menu, and the graphics options. Installing is in the
[README](../README.md).

## Controller

Any pad SDL knows works, over USB or Bluetooth: Xbox, PlayStation, Switch Pro,
Steam Deck, and many more. The buttons are the Xbox's, with the bumpers as Black
and White. Back opens and closes the in-game menu, so the game's own Back is
Esc on the keyboard.

## Keyboard

Keys go by position, so W A S D on a QWERTY keyboard is Z Q S D on an AZERTY
one.

- **W A S D:** Move (left stick), and the menus.
- **Arrow keys:** The camera (right stick).
- **Space or J:** A, to jump.
- **K:** B.
- **H:** X.
- **L:** Y.
- **E:** R trigger, to talk and spray.
- **Q:** L trigger.
- **R:** Black.
- **F:** White.
- **Return:** Start.
- **Esc:** Back.
- **C and V:** Left and right stick click.

## Settings keys

These work in a window or full screen, and each one is remembered.

- **F1, or Back on a controller:** The in-game menu.
- **F11 or Alt+Return:** Full screen.
- **F10:** 4:3, and back to widescreen at Match screen, your screen's own
  shape. Display > Aspect ratio has the other shapes. Widescreen widens the
  view rather than stretching it: the HUD and text keep their shape, the HUD
  sits at the screen's edges, the pause menus and the race previews are
  laid out across the screen, the garage's menus and VS mode's setup screen
  carry their colours on to its edges, the result screens' banner, time and
  choices sit at the right edge, clear of the dance, and the title screen
  fills the screen from its first frame, its opening's bars sweeping in from
  the screen's edges.
- **F9:** Resolution, 1× to 6× the Xbox's 640×480 (2× by default), and as
  much wider as the shape in widescreen (3× at 16:9 is 2560×1440). The game
  restarts to apply it, so save first.
- **F8, or R3 on a controller:** The free camera.

## Free camera

F8, or R3 (a click of the right stick), gives the right stick to the camera,
which turns around the player, and forward is wherever it looks. The L
trigger puts it back behind the player, as it does the game's camera. It
also turns the graffiti camera off, like the option in the Jet Set Radio
re-release: spraying no longer turns the camera to the wall. Talking to
someone doesn't turn it to them either.

Inversion, sensitivity, and the deadzone are in the in-game menu under
Controller, and in the settings file (see the `README.txt` in the download).
With no keyboard, as in Steam's Game Mode, R3 does what F8 does: the game
itself never uses R3.

**Auto centre**, under Controller, eases the view back behind the player
once the right stick has been left alone for a while, as the game's camera
trails them; a push of the stick takes over again at once. It is off by
default, and has four levels: Mild waits two seconds and comes back slowly,
Medium a second and a half, High a second, and Aggressive half a second,
coming back quickly.

The free camera only steers the camera of the skater you control. When the
game moves a skater by itself, as when a scene follows Beat or another
skater heads off, the game's own camera shows it. Pausing keeps the view
where you left it. Near a wall, it keeps the wall out of the view as the
game's camera does (Near walls, below).

## Near walls

The camera keeps walls out of the view, the game's own and the free camera
alike. On the Xbox, the game's camera stops at a wall it moves into. But it
tests a single point, and the picture is wider than that point, more so in
widescreen, so the edge of the picture could cut into a wall beside the camera,
or one it meets at an angle, and show what is behind it. And it tests only when
the camera itself moves through a surface, so when a half pipe's curve or a
wall came between as the player moved, the camera stayed behind it. Now, near a
wall, the view shifts a little to the side, or up, away from a wall or the
ground at its edge, and comes in towards the player only when that isn't
enough. Behind a wall or a half pipe's curve, it comes in to the near side of
it. Once a wall has been out of the way for a moment, the view eases back out,
and back across, so walls that come and go, as in a tight space, don't shake
it, and the end of a wall you skate past doesn't jerk it. Something narrower
than the picture, such as a pole or a rail between you and the camera, doesn't
move it. The view never comes closer to the player than just in front of them:
right up against a wall with the camera turned into it, you see through that
wall rather than the view turning over.

## In-game menu

The in-game menu is a [Dear ImGui](https://github.com/ocornut/imgui) menu bar
drawn over the game. F1, or Back on a controller, opens and closes it, and it
opens on the bar alone. It works with a controller, the keyboard, or the mouse.
B or Esc goes back a step, and a click outside the menus shuts them, back to
the bar. The game keeps running while it is open, but your input goes to the
menu.

- **File:** About (the version and system details), Restart, and Exit.
- **Display:** Full screen, the aspect ratio, the render scale (1× to 6×), the
  draw distance, Far buildings, Skip intro, and Music info in pause. The
  aspect ratio is 4:3 (the Xbox's own, widescreen off), 16:10, 16:9, 21:9, or
  Match screen, the shape of the display the window is on. It changes at once, and is kept as
  `aspect=` (with `widescreen=` for on or off) in the settings file. Draw
  distance is how far away the game draws the things in its world: crowds,
  spray cans, props you can knock over, traffic, the police and the race's
  checkpoints. 1x is the game's own distances, 2x to 4x twice to four times
  them, and Unlimited as far as the camera sees. It changes at once, costs
  more the further it goes, and is kept as `drawdistance=` (0 for 1x to 4 for
  Unlimited) in the settings file. Above 2x it will struggle on weak hardware.
  Far buildings draws every part of the map the game ever shows, with the
  things in it, not only the part it shows from where the camera is. A part it
  never shows stays hidden, such as the second copy of Dogenzaka Hill's
  street. Off by default; it changes at once, costs more, and is kept as
  `farbuildings=` (1 or 0) in the settings file. Skip intro leaves out the
  logos and the graffiti disclaimer the game opens with, and starts at the
  title screen's loading. Off by default, so the intro plays as on the Xbox.
  It is kept as `skipintro=` in the settings file. Music info in pause shows
  the song playing, its number, title and artist, in both pause menus, on the
  left, above Rotate map. It follows the songs of the street mixes too, and
  shows nothing for the title's and the story's own music. Off by default; it
  changes at once, and is kept as `musicinpause=` (1 or 0) in the settings
  file.
- **Graphics:** The [graphics options](#graphics-options) below.
- **Audio:** Audio effects, our own versions of the console's sound effects:
  the 3D sounds placed to your left or right as the game sets them, the
  characters' voices kept on them as they move, the reverb the game sets for
  a place, as in the sewers, and a limiter of their own for the 3D sounds,
  so a busy moment no longer turns the music and voices down. On by default;
  off, the sound is as before them. It is kept as `audioeffects=` in the
  settings file, and changing it restarts the game. [The audio
  effects](technical/audio-effects.md) says how they work.
- **Controller:** The free camera, Auto centre, and the free camera's
  inversion, sensitivity, and deadzone.
- **Developer:** Settings for testing and bug reports.
  - **Log to file:** Stops or starts writing to `last-run.log` at once.
  - **Logging:** The engine's diagnostic logs, for bug reports.
    RECOMP_HEAP_GUARD logs the game's mistakes with the memory it releases,
    and where in the game's code each one is: a block released twice, used
    after its release, or released through the wrong call. While it is on,
    the memory the game releases is kept back, out of reach, until the game
    needs that space again; where the game's memory goes doesn't change.
  - **Engine switches:** Turn parts of the engine off, or change how they
    work, to compare.
  - **Performance:** What the engine does for speed, each on by default
    and ticked; untick one to compare. Use P cores (Linux) runs the game's
    two busiest threads on the processor's P cores, its fast ones, where it
    has two kinds (as the Ally X does). It leaves the clocks, the power
    limit and the power profile as they are, and the log says what it
    found. GPU queue lets the game run ahead of the graphics; unticked, the
    game waits for them after each batch of work, as before b1.2. Display
    pacing (Linux) times the game's frames by the display's refreshes;
    unticked, by the game's own clock, as before b1.2. Sleep while waiting
    for the GPU lets the game sleep while it waits for the graphics;
    unticked, it spins, as before b1.2. Compressed textures direct hands
    the game's compressed textures to the graphics driver as they are;
    unticked, they are decoded first, as before b1.2.
  - **Experimental:** Changes to how the game runs, to try out, when there
    are any.
  - **Purge shaders:** Restarts the game and deletes the shader cache as it
    starts.

  Logging's and Engine switches' rows are named by the environment variable
  each one sets, in alphabetical order. Those two, Performance and
  Experimental are kept in `developer.cfg` beside the settings file, and
  apply from the next start.

Everything is remembered. Restart, Exit, a new render scale, the shader cache,
Audio effects, and Purge shaders ask first, as anything since your last save
is lost: all but Exit restart the game. The other rows marked (Restart) apply
from the next start, which File > Restart gives.

## Graphics options

These are in the in-game menu, under Graphics.

- **Shader cache:** Keeps the shaders the game builds, and builds them again
  when it starts, so play doesn't pause for them. On by default. Changing it
  restarts the game.
- **Anisotropic filtering:** 2× to 16×. It keeps textures sharp where they are
  seen at an angle, such as streets running into the distance. Off by default.
- **Anti-aliasing:** Smooths jagged edges. SMAA costs little and smooths the
  finished picture, the HUD included. MSAA (2×, 4×, or 8×) gives the cleanest
  edges but asks the most of the graphics card. Use either, or both. Off by
  default.
