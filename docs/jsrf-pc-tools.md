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
what will be written, and a dated backup of what was there is made first, in a
folder named by the date and time, such as `2026-11-26 21.30.00`.

The tool's own settings, such as the emulators' folders you picked, are kept
in `tools.cfg`, beside JSRF-PC's settings file.

## Save manager

Keeps JSRF-PC's saves: the designs in them, your own save slots, the backups,
and saves brought in from other emulators or sent out to them. Anything that
changes JSRF-PC's saves makes a backup of them first, so it can be undone from
**Backups**. Close JSRF-PC before changing its saves, so it doesn't save over
them.

- **JSRF-PC's saves:** What the game loads now: the game save, with when it
  was last saved, and each graffiti design, with its preview, its place in the
  garage (page, row and column) and its size. Tick designs and press
  **Delete** to take them out.
- **Save slots:** Eight slots, each holding a copy of all of JSRF-PC's saves,
  the game save and every design. Pick a slot, then **Save here** (naming it
  if you like), **Load** (it becomes JSRF-PC's saves), **Rename** or
  **Clear**. They're kept in `Save slots`, beside JSRF-PC's saves.
- **Backups:** Every backup, newest first, each with its date and what it
  holds. The ones the tool makes say what they came before, such as "Before
  loading slot 3". Pick one, then **Restore** (it becomes JSRF-PC's saves),
  **Rename** (its date stays) or **Delete**. They're kept in `Save backups`.
- **Other emulators:** With **Show their folders and saves here** ticked,
  Cxbx-Reloaded, xemu and xbased are listed with where each keeps its saves
  and what's there. A folder the tool doesn't find in its usual place, or
  another one you want, is picked with **Pick...** or **Change...**, and is
  remembered. Pick an emulator, then:
  - **Import:** brings its saves into JSRF-PC. The list shows each save with
    what will happen to it; untick any you don't want. Each save is signed
    again for JSRF-PC as it's written. JSRF keeps one game save, so an
    imported game save replaces yours; the warning names it. A design whose
    place in the garage is taken goes into a free one.
  - **Export:** sends JSRF-PC's saves to xbased or Cxbx-Reloaded, signed so
    each loads them. Cxbx-Reloaded's signature comes from the game's own
    `default.xbe`, read from the game JSRF-PC was given. xemu and a real Xbox
    need the console's own keys, which the tool never asks for, so they can't
    be exported to.

  **Import a folder...** and **Import a disk image...** bring saves in from
  anywhere else, such as saves copied off an Xbox; dropping a folder or image
  on the window does the same. An export's backup is of the emulator's saves,
  in a folder named after it inside `Save backups`.

## Graffiti

Makes your own pictures into graffiti designs. The game lists them on the
garage's own-designs tab, as if you had painted them in its editor: you can
pick them, spray them and change them in the editor. The page has three
sections.

**Pictures**

- **Add pictures... and Add folder...:** JPEG, PNG, WebP, AVIF, SVG and
  TIFF, picked several at a time, or dropped on the window. A folder is
  looked through for them, four folders deep, and anything else is left out.
  An SVG is drawn as far as its shapes, paths and gradients go: text, reused
  symbols and style sheets aren't drawn. Remove all empties the list.
- **Make white backgrounds clear:** the game draws a pixel solid or clear,
  so one half clear or more is clear. While this is ticked, as it starts, a
  solid picture with a mostly white edge comes in with the white joined to
  its edge made clear.

**Designs**

Each picture as the design the game will have, grouped by size, with its
place: its page, row and column on the own-designs tab. Choose one to change
it.

- **Size:** Auto gives a picture the size nearest its shape: S for a square
  or tall one, M for 3:2, L for 2:1 and XL for 4:1. Any size can be picked
  by hand, SS among them.
- **Fit:** the whole picture with clear margins, as it starts; a crop box,
  dragged, and sized with the mouse wheel or its slider; or stretched.
- **Make the white background clear:** untick it to keep a design's white,
  or tick White inside it too for all of it.
- **In the garage:** the first free place, in the list's order, or a page,
  row and column given by hand with Move; Reset gives it the first free one
  again. A design never goes into a place that holds one. The page uses the
  garage's first 228 places of 280, pages 1 to 8 and the first four of page
  9, until a design past them has been checked in the game.
- **Remove:** takes the design out of the list. Its picture file stays.

**Import**

Import asks first, listing each design with its place. Then every save
there is backed up, in `Save backups` beside JSRF-PC's saves, and each
design is written as a new save, `GRAFFITI001` to `GRAFFITI228`, signed as
JSRF-PC signs saves, with the size's icon from the game's own files when
JSRF-PC was given them as a folder; from a disc image, the saves go without
one. Clear imported takes the imported designs out of the list.

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
- **Pick game...:** The same, from another copy of the game. A game
  folder dropped on the window does it too.
- **Remove:** Removes `start-states.txt`, so missions start with a
  new game's progress again.

The backup of the `start-states.txt` there before is in `Start states backups`
beside it. JSRF-PC reads the file when it starts, so restart it if it's
running.

The file is made from your own copy of the game, so it is never in a package:
everyone makes their own. Every copy of the standalone US disc gives the same
file.
