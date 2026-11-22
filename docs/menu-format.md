# In-game menu format

Every row, box, and line of text in the in-game menu (Dear ImGui, F1 or Back
on a controller) follows these rules, and so does anything added to it later.
The rows are declared in `src/menu/menu.h` and drawn by `src/menu/menu.cpp`;
each menu's rows live with the setting they change (File in
`src/menu/menu_sdl.cpp`, Display, Graphics and Purge shaders in
`src/nv2a_gl/nv2a_gl.c`, Display's Draw distance and Building distance, with
Far buildings and Far shadows, in `port/draw_distance.c` and Music info in
pause in `port/music_info.c`, Audio in `port/audio.c`, Input's Controller
layout and Keyboard layout in `src/input/input_remap.c`, its Button prompts
in `src/input/input_prompts.c`, its Free
camera in `port/freecam.c`, and its Camera in `port/camera_scenes.c` (whose
first two ticks, Default cam fix and No skater fade, `port/camera_walls.c`
keeps), Keyboard moves in `port/keyboard_move.c` and Stick
shape in `src/input/stick_shape.c`, Gameplay's Skip intro in
`port/intro.c`, Skip cutscenes in `port/cutscene_skip.c` and Visual indicator
in `port/soul_indicator.c`, Developer in `port/developer.c`, with Missions'
rows in `port/missions_menu.c` (its Photo mode's tick in `port/photo_mode.c`),
Playback's in `port/playback.c` and its Far
buildings test in `port/draw_distance.c`). A row marked last goes below any added later
(`last` in `menu.h`). A row can instead go right below another row of its
menu, which it names, whichever of the two was added first (`under` in
`menu.h`): rows added as the game starts can sit among the window's, as Draw
distance and the Building distance submenu sit under the HUD submenu, in the
order they were added. If no row has that
name, it goes at the end of its menu, above the last row, and the log says so
once.

A row can also go in a submenu of its menu, which it names (`group` in
`menu.h`): the submenu is a row of its own (`XBOX_ITEM_GROUP`), with its
label and hint, and holds the rows that name it in the order they were added,
whichever was added first. A submenu nobody has put a row in isn't shown. If
no submenu has that name, the row goes at the end of its menu, above the last
row, and the log says so once. A submenu can sit under a row as other rows
do: Building distance, with Far buildings and Far shadows, sits under HUD,
below Draw distance. Display ends with one, Enhancements, marked last and
added by the window (`nv2a_gl.c`), for what goes beyond the console (Music
info in pause, then See through fix, added by the window). A new
enhancement goes in it, not in Display itself. Graphics
ends with one, Upscaling, marked last and added by the window too, for the
rows that draw the game from sharper copies: Menus and HUD, Buttons (the
button prompts' pictures), Textures, Graffiti, Sharpness, Edge strength,
then Cleanup, a row of two ticks.
Input is five submenus, and nothing else: Controller layout, then Keyboard
layout, added with their rows by `src/input/input_remap.c` (Layouts, below);
Button prompts, added with its row, Style, by `src/input/input_prompts.c`
right after them; Free camera, added with its rows by
`port/freecam.c` (Enable free camera, Auto centre, Invert X, Invert Y,
Sensitivity X, Sensitivity Y, Deadzone, Mouse look, Mouse sensitivity,
then Reset to default); then Behaviour, for how the
game's camera, the keys and the sticks act: Camera, a row of five ticks
(Default cam fix, No skater fade, Graffiti cam, Half pipe cam, then Loop
cam), Keyboard moves, then Stick shape. Each of those three modules adds Behaviour
with its rows, and those after the first add nothing, as a submenu named as
one before it in its menu adds nothing (`group` in
`menu.h`). A new input setting goes in one of them, or in a submenu of its
own. Gameplay, after Input, is submenus too, and nothing else, for how the
game plays: Skips, for the parts of the game a player can leave out (Skip
intro, then Skip cutscenes), then Graffiti souls, for help with the graffiti
soul challenges (Visual indicator). The window adds them (`nv2a_gl.c`), in
their order, so they keep it whichever module adds its rows first. A new skip
goes in Skips, and a new gameplay setting in one of Gameplay's submenus, or
in a submenu of its own.

## Wording

- **Sentence case.** The first letter of a phrase is a capital and the rest
  are lower case, as in a sentence: "Aspect ratio", "Shader cache", "Log to
  file", "Purge shaders", "Free camera". This covers menu titles, rows,
  choices, box titles, and buttons.
- Acronyms, names, and axis letters keep their own capitals: SMAA, MSAA, GPU,
  USB, SDL, OpenGL, Dear ImGui, JSRF-PC, "Invert X".
- **Developer's switches are the exception.** Each row under Developer >
  Logging and Developer > Engine switches is the environment variable as
  ticking it sets it: "RECOMP_GL_PROFILE (Restart)" sets it to 1, and
  "RECOMP_GL_VCACHE=0 (Restart)" to 0, for a switch that turns something
  off, and "RECOMP_APU_MIX=xemu (Restart)" to the word the engine reads.
  Developer > Performance's and Developer > Experimental's rows have plain
  names, as "Use P cores (Restart)" and "Game runs ahead of the GPU
  (Restart)" (b1.1's experiment).
- No keyboard shortcuts in a label: "Free camera", not "Free camera (F8)".
  docs/playing.md and the packages' README.txt list the keys.
- A box's message is a whole sentence, ending in a full stop. It must be as
  short as possible, while still conveying the correct message.

## Hints

- Every row but a separator has a hint (`hint` in `menu.h`): what the row
  does, shown in the strip along the bottom while the pad or the pointer is
  on the row, or on its choices or its - and +. If a row has none, or one
  too long, the log says so, for the first such row.
- A hint says what the row does, not how to use the menu, in a sentence
  ending in a full stop: "Sets how far the right stick moves before the
  camera turns." A second, short one may say what it costs ("GPU heavy.",
  "CPU heavy.", "Light on the GPU.") or when it applies ("Applies on
  restart."). It must be as short as possible, and is at most 70 characters
  (`MENU_HINT_LONGEST`), so it fits in two lines at 640 × 480.
- A hint names no key, as a label doesn't.
- A row of choices or ticks may give each its own hint (`choice_hints` in
  `menu.h`), shown in the row's place while the pad or the pointer is on
  that choice, and following the rules above; a choice with none shows the
  row's. Input > Behaviour > Camera gives one for each tick (spriteice's
  card, 22 November 2026). The strip is as tall as the longest of them
  needs too.
