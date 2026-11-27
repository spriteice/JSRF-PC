# JSRF-PC Tools

JSRF-PC Tools is a separate program beside the launcher in both packages:
`JSRF-PC-Tools.exe` on Windows, `jsrf-pc-tools` on Linux. It works on JSRF-PC's
own files, its saves and its settings, while the game isn't running. It never
changes the game's files, and nothing it makes from them leaves your PC.

## Starting it

Start it from JSRF-PC's folder, the one with the launcher in it, and it finds
everything by itself. Started anywhere else, it asks for that folder: pick it,
or drop it on the window. It says why it asks: the folder it looked in, or
what it was given that isn't a folder or an option it knows. The pages that
work on saves need JSRF-PC to have been given the game once, so play it once
first; the Build page works before that.

The window can be resized freely, down to a size where nothing is cut off.
Its text and buttons follow the system's scale: Windows' scale setting, or on
Linux the desktop's (as it hands it to X11 programs, Xft.dpi; under Wayland
the desktop scales the window itself). Where that comes out too big or too
small, as it can on a handheld's small screen, pick another under **Scale**,
at the top right: Auto, or 75% to 300%. Ctrl and + or - change it a step, and
Ctrl and 0 goes back to Auto. The scale picked is kept in `tools.cfg`.

Each page has its own tab. Files and folders can be dropped on the window, and
go to the page that shows.

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
  was last saved and where it came from ("Game save: loaded from slot 2
  ("Mine") on 27 Nov 2026, 10:59am. First from xbased."), and each graffiti
  design, with its preview, its place in the garage (page, row and column)
  and its size. Once the game has saved over a save the tool put there, the
  line ends "The game has saved since." Tick designs and press **Delete** to
  take them out.
- **Save slots:** Eight slots, each holding a whole set of saves, the game
  save and every design. Pick a slot, then **Save here** (JSRF-PC's saves,
  naming it if you like), **Load** (it becomes JSRF-PC's saves), **Rename**
  or **Clear**. A slot can also be filled from a backup (**Copy to slot...**
  under Backups) or straight from another emulator (**Import into a slot...**
  when importing). **Holds** says where a slot's game save first came from
  ("Game save from xbased"), and hovering over a slot says how it got there.
  They're kept in `Save slots`, beside JSRF-PC's saves.
- **Backups:** Every backup, newest first, each with its date and what it
  holds. The ones the tool makes say what they came before, such as "Before
  loading slot 3". Pick one, then **Restore** (it becomes JSRF-PC's saves),
  **Copy to slot...** (it goes into a slot you pick, which is replaced),
  **Rename** (its date stays) or **Delete**. They're kept in `Save backups`.
  Where JSRF-PC's saves came from is kept in `Save origin.txt`, beside them,
  and each backup takes a copy of it.
- **Other emulators:** With **Show their folders and saves here** ticked,
  Cxbx-Reloaded and xbased are listed with where each keeps its saves and
  what's there. A folder the tool doesn't find in its usual place, or
  another one you want, is picked with **Pick...** or **Change...**, and is
  remembered. Pick an emulator, then:
  - **Import:** brings its saves into JSRF-PC. The list shows each save with
    what will happen to it; untick any you don't want. Each save is signed
    again for JSRF-PC as it's written. JSRF keeps one game save, so an
    imported game save replaces yours; the warning names it. A design whose
    place in the garage is taken goes into a free one. **Import into a
    slot...** puts the ticked saves in a save slot instead, in place of what
    it holds, and leaves JSRF-PC's own saves alone.
  - **Export:** sends JSRF-PC's saves to xbased or Cxbx-Reloaded, signed so
    each loads them. Cxbx-Reloaded's signature comes from the game's own
    `default.xbe`, read from the game JSRF-PC was given. A real Xbox needs
    the console's own keys, which the tool never asks for, so it can't be
    exported to.

  **Import a folder...** brings saves in from anywhere else, such as saves
  copied off an Xbox; dropping a folder on the window does the same. xemu
  isn't supported. An export's backup is of the emulator's saves,
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
there is backed up, in `Save backups` beside JSRF-PC's saves (named, under
the Save manager's Backups, for what it came before, such as "Before adding
6 designs"), and each design is written as a new save, `GRAFFITI001` to
`GRAFFITI228`, signed as JSRF-PC signs saves, with the size's icon from the
game's own files when JSRF-PC was given them as a folder; from a disc image,
the saves go without one. Clear imported takes the imported designs out of
the list.

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

## Build

What the launcher's first start does, a step at a time: the engine built on
your PC from your own copy of the game, and the tools that build it.

- **Engine:** Where the engine is, when it was made, and its size.
  - **Check version** asks the engine which release it was built as (its
    `--version`), and says so if that isn't this release's.
  - **Check signature** works out the engine's SHA-256 again and compares it
    with the one this release builds (`kit/expected.txt`), as the launcher
    does before every start. When it isn't the release's, the launcher offers
    to build it again at the next start.
  - **Use it anyway** keeps an engine that isn't the release's, as the first
    start's "Play anyway?" does: the launcher then starts with it.

  Both checks run by themselves when the page is first shown, and after a
  build.
- **Build:**
  - **Build engine** builds the engine from the game: the code generated from
    its `default.xbe`, then compiled. When the code generated before is from
    the same game and this release, and unchanged, it is compiled as it is,
    which saves the minutes translating takes; otherwise it is generated
    first. The engine there stays until the new one is complete.
  - **Generate code** only translates the game into C, the generated code,
    in `build/generated`, and checks its hash against the release's. Nothing
    is compiled, and the engine stays as it is.
  - **Delete generated code** deletes the generated code and what the build
    made from it (`build/generated`, `work`, `obj`, `tmp` and its copy of
    `default.xbe`). The engine and the build's log stay.
  - **Pick game...** and **Pick disc image...** build from another copy of
    the game than the one JSRF-PC was given. A game folder or disc image
    dropped on the window does it too.

  A bar shows how far it has got, with **Stop**. The log of the last build,
  download or generating is `build/build.log`.
- **Tools:** Python with capstone, and Zig, which building uses.
  - **Download tools** downloads them from their own sites (Python from
    GitHub, capstone and Zig from PyPI), each checked by its SHA-256, into
    `tools` beside JSRF-PC, in place of what's there.
  - **Delete tools** deletes that folder. Building again downloads them
    again.

  Build engine and Generate code download the tools first when they aren't
  there.

A package with the engine built in, such as a test build, has no kit: the page
shows its engine, and has nothing to build.

Closing the window stops what the page is doing, and the engine there stays as
it was. The generated code is made from your own copy of the game, so it stays
on your PC: it is never in a package.

## Scripts

For testing, the tool can play a list of steps on its own window, as a person
would, and check what it shows:

    jsrf-pc-tools [JSRF-PC's folder] --script <steps> [--out <folder>]

It picks tabs, presses buttons, rows and lists found by their names, types,
answers its own file dialogs, waits for each piece of work to end, checks the
window's text and the files on disk, and takes screenshots. Real mouse and
keyboard input to the window is ignored meanwhile. Each step is written to
`script.log` in the out folder (the steps file's folder when none is given)
as it ends, `PASS` or `FAIL` with why, then an `END` line; screenshots go
there too. The tool closes at the end, and its exit code is the number of
steps that failed (100 when the steps file can't be read). A run stops after
30 minutes.

Run scripts only on a copy of a JSRF-PC folder, as they save, load, clear,
import and delete there. `launcher/tools/scripts/` has the ones used to test
the tool: `general.txt` goes through every page, `scales.txt` and
`reopen.txt` through Scale, `tabs.txt` visits every tab and waits to be
closed from outside, and `kept.txt` checks that the game save's kept graffiti
tags (`kept-graffiti.txt`, in its own folder) go with a slot, Load, Restore,
the backups, a folder import and an export to xbased. `general.txt` wants
`pictures/` beside it (there) and `import-source/`, a folder of saves to
import from, such as a copy of a `data/saves`.

A steps file has one step a line; a line starting `#` is a comment.

| Step | What it does |
|---|---|
| `tab <name>` | Picks a tab. |
| `click <item>` | Presses a button, row, box, tab or list entry. |
| `hover <item>` | Keeps the mouse over it long enough for its tooltip. |
| `type <text>` | Types into the box being typed in. |
| `key <keys>` | Presses keys, such as `enter`, `escape`, `ctrl+a`, `ctrl+minus` or `ctrl+0`. |
| `answer <path>` | Answers the next file dialog with that path (files: several, between ` \| `); `cancel-dialog` cancels the next. Give them before the click that opens the dialog. |
| `drop <path>` | Drops a file or folder on the window. |
| `wait [seconds]` | Waits for the page's work to end (at most 120 s, or as given). |
| `pause <seconds>` | Waits that long. |
| `expect-text <text>` / `expect-no-text <text>` | The window shows that text, or doesn't, within 5 s. |
| `expect-enabled <item>` / `expect-greyed <item>` | The item can be pressed, or is greyed. |
| `expect-file <path>` / `expect-no-file <path>` | The file is there, or isn't. |
| `shot <name>` | A screenshot, `<name>.png`, noted with the window's size and scale. |
| `note <text>` | Goes in the log as it is. |
| `close` | Ends the script and closes the tool. |

An item is named by its label as the window shows it, such as `Save here`,
with the heading of the section it's in when the label is in more than one,
such as `Save slots > Rename`. `~text` names the first item whose label has
that text in it, and `@n` the nth of those (`Backups > ~2026@1`). The two
drop-down lists are named `Scale` and `Slot`. While a dialog is open only its
items are found, and while a list is open its entries come first.

Paths may use `$HERE` (the steps file's folder, which a relative path is
taken from), `$OUT`, `$GAME` (JSRF-PC's folder), `$SAVES` and `$SETTINGS`.
