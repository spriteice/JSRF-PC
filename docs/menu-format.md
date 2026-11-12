# In-game menu format

Every row, box, and line of text in the in-game menu (Dear ImGui, F1 or Back
on a controller) follows these rules, and so does anything added to it later.
The rows are declared in `src/menu/menu.h` and drawn by `src/menu/menu.cpp`;
each menu's rows live with the setting they change (File in
`src/menu/menu_sdl.cpp`, Display, Graphics and Purge shaders in
`src/nv2a_gl/nv2a_gl.c`, Display's Draw distance and Far buildings in
`port/draw_distance.c`, Skip intro in `port/intro.c` and Music info in pause
in `port/music_info.c`, Audio in `port/audio.c`, Controller in
`port/freecam.c` and Default cam fix in `port/camera_walls.c`, Developer in
`port/developer.c`). A row marked last goes below any added later (`last` in
`menu.h`). A row can instead go right below another row of its menu, which it
names, whichever of the two was added first
(`under` in `menu.h`): rows added as the game starts can sit among the
window's, as Draw distance and Far buildings sit under Render scale, in the
order they were added, or among another module's, as Default cam fix sits
under Controller's Free camera. If no row has that name, it goes at the end of
its menu, above the last row, and the log says so once.

A row can also go in a submenu of its menu, which it names (`group` in
`menu.h`): the submenu is a row of its own (`XBOX_ITEM_GROUP`), with its
label and hint, and holds the rows that name it in the order they were added,
whichever was added first. A submenu nobody has put a row in isn't shown. If
no submenu has that name, the row goes at the end of its menu, above the last
row, and the log says so once. Display ends with two, both marked last and
added by the window (`nv2a_gl.c`): Skips, for the parts of the game a player
can leave out (Skip intro), then Enhancements, for what goes beyond the
console (Music info in pause, then See through fix, added by the window). A
new skip or enhancement goes in one of them, not in Display itself. Graphics
ends with one, Upscaling, marked last and added by the window too, for the
rows that draw the game from sharper copies: Menus and HUD, Textures,
Graffiti, Sharpness, then Edge strength.

## Wording

- **Sentence case.** The first letter of a phrase is a capital and the rest
  are lower case, as in a sentence: "Render scale", "Shader cache", "Log to
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
- The tests of the modules that add rows (tests/audio, camera_walls,
  draw_distance, intro, music_info, developer) check that each has one.

## (Restart)

- A row whose change needs the game to restart ends its label with
  ` (Restart)`, after its name: "Render scale (Restart)".
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
  whichever was pressed last. Each button or key is on a cap: A in the dark
  green and B in a red as dark, as the pad colours them, and the rest grey
  (`menu_style_cap`). The strip is as tall as the longest hint needs at the
  window's width, so it stays still as the pad moves.
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

- Presets (Restart) is File's second row, under About, with a separator
  above and below it, as between About and Restart before it. Its choices
  are Xbox mode, JSRF-PC default and Max quality, in that order
  (`XBOX_PRESET` in `menu.h`).
- **Xbox mode** is the game as the Xbox runs it: 4:3 at 1x, with everything
  JSRF-PC adds to how it looks and plays turned off. What the console did
  itself stays on: the audio effects, which are our versions of its own
  sound, and the shader cache, which changes nothing on screen.
  **JSRF-PC default** is the settings a new install starts with. **Max
  quality** is JSRF-PC default with each graphics and display setting that
  has a most at its most: the last choice, or the most the device has.
- The tick is on the preset whose value every row it sets has now, and on
  none when one differs. A new install is at JSRF-PC default.
- Choosing one asks first, as a (Restart) row does. OK sets each row
  through its own function, as choosing that row would, and saves it as the
  row does. A (Restart) row keeps its value instead, and the game restarts
  once, after them all, if one changed.
- **Every row of Display, Graphics, Audio and Controller**, in a submenu or
  not, gives its value in each preset where it's added (`presets` and
  `preset` in `menu.h`), with JSRF-PC default's from the constant its own
  default comes from, so the preset moves with the default. Or it says it's
  the player's own (`XBOX_PRESETS_OWN`): Fullscreen, which is how the window
  sits, not how the game looks, and the free camera's tuning (Auto centre,
  Invert X and Y, Sensitivity X and Y, Deadzone), which matters only with
  the free camera on. A (Restart) row the presets set also has `keep`, which
  keeps a value for the next start without restarting. A row that says
  nothing, or that asks first with no `keep`, is told once in the log and
  left alone, as a row with no hint is told. File's and Developer's rows are
  never set, and a submenu (`XBOX_ITEM_GROUP`) is no setting, so says
  nothing.
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
- A new experiment, an environment variable that tries out a change that may
  become the default later, also gets a row, but under Developer >
  Experimental (`k_tries`), with a name of its own in `k_tries_row`.
- A switch that works on some platforms only has a row on those alone, as
  RECOMP_CPU_PROFILE (Linux and Windows) and Use P cores (Linux) do. A
  variable that takes a value, such as an address, is not a switch and has
  no row, unless one word of it is a switch: its row sets that word, as
  RECOMP_APU_MIX=xemu does.
