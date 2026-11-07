# In-game menu format

Every row, box, and line of text in the in-game menu (Dear ImGui, F1 or Back
on a controller) follows these rules, and so does anything added to it later.
The rows are declared in `src/menu/menu.h` and drawn by `src/menu/menu.cpp`;
each menu's rows live with the setting they change (File in
`src/menu/menu_sdl.cpp`, Display, Graphics and Purge shaders in
`src/nv2a_gl/nv2a_gl.c`, Display's Draw distance in `port/draw_distance.c` and
Skip intro in `port/intro.c`, Audio in `port/audio.c`, Controller in
`port/freecam.c`, Developer in `port/developer.c`). Skip intro is Display's
last row, below any added later (`last` in `menu.h`). A row can instead go
right below another row of its menu, which it names, whichever of the two was
added first (`under` in `menu.h`): rows added as the game starts can sit among
the window's, as Draw distance sits under Render scale. If no row has that
name, it goes at the end of its menu, above the last row, and the log says so
once.

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

## (Restart)

- A row whose change needs the game to restart ends its label with
  ` (Restart)`, after its name: "Render scale (Restart)".
- Choosing it asks first, with the "Are you sure?" box (the row's `confirm`),
  and a message saying what the restart does and what is lost:
  "The game restarts <doing what>, losing unsaved progress.", as in "The
  game restarts at the new scale, losing unsaved progress."
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

## Behaviour

- The menu opens on the bar alone, with nothing dropped down.
- B and Esc go back one level, down to the bar.
- A click outside the menus shuts them all, back to the bar. A click outside
  the bar with nothing open does nothing.
- Choosing a row leaves its menu open, so the change can be seen.

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
