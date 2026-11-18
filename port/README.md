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
its top saying what it does and where it reaches the game. Developer >
Missions, for one, is `missions.c`, which starts the game at a story mission
through the game flow's update in `recomp_manual.c`, and `missions_menu.c`,
its rows in the in-game menu, made from the game's own mission files as the
engine starts.

The bulk of the port — the hundreds of thousands of lines of C the pipeline
generates from the XBE — is **not** here and will not be: it is derived from a
copyrighted binary. It is produced on your machine, from your own disc, by the
pipeline in `tools/`.

`CMakeLists.txt` here builds the game from this repository once the generated
code is in `port/recomp/gen` (see the main README), and stamps it with the
version of the git tag the tree is at.
