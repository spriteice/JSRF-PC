<p align="center">
  <img src="docs/images/banner.png" width="640" alt="Jet Set Radio Future Unleashed">
</p>

# JSRF Unleashed

**Jet Set Radio Future, running natively on Linux and Windows.** Not an emulator — the
game's own code, statically recompiled into C and built as a native
application.

> **This project is about one game.** It is not an Xbox emulator and it will
> not run other Xbox titles. Everything here — the graphics translation, the
> audio path, the file layout, the per-function fixes — is aimed at getting
> *Jet Set Radio Future* right. If you want to run Xbox games in general, you
> want [xemu](https://xemu.app); if you want to port a different Xbox title,
> you want the [toolkit this is built on](docs/xboxrecomp-toolkit.md).

**You need your own copy of the game.** The download is the engine only — no
game files, no textures, no music, no video. It plays the files from a disc
you own.

### This fork

Thanks to [JuMatt](https://github.com/JuMatt) for the original port, which
this fork builds on.

- **Portable mode:** a `portable` folder next to the launcher keeps the
  settings, saves, logs and cache inside it. _(Linux, Windows)_
- **In-game menu:** F1, or Back on a controller. _(Linux, Windows)_
- **Free camera:** F8 gives the right stick to the camera, which turns around
  the player; the L trigger puts it back behind. _(Linux, Windows)_
- Various bug fixes.

---

## Download

This fork's only release so far is a **release candidate** of the coming
0.5.3a beta: a test build, so expect bugs.

| Platform | Download |
|---|---|
| Linux, x86_64 (glibc 2.34 or later: SteamOS, Ubuntu 22.04, Debian 12, Fedora 36 and newer) | [JSRF-0.5.3a-rc-linux-x86_64.tar.gz](https://github.com/spriteice/JetSetRadioFuture-Unleashed/releases/download/v0.5.3a-rc/JSRF-0.5.3a-rc-linux-x86_64.tar.gz) — the main target, and the most tested |
| Windows 10 or 11, 64-bit | [JSRF-0.5.3a-rc-windows-x86_64.zip](https://github.com/spriteice/JetSetRadioFuture-Unleashed/releases/download/v0.5.3a-rc/JSRF-0.5.3a-rc-windows-x86_64.zip) — starts and plays, but largely untested |

Every version is on the [Releases](https://github.com/spriteice/JetSetRadioFuture-Unleashed/releases) page, with what changed in it; the [changelog](CHANGELOG.md) has the upstream versions this fork builds on.

### Installing on Linux

1. Extract the archive anywhere.
2. Run `./jsrf-unleashed`. The first time, it asks for the folder that
   contains `default.xbe`, or the one your disc image (`.iso`) is in (with a
   folder picker if `zenity` or `kdialog` is installed, otherwise in the
   terminal), and remembers it; an image is unpacked once, by the game
   itself. `./jsrf-unleashed /path/to/game.iso` works too.
3. `./jsrf-unleashed --install` adds it to your applications menu, if you
   like.

It needs OpenGL 3.3 drivers and an X11 or Wayland desktop; SDL2 is bundled.
Saves are kept in the game folder, in `UDATA` (where an Xbox keeps them), so
copy that folder to back them up, and keep it if you replace the game files.
The `README.txt` inside lists the options (`--scale`, `--fullscreen`,
`--choose`) and where the settings and logs (`last-run.log`) are kept.

**Portable mode** keeps the settings, saves, logs and cache in one folder
next to the launcher, and nothing in your home folder:

1. Make a folder named `portable` next to `jsrf-unleashed`.
2. Run `./jsrf-unleashed`. It asks where the game is again, then copies the
   saves it finds in the game folder (`UDATA` and `TDATA`) into
   `portable/saves`; the originals stay where they were.
3. Back up `portable/saves` to keep your progress.

Keep the game in the extracted folder too, and the whole folder can be moved,
to an SD card for instance: a game folder inside it is remembered relative to it,
and a disc image is unpacked into `portable/games`. To update, extract the
new version over this folder, or move `portable` into the new one: deleting
`portable` deletes your saves with it.

### Installing on Windows

1. Unzip the archive anywhere, and keep the `engine` folder beside
   **JSRF Unleashed.exe**.
2. Run **JSRF Unleashed.exe**. It is not signed (that costs money every
   year), so the first time Windows may say it protected your PC: click
   **More info**, then **Run anyway**.
3. It asks where your game is: pick the folder that contains `default.xbe`,
   or the folder with your disc image (`.iso`) in it, which it offers to
   unpack once into `%LOCALAPPDATA%\JSRF Unleashed`. It remembers the choice.

It needs graphics drivers with OpenGL 3.3, which any GPU from the last ten
years has. Saves are kept in the game folder, in `UDATA` (where an Xbox keeps
them), so copy that folder to back them up, and keep it if you replace the
game files. The `README.txt` inside lists the options and where the settings
and logs (`last-run.log`) are kept.

**Portable mode** keeps the settings, saves, logs and cache in one folder
next to the launcher, and nothing in `%LOCALAPPDATA%`:

1. Make a folder named `portable` next to **JSRF Unleashed.exe**.
2. Run **JSRF Unleashed.exe**. It asks where the game is again, then copies
   the saves it finds in the game folder (`UDATA` and `TDATA`) into
   `portable\saves`; the originals stay where they were.
3. Back up `portable\saves` to keep your progress.

Keep the game in the unzipped folder too, and the whole folder can be moved,
to a USB stick for instance, even under another drive letter: a game folder inside
it is remembered relative to it, and a disc image is unpacked into
`portable\games`. To update, unzip the new version over this folder, or move
`portable` into the new one: deleting `portable` deletes your saves with it.

### In-game menu (ImGui)

The in-game menu is a [Dear ImGui](https://github.com/ocornut/imgui) menu bar
drawn over the game. **F1**, or Back on a controller, opens and closes it. It
works with a controller, the keyboard or the mouse; B or Esc goes back a step.
The game keeps running while it is open, but your input goes to the menu.

- **File:** About (the version and system details) and Exit.
- **Graphics:** full screen, widescreen 16:9, the render scale (1× to 4×) and
  anisotropic filtering (Off to 16×).
- **Controller:** the free camera, and its inversion, sensitivity and
  deadzone.

Everything is remembered. Exit and a new render scale ask
first, as anything since your last save is lost.

### Portable limitations

- The `portable` folder has to be writable. If it is not, the game says so
  and stops, rather than falling back to your home folder or
  `%LOCALAPPDATA%`.
- Saves are copied in from the game folder only the first time. Progress
  made in portable mode stays in `portable`: remove the folder and the game
  goes back to the older saves in the game folder.
- Only a game kept inside the launcher's folder moves with it. A game
  anywhere else is remembered by its full path, and if that changes (another
  drive letter, say), the game asks for it again.
- Graphics drivers, sound and the folder picker still keep their own small
  files in your home folder or user profile, as they do for any program.
- On Linux, `--install` does not work, as the applications menu is in your
  home folder.

### Your game files

You need the **standalone USA (NTSC-U) disc** of *Jet Set Radio Future* — the
game code inside the app is translated from that disc's executable, address
for address, so other builds of the game will not work: the European and
Japanese releases, and the **Sega GT 2002 + JSRF combo disc** (the one that
came with the Xbox), whose JSRF is a different, later build. Every app checks
the files before starting and says what they are if they are not the right
ones. Make an image of your own disc (`.iso`), or extract it: you want the
folder with `default.xbe`, `Media/` and the rest.

## Playing

**Controller:** any pad SDL knows — Xbox, PlayStation, Switch Pro, Steam Deck
and many more, over Bluetooth or USB. The buttons are the Xbox's, with the
bumpers as Black and White. Back opens the in-game menu, so the game's own
Back is Esc on the keyboard.

**Keyboard.** Keys go by position, so `W A S D` on a QWERTY keyboard is
`Z Q S D` on an AZERTY one:

| Key | Does | Key | Does |
|---|---|---|---|
| W A S D | move (left stick), menus | Arrow keys | camera (right stick) |
| Space or J | A — jump | K | B |
| H | X | L | Y |
| E | R trigger — talk, spray | Q | L trigger |
| R | Black | F | White |
| Return | Start | Esc | Back |
| C / V | left / right stick click | | |

**Settings keys**, in a window or full screen:

| Key | Does |
|---|---|
| F1, or Back on a controller | the in-game menu: **File** (About, Exit), **Graphics** (the next three, and anisotropic filtering) and **Controller** (the free camera and its settings) |
| F11 or Alt+Return | full screen. Remembered. |
| F10 | widescreen 16:9: a wider field of view, not a stretch; the HUD and text keep their shape, and the title screen stays 4:3. Remembered. |
| F9 | resolution: 1× to 4× the Xbox's 640×480 (2× by default). Remembered. The game restarts to apply it, so save first. |
| F8 | the free camera, below. Remembered. |

**Free camera.** F8 gives the right stick to the camera, which turns around
the player, and forward is wherever it looks. The L trigger puts it back
behind the player, as it does the game's camera. It also turns the graffiti
camera off, like the option in the Jet Set Radio re-release: spraying no
longer turns the camera to the wall. Inversion, sensitivity and
the deadzone are in the in-game menu's **Controller** menu, and in the
settings file (see the `README.txt` in the download). With no keyboard, as in
Steam's Game Mode, turn it on from that menu.

## Status

Playable, early, and not yet played from start to finish.

| | |
|---|---|
| Title, menus, new game, saving and loading | works |
| New Game through the tutorial, the DJ K scene and Dogenzaka Hill | works — the part played and tested most |
| Skating, grinding, tricks, graffiti, the pause map | works |
| Music, sound effects, voices | works |
| Widescreen 16:9 | the game view mostly works; some menus and effects are still 4:3 |
| Linux build | the main target, and the most tested |
| Windows build | starts and plays, but largely untested |
| Later areas | little tested — expect bugs |
| Known issues | an occasional "disc error" screen during a load |

Found a bug? [Open an issue](https://github.com/spriteice/JetSetRadioFuture-Unleashed/issues)
with your `last-run.log` and what you were doing.

## Why static recompilation

The game's x86 machine code is translated, function by function, into C, which
is then compiled for whatever CPU you are on. There is no interpreter and no
JIT at runtime, so **it is fast**: the game's logic runs as native code, at
native speed.

What still has to be emulated is the *hardware*: the NV2A graphics processor,
the MCPX audio processor, and the Xbox kernel the game calls into. Those parts
come from the toolkit and from [xemu](https://xemu.app), whose NV2A and APU
work this stands on.

## Building from source

The generated code — hundreds of thousands of lines of C translated from the
game's executable — is not in this repository and will not be: it is derived
from SEGA's binary. You produce it on your own machine, from your own
`default.xbe`, with the pipeline in [`tools/recomp`](tools/recomp) — the
[getting-started guide](docs/GETTING_STARTED.md) walks through it (it is written
for the toolkit in general), `--exclude-manual port/recomp_manual.c` tells
it about the hand-written overrides in [`port/`](port), and `--back-edge-yield`
lets a guest thread that is busy in a loop hand the CPU to the others. Put the
result in `port/recomp/gen`, then:

```sh
cmake -S port -B build -DCMAKE_BUILD_TYPE=Release    # on Linux add -DNV2A_GL_CONTEXT=sdl
cmake --build build
```

On Linux, [`launcher/linux/package.sh`](launcher/linux/package.sh) packages the
build and records how the release one is made. The Windows build is
cross-compiled on Linux with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw)
and the toolchain file
[`launcher/windows/llvm-mingw.cmake`](launcher/windows/llvm-mingw.cmake);
[`launcher/windows/package.sh`](launcher/windows/package.sh) packages it and
records the rest (SDL2, libepoxy). The version comes from the git tag the
tree is at.

### Linux

These steps build the game for the Linux PC you run them on, from your own
`default.xbe`. They have been tried on a fresh Ubuntu 24.04; the dependency
script also knows Debian, Arch and Fedora.

1. **Get the source and the build tools.** You need git and Python 3.10 or
   later; on Debian and Ubuntu, `sudo apt install git python3-venv` gets both.
   Then:

   ```sh
   git clone -b TESTING https://github.com/spriteice/JetSetRadioFuture-Unleashed.git
   cd JetSetRadioFuture-Unleashed
   bash tools/linux/install_deps.sh
   ```

   The script asks for your password, because it installs packages with
   `sudo`.

2. **Set up Python.** The tools need one package, `capstone`:

   ```sh
   python3 -m venv .venv
   . .venv/bin/activate
   pip install capstone
   ```

   If you open a new terminal later, run `. .venv/bin/activate` again first.

3. **Add your game file.** Copy `default.xbe` from your disc into a folder
   called `game_files`, or take it straight from a disc image:

   ```sh
   mkdir -p game_files
   python3 -m tools.xiso get /path/to/game.iso default.xbe -o game_files/
   ```

4. **Translate the game's code to C.** The result goes in `port/recomp/gen`,
   which git ignores: it is made from the game, so it stays on your machine.

   ```sh
   python3 -m tools.xbe_parser game_files/default.xbe --json game_files/default_analysis.json
   python3 -m tools.disasm game_files/default.xbe
   python3 -m tools.func_id game_files/default.xbe
   python3 -m tools.abi_analysis game_files/default.xbe
   python3 -m tools.recomp game_files/default.xbe --all --split 1000 --back-edge-yield \
       --exclude-manual port/recomp_manual.c --gen-dir port/recomp/gen
   ```

   The last command should report `0 failed`.

5. **Build the engine.**

   ```sh
   cmake -S port -B build -DCMAKE_BUILD_TYPE=Release -DNV2A_GL_CONTEXT=sdl
   cmake --build build -j"$(nproc)"
   ```

6. **Package it and play.**

   ```sh
   launcher/linux/package.sh build/jsrf_recomp "$(pkg-config --variable=libdir sdl2)/libSDL2-2.0.so.0"
   tar xzf JSRF-Unleashed-linux-x86_64.tar.gz -C ~
   ~/JSRF-Unleashed-linux-x86_64/jsrf-unleashed
   ```

   From here it works like the download: see
   [Installing on Linux](#installing-on-linux).

A package made this way uses your system's SDL2, so it is for the machine that
built it. To make one that runs on other machines too, such as a Steam Deck or
another SteamOS handheld, follow the release recipe at the top of
[`launcher/linux/package.sh`](launcher/linux/package.sh).

## Legality

The releases contain the engine: the runtime (the kernel layer and the
graphics and audio hardware models) and the game's own code, machine-translated
to native code. They contain **no game data** — no textures, models, music,
video or text — and do nothing without the files from your own copy of the
game. This repository holds no game code at all; that is generated on your
machine from your own disc. It is the same approach as
[Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) and
[Zelda 64: Recompiled](https://github.com/Zelda64Recomp/Zelda64Recomp).

*Jet Set Radio Future* is © SEGA. This is an unofficial fan project, not
affiliated with or endorsed by SEGA. If you represent a rights holder and have
a concern, please open an issue.

## Licence

MIT for this project's own code; the MCPX audio-processor code extracted from
xemu is LGPL-2.1-or-later. See [LICENSE](LICENSE) and [NOTICE](NOTICE).

## Credits

- [JuMatt](https://github.com/JuMatt) — the original port,
  [JSRF Unleashed](https://github.com/JuMatt/JetSetRadioFuture-Unleashed),
  which this fork builds on.
- [xboxrecomp](https://github.com/sp00nznet/xboxrecomp) — the static
  recompilation toolkit this is a port on top of. Its documentation is kept at
  [docs/xboxrecomp-toolkit.md](docs/xboxrecomp-toolkit.md).
- [xemu](https://xemu.app) — the NV2A and MCPX APU hardware models, without
  which none of the graphics or audio would exist.
- The title lettering in the banner is set in
  [Nise JSRF](https://www.dafont.com/nisejsrf.font), a free font by Yoyo.
- Smilebit and SEGA, who made the game.
