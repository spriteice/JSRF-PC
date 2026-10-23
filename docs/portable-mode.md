# Portable mode

Portable mode keeps the settings, saves, logs, and cache in one folder next to
the launcher, and nothing in your home folder or `%LOCALAPPDATA%`.

## Turning it on

1. Make a folder named `portable` next to the launcher: `jsrf-pc` on Linux,
   `JSRF-PC.exe` on Windows.
2. Start the game. It asks where the game is again, then copies the saves it
   finds in the game folder (`UDATA` and `TDATA`) into `portable/saves`
   (`portable\saves` on Windows). The originals stay where they were.
3. Back up `portable/saves` to keep your progress.

## Moving and updating

Keep the game in the launcher's folder too, and the whole folder can be moved,
to an SD card or a USB stick for instance, even under another drive letter on
Windows. A game folder inside it is remembered relative to it, and a disc image
is unpacked into `portable/games`.

To update, extract the new version over this folder, or move `portable` into
the new one. Deleting `portable` deletes your saves with it.

## Limitations

- **It must be writable:** If the `portable` folder is not, the game says so
  and stops, rather than falling back to your home folder or `%LOCALAPPDATA%`.
- **Saves are copied in once:** Only the first time. Progress made in portable
  mode stays in `portable`. Remove the folder and the game goes back to the
  older saves in the game folder.
- **Only a game inside the folder moves with it:** A game anywhere else is
  remembered by its full path, and if that changes (another drive letter, say),
  the game asks for it again.
- **Some small files stay outside:** Graphics drivers, sound, and the folder
  picker keep their own small files in your home folder or user profile, as
  they do for any program.
- **No menu entry on Linux:** `--install` does not work, as the applications
  menu is in your home folder.
