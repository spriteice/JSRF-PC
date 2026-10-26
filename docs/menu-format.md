# In-game menu format

Every row, box and line of text in the in-game menu (Dear ImGui, F1 or Back on
a controller) follows these rules, and so does anything added to it later.
The rows are declared in `src/menu/menu.h` and drawn by `src/menu/menu.cpp`;
each menu's rows live with the setting they change (File in
`src/menu/menu_sdl.cpp`, Graphics and Purge shaders in
`src/nv2a_gl/nv2a_gl.c`, Controller in `port/freecam.c`, Developer in
`port/developer.c`).

## Wording

- **Sentence case.** The first letter of a phrase is a capital and the rest
  are lower case, as in a sentence: "Render scale", "Shader cache", "Log to
  file", "Purge shaders", "Free camera". This covers menu titles, rows,
  choices, box titles and buttons.
- Acronyms, names and axis letters keep their own capitals: SMAA, MSAA, GPU,
  USB, SDL, OpenGL, Dear ImGui, JSRF-PC, "Invert X".
- **Developer's switches are the exception.** Each row under Developer >
  Logging and Developer > Engine switches is the environment variable as
  ticking it sets it: "RECOMP_GL_PROFILE (Restart)" sets it to 1, and
  "RECOMP_GL_TITLE_MIPS=0 (Restart)" to 0, for a switch that turns
  something off. Developer > Experimental's rows have their usual names:
  "Game runs ahead of the GPU (Restart)".
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
- Where the game cannot restart itself (Android), OK saves the change for the
  next start, and a row that means nothing without a restart (Render scale)
  is not shown.
- **Developer's switches are the exception.** The rows under Developer >
  Logging, Engine switches and Experimental are marked (Restart) and apply
  from the next start, but they do not offer to restart, so several can be
  changed first. File > Restart then applies them. Log to file changes at
  once and has no (Restart).
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
-testing-ce20630-menu

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

- A new logging switch, an environment variable that only adds lines to the
  log, also gets a row under Developer > Logging (`k_logging` in
  `port/developer.c`).
- A new engine switch, an environment variable that changes what the engine
  does, to compare one way with the other, also gets a row under Developer >
  Engine switches (`k_engine`).
- A new experiment, an environment variable that tries out a change that may
  become the default later, also gets a row, but under Developer >
  Experimental (`k_tries`), with a name of its own in `k_tries_row`.
- A switch that works on some platforms only has a row on those alone, as
  RECOMP_CPU_PROFILE (Linux) does. A variable that takes a value, such as an
  address, is not a switch and has no row.
