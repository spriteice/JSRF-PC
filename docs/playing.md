# Playing JSRF-PC

The controls, the in-game menu, and the graphics options. Installing is in the
[README](../README.md).

## Controller

Any pad SDL knows works, over USB or Bluetooth: Xbox, PlayStation, Switch Pro,
Steam Deck, and many more. The buttons are the Xbox's, with the bumpers as Black
and White. Back opens and closes the in-game menu, so the game's own Back is
Esc on the keyboard, unless you give it a button of the pad's.

Input > Controller layout changes which button presses each of the game's
controls, for every pad: choose a control, then press its button or pull its
trigger, or press Back (or F1) to leave it with none. The button that control
had goes to whichever control had the one you pressed, so the two swap. A
trigger on a game trigger keeps its pressure; a button gives a full press.
Any button SDL reports can be used, the back paddles, Misc and the touchpad
too, but Guide, the system's, and Back, the menu's. The sticks stay as they are. Its last control, Toggle free
camera, is the [free camera](#free-camera)'s switch, R3 by default, so the
game's own Right stick click starts with none. Reset to default puts every
button back.

The game's button icons show the controller in your hands. The game draws
four buttons as pictures, A, B, X and Y, and names the rest in words.
Input > Button prompts > Style is Auto, Default, Xbox, PlayStation, Nintendo
or Keyboard. Default, the style a new install starts with and the one File >
Presets > Xbox sets, is the game as the Xbox shows it: its own icons, its 3D
A and B and its words, whatever the controller or the layouts. Auto follows
the pad you last pressed, as SDL reports it: a PlayStation pad shows Cross,
Circle, Square and Triangle in their colours, a Switch Pro pad or Joy-Cons
the Switch's letters where the pad has them, and any other pad, the Steam
Deck's own controls among them, the Xbox's; and keys while the keyboard or
the mouse was the last thing you used, the pad's again at its next press.
The other styles fix the look: Xbox, PlayStation and Nintendo the pad's
whatever you press, and Keyboard keys, in the icons and along the F1 menu's
foot alike; with Default, the foot follows the last thing pressed, as in
Auto. In every style but Default, the icons follow both layouts: move A to Y
on Controller layout and the A icon shows Y (or Triangle); on the keyboard,
each shows the first key the control has on Keyboard layout. A control with
nothing on it keeps the game's icon, and a button the
pad set has no picture of shows the nearest: the back paddles the Xbox
Elite's, and the touchpad PlayStation's. The PlayStation's Mute button, and
a key with no picture of its own, is a blank key with its name on it. In
every style but Default, the A and B beside a message box's text, Skip
cutscenes' A, and the Start in the speech bubble at a graffiti save point,
are the style's pictures, flat and still pulsing, where the game turns its
own in 3D; and where the game's text names the A or B button by its letter,
the letter is the style's picture of it, a little taller than the text, in
every language the game has: the line to press B to go back in the pause
menu's notebook and graffiti picker, Roboy's test run's line to press A to
start, the save, load and memory messages, and the graffiti save screen's.
The talks and the missions' messages, the tutorial's among them (Corn's
lines, Gum's and the lessons on the screen), name the A, B, X and Y
buttons, the triggers and the left thumbstick in words, in English and
Japanese: a button's letter is the style's picture, the word button
staying, and a trigger's or the stick's words are its picture, the quote
marks and the colour round them staying the game's. As a talk types its
line out, a name's picture comes in whole as the typing reaches it.
The help line that scrolls along the top of the graffiti editor names the
D-pad, the left stick and the triggers too, in words, and each name there
is the style's picture, scrolling with the line: a pad's D-pad or left
stick as one picture with the ways the line means lit (a picture for each
way instead, where Controller layout has one of those D-pad ways on
another button), and with keys, the key for each way, side by side. The
word START stays a word. Where a menu leans its buttons, as the VS menu and the graffiti
menus slant theirs, the style's pictures are drawn upright, in the same
place. Everywhere the style's pictures are drawn a little larger
than the game's own icons: in the menus, on the HUD, beside the message
boxes, in the lines of text and along the F1 menu's foot. The style is kept as
`button_prompts=` in the settings file (0 Auto, 1 Default, the default, 2
Xbox, 3 PlayStation, 4 Nintendo, 5 Keyboard). The pads' pictures are
Juliocacko's Free Input Prompts, and the keys and the mouse Xelu's Free
Controller & Key Prompts, both free to use (CC0): the Xbox face buttons in
their colours, the PlayStation's symbols in colour on grey buttons,
Nintendo's in red, yellow, blue and green, and every other button and key
dark grey with its letters white, so they show on light and dark alike. A
key or button they have no picture of, such as Scroll Lock, the
PlayStation's Mute or an Xbox Elite pad's paddles (P1 to P4), is drawn as a
blank key with its name.

## Keyboard

Keys go by position, so W A S D on a QWERTY keyboard is Z Q S D on an AZERTY
one. These are the defaults: Input > Keyboard layout changes which keys, or
mouse buttons, press each control, and Reset to default puts them back.
Each control has two: choose its row, press its first key, then its second.
F1, or Back on a controller, is none at either, so pressing it twice leaves
the control with no key. The control that had a key you press gets the one
it replaces, so the two swap, as on Controller layout. Any key can be used but
F1 and F8 to F11, and the mouse's left, middle, right, back and forward
buttons. Keyboard moves steers with whatever keys the left stick has.

- **W A S D:** Move. They are the left stick, so W goes away from the
  camera. With Input > Behaviour > Keyboard moves at Skater, W goes the way
  the skater faces, however the camera turns, A and D turn it while held, W
  with A or D turns it wider, and S turns it round.
- **U, H, J and K:** The d-pad's up, left, down and right, for the menus.
- **Arrow keys:** The camera (right stick).
- **Space or the left mouse button:** A, to jump. Each is A on its own, so
  letting go of one while the other is held keeps A held.
- **Left Shift or the middle mouse button:** B, to boost.
- **C:** X.
- **V:** Y.
- **E or the right mouse button:** R trigger, to talk and spray.
- **Q:** L trigger.
- **R:** Black.
- **T:** White.
- **Return or keypad Enter:** Start.
- **Esc, or a keyboard's Back key:** Back.
- **Left Ctrl:** Left stick click, to grind on the floor.
- **Right Ctrl:** Right stick click.
- **The mouse:** The free camera, while it is on (Free camera, below), and
  photo mode's camera (Photo mode, below).

Every key and mouse button above is on one control only.

## Settings keys

These work in a window or full screen, and each one is remembered.

- **F1, or Back on a controller:** The in-game menu.
- **F11 or Alt+Return:** Full screen or a window. The game starts full
  screen until you choose a window. Started with `--minimized`
  (`JSRF-PC.exe --minimized`, `./jsrf-pc --minimized`), it opens minimized,
  in a window, leaving the keyboard where it was, for that start only:
  restore it to play. On Linux it may show for a moment first, and in Game
  Mode it opens as usual.
- **F10:** 4:3, and back to widescreen at Match screen, your screen's own
  shape, which the game starts at until you choose. Display > Aspect ratio
  has the other shapes. Widescreen widens the
  view rather than stretching it: the HUD and text keep their shape, the HUD
  sits at the screen's edges, the pause menus and the race previews are
  laid out across the screen, the graffiti editor's bar of static is
  stretched across with its help line scrolling its whole length, the
  garage's menus and VS mode's setup screen
  carry their colours on to its edges, the result screens' banner, time and
  choices sit at the right edge, clear of the dance, and the title screen
  fills the screen from its first frame, its opening's bars sweeping in from
  the screen's edges.
- **F9:** Resolution, in a box of its own over the game: the height the game
  is drawn at, Default (the Xbox's 640×480), 720p, 900p, 1080p, 1440p, 1600p,
  2160p, or Match screen, your screen's own height (up to 2160), which the
  game starts at until you choose. The width follows the aspect ratio (1080p
  at 16:9 is 1920×1080), and the picture is fitted to the window. It changes
  at once, with no restart. Esc, B on a controller, Cancel or F9 again shuts
  the box with nothing chosen.
- **F8, or R3 on a controller:** The free camera.

## Free camera

F8, or R3 (a click of the right stick), gives the right stick to the camera,
which turns around the player, and forward is wherever it looks. The L
trigger puts it back behind the player, as it does the game's camera.
Talking to someone doesn't turn it to them. The game's own shots, such as
the camera turning to the wall you spray, are Input > Behaviour > Camera's,
with the free camera on or off ([In-game menu](#in-game-menu), below):
ticked, the game takes the camera for them as on the Xbox, and once a shot
is over the free camera glides from the shot's view back to where you had
it as the shot began, over 0.4 of a second, on foot or on a rail: turned as
far from the game's own camera as it was then, so it is the same way round
your skater, whichever way they now face. Into each shot it cuts, as the
game's camera does. The right stick or the mouse steers where the glide
goes, at once. Your skater keeps going the way the left stick sends them
through the shot and the glide out, as through the game's own cut to the
shot: let go of the stick, or turn it, and it goes by the view the camera
is gliding to. Unticked, like the graffiti camera option in the Jet Set Radio re-release,
they don't play, and the free camera stays. With Half pipe cam unticked, it
rises and falls with your skater through a half pipe's jump, as the game's
camera does then.

Inversion, sensitivity, and the deadzone are in the in-game menu under
Input > Free camera, and in the settings file (see the `README.txt` in the
download).
With no keyboard, as in Steam's Game Mode, R3 does what F8 does. The game
itself never uses R3, so its own Right stick click starts with no button, and
Toggle free camera in Input > Controller layout can move the switch to
another button.

**The mouse** turns the free camera too, as the right stick does, and
the two add together. While the free camera is on and the in-game menu is
shut, the game holds the pointer: hidden, and kept in the window. Opening
the menu, turning the free camera off, or switching to another window
gives it back. Invert X and Invert Y apply to the mouse as well, Mouse
sensitivity sets how fast it turns the camera (5 by default: a full turn in
3,600 of the mouse's counts, about 11 cm at 800 dpi), and turning Mouse
look off, under Input > Free camera, leaves the mouse and the pointer alone.
With no mouse, as in Game Mode, nothing changes, and a finger on a touch
screen doesn't turn the camera.

**Auto centre**, under Input > Free camera, eases the view back behind the
player once the right stick and the mouse have been left alone for a while,
as the game's camera trails them; a push of the stick, or a move of the
mouse, takes over again at once. It is off by
default, and has four levels: Mild waits two seconds and comes back slowly,
Medium a second and a half, High a second, and Aggressive half a second,
coming back quickly.

The free camera only steers the camera of the skater you control. When the
game moves a skater by itself, as when a scene follows Beat or another
skater heads off, the game's own camera shows it. Pausing keeps the view
where you left it. Near a wall, it keeps the wall out of the view as the
game's camera does (Near walls, below).

The game splits each map into areas, and draws only what the area the
camera is in can see. The free camera can look from outside every area, as
from beside a tower on the way down, where the game would draw nothing of
the level: the level is then drawn as from where the game's own camera
stands, or, when that is outside every area too, the whole map is, and your
skater is drawn even when they fall outside every area themselves.

## Photo mode

Developer > Missions > Photo mode, ticked, holds the world still, hides the
HUD, and gives you a camera that flies anywhere: through walls and floors,
and out of the map. It starts once you have control: no scene, fade or talk
running, the game's pause menu shut, and the game's camera following you for
half a second. The camera starts where the game's was. Unticking it (Back or
F1 for the in-game menu) lets the world go on, brings the HUD back and gives
the camera back to the game; ticking it again holds the world again.

- **Left stick (W A S D):** Forward, back and sideways, along where the view
  faces.
- **Right stick, and the mouse:** Turn and tilt the view.
- **R trigger and L trigger:** Straight up and down.
- **White held (R1, or T):** Four times as fast.
- **Black held (L1, or R):** A quarter as fast.
- **Y:** Back to where photo mode started.

A, B and X do nothing, so your right thumb can turn the view while you fly.

The look goes by Input > Free camera's settings: Invert X and Invert Y, the
sensitivities, the deadzone, Mouse look and Mouse sensitivity. While the
world is held, the game sees nothing you press, so nothing jumps or moves
on. The game holds the pointer as it does for the free camera.

What's drawn is drawn from where the camera is: Far buildings, Far shadows
and Draw distance go from there, as from the game's own camera. Outside every
area of the map, as inside a wall, the level is drawn as from the last place
the camera was in one, until it comes into another. Where the map joins two
of its parts, as Kibogaoka Hill's tunnel does, the camera goes through as
you would, and comes out in the other part.

The tick carries over a mission or Load game picked in Developer >
Missions, so mission after mission starts in photo mode; it is off when the
game starts afresh, or after File > Restart. It doesn't start while
Developer > Playback records or plays a recording.

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
wall rather than the view turning over. That close, your skater fades out,
as the game fades them when its camera comes near and hides them when it's
right on them, as on the Xbox. No skater fade, the tick under Default cam
fix in Input > Behaviour > Camera (off by default), keeps the camera further out
instead, where the game shows the player whole: with a wall nearer than that
on the camera's side, as when you fall down a tower with the free camera
turned to it, you see through the wall and still see the player. Where the
game itself puts its own camera that close, it stays there, as on the Xbox.
The game's own shots (Input > Behaviour > Camera), such as the camera
turning to the graffiti you spray, are left as the game frames them, as on
the Xbox. Camera's first tick,
Default cam fix, turns this off for the game's own camera, which then stops at
walls as on the Xbox, with No skater fade or without it; the free camera keeps
walls out of its view either way.

## In-game menu

The in-game menu is a [Dear ImGui](https://github.com/ocornut/imgui) menu bar
drawn over the game, in the original Xbox's greens, with the game dimmed
behind it. F1, or Back on a controller, opens and closes it, and it opens on
the bar alone. It works with a controller, the keyboard, or the mouse.
B or Esc goes back a step, and a click outside the menus shuts them, back to
the bar. Along the bottom, a line says what the row you're on does, beside
the buttons to press, in Input > Button prompts' style. The game keeps
running while it is open, but your input goes to the menu.

- **File:** About (the version and system details), Presets, Restart, and Exit.
  Presets, a submenu, sets many settings at once. Xbox (Restart) and Modern
  (Restart) change gameplay as well as visuals; after a line, Performance,
  Balanced and Quality change only visuals. Xbox is the game as the Xbox
  runs it: 4:3 at Default resolution (640×480), with everything JSRF-PC adds
  to how it looks and plays turned off, and Audio effects on, as the Xbox's
  own sound has them. Modern is the author's preferred way to play: the HUD
  at 0.8x, the draw distance, Far buildings and Far shadows at 3x, MSAA at
  4x, anisotropic filtering at 16x, Upscaling's Menus and HUD and Textures
  at 8x, Graffiti at 4x and Buttons off, Sharp, Strong and both Cleanup
  ticks, Audio effects on, Keyboard moves on Camera, Stick shape Square,
  Button prompts' Style Auto, Default cam fix and Half pipe cam on and the
  game's other two shots off, the free camera on, and Skip intro, Skip
  cutscenes and Visual indicator on. Xbox and Modern each ask first, and restart the game. The
  other three apply straight away, with no restart. Performance, which a
  new install starts with, has the draw distance, Far buildings and Far
  shadows at 1x, no anti-aliasing or anisotropic filtering, and Upscaling's
  Textures and Graffiti off. Balanced
  has the draw distance at 2x, Far buildings and Far shadows at 3x, MSAA at
  4x in place of SMAA, anisotropic filtering at 8x, and Upscaling at 4x but
  for Buttons. Quality has the draw distance, Far buildings and Far
  shadows, anisotropic filtering, Upscaling's Menus and HUD, Buttons,
  Textures and Graffiti, and both anti-aliasings at their most, as far as
  the graphics card goes; Far buildings and Far shadows at All draw parts of
  a map you never see from where you are, such as the top of Dogenzaka Hill
  in its sky. All but Xbox have the resolution and aspect ratio at Match
  screen, Music info in pause and See through fix on, and Upscaling's Menus
  and HUD at 4x or more. A preset sets every setting but Developer's,
  Input's two layouts, the free camera's own settings, Mute, Shader cache
  and, but for Xbox and Modern, Audio effects: each one also puts the game
  full screen and the HUD shown, at its own size but in Modern.
  Input, Gameplay, and Upscaling's Sharpness, Edge strength and Cleanup are
  yours: only Xbox and Modern change them. Xbox sets Keyboard moves to
  Camera, Stick shape to Square and Button prompts' Style to Default, turns
  the free camera, Default cam fix and No skater fade off, ticks the game's
  three shots, puts Sharpness and Edge strength at Normal with no Cleanup,
  and turns Skip intro, Skip cutscenes and Visual indicator off. The other
  three leave all of them as they are. The one ticked is the
  one your settings match, if any. Below them, after a line, Reset (Restart)
  puts every setting back to how a new install has it, once you say yes,
  and restarts the game: Developer's switches and both layouts too. It
  keeps where the game is, so it isn't asked for again, and your saves, the
  shader cache, your logs, Missions' start states and Playback's
  recordings, which are no settings. It is there where Restart is.
- **Display:** Full screen, the resolution (its choices as F9 gives them),
  the aspect ratio, the HUD submenu, the draw distance, the Building distance submenu (Far
  buildings and Far shadows), and a submenu at the end, Enhancements, with
  Music info in pause and See through fix. In HUD, HUD scale draws the HUD at 0.5× to 1.5× its size
  (1× the default), a tenth at each − or +, each piece keeping its distance
  from the screen's edges; Disable HUD hides it. Both change at once, and are
  kept as `hudscale=` (0.5 to 1.5) and `hudoff=` (1 for hidden) in the
  settings file. In split screen the HUD keeps its own size, though Disable
  HUD still hides it. The resolution changes at once, and is kept as
  `resolution=` (`default`, `720`, `900`, `1080`, `1440`, `1600`, `2160` or
  `match`) in the settings file; a `scale=` from before it (1 to 6) is taken
  as the resolution at least that high, up to 2160p. The higher it is, the
  more it asks of the graphics card. The
  aspect ratio is 4:3 (the Xbox's own, widescreen off), 16:10, 16:9, 21:9, or
  Match screen, the shape of the display the window is on, the default. It changes at once, and is kept as
  `aspect=` (with `widescreen=` for on or off) in the settings file. Draw
  distance is how far away the game draws the things in its world: crowds,
  spray cans, props you can knock over, traffic, the police and the race's
  checkpoints. 1x, the default, is the game's own distances, 2x to 4x twice
  to four times them, and Unlimited as far as the camera sees. It changes at once, costs
  more the further it goes, and is kept as `drawdistance=` (0 for 1x to 4 for
  Unlimited) in the settings file. Above 2x it will struggle on weak hardware.
  In Building distance, Far buildings draws more of the map, with the things
  in it, so buildings further off don't pop in as you come near. The game
  splits each map into areas, and draws only the ones the area the camera is
  in lists. Off is the game's own; 1x (the default), 2x and 3x draw what the
  game would show from the areas one, two or three areas ahead; and All draws
  every area the game ever shows, which can put a part of the map in view
  where the game never shows it, such as the top of Dogenzaka Hill in the sky
  at its bottom. At any choice, a stretch partway up Dogenzaka Hill, and the
  hill past it, isn't drawn from the street below it, where the game hides it
  too: from there its buildings and traffic would hang in the sky. Nor is the
  rest of the hill drawn from the low street at its far corner, under its top,
  or on the way to it. At All on Hikage Street, the space above each of the
  street's three ends is drawn only from the ends and from up in it, as what
  is built there would stand in the street's view further in, so its buildings
  come in as you reach an end. On Kibogaoka Hill, the buildings past the
  bowl's far side aren't drawn from the rim by the tunnel, just past it, or on
  the way to it, where they would hang in the air, nor are some in the map's
  middle; they come back in the tunnel. Nor, from the east edge of the map's
  middle and beyond it, are three pieces of the bowl's west side drawn, or
  what lies only past them: at 3x and All one of them would hang in the air
  too. At All on Chuo Street, two pieces of scenery from past the long
  halfpipe's end wall aren't drawn from a stretch further down it. In 99th
  Street's opening scene, the one the first time you come in, nothing more
  than the game's own is drawn, as from the street west of the plaza thin
  lines crossed its close-up of the dragon; along that street the rest of the
  time it draws as anywhere. It changes at once, costs more the further it
  goes, and is kept as `farbuildingsahead=` (0 for Off to 4 for All) in the
  settings file.
  Far shadows, below it, draws the shadows of the areas Far buildings adds
  too, as many areas ahead as its own choice, Off to All, and never further
  than Far buildings, so they don't pop in as you come near, at the cost of
  minor visual glitches, such as patches of light in shadows that move with
  the camera. On Chuo Street, the scenery past the long halfpipe's end wall
  keeps its shadows only where the game shows them, as at All they lit
  patches on that wall, and so does some scenery on Kibogaoka Hill, whose
  shadows lit patches on a walkway across the bowl from it. Off, those areas
  have no shadows until you're near enough for the game to show them itself.
  1x by default; it does nothing with Far buildings off, changes at once, and
  is kept as `farshadowsahead=` (0 for
  Off to 4 for All) in the settings file. Before b2.1-hotfix the two were on
  or off, kept as `farbuildings=` and `farshadows=`: until you choose again,
  1 there reads as 3x and 0 as Off. Music info in pause shows
  the song playing, its number, title and artist, in both pause menus, on the
  left, above Rotate map. It follows the songs of the street mixes too, and
  shows nothing for the title's and the story's own music. On by default; it
  changes at once, and is kept as `musicinpause=` (1 or 0) in the settings
  file. See through fix shows characters and dynamic world objects through
  glass and water, tinted by them: the game draws them after the glass, and
  on the Xbox the glass hides them, as the crowd behind a shop's window.
  Faded characters, as your skater close to the camera, and the level's
  arrow as it fades in, let what is behind them show through too, tinted by
  them, their own far side as well: on the Xbox they hide what the game
  draws after them, such as other characters, glass and water. Their dark
  outline then shows round the edge of each part, not across it. The
  copies of your skater that trail behind at speed, and the crowd, are
  drawn as on the Xbox: the trail in shadow is shadowed, and hides the
  dash's burst behind it. On by default; it changes at once, and is kept as
  `seethru=` (1 or 0) in the settings file.
- **Graphics:** The [graphics options](#graphics-options) below.
- **Audio:** Mute, then Audio effects. Mute silences the game's sound at
  once, and ticked again, brings it back; the game and its sound run on as
  ever, only silent. Off by default, it is kept as `mute=` (1 or 0) in the
  settings file, and no preset changes it. Audio effects are our own
  versions of the console's sound effects: the 3D sounds placed to your left
  or right as the game sets them, the characters' voices kept on them as
  they move, the reverb the game sets for a place, as in the sewers, and a
  limiter of their own for the 3D sounds, so a busy moment no longer turns
  the music and voices down. On by default; off, the sound is as before
  them. It is kept as `audioeffects=` in the
  settings file, and changing it restarts the game. Only File > Presets >
  Xbox (Restart) and Modern (Restart) change it, turning it on.
  [The audio
  effects](technical/audio-effects.md) says how they work.
- **Input:** Five submenus. Controller layout and Keyboard layout set
  which button, or key or mouse button, presses each of the game's
  controls ([Controller](#controller) and [Keyboard](#keyboard), above),
  and Controller layout the free camera's switch too. Each change is kept at
  once in `remap.cfg`, beside the settings file, a `pad_<control>=` or
  `key_<control>=` line with SDL's name for it, and only for a control that
  differs from its default; Reset to default puts its page back. No preset
  changes them. Button prompts has Style, the look of the game's button
  icons and of the buttons along the menu's foot ([Controller](#controller),
  above), Default in File > Presets > Xbox and Auto in Modern. Free camera has
  [the free camera](#free-camera)'s own settings: Enable free camera, Auto
  centre, Invert X, Invert Y, Sensitivity X, Sensitivity Y, Deadzone (10% by
  default), Mouse look (on by default), and Mouse sensitivity (5 by
  default), then Reset to default, which asks first, and puts all of them
  but Enable free camera back to their defaults. Behaviour has Camera,
  Keyboard moves, then Stick shape. Camera is five ticks, each with its own
  line along the bottom: Default cam fix and No skater fade, for walls, then
  the game's own shots, Graffiti cam, Half pipe cam and Loop cam. Default
  cam fix keeps the game's own camera out of
  walls (Near walls, above). Off by default, so the game's camera is as on the Xbox. It
  changes at once, and is kept as `dcamfix=` (1 or 0) in the settings file.
  No skater fade keeps the camera as far from your skater as the game shows
  them whole, seeing through a wall nearer than that ([Near
  walls](#near-walls), above): the free camera always, and the game's own
  while Default cam fix is on. Off by default, and in the Xbox and Modern
  presets: the camera comes in to the wall and your skater fades out as it
  nears them. It
  changes at once, and is kept as `noskaterfade=` (1 or 0) in the settings
  file. The last three ticks are the game's own camera shots: Graffiti cam,
  the camera turning to the graffiti you spray; Half pipe cam, the shot of
  a jump in a half pipe; and Loop cam, the fixed shots a stage places, as
  when you grind a loop or jump past one. Ticked, the game takes the camera
  for that shot as on the Xbox, whatever the free camera, Default cam fix
  and No skater fade; unticked, the shot doesn't play, and the camera keeps
  following you, the free camera too if it's on. Through a half pipe's jump
  with Half pipe cam unticked, the camera rises and falls with you, at its
  own angle and distance, and settles back as you land; on the Xbox's own
  follow camera you would fly up out of view. With Half pipe cam unticked,
  the free camera can go through a half pipe's wall, riding it with the
  camera turned into it or coming down from a big jump: a known issue,
  which Modern leaves ticked for. After a shot the free
  camera glides back to where you had it ([Free camera](#free-camera),
  above). The camera that holds
  still as you skate out of a level's exit is no tick's: it is always as on
  the Xbox. All three are ticked by default, and in the Xbox preset, and
  Half pipe cam alone in Modern. Each
  changes at once, and is kept as `graffiti_cam=`, `half_pipe_cam=` and
  `loop_cam=` (1 or 0) in the settings file. Keyboard moves
  sets how W A S D move the skater ([Keyboard](#keyboard), above): from the
  way the camera faces (Camera, the default), or from the way the skater
  faces (Skater), which the camera turning, the free camera or the game's
  own, doesn't change. It changes at once, and is kept as `keyboard_moves=`
  (0 Camera, 1 Skater) in the settings file. Stick shape sets how far a
  controller's sticks reach on a diagonal: Square, the default, each
  stick's circle stretched onto the square the Xbox controller's sticks
  reach, so a full push is full on both axes at any angle, or Round, as the
  pad gives them. The game takes the skater's push from how far the stick is
  pushed, whichever way, so Round already skates at full speed on a
  diagonal; with Square, a part push on a diagonal counts up to 41% more
  than the same push straight, as on the Xbox, and turns the free camera
  that much faster. The keys stay as they are. It changes at once, is
  Square in the Xbox and Modern presets,
  and is kept as `stick_shape=` (0 Square, 1 Round) in the settings file. A
  recording holds the sticks as the game got them, so it plays back the
  same whichever shape is set.
- **Gameplay:** Two submenus: Skips, with Skip intro and Skip cutscenes,
  then Graffiti souls, with Visual indicator. Skip intro leaves out the
  logos and the graffiti disclaimer the game opens with, and starts at the
  title screen's loading. Off by default, so the intro plays as on the Xbox.
  It is kept as `skipintro=` in the settings file. Skip cutscenes lets you
  leave a story scene, or a preview where the camera follows a rival to
  their place: while one plays, press A (the jump button), and "Press A
  again to skip" fades in at the bottom right for 3 seconds, with the A
  button as the game's message boxes show it (its own green A, or the
  button prompts' style's, [Controller](#controller)); press A again while it
  shows, and the scene ends as it does at its own end (a preview as when
  the rival arrives, so a scene that follows the arrival still plays, and
  two presses skip it too), the story going on from there. With it ticked,
  that is the one way for every scene and preview, those you've seen before
  too: the game's own skip of a scene or preview seen before, at one press
  of A or Start, is left out, and is back when it's off. Previews the game
  never lets you skip, as Poison Jam's rounds in Shibuya Terminal or Beat's
  in Dogenzaka Hill, are covered too. Nothing shows
  until the first press. The prompt is drawn with the HUD, so Display >
  HUD's scale sizes it, and with Disable HUD it isn't shown, though two
  presses still end the scene. Off by default, so scenes play to their end
  as on the Xbox; it changes at once, and is kept as `skipcutscenes=` (1 or
  0) in the settings file. Visual indicator shows the graffiti soul, in
  white, at the bottom left, level with the speed bar, while one of an
  area's special soul challenges is under way and on track: a course of
  moves, or Shibuya Terminal's 13 platforms. It comes in once the challenge
  can fail: at the first platform, or once a course's opening move is done
  (on Chuo Street, a second's grinding on the first rail, which the game
  lets you add up over tries). It pulses at each new platform, turns red
  and goes when the challenge fails, and goes when the soul is yours. The
  challenges themselves are as on the Xbox. It goes to the screen's left edge in widescreen, and HUD scale and
  Disable HUD size it and hide it with the rest of the HUD. Off by default;
  it changes at once, and is kept as `soulindicator=` (1 or 0) in the
  settings file.
- **Developer:** Settings for testing and bug reports.
  - **Log to file:** Stops or starts writing to `last-run.log` at once.
  - **Logging:** The engine's diagnostic logs, for bug reports.
    JSRF_EVT_LOG=0.2 logs, every 0.2 s, where each skater is posed: the
    code that posed it, the space it was posed in, its lowest and highest
    bones, and each character's position.
    JSRF_FLOW_LOG logs a line each time a mission moves on a step, with the
    step's number and the mission's: asking for its files, its setup, play,
    and the hand-over to the next mission.
    JSRF_TALK_LOG logs the story's events as they run: each talk started,
    with the code that started it, the mission script's entries and the
    triggers that fire, every story flag that changes, and the talk zones a
    character walks into.
    RECOMP_HEAP_GUARD logs the game's mistakes with the memory it releases,
    and where in the game's code each one is: a block released twice, used
    after its release, or released through the wrong call. While it is on,
    the memory the game releases is kept back, out of reach, until the game
    needs that space again; where the game's memory goes doesn't change.
    RECOMP_INPUT_LATENCY logs how long each press on the first controller or
    the keyboard takes to reach the screen, a line each: when the game read
    it, when the first frame drawn after that was reached, handed over, and
    swapped, and on Linux the display's next refresh after that, the soonest
    it can be seen. For a key, also how long before the read the window took
    it in. Every 10 s, each of those steps' median and worst. What the
    compositor and the screen add after that isn't in it.
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
  - **Missions:** Restarts the game at a story mission, for testing. It is
    a developer option, so a mission started this way may not play as it
    does in the story. Character is who you start as: any of the game's 24
    characters, Garam among them, Yoyo to begin with, as in a new game.
    Load game, between two lines below it, restarts the game at the title's
    Load Game screen, with the saves of the runs started here. Below it,
    with a line under them, are [Photo mode](#photo-mode)'s tick and
    Record's. With Record ticked, a mission or Load game picked here is
    recorded from its first frame, as Playback's Record (below) does, and
    the recording plays back from the same start. Below that,
    each level, in story order, as "Dogenzaka Hill (stg12)", is a list of
    its missions, each with its number, its kind and, for a race or a
    battle, who it is against, as "0252 (flag game, Rapid99)". The garage
    and Gouji's Tower (stg40), the final fight, are levels of their own, and
    the missions on sets the game doesn't name are in Other sets, at the
    foot. Picking one asks first, as anything since your last save is lost,
    then restarts the game at that mission: the intro and the title are
    left out, and a new game starts there, with a new game's progress. With
    a test build's `start-states.txt` beside the settings file, which is
    made from the game's files and is never in a package, it starts with
    the story up to that mission done instead. A run started here keeps its saves in `mission-saves` in the save folder,
    apart from your own, which it never opens. File > Restart afterwards
    starts the game as usual. Character is kept in `developer.cfg`; Photo
    mode and Record are kept from pick to pick, but not after File > Restart
    or when the game starts afresh.
  - **Playback:** Records what you press, and plays a recording back, for
    testing. Record (Restart) asks first, then restarts the game and
    records every press the game reads, on every controller, from its
    first frame, until Stop recording, File > Exit, closing the window, or
    the game restarting (File > Restart, or any row that restarts it).
    The [free camera](#free-camera) is recorded too: its stick, the
    mouse's turns of it, turning it on and off, and its settings under
    Input > Free camera, which a recording plays back in place of yours
    while it plays, leaving yours as they were. So are Input > Behaviour >
    Camera's ticks for the game's three shots; a recording made before them
    plays the game's shots as those builds did, the graffiti shot only while
    the free camera is off, one made before the free camera eased back
    after a shot plays without the ease, one made before it glided
    eases its angle back from the game's view, with no glide in or out,
    and one made while it glided into the graffiti shot glides in as it
    did. Each recording is a folder
    in `recordings` beside the settings file (`portable/recordings` in
    portable mode), named by when it started, in UTC, as
    `2026-11-19-091500`. Play (Restart) lists the recordings there were
    when the game started, newest first; picking one asks first, then
    restarts the game and plays it. Press anything on a controller or the
    keyboard, move the mouse while the free camera steers with it, or turn
    the free camera on or off or change its settings or the shots' ticks,
    and it stops, and
    the game is yours from there; it stops itself at the recording's end,
    or if the game has gone another way (below). The launcher plays one
    with `--play <folder>`, and closes the game at its end. A recording is
    pressed frame by frame, not by time, and waits for each load, mission,
    cutscene and step of a mission it reached, so a slower load, or a
    mission's music ready a moment later, moves nothing; one made before it
    kept the steps waits for the rest only. While a run is recorded or
    played, each piece of music or speech starts a second after the game
    asks for it, the same frame every time, rather than as soon as the disc
    has it, so what the game waits on (a course's countdown, a mission's
    music) comes on the same frame in a play as when you recorded it; one
    recorded before this plays the sound as the game does. The game
    doesn't play exactly the same twice, so a long run can drift: each
    second the recording knows where your skater was, and when it stood
    still with nothing pressed playback puts the skater back there. Each
    second it also knows the game's random numbers and how many times the
    game drew them since the second before, on its own thread and on
    others: playback puts them back each second and when it puts the
    skater back, and the log says where they weren't as recorded. So with
    the free camera's exact heading and pitch, kept as it takes over from
    the game's camera and each second. From your first press to the end,
    the recording keeps every frame's place, heading and camera too, the
    camera's eye, and a fingerprint of the skater's whole state, and the log
    says the first frame a play wasn't the same, and what went first. If the
    skater ends up far from where they were, or the game loads something
    the recording didn't, playback stops, and the log says where. Start a
    recording from the same saves you will play it with. The settings that
    change how a run plays, Default cam fix, No skater fade, Skip intro,
    Skip cutscenes, Draw distance, Far buildings and Far shadows, are kept
    in it as they were and as you changed them: a play uses them, and
    yours are back as it ends, the settings file untouched. While a
    recording plays, a white disc with a play sign shows at the middle of
    the HUD's left edge, straight above the graffiti soul badge; as the
    play ends it turns green (finished), red (off route or stopped) or
    blue (you took the game back), holds for two seconds, then fades out.
    HUD scale sizes it, widescreen moves it to the edge, and Disable HUD
    hides it.
    A recording's folder can also hold `steps.txt`, a line each: `idle 2:
    menu Graphics > Anti-aliasing (SMAA) > Off` chooses that row as a click
    would at the recording's second idle moment, holding the recording
    there while it does; `frame 600: screenshot` writes the picture shown
    then as a BMP; `idle 1: mark <words>` puts a line in the log. Every
    line is checked before anything is played, and a row that isn't there
    stops it. With `--play`, the folder can hold `profile/` too: a settings
    file (`settings.cfg`), `developer.cfg`, `start-states.txt`, `saves/`
    and `cache/`, as a portable folder has them. Each play then runs on a
    copy of it, in the folder's `plays/<date>-<time>/` with its
    screenshots, so your own settings and saves are never opened, and
    `steps.txt`'s `setting mute=1` and `developer RECOMP_GL_PROFILE=1`
    lines are written into that copy first. Menu rows and those lines need
    a `profile/`, as they change settings; Play (Restart) leaves one out.
    A recording made from Developer > Missions with Record ticked notes
    the mission, the character and Photo mode it started with, and the
    story's progress it started from: playing it starts the game at that
    mission again, as the pick did, and stops at once if the mission
    doesn't start there with the same progress (from the same test build's
    `start-states.txt`), and the log says why.
    Lines starting `[PLAYBACK]` in the log say what it did, with a summary
    at the end.
  - **Experimental:** Changes to how the game runs, to try out, when there
    are any.
  - **Far buildings test:** Finds which area Far buildings adds draws
    something out of place, such as buildings floating in the sky. Stand
    where it shows, and press + on Leave out area: each press leaves out the
    next area Far buildings adds from where the camera is, and the log names
    it, until it is back at -1, none. An area the game draws itself is never
    left out. Game's backdrop depth draws the backdrop with its depth, as the
    game does, which hides what Far buildings adds beyond it. Neither is
    kept: each start, and each new map, begins with none left out. While Far
    buildings is on, the log also names the area the camera is in.
  - **Purge shaders:** Restarts the game and deletes the shader cache as it
    starts.

  Logging's and Engine switches' rows are named by the environment variable
  each one sets, in alphabetical order. Those two, Performance and
  Experimental are kept in `developer.cfg` beside the settings file, and
  apply from the next start.

Everything is remembered but Far buildings test. Restart, Exit, File >
Presets' Xbox, Modern and Reset, the shader cache, Audio effects, and Purge
shaders ask first, as anything since your last save is lost: all but Exit
restart the game. The resolution and the other presets change at once, with no
restart. The other rows marked (Restart) apply from the next start, which File
> Restart gives.

## Graphics options

These are in the in-game menu, under Graphics.

- **Shader cache:** Keeps the shaders the game builds, and builds them again
  when it starts, so play doesn't pause for them. On by default. Changing it
  restarts the game, and no preset changes it.
- **Anisotropic filtering:** 2× to 16×. It keeps textures sharp where they are
  seen at an angle, such as streets running into the distance. Off by default.
- **Anti-aliasing:** Smooths jagged edges. SMAA costs little and smooths the
  finished picture, the HUD included. MSAA (2×, 4×, or 8×) gives the cleanest
  edges but asks the most of the graphics card. Use either, or both. Both are
  off by default.
- **Upscaling:** A submenu at the foot of Graphics, with seven rows:
  - **Menus and HUD:** Off, 2×, 4×, or 8×. The menus, text, HUD and icons are
    drawn from sharper copies of the game's art, which the graphics card makes
    the first time each piece is shown. It helps above Default resolution; at
    Default they look as they always have. Each size suits a resolution: 2× for
    720p and 900p, 4× for 1080p to 1600p, and 8× for 2160p, whose copies take
    the most memory. 4× by default.
  - **Buttons:** Off, 2×, 4×, or 8×. The button prompts' pictures JSRF-PC
    adds, in every Button prompts style but Default, are drawn from copies of
    them enlarged by Super-xBR as the game's art is, which the graphics card
    makes the first time each picture is shown: on the HUD, in the menus and
    message boxes, and along the foot of the F1 menu. It changes nothing
    else; the game's own icons are Menus and HUD's. The pictures are smooth
    and at most 128 pixels a side, larger than the game draws them up to
    about 1440p, so Off draws them as they are, smoothly enlarged where the
    screen is larger; 2× is enough at 2160p. A copy takes up to 1 MB of
    memory at 4× and 4 MB at 8×, and only the pictures shown are copied.
    Off by default.
  - **Textures:** Off, 2×, 4×, or 8×. The textures of the 3D world (walls, the
    ground, signs and the characters) are drawn from sharper copies, which the
    graphics card makes in a level's first moments, a few a frame. They show
    where a surface is close enough to the camera for its texture to be
    enlarged, at any resolution and more so the higher it is. Distant
    surfaces, cut-out edges such as fences, and the characters' light and
    shadow look as they always have. No
    copy is larger than 2048 a side, so 8× enlarges the larger textures 4×. The
    copies take up to 4 GB of the graphics card's memory, 1 to 2.5 GB at 4× in
    play; whatever doesn't fit keeps the game's own textures. Off by default.
  - **Graffiti:** Off, 2×, 4×, or 8×. The graffiti tags are drawn from sharper
    copies. The tags come 16 to a sheet of 1024 × 1024, and the graphics card
    makes a sheet's copy the first time it is drawn, after the 3D world's; the
    garage's graffiti picker has its copy made as it draws it, which can pause
    the menu for a moment. The copies take up to 1 GB of the graphics card's
    memory, apart from Textures': about 85 MB a sheet at 4× and 340 MB at 8×.
    Off by default.
  - **Sharpness:** Soft, Normal, or Sharp. How crisp the edges in the 2D, 3D,
    buttons' and graffiti copies are. Normal is Super-xBR as its author set it; Sharp draws
    edges crisper and Soft softer, neither with halos. Changing it makes every
    copy again, which can pause the game for a moment. Normal by default, and
    in File > Presets > Xbox; Sharp in Modern.
  - **Edge strength:** Soft, Normal, or Strong. How faint a diagonal edge the
    2D, 3D, buttons' and graffiti copies follow. Normal is Super-xBR as its author set it;
    Strong follows fainter diagonals too, for smoother lines in fine detail, and
    Soft only clear ones, blending the rest. Changing it makes every copy again,
    which can pause the game for a moment. Normal by default, and in File >
    Presets > Xbox; Strong in Modern.
  - **Cleanup:** DXT deblock and De-dither, each ticked on or off. They clean
    the game's textures up before the copies are made, so the upscaling
    doesn't sharpen what the textures' formats left in them. DXT deblock
    smooths the faint steps where a DXT texture's 4×4 blocks meet; De-dither
    smooths the dot patterns of 16-bit and DXT textures. Each leaves larger
    differences, edges in the picture, as they are, and neither changes a
    texture's see-through parts. Changing either makes every copy again,
    which can pause the game for a moment. Both off by default, and in File >
    Presets > Xbox; both on in Modern.
