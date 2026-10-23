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
- **F8:** The free camera.

## Free camera

F8 gives the right stick to the camera, which turns around the player, and
forward is wherever it looks. The L trigger puts it back behind the player, as
it does the game's camera. It also turns the graffiti camera off, like the
option in the Jet Set Radio re-release: spraying no longer turns the camera to
the wall.

Inversion, sensitivity, and the deadzone are in the in-game menu under
Controller, and in the settings file (see the `README.txt` in the download).
With no keyboard, as in Steam's Game Mode, turn the free camera on from that
menu.

## In-game menu

The in-game menu is a [Dear ImGui](https://github.com/ocornut/imgui) menu bar
drawn over the game. F1, or Back on a controller, opens and closes it, and it
opens on the bar alone. It works with a controller, the keyboard, or the mouse.
B or Esc goes back a step, and a click outside the menus shuts them, back to
the bar. The game keeps running while it is open, but your input goes to the
menu.

- **File:** About (the version and system details) and Exit.
- **Graphics:** Full screen, widescreen 16:9, the render scale (1× to 4×),
  and the [graphics options](#graphics-options) below.
- **Controller:** The free camera, and its inversion, sensitivity, and deadzone.
- **Developer:** Log to file, which stops or starts writing to `last-run.log`
  at once. Logging, the engine's diagnostic logs for bug reports, kept in
  `developer.cfg` beside the settings file and applied from the next start.
  Purge shaders, which restarts the game and deletes the shader cache,
  `states.dat` included, as it starts.

Everything is remembered. Exit, a new render scale, the shader cache, and Purge
shaders ask first, as anything since your last save is lost: all but Exit
restart the game. The other rows marked (Restart) apply from the next start.

## Graphics options

These are in the in-game menu, under Graphics.

- **Shader cache:** Keeps the shaders the game builds, and builds them again
  when it starts, so play doesn't pause for them. On by default. Changing it
  restarts the game.
- **Anisotropic filtering:** 2× to 16×. It keeps textures sharp where they are
  seen at an angle, such as streets running into the distance. Off by default.
- **Anti-aliasing:** Smooths jagged edges. FXAA costs little but softens the
  picture slightly. MSAA (2×, 4×, or 8×) gives the cleanest edges but asks the
  most of the graphics card. Use either, or both. Off by default.
