# Building on Linux

How to build JSRF-PC from source, from your own `default.xbe`.

The generated code, hundreds of thousands of lines of C translated from the
game's program, is not in this repository and never will be: it is derived
from SEGA's binary. You make it on your own machine with the pipeline in
[`tools/recomp`](../tools/recomp). The steps below run it with the options this
game needs: `--exclude-manual port/recomp_manual.c` tells it about the
hand-written overrides in [`port/`](../port), and `--back-edge-yield` lets a
guest thread that is busy in a loop hand the CPU to the others. The
[getting-started guide](GETTING_STARTED.md) explains the pipeline in more
depth; it is written for the toolkit in general.

## Steps

These build the game for the Linux PC you run them on. They have been tried on
a fresh Ubuntu 24.04; the dependency script also knows Debian, Arch, and Fedora.

1. **Get the source and the build tools.** You need git and Python 3.10 or
   later; on Debian and Ubuntu, `sudo apt install git python3-venv` gets both.
   Then:

   ```sh
   git clone https://github.com/spriteice/JSRF-PC.git
   cd JSRF-PC
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
   tar xzf JSRF-PC-linux-x86_64.tar.gz -C ~
   ~/JSRF-PC-linux-x86_64/jsrf-pc
   ```

   From here it works like the download: see
   [Installing on Linux](../README.md#linux).

## A package for other machines

A package made this way uses your system's SDL2, so it is for the machine that
built it. To make one that runs on other machines too, such as a Steam Deck or
another SteamOS handheld, follow the release recipe at the top of
[`launcher/linux/package.sh`](../launcher/linux/package.sh).

## Windows

The Windows build is cross-compiled on Linux with
[llvm-mingw](https://github.com/mstorsjo/llvm-mingw) and the toolchain file
[`launcher/windows/llvm-mingw.cmake`](../launcher/windows/llvm-mingw.cmake).
[`launcher/windows/package.sh`](../launcher/windows/package.sh) packages it,
and the top of that file records the rest (SDL2, libepoxy).

## The version

The version comes from the release tag the source is at, such as `b1`, and
[versioning](versioning.md) explains it. A clone has the tags. A copy without
its git history, such as a downloaded zip, says `dev` unless it is given one
with `-DJSRF_VERSION=`.
