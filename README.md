<p align="center">
  <img src="docs/images/banner.png" width="640" alt="Jet Set Radio Future Unleashed">
</p>

# JSRF Unleashed

**Jet Set Radio Future, running natively on Mac, Windows, Linux, Android, iPhone and iPad.** Not an emulator — the
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

---

## Download

| Platform | Download |
|---|---|
| macOS 12 or later, Apple Silicon (M1 or later) | [JSRF-Unleashed-macOS-arm64.zip](https://github.com/JuMatt/JetSetRadioFuture-Unleashed/releases/latest/download/JSRF-Unleashed-macOS-arm64.zip) |
| Linux, x86_64 (glibc 2.34 or later: Ubuntu 22.04, Debian 12, Fedora 36 and newer) | [JSRF-Unleashed-linux-x86_64.tar.gz](https://github.com/JuMatt/JetSetRadioFuture-Unleashed/releases/latest/download/JSRF-Unleashed-linux-x86_64.tar.gz) — less tested than the Mac build |
| iPhone and iPad, iOS 15 or later (iPhone XS or newer recommended) | [JSRF-Unleashed-iOS.ipa](https://github.com/JuMatt/JetSetRadioFuture-Unleashed/releases/latest/download/JSRF-Unleashed-iOS.ipa) — less tested on iPads |
| Windows 10 or 11, 64-bit | [JSRF-Unleashed-windows-x86_64.zip](https://github.com/JuMatt/JetSetRadioFuture-Unleashed/releases/latest/download/JSRF-Unleashed-windows-x86_64.zip) — tested under Wine |
| Android 10 or later, 64-bit, OpenGL ES 3.0 | [JSRF-Unleashed-android-arm64.apk](https://github.com/JuMatt/JetSetRadioFuture-Unleashed/releases/latest/download/JSRF-Unleashed-android-arm64.apk) — tested in the Android emulator |

Every version is on the [Releases](https://github.com/JuMatt/JetSetRadioFuture-Unleashed/releases) page; what changed is in the [changelog](CHANGELOG.md).

### Installing on macOS

1. Unzip, and move **JSRF Unleashed** to Applications (or wherever you like).
2. Open it. The app is not notarised by Apple — that needs a paid developer
   account — so the first time, macOS says it cannot check it. Click
   **Done**, open **System Settings › Privacy & Security**, find
   *"JSRF Unleashed" was blocked* and click **Open Anyway**. (On macOS 13 or
   earlier, right-click the app and choose **Open** instead.) This is needed
   once.
3. It asks where your game is: pick the folder that contains `default.xbe`,
   or a disc image (`.iso`), which it unpacks once by itself — nothing else to
   install. You can also drop either onto the app. It remembers the choice,
   and if the files are not the right ones (another build of the game,
   another game's disc, a zip), it says what they are.

Saves are kept in the game folder, in `UDATA` (where an Xbox keeps them), so
copy that folder to back them up, and keep it if you replace the game files.
Each run writes `last-run.log` into the game folder (with a few older ones in
`logs/`) — attach it when you report a bug.

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
years has. The **Video** menu at the top of the window sets the resolution,
from 1× to 4× the Xbox's 640×480 (2× by default; the game restarts to apply
it), widescreen 16:9 and full screen; the first two are remembered. The same
are on **F9**, **F10** and **F11** (or Alt+Enter), which also work in full
screen, where the menu is hidden.
Controllers work through SDL: Xbox, PlayStation, Switch Pro and many more.
Saves are kept in the game folder, in `UDATA`, as on the Mac. The
`README.txt` inside lists the options and where the settings and logs
(`last-run.log`) are kept.

On a Mac, the Windows build also runs in [CrossOver](https://www.codeweavers.com/crossover)
(a Windows 10 64-bit bottle, then **Run Command…** on JSRF Unleashed.exe),
but the Mac build is the better choice there.

### Installing on iPhone or iPad

The app is not on the App Store, so it is installed with **AltStore**, which
signs it with your own Apple ID. The steps below use a Mac (AltServer also
runs on Windows: see [altstore.io](https://altstore.io)).
[SideStore](https://sidestore.io) and [Sideloadly](https://sideloadly.io) work
too, with the same source or the `.ipa` from the release.

**1. Install AltStore** (once)

1. Download [AltServer for macOS](https://cdn.altstore.io/file/altstore/altserver.zip),
   unzip it, move **AltServer** to Applications and open it. It lives in the
   menu bar.
2. Plug the iPhone into the Mac, unlock it, and tap **Trust** if it asks.
3. In Finder, select the iPhone and tick **Show this iPhone when on Wi-Fi**,
   so AltStore can refresh apps later without the cable.
4. Click the AltServer icon in the menu bar › **Install AltStore** › your
   iPhone, and sign in with your Apple ID. If it asks to install a Mail
   plug-in, follow its instructions.
5. On the iPhone: **Settings › General › VPN & Device Management** › your
   Apple ID › **Trust**.
6. **Settings › Privacy & Security › Developer Mode** (iOS 16 or later): turn
   it on and let the phone restart.

**2. Install JSRF Unleashed**

7. Open AltStore › **Sources** › **+**, and paste:

   ```
   https://raw.githubusercontent.com/JuMatt/JetSetRadioFuture-Unleashed/main/altstore/source.json
   ```

8. **Browse** › **JSRF Unleashed** › **FREE**. New versions show up there as
   updates.

**3. Add your game**

9. Open JSRF Unleashed once. It creates its folder and shows where the game
   goes.
10. Copy the game into **On My iPhone › JSRF Unleashed**: this folder, and
    nowhere else. Copy either the disc image (`.iso`) or the extracted game
    folder (the one with `default.xbe` in it; see [Your game files](#your-game-files)):
    - from the Mac: Finder › your iPhone › **Files** tab, and drag it onto
      **JSRF Unleashed**;
    - on the iPhone: in the **Files** app, from iCloud Drive, Google Drive or
      wherever it is.
11. Back in the app: for an `.iso`, tap **Unpack** (once, a minute or two; it
    then offers to delete the image). Then the game starts.

> **Updates:** when a new version is out, AltStore shows it in **My Apps**:
> tap **Update** (with AltServer reachable, as for a refresh). It installs
> over the app and keeps the game and your saves; deleting the app first
> deletes them.
>
> **Free Apple ID:** the app stops opening after **7 days**. Refresh it in
> AltStore › **My Apps** › **Refresh All**, with the Mac on the same Wi-Fi and
> AltServer running. A free Apple ID also allows only 3 sideloaded apps at a
> time, AltStore included.
>
> **In the EU**, use this AltStore (AltStore Classic), not AltStore PAL: PAL
> only installs apps notarised by Apple.

A controller is the way to play (any one iOS supports: Xbox, PlayStation,
MFi, Backbone and the like). Without one, the app shows on-screen controls,
with Start and Back at the top. The ⚙ button has the picture (**16:9**, the
default; **Fill the screen**, the game widened to the whole of an iPhone's
screen; or the console's **4:3**), the on-screen controls and the resolution.
Saves are kept inside the game's folder (in
`UDATA`), so deleting or replacing that folder deletes them too; each run's log
is in `Logs`.

The game's memory map needs 4 GB of address space, which recent iPhones give
an app. If an older device does not, the app says so when it starts.

### Installing on Android

1. Download **JSRF-Unleashed-android-arm64.apk** on the phone and open it.
   It is not on Google Play, so Android asks you to allow installing apps
   from your browser (or file manager): allow it, then **Install**.
2. Open **JSRF Unleashed**. It shows where the game goes, and two ways to put
   it there (see [Your game files](#your-game-files)):
   - **Choose the disc image (.iso)**: pick your image, wherever it is; the
     app unpacks it once into its own folder (about 3 GB of free space);
   - or copy the extracted game folder (the one with `default.xbe` and `Media`
     in it) from a computer over USB, named `game`, into
     `Android/data/com.jumatt.jsrfunleashed/files/`, then tap
     **I copied the folder: start**.
3. The game starts, and from then on it starts straight away.

A controller is the way to play: Xbox, PlayStation, Switch Pro and others,
over Bluetooth or USB. Without one, the app shows on-screen controls, as on
iPhone, with Start and Back at the top; Android's Back is the Xbox Back
button. The ⚙ button has widescreen, the on-screen controls and the
resolution (used from the next start). Saves are kept in the game folder (in
`UDATA`), so **uninstalling the app deletes them** with the game: copy `UDATA`
somewhere first. Each run's log is
`Android/data/com.jumatt.jsrfunleashed/files/Logs/last-run.log`.

It needs Android 10 or later on a 64-bit phone or tablet with OpenGL ES 3.0
(any from the last several years). So far it has been run in the Android
emulator only — unpacking a disc image, boot, title, attract demo, the
on-screen controls — not on a phone yet: reports are welcome.

### Installing on Linux

Extract the archive anywhere and run `./jsrf-unleashed`. The first time, it
asks for the folder that contains `default.xbe`, or the one your disc image
(`.iso`) is in (with a folder picker if `zenity` or `kdialog` is installed,
otherwise in the terminal), and remembers it; an image is unpacked once, by
the game itself. `./jsrf-unleashed /path/to/game.iso` works too. `./jsrf-unleashed --install` adds it to your applications menu. It needs
OpenGL 3.3 drivers and an X11 or Wayland desktop; SDL2 is bundled. The
`README.txt` inside lists the options (`--scale`, `--fullscreen`, `--choose`)
and where settings, saves and logs are kept.

On Linux the keyboard layout is the same as below; **F11** (or Alt+Return)
toggles full screen, **F10** widescreen, and **F9** sets the resolution (1× to
4×; the game restarts to apply it).

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

**Controller:** any pad macOS recognises — Xbox, DualSense, DualShock 4 and
others, over Bluetooth or USB. On Windows, Linux and Android, any pad SDL
knows.

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

**Controls › Keyboard…** in the menu bar shows the same list.

**Video menu:**
- **Resolution** — the internal resolution, 1× to 4× (2× by default). The
  game restarts to apply it.
- **Renderer** — Metal (the default) or OpenGL. The game restarts to apply it.
- **Full Screen** — Ctrl ⌘ F.
- **Widescreen (16:9)** — ⇧ ⌘ W. The game is shown at 16:9 with a wider
  field of view, not stretched; the HUD and text keep their shape. The title
  screen stays 4:3.

## Status

Playable, early, and not yet played from start to finish.

| | |
|---|---|
| Title, menus, new game, saving and loading | works |
| New Game through the tutorial, the DJ K scene and Dogenzaka Hill | works — the part played and tested most |
| Skating, grinding, tricks, graffiti, the pause map | works |
| Music, sound effects, voices | works |
| Widescreen 16:9 (and the whole screen, on iPhone) | works in game; the title screen stays 4:3 |
| Linux build | boots, menus, starting a new game; far less played than the Mac build |
| Windows build | tested under Wine |
| iPhone / iPad build | plays on iPhone; less tested on iPads |
| Android build | boots to the title and the attract demo in the Android emulator; not yet tried on a phone |
| Later areas | little tested — expect bugs |
| Known issues | an occasional "disc error" screen during a load |

Found a bug? [Open an issue](https://github.com/JuMatt/JetSetRadioFuture-Unleashed/issues)
with your `last-run.log` and what you were doing.

## Why static recompilation

The game's x86 machine code is translated, function by function, into C, which
is then compiled for whatever CPU you are on. There is no interpreter and no
JIT at runtime. Two things follow from that:

- **It is fast.** The game's logic runs as native code, at native speed.
- **It can ship where JITs cannot.** iOS forbids runtime code generation, which
  is why there is no real Xbox emulator on an iPhone. A statically recompiled
  binary has nothing to generate — which is why there can be an iPhone build
  at all.

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
launcher/package_mac.sh build/jsrf_recomp             # macOS: the app, zipped
```

On Linux, [`launcher/linux/package.sh`](launcher/linux/package.sh) packages the
build and records how the release one is made. The Windows build is
cross-compiled on Linux with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw)
and the toolchain file
[`launcher/windows/llvm-mingw.cmake`](launcher/windows/llvm-mingw.cmake);
[`launcher/windows/package.sh`](launcher/windows/package.sh) packages it and
records the rest (SDL2, libepoxy). For iPhone and iPad, see
[docs/ios.md](docs/ios.md) (Xcode's iOS SDK, the same CMake project,
[`launcher/ios/package_ios.sh`](launcher/ios/package_ios.sh) for the `.ipa`); for
Android, [launcher/android/README.md](launcher/android/README.md) (the NDK, SDL
2.32's source, [`launcher/android/build_apk.sh`](launcher/android/build_apk.sh)
for the `.apk`, no Gradle).
How the Mac and iPhone renderer works is in
[docs/metal-backend.md](docs/metal-backend.md). The version comes from the git
tag the tree is at.

### Linux

These steps build the game for the Linux PC you run them on, from your own
`default.xbe`. They have been tried on a fresh Ubuntu 24.04; the dependency
script also knows Debian, Arch and Fedora.

1. **Get the source and the build tools.** You need git and Python 3.10 or
   later; on Debian and Ubuntu, `sudo apt install git python3-venv` gets both.
   Then:

   ```sh
   git clone https://github.com/JuMatt/JetSetRadioFuture-Unleashed.git
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

- [xboxrecomp](https://github.com/sp00nznet/xboxrecomp) — the static
  recompilation toolkit this is a port on top of. Its documentation is kept at
  [docs/xboxrecomp-toolkit.md](docs/xboxrecomp-toolkit.md).
- [xemu](https://xemu.app) — the NV2A and MCPX APU hardware models, without
  which none of the graphics or audio would exist.
- The title lettering in the banner is set in
  [Nise JSRF](https://www.dafont.com/nisejsrf.font), a free font by Yoyo.
- Smilebit and SEGA, who made the game.
