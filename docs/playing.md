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
- **F10:** Widescreen 16:9. It widens the view rather than stretching it: the
  HUD and text keep their shape, and the title screen stays 4:3.
- **F9:** Resolution, 1× to 4× the Xbox's 640×480 (2× by default). The game
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
where you left it. Near a wall, the view comes in towards the player to keep
out of it, and eases back out once the wall is past.

## In-game menu

The in-game menu is a [Dear ImGui](https://github.com/ocornut/imgui) menu bar
drawn over the game. F1, or Back on a controller, opens and closes it, and it
opens on the bar alone. It works with a controller, the keyboard, or the mouse.
B or Esc goes back a step, and a click outside the menus shuts them, back to
the bar. The game keeps running while it is open, but your input goes to the
menu.

- **File:** About (the version and system details), Restart, and Exit.
- **Display:** Full screen, widescreen 16:9, the render scale (1× to 4×), and
  Skip intro. Skip intro leaves out the logos and the graffiti disclaimer the
  game opens with, and starts at the title screen's loading. Off by default,
  so the intro plays as on the Xbox. It is kept as `skipintro=` in the
  settings file.
- **Graphics:** The [graphics options](#graphics-options) below.
- **Controller:** The free camera, Auto centre, and the free camera's
  inversion, sensitivity, and deadzone.
- **Developer:** Settings for testing and bug reports.
  - **Log to file:** Stops or starts writing to `last-run.log` at once.
  - **Logging:** The engine's diagnostic logs, for bug reports.
  - **Engine switches:** Turn parts of the engine off, or back to how they
    were, to compare.
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
and Purge shaders ask first, as anything since your last save is lost: all but
Exit restart the game. The other rows marked (Restart) apply from the next
start, which File > Restart gives.

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
