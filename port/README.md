# The JSRF-specific half

Everything in this repository except this folder and `launcher/` is
title-agnostic machinery — the kernel layer, the NV2A and MCPX models, the
recompilation pipeline. This folder is the part that is about *Jet Set Radio
Future* and nothing else, and these two files are its core.

- **`main.c`** — the port's entry point. Loads the XBE for its data sections,
  maps the Xbox address space, starts the kernel and the hardware models, and
  calls the recompiled entry point. It knows the game's entry-point address
  and where to find its files.

- **`recomp_manual.c`** — hand-written overrides for individual recompiled
  functions, by guest address. Most are instrumentation that answers a
  specific question about a specific fault, kept because the question tends
  to come back. The comments say which fault each one was written for. A new
  one goes here only when the game function's own logic is the cause
  (docs/project-rules.md, Where code goes 6): a function the lifter got
  wrong is fixed in the recompiler, in `tools/recomp`. It also holds the
  hooks of the port's own features, which add to what the game does with
  nothing in it wrong, such as Skip intro, Music info in pause and the
  graffiti soul indicator. Each hook calls its feature's own file in this
  folder (`intro.c`, `music_info.c`, `soul_indicator.c`), which holds the
  feature's setting and logic in plain C that a test links.

The other files here are the port's own features, each with a comment at
its top saying what it does and where it reaches the game. `pad.c` is the
game's pad API answered from the host's pads: `recomp_manual.c` keeps the
eight entry points the game calls, as the pipeline reads that file for what
is written by hand, and each calls `pad.c`. `playback.c` is input
playback (Developer > Playback): the pad as the game read it, the free
camera's switch and tuning, and the Camera ticks for the game's shots
(`camera_scenes.c`), recorded by the game's own frame and played
back, from `pad.c`, from `freecam.c` and from hooks in `recomp_manual.c` on
the game's main loop, its loads, its cutscenes and its camera; a run started
from Developer > Missions is recorded with where it started
(`missions_started`), and played from there again. Developer >
Missions, for one, is `missions.c`, which starts the game at a story mission
through the game flow's update in `recomp_manual.c`, and `missions_menu.c`,
its rows in the in-game menu, made from the game's own mission files as the
engine starts. Missions' Photo mode row is `photo_mode.c`: the world held
with the game's own hold, the HUD hidden and a camera that flies, through
the map's portals as a skater goes, from hooks in `recomp_manual.c` on the
game's main loop (which reads the portals from the game's own list), its
frame's draw, its camera's draw and the set of areas it draws. The message
boxes' A and B and the graffiti save point's Start, drawn flat in the
button prompts' styles, are `prompt_buttons.c`, from the models' draw's and
the node functions' overrides in `recomp_manual.c`: it marks where each
button is, and the renderer draws the style's picture there
(`src/nv2a_gl/nv2a_prompts.h`). The Start's model is the whole speech
bubble, so only its ball's node is taken, and the rest drawn as the game
draws it; its picture is fitted under the bubble's word, where the others
are a quarter larger than the game's.
It also has the mark for a size of a screen's own for its upright buttons,
which nothing writes now: the VS menu's Back is drawn at every menu's size,
and what draws it is still to be found (b3.1 row 9).
The game's lines that name a control (the A or B by its letter, and in the
graffiti editor's help lines the D-pad, the left stick and the triggers in
words) are `prompt_text.c`, from the font calls' overrides there: it knows
each line by its bytes and draws it in pieces, each name's place marked
for the style's pictures the same way. The editor's help line scrolls
along a bar of static, in a ticker of the game's own: its override there
gives the game's measure the line's width with its pictures, marks the
ticker's window for them, and in widescreen draws the line in copies along
the bar, coming on at its right end, which `widescreen_menus.c` names among
the editor's parts and has drawn across (known bug 77).

The bulk of the port — the hundreds of thousands of lines of C the pipeline
generates from the XBE — is **not** here and will not be: it is derived from a
copyrighted binary. It is produced on your machine, from your own disc, by the
pipeline in `tools/`.

`CMakeLists.txt` here builds the game from this repository once the generated
code is in `port/recomp/gen` (see the main README), and stamps it with the
version of the git tag the tree is at.
