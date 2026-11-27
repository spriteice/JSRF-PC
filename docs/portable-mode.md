# The data folder

JSRF-PC keeps the settings, saves, logs, and cache in a folder named `data`
next to the launcher (`jsrf-pc` on Linux, `JSRF-PC.exe` on Windows), and
nothing in your home folder or `%LOCALAPPDATA%`. The launcher makes it the
first time it starts. In b2.2 and before, this was portable mode, which a
folder named `portable` turned on.

## What is in it

| In `data` | What it is |
|---|---|
| `data/settings.cfg` | The settings, with `developer.cfg`, `remap.cfg` and `start-states.txt` beside them |
| `data/saves/` | The saves (`UDATA` and `TDATA`): back this folder up to keep your progress |
| `data/logs/` | `last-run.log` and the five logs before it |
| `data/cache/` | The game's cache: safe to delete |
| `data/games/` | Disc images, unpacked |
| `data/recordings/` | Developer > Playback's recordings |

The first time a game is played, the launcher copies the saves it finds in the
game folder (`UDATA` and `TDATA`) into `data/saves` (`data\saves` on Windows).
The originals stay where they were.

## Coming from b2.2 or before

- **With a `portable` folder:** The launcher renames it to `data`, and a game
  unpacked into `portable/games` is found again in `data/games`. If the folder
  can't be renamed, the launcher says why and changes nothing.
- **Without one:** The launcher copies what b2.2 kept in your home folder into
  the new `data` folder, once: the settings and the game they remember,
  `developer.cfg`, `remap.cfg`, `start-states.txt`, and `recordings`. On Linux
  they come from `~/.config/jsrf-pc` (or `$XDG_CONFIG_HOME/jsrf-pc`), and on
  Windows from `%LOCALAPPDATA%\JSRF-PC`. Nothing there is changed or deleted.
  The cache and the logs stay behind, and a disc image unpacked there stays
  where it is and is played from there.
- **With both a `data` and a `portable` folder:** The launcher can't tell which
  one has your progress, so it says so and starts nothing. Move the one you
  don't play with out of the launcher's folder.

**Going back to b2.2** in the same folder: b2.2 doesn't know the `data`
folder, so it uses your home folder as it did before, with its own settings and
the saves in the game folder. Nothing is lost, but it doesn't see the progress
made since.

## Moving and updating

Keep the game in the launcher's folder too, and the whole folder can be moved,
to an SD card or a USB stick for instance, even under another drive letter on
Windows. A game folder inside it is remembered relative to it, and a disc image
is unpacked into `data/games`.

To update, extract the new version over this folder, or move `data` into the
new one. Deleting `data` deletes your saves with it.

On Linux, `--install` adds JSRF-PC to your applications menu, pointing at this
folder. After moving the folder, run `--install` again.

## Limitations

- **It must be writable:** If the `data` folder can't be written, or the
  launcher's folder can't when `data` has to be made or renamed, the launcher
  says so and stops, rather than keeping your saves somewhere else. Under
  `Program Files` on Windows, for instance, move JSRF-PC to a folder of your
  own.
- **Saves are copied in once:** Only the first time. Progress made since stays
  in `data`.
- **Only a game inside the folder moves with it:** A game anywhere else is
  remembered by its full path, and if that changes (another drive letter, say),
  the game asks for it again.
- **Some small files stay outside:** Graphics drivers, sound, and the folder
  picker keep their own small files in your home folder or user profile, as
  they do for any program. On Linux, so does the menu entry `--install` adds.