- The tests of the modules that add rows (tests/audio, camera_scenes,
  draw_distance, freecam, intro, keyboard_move, stick_shape, music_info,
  soul_indicator, developer) check that each has one.

## (Restart)

- A row whose change needs the game to restart ends its label with
  ` (Restart)`, after its name: "Shader cache (Restart)".
- Choosing it asks first, with the "Are you sure?" box (the row's `confirm`),
  and a question saying what OK does and what is lost: "<Do what>, losing
  unsaved progress?", as in "Apply selection and restart, losing unsaved
  progress?" or Purge shaders' "Delete the shader cache, losing unsaved
  progress?"
- OK saves the change and restarts the game (`sdl_restart` in nv2a_gl.c).
  Cancel and B change nothing; the pad starts on Cancel.
- **Developer's switches are the exception.** The rows under Developer >
  Logging, Engine switches, Performance, and Experimental are marked
  (Restart) and apply from the next start, but they do not offer to
  restart, so several can be changed first. File > Restart then applies
  them. Log to file changes at once and has no (Restart).
- A row that applies at once has no (Restart) and no box, unless it loses
  progress (File > Exit asks first).

## Boxes

- Every box sits in the middle of the window and cannot be dragged
  (`box_begin` and `box_centre` in menu.cpp). New boxes use them.
- B or Esc is Cancel. A box closes back to the menu it was opened from.
- A bind row's box is the exception (Bind rows, below): B is a button it
  takes, and the pad's Back or F1 is none, so Esc alone shuts it unchanged,
  and only while it waits for a pad. It closes back on its row, in its
  submenu.
- F9's box, Resolution, is the window's own, over the game with no menu
  round it (`menu_build_box` in menu.cpp, put up by `xbox_MenuBox` in
  menu_sdl.cpp). It opens only while the menu is shut, and the game goes on
  behind it. Its title, its text, then a button for each choice, in one
  row, the one in use drawn held down, and Cancel last. A choice applies at
  once and shuts it; Cancel, B and Esc shut it with nothing chosen, and so
  do F9, F1 and the pad's Back. It closes back to the game.

## Bind rows

- A bind row (`XBOX_ITEM_BIND` in `menu.h`) is a control, with the input
  that presses it beside it, in the text's own colour. Choosing it opens a
  box with no buttons, titled with its submenu ("Controller layout"):
  "Press a button for A, or Back for none." for a pad's row, or "Press a
  key or mouse button for A, or F1 for none." for the keyboard's (`takes`).
- The next press it takes is the row's, handed to its `bind`, and reaches
  neither the menu nor the game: a pad's button, or a trigger pressed past
  half way, on any pad; or a key, by its place, or a mouse button. The
  window finds it in SDL's events (`xbox_MenuEvent` in `menu_sdl.cpp`).
  A press the row can't have leaves the box waiting, with "That one can't
  be used." under its message.
- The pad's Back and F1 are none (`XBOX_BIND_NO_INPUT`, handed to the row
  as a press), so a control can be left with nothing. As they open and shut
  the menu, neither can be bound, and no layout can shut a player out of
  the menu. There is no cancel but Esc, while the box waits for a pad: it
  shuts the box with nothing handed over.
