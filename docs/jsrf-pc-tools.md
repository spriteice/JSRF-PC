# JSRF-PC Tools

JSRF-PC Tools is a separate program beside the launcher in both packages:
`JSRF-PC-Tools.exe` on Windows, `jsrf-pc-tools` on Linux. It works on JSRF-PC's
own files, its saves and its settings, while the game isn't running. It never
changes the game's files, and nothing it makes from them leaves your PC.

## Starting it

Start it from JSRF-PC's folder, the one with the launcher in it, and it finds
everything by itself. Started anywhere else, it asks for that folder: pick it,
or drop it on the window. JSRF-PC must have been given the game once, so play
it once first.

The window can be resized freely, down to a size where nothing is cut off, and
its text follows your display's scale. Each page has its own tab. Files and
folders can be dropped on the window, and go to the page that shows.

Nothing is written until you press a page's own button. A warning then says
what will be written and where, and a dated backup of what was there is made
first, in a folder named by the date and time, such as `2026-11-26 21.30.00`.

## Saves

Brings saves in from other emulators, and sends JSRF-PC's saves out to them.

- **Import into JSRF-PC:** From Cxbx-Reloaded, xemu or xbased, found where each
  keeps its saves, or from any folder or disk image, such as saves copied off
  an Xbox. Drop it on the window, or press its button. The list shows each save
  with what will happen to it; untick any you don't want. Each save is signed
  again for JSRF-PC as it is written. JSRF keeps one game save, so an imported
  game save replaces yours; the warning names it. A graffiti design that
  clashes with one already here goes into a free slot.
- **Export from JSRF-PC:** To xbased, or to Cxbx-Reloaded, signed so each loads
  them. Cxbx-Reloaded's signature comes from the game's own `default.xbe`,
  read from the game JSRF-PC was given. xemu and a real Xbox need the console's
  own keys, which the tool never asks for, so they aren't offered.

The backup is of the saves where they go, in `Save backups` beside JSRF-PC's
saves: straight in it for an import, and in a folder named for the emulator
for an export.

## Missions

Makes the file that lets the game's Developer > Missions start a story mission
with the story up to it done ([playing.md](playing.md#in-game-menu),
Missions). Without it, each mission starts with a new game's progress.

- **Make start states:** Reads the mission files of the game JSRF-PC was given
  (`Media/Mission`), checks it is the standalone US disc's, and walks the
  story through them from New Game, chapter by chapter, to the start of each
  story mission. That takes a moment. The warning then says how many of
  the story's missions the walk reached; those no exit leads to start at their
  chapter's opening, and Developer > Missions marks them as off the story's
  way. The file, `start-states.txt`, is written beside JSRF-PC's settings
  file. If the one there is the same, byte for byte, nothing is written.
- **Pick the game's folder:** The same, from another copy of the game. A game
  folder dropped on the window does it too.
- **Remove start states:** Removes `start-states.txt`, so missions start with a
  new game's progress again.

The backup of the `start-states.txt` there before is in `Start states backups`
beside it. JSRF-PC reads the file when it starts, so restart it if it's
running.

The file is made from your own copy of the game, so it is never in a package:
everyone makes their own. Every copy of the standalone US disc gives the same
file.
