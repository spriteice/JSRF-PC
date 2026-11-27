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

## Graffiti

Makes your own pictures into graffiti designs. The game lists them on the
garage's own-designs tab, as if you had painted them in its editor: you can
pick them, spray them and change them in the editor.

- **Add pictures... and Add a folder...:** JPEG, PNG, WebP, AVIF, SVG and
  TIFF, picked several at a time, or dropped on the window. A folder is
  looked through for them, four folders deep, and anything else is left out.
  An SVG is drawn as far as its shapes, paths and gradients go: text, reused
  symbols and style sheets aren't drawn.
- **The folder:** each picture as the design the game will have, grouped by
  size, with the slot it will take: its page, row and column on the
  own-designs tab. Pick one to change it.
- **Size:** Auto gives a picture the size nearest its shape: S for a square
  or tall one, M for 3:2, L for 2:1 and XL for 4:1. Any size can be picked
  by hand, SS among them.
- **Fit:** the whole picture with clear margins, as it starts; a crop box,
  dragged, and sized with the mouse wheel or its slider; or stretched.
- **Clear:** the game draws a pixel solid or clear, so one half clear or
  more is clear. While Make white backgrounds clear is ticked, as it starts,
  a solid picture with a mostly white edge comes in with the white joined to
  its edge made clear. Untick it on that design to keep the white, or tick
  White inside it too for all of it.
- **Slot:** the first free ones, in the folder's order, or one given by
  hand. A design never goes into a slot that holds one. The page uses the
  first 228 of the game's 280 slots, 8 pages and four of the 9th, until a
  design past them has been checked in the game.
- **Import to save:** The warning lists each design with its slot. Each is
  written as a new save, `GRAFFITI001` to `GRAFFITI228`, signed as JSRF-PC
  signs saves, with the size's icon from the game's own files when JSRF-PC
  was given them as a folder; from a disc image, the saves go without one.

The backup is of every save there, in `Save backups` beside JSRF-PC's
saves, before anything is written.

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