- Dear ImGui reads the pad itself, so from the box until the pad is let go,
  its pad navigation is held off, and B is no Back: the press that binds,
  or is none, does nothing more in the menu.
- A row whose control has a second input (`bind_second`), as each of
  Keyboard layout's has a second key, takes two presses: "Press a key or
  mouse button for A, or F1 for none.", then "Press a second key or mouse
  button for A, or F1 for none." ("a button", and Back, for a pad's). The
  first is handed to its `bind`, the second to `bind_second`. At either,
  the pad's Back or F1 is none, so a control can be left with nothing:
  Back twice.
- The box closes back on its row, so the next control is a step away.
- A menu path (`menu_path_run` in `menu.h`) stops at a bind row, and
  doesn't run it: its box waits for a press, which a path can't give.

## Layouts

- Input's first two submenus are the layouts (`src/input/input_remap.c`):
  Controller layout, a bind row for each of the game's buttons (A, B, X, Y,
  Black, White, L trigger, R trigger, Start, Back, Left stick click, Right
  stick click, then the d-pad's four), then Toggle free camera, the port's
  own (the free camera's switch, `port/freecam.c`, which the game never
  sees: R3 by default, so Right stick click starts with none); and Keyboard
  layout, a row for each of the sticks' directions, then the game's buttons,
  which keys and mouse buttons press (the free camera's key is F8, the
  window's). Each ends with Reset to default, which puts every row of its
  page back.
- A row shows its control's inputs, by the names the menu gives them ("LB",
  "LT", "Paddle 1"; a key by SDL's name, "Space"; "Mouse right"), two with a
  comma where it has a second ("Space, J"), or None.
- On Controller layout, a press binds that button to the row's control
  alone, and the control that had it gets the row's old button, so the two
  swap. The pad's Back or F1 leaves the control with none.
- On Keyboard layout, a row takes two presses (Bind rows): the control's
  first key, then its second. Either can be none (F1 or the pad's Back; the
  first key again, for the second), so a control can have no key. Each
  goes in its place, and the control that had that key gets the place's
  old key, so the two swap; a key moved between a row's own two places
  swaps them.
- A row can't have the pad's Guide or Back, or the window's keys: F1, F9 to
  F11 and the keys the game binds, as F8. Nor a key SDL has no name for,
  which the settings couldn't keep.
- The layouts are a player's own: no preset sets them (File > Presets).

## Look

- The menu's look is `src/menu/menu_style.cpp`'s, and the setup's window
  (`launcher/setup/setup_ui.cpp`) has the same one: Dear ImGui's dark style
  with the original Xbox's two greens in place of its blue, rounded corners
  (on the green behind a row and the pad's cursor round it too), and rows a
  little further apart than the dark style's, for a pad.
- The console's green (#92C83E), the bright one, is for thin marks on the
  dark: the pad's cursor, a tick in a box, a slider's grab. Its dark green
  (#397C2D) goes behind white text: the row the pad is on, buttons, the open
  menu's title, a box's title bar, and the progress bar.
- White text is at 4.5:1 or more on everything it is drawn on, and the bright
  marks at 3:1 or more, with the game at its brightest or its darkest behind
  the menu. tests/menu_style checks both, and that no colour is left in the
  dark style's blue.
- The game is dimmed behind the menu, and further behind a box: black at
  40% each time (the style's `ModalWindowDimBg`), so the menu reads on the
  brightest stage and a setting's change can still be seen.
- The strip along the bottom, in the bar's colour, shows the row's hint on
  the left and on the right the buttons to press: A Select, B Back, and Back
  Close on a pad, or Enter Select, Esc Back, and F1 Close on the keyboard,
  whichever was pressed last in Input > Button prompts' Auto and Default
  looks; always the keyboard's in its Keyboard look, and always the pad's in
  Xbox, PlayStation and Nintendo, as the game's icons are then. In a bind
  row's box, only the one that leaves its control none: Back None, or F1
  None. Each is the button prompts' picture of it (Cross Select, Circle Back
  and Create Close on a PlayStation pad, the keys' pictures on the
  keyboard), in Default the one Auto would show, which the window makes and
  hands to the menu through one function (`nv_window_prompt_picture`), never
  the layouts': the menu's own buttons can't be moved. Where the window has
  no picture, it is on a cap: A in the dark green and B in a red as dark, as
  the pad colours them, and the rest grey (`menu_style_cap`). Whether the
  strip shows the keys or the pad whatever was pressed, the window says
  through another (`nv_window_prompt_keys`). The strip is as tall as the longest hint
  needs at the window's width, so it stays still as the pad moves.
- Every menu, submenu and list of choices stays between the bar and the
  strip, over neither: one opened low down goes up to fit, and one taller
  than the room between them scrolls, with a scroll bar.
- Colours come from the style alone: nothing in a menu sets one of its own.

## Behaviour

- The menu opens on the bar alone, with nothing dropped down.
- It fades in as it opens and out as it shuts, in a tenth of a second
  (`MENU_FADE_SECONDS` in `menu.h`). As it fades out, it takes no input:
  the controls are the game's again at once.
- B and Esc go back one level, down to the bar.
- A click outside the menus shuts them all, back to the bar. A click outside
  the bar with nothing open does nothing.
- Choosing a row leaves its menu open, so the change can be seen.

## File > Presets

- Presets is File's second row, under About, with a separator
  above and below it, as between About and Restart before it. Its choices
  are Xbox mode, JSRF-PC default and Max quality, in that order
  (`XBOX_PRESET` in `menu.h`).
- **Xbox mode** is the game as the Xbox runs it: 4:3 at Default, the
  Xbox's 640x480, with everything JSRF-PC adds to how it looks and plays
  turned off.
  **JSRF-PC default** is the settings a new install starts with. **Max
  quality** is JSRF-PC default with each graphics and display setting that
  has a most at its most: the last choice, or the most the device has. Far
  buildings and Far shadows are the exception, at 3x, not All: All draws
  parts of a map the game never shows from where the camera is, such as
  Dogenzaka Hill's top in its sky.
- The tick is on the preset whose value every row it sets has now, and on
  none when one differs. A new install is at JSRF-PC default.
- Choosing one applies it at once, with no box: it sets each row through
  its own function, as choosing that row would, and saves it as the row
  does. No preset sets a (Restart) row, so none restarts the game: Shader
  cache and Audio effects are left to the player, as Mute is (spriteice's
  card, 02:00 UTC on 2026-11-21). A (Restart) row a preset did set would keep its value
  instead, and the game would restart once, after them all, if one changed.
- **A preset sets every setting but Developer's, and the rows that say a
  preset leaves them alone** (`XBOX_PRESETS_LEFT` in `menu.h`): a player's
  own choices, not a quality setting, each with its own way back to its
  default, as the layouts' rows are (Layouts, above), and the free camera's
  tuning, with its Reset to default (spriteice's card, 22 November 2026).
  Every other row of Display,
  Graphics, Audio, Input and Gameplay, in a submenu or not, gives its value in
  each preset where it's added (`presets` and `preset` in `menu.h`), with
  JSRF-PC default's from the constant its own default comes from, so the
  preset moves with the default. That takes in Fullscreen, full screen in
  each, Keyboard moves, at Camera in each, Stick shape, at Square in
  each, and Button prompts' Style, at Default in each (spriteice, 23:57 UTC
  on 2026-11-21). A row of
  ticks (`XBOX_ITEM_TOGGLES`), as
  Upscaling's Cleanup, gives the ticks it has in each, and a preset changes
  each tick that differs, as choosing it would. A (Restart) row the presets
  set also has `keep`, which keeps a value for the next start without
  restarting. A row that says nothing, or that asks first with no `keep`,
  is told once in the log and left alone, as a row with no hint is told.
  File's and Developer's rows are never set, and a submenu
  (`XBOX_ITEM_GROUP`) is no setting, so says nothing.
- **A row the presets set may name presets that leave it** (`leave` in
  `menu.h`, a bit for each): those don't change it, and their tick goes by
  the other rows. Xbox mode sets Input > Behaviour > Camera, Default cam
  fix and No skater fade off and the game's three shots ticked, and Input >
  Free camera's Enable free camera, off; JSRF-PC default and Max quality
  leave both as the player has them (spriteice, 22 November 2026), each
  tick and the switch its own way back. A row every preset leaves is told
  once in the log and left alone.
- The tests of the modules that add rows check their values, and that
  JSRF-PC default's is what a start with no settings gives.

## File > About

- One section per subject, in this order: build info, OS, graphics, SDL,
  OpenGL, Dear ImGui.
- Each section is its name on a line of its own, then each detail on a line
  starting with `-`, the version first where it has one.
- One blank line between sections, and none inside a section.
- A new section follows the same format and goes where it belongs in that
  order (at the end if it fits nowhere else).

For example, on Linux:

```
JSRF-PC
-b1.1

Linux
-Built with Ubuntu Clang 14.0.0

Graphics
-<the graphics card, as its driver names it>

SDL
-2.30.12
-Built against 2.30.12
-x11 video

OpenGL
-<the OpenGL version, as its driver gives it>

Dear ImGui
-1.93.0
```

## Developer

- A new logging switch, an environment variable that adds lines to the log,
  or changes what the engine does only to find a bug and log it, as
  RECOMP_HEAP_GUARD does, also gets a row under Developer > Logging
  (`k_logging` in `port/developer.c`).
- A new engine switch, an environment variable that changes what the engine
  does, to compare one way with the other, also gets a row under Developer >
  Engine switches (`k_engine`).
- Logging's and Engine switches' rows are in alphabetical order by the
  variable's name, word by word: an underscore comes before any letter, as
  a space would. A new switch goes where it falls in that order.
  tests/developer checks.
- A new performance switch, an environment variable that turns off
  something the engine does by default for speed, also gets a row under
  Developer > Performance (`k_perf`), with a plain name in `k_perf_row`
  for what the engine does, ticked while it does it: "GPU queue (Restart)"
  is ticked by default, and unticking it sets RECOMP_NV2A_QUEUE=0.
  tests/developer checks that every one is ticked by default.
- Use P cores is Performance's first row, and stays the first: a new one
  goes below it. tests/developer checks.
- Performance comes after Engine switches, so it is above Purge shaders
  (added later, by nv2a_gl.c).
- Missions is the submenu right below Performance (`XBOX_GROUP_MISSIONS`,
  added by `port/developer.c`). Its rows are `port/missions_menu.c`'s, made
  as the engine starts from the game's own files and the names in its
  program, so nothing of the game's, its missions, levels or characters, is
  written into the repository. In this order: Character, a line, Load
  game, a line, Photo mode, a tick (`port/photo_mode.c`'s, not kept), a
  line, then the levels. Each level is a choice of its missions, and the
  levels share one function, which each row tells which level it is
  (`pick_with` and `data` in `menu.h`). tests/developer and tests/missions
  check.
- Playback is the submenu right below Missions (`XBOX_GROUP_PLAYBACK`,
  added by `port/developer.c`). Its rows are `port/playback.c`'s, in this
  order: Record (Restart), Stop recording, then Play (Restart), a choice
  of the recordings found as the engine starts, newest first, shown only
  when there is one. Record and Play ask first and restart, as a (Restart)
  row does; Stop recording applies at once. A recording's steps can't run
  Record or Play, as `menu_path_run` refuses a row that asks first.
  tests/developer and tests/playback check.
- A new experiment, an environment variable that tries out a change that may
  become the default later, also gets a row, but under Developer >
  Experimental (`k_tries`), with a name of its own in `k_tries_row`.
- A switch that works on some platforms only has a row on those alone, as
  RECOMP_CPU_PROFILE (Linux and Windows) and Use P cores (Linux) do. A
  variable that takes a value, such as an address, is not a switch and has
  no row, unless one word of it is a switch: its row sets that word, as
  RECOMP_APU_MIX=xemu does.
