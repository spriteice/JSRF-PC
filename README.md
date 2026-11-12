# JSRF-PC

JSRF-PC is built on
[JSRF Unleashed](https://github.com/JuMatt/JetSetRadioFuture-Unleashed) by
[JuMatt](https://github.com/JuMatt). It aims to add various features and bug
fixes for Linux and Windows, the only platforms it supports, focusing on
compatibility and performance.

> ⚠️ **Written by AI:** All of JSRF-PC's additions to the upstream code are
> written by AI, taking care to test features, fix bugs, and follow the
> project's structure.

**Jet Set Radio Future, statically recompiled to run natively on Linux and
Windows.** The game's own code is translated into C and built as a native
program.

**You need your own, legally acquired copy of the game.** This repository
contains no game files.

## Features

- **In-game menu:** F1, or Back on a controller, for the display, graphics,
  audio, controller, and developer settings.
- **Presets:** Xbox mode, the game as the Xbox runs it; JSRF-PC default; or
  Max quality, with the graphics at their most. File > Presets.
- **Resolution scaling:** 1× to 6×. 3× by default.
- **True widescreen:** 16:10, 16:9, 21:9, or Match screen. The picture is
  drawn as wide as the shape, so neither axis is stretched, and the HUD and
  menus are laid out across the screen.
- **Draw distance:** Crowds, spray cans, props, traffic, and the police and
  their vehicles drawn up to four times further, or an unlimited distance. Far
  buildings keeps the city's buildings from popping in.
- **Music info in pause:** The song playing, its title and artist, drawn in
  the pause menus using the game's font and styling. Can be toggled at any
  time.
- **Free camera:** Modern right-stick camera controls, with various settings,
  and smooth geometry tracking to keep the camera out of walls. The graffiti cam
  is turned off when the free camera is enabled.
- **Audio effects:** A targeted, discrete sound engine that mimics the original
  hardware's audio effects with minimal performance overhead: 3D audio and
  reverb.
- **Anti-aliasing:** MSAA and SMAA _(JuMatt)_, alone or together.
- **Anisotropic filtering:** Sharper textures at an angle.
- **2D upscaling:** Sharper menus, text and HUD at high render scales, drawn
  from copies of the game's art made with Super-xBR.
- **3D upscaling:** Sharper walls, ground, signs and characters close up,
  drawn from copies of the game's textures made with Super-xBR.
- **Graffiti upscaling:** Sharper graffiti tags, drawn from Super-xBR copies
  of their own.
- **Performance:** Smooth frame pacing and optimised engine/renderer on both
  Linux and Windows.
- **Portable mode:** Everything in one folder that can be moved.

## Download

The latest release is **b2**.

| Platform | File |
|---|---|
| Linux x86_64: SteamOS, Ubuntu 22.04+, and most modern distros. | [JSRF-PC-b2-linux-x86_64.tar.gz](https://github.com/spriteice/JSRF-PC/releases/download/b2/JSRF-PC-b2-linux-x86_64.tar.gz) |
| Windows 10 or 11, x86_64. | [JSRF-PC-b2-windows-x86_64.zip](https://github.com/spriteice/JSRF-PC/releases/download/b2/JSRF-PC-b2-windows-x86_64.zip) |
| Linux tools, to build the engine without downloading (optional). | [JSRF-PC-tools-linux-x86_64.tar.gz](https://github.com/spriteice/JSRF-PC/releases/download/b2/JSRF-PC-tools-linux-x86_64.tar.gz) |
| Windows tools, to build the engine without downloading (optional). | [JSRF-PC-tools-windows-x86_64.zip](https://github.com/spriteice/JSRF-PC/releases/download/b2/JSRF-PC-tools-windows-x86_64.zip) |

See the [changelog](CHANGELOG-JSRF-PC.md) for what changed in each version,
and the [releases](https://github.com/spriteice/JSRF-PC/releases) page for
every download.

## Installing

### Linux

1. Extract the archive anywhere.
2. Run `./jsrf-pc`. The first time, it builds the engine from your game
   ([Building the engine on the first start](#building-the-engine-on-the-first-start),
   below): pick the folder with `default.xbe` in it, or your disc image (an
   XISO or a full disc image, usually `.iso`), which is unpacked once. It
   remembers your choice.
3. If you like, `./jsrf-pc --install` adds it to your applications menu.

### Windows

1. Extract the archive anywhere.
2. Run `JSRF-PC.exe`. It isn't signed, so the first time Windows may say it
   protected your PC: click More info, then Run anyway.
3. The first time, it builds the engine from your game
   ([Building the engine on the first start](#building-the-engine-on-the-first-start),
   below): pick the folder with `default.xbe` in it, or your disc image (an
   XISO or a full disc image, usually `.iso`), which is unpacked once. It
   remembers your choice.

### Building the engine on the first start

The download holds none of the game, so the first time it runs, the launcher
builds the engine from your own copy, once. It asks first, then downloads its
tools (Python, capstone, and Zig, about 100 MB, each checked) and builds, with
one progress bar: a few minutes. To build without downloading, extract your
system's tools archive (above) into the same folder first. Nothing is
installed: it all stays in that folder. Afterwards it offers to delete the
tools and the code made from your game, and the engine stays. A new release
extracted over the old folder builds its own engine at its first start.

### Saves, settings, and logs

- **Saves:** Kept in the game folder's `UDATA`, as on an Xbox. Copy it to back
  them up. A game played from a disc image keeps them in its unpacked copy,
  in `~/.local/share/jsrf-pc` on Linux or `%LOCALAPPDATA%\JSRF-PC` on Windows.
- **Settings and logs:** The `README.txt` in the download says where they are
  kept, and lists the launch options.
- **Portable mode:** Make a folder named `portable` next to the launcher, and
  the settings, saves, logs, and cache stay inside it. See
  [portable mode](docs/portable-mode.md).

### Your game

You need the **standalone USA (NTSC-U) disc**. No other version will work. All
game files must be legally acquired.

## Playing

Any controller SDL knows works, over USB or Bluetooth: Xbox, PlayStation,
Switch Pro, Steam Deck, and many more. The keyboard works too: W A S D to move,
the arrow keys for the camera, and Space to jump.
[Playing](docs/playing.md) has every key, the in-game menu, and the
graphics options. A controller is the recommended way to play.

## Status

The game can be completed. Please expect minor bugs while the engine is
still in beta.

- **Works:** The game can be played from start to finish with no major
  issues.
- **Known issues:** Each is described in the
  [latest release's notes](https://github.com/spriteice/JSRF-PC/releases/latest).
  - VS mode can't be started: the game doesn't see a second controller.
  - Occasional crashes from bugs not yet found.
  - Some elements half a pixel out of place.
  - Black screenshots on Windows.
  - Graffiti menu icons on some AMD graphics on Windows.

**Found a bug?** [Open an issue](https://github.com/spriteice/JSRF-PC/issues)
and attach the `last-run.log` from that run.

## How it works

The game's x86 code is translated, function by function, into C and compiled
for your CPU, so its logic runs at native speed with no interpreter or JIT.
The Xbox hardware it talks to, the NV2A graphics chip, the MCPX audio chip, and
the kernel, is still emulated, using the
[xboxrecomp toolkit](docs/xboxrecomp-toolkit.md) and [xemu](https://xemu.app)'s
hardware models.

## Building from source

The generated code is made from the game's program, so it is never in this
repository: you make it on your own machine, from your own `default.xbe`.
[Building on Linux](docs/building-on-linux.md) has every step, and how the
Windows build is cross-compiled.

## Legality

The releases contain **nothing of the game**: no game data and none of its
code. The engine, the game's code translated into a native program, is built
on your PC from your own copy at the first start, and stays there. This
repository holds no game code at all, only the tools to recompile the game's
code from your own disc.

*Jet Set Radio Future* is © SEGA. This is an unofficial fan project, not
affiliated with or endorsed by SEGA. If you represent a rights holder and have
a concern, please open an issue.

## Licence

JSRF-PC is under the GNU General Public License, version 2 or any later
version, with one additional permission: the code made from your game's
program is never under it. The code from JSRF Unleashed and xboxrecomp stays
under the MIT licence, and the MCPX audio-processor code extracted from xemu
under LGPL-2.1-or-later. The releases up to b1.4 were MIT, and stay so. See
[LICENSE](LICENSE) and [NOTICE](NOTICE).

## Credits

- [JuMatt](https://github.com/JuMatt): the original port,
  [JSRF Unleashed](https://github.com/JuMatt/JetSetRadioFuture-Unleashed),
  which JSRF-PC is built on, with the original widescreen and resolution
  scaling.
- [xboxrecomp](https://github.com/sp00nznet/xboxrecomp): the static
  recompilation toolkit this is a port on top of. Its documentation is kept at
  [docs/xboxrecomp-toolkit.md](docs/xboxrecomp-toolkit.md).
- [xemu](https://xemu.app): the NV2A and MCPX APU hardware models, without
  which none of the graphics or audio would exist.
- [Dear ImGui](https://github.com/ocornut/imgui): the library the in-game menu
  is built with.
- Smilebit and SEGA, who made the game.
