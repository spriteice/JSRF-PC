# Getting Started with Xbox Static Recompilation

> **From the xboxrecomp toolkit,** by Ned Heller (sp00nznet), written for
> the toolkit's own work, much of it on Burnout 3: Takedown, on Windows with
> Direct3D 11. JSRF-PC keeps it for how the pipeline and runtime work. Where
> it differs from how JSRF-PC is built or changed,
> [building-on-linux.md](building-on-linux.md) and
> [project-rules.md](project-rules.md) are right, and this doc is out
> of date there: say so. The generated code is never edited, and a bug is
> fixed where it starts, not stubbed out or guarded around.

This guide walks through what each step of the pipeline does, from the XBE to the generated C. In JSRF-PC, steps 2 to 5 are run with the commands in [building-on-linux.md](building-on-linux.md), step 4, which this guide's commands follow, and JSRF-PC's own build takes over after step 5.

## What You Need

- JSRF-PC builds on Linux, and cross-compiles its Windows build there: [building-on-linux.md](building-on-linux.md), steps 1 and 2, sets up what you need.
- Your own copy of the game, as a disc image or its `default.xbe`.

Optional but very helpful:
- **xemu**, an Xbox emulator, for live debugging through its GDB stub ([technical/xemu-debugging.md](technical/xemu-debugging.md))
- **Ghidra or IDA**, for reading tricky functions by hand

## Step 0: Get the Source

JSRF-PC is one repository, with the pipeline in `tools/` and the runtime in `src/`: clone it as [building-on-linux.md](building-on-linux.md), step 1, says. Steps 1 to 5 run from its top folder, with the Python environment of its step 2 active.

## Step 1: Extract the XBE

Take `default.xbe` from your disc image with the repository's own tool ([formats/disc-image.md](formats/disc-image.md)):

```bash
mkdir -p game_files
python3 -m tools.xiso get /path/to/game.iso default.xbe -o game_files/
```

You should now have `game_files/default.xbe`.

## Step 2: Parse the XBE

```bash
python3 -m tools.xbe_parser game_files/default.xbe --json game_files/default_analysis.json
```

The `--json` file is **required by Step 3** — the disassembler reads the section
layout from it. Name it `<anything>_analysis.json` and keep it next to the XBE;
Step 3 finds it automatically.

This outputs:
- **Entry point** — where execution starts (e.g., `0x001D2807`)
- **Section layout** — VA, size, and raw offset for .text, .rdata, .data, and XDK lib sections
- **Kernel imports** — which of the 147 kernel functions the game uses
- **XDK version** — helps identify which XDK libraries were linked

Nothing here is set by hand: the runtime maps the sections from the XBE itself (src/kernel/xbox_memory_layout.c).

## Step 3: Disassemble

```bash
python3 -m tools.disasm game_files/default.xbe
```

This typically takes 30-60 seconds for a 2-3 MB .text section. Output goes to `tools/disasm/output/`:

- `functions.json` — every detected function with address, size, instruction count
- `xrefs.json` — call graph (who calls whom)

Check the stats: you should find thousands of functions. A typical Xbox game has 10,000-25,000 functions including CRT and middleware.

## Step 4: Identify Library Functions

```bash
python3 -m tools.func_id game_files/default.xbe
```

This classifies functions into categories:
- **CRT** — C runtime (malloc, free, memcpy, etc.) — usually safe to leave as-is
- **RW** — RenderWare engine (if applicable) — may need manual overrides
- **XDK** — Xbox SDK library code (D3D8, DirectSound, etc.)
- **GAME** — Game-specific code — your main focus
- **STUB** — Empty/trivial functions — safe to ignore

## Step 4.5: Recover Calling Conventions

```bash
python3 -m tools.abi_analysis game_files/default.xbe
```

This works out each function's calling convention, parameter count, return type and frame shape, so Step 5 can give it an accurate C signature. Its output, `tools/abi_analysis/output/abi_functions.json`, is where Step 5 looks for it.

## Step 5: Recompile

```bash
python3 -m tools.recomp game_files/default.xbe --all --split 1000 --back-edge-yield \
    --exclude-manual port/recomp_manual.c --gen-dir port/recomp/gen
```

`--exclude-manual` leaves out the functions written by hand in `port/`, and `--back-edge-yield` lets a guest thread that is busy in a loop hand the CPU to the others. This is the big one: it can take several minutes, and should report `0 failed`. Output:

```
port/recomp/gen/
├── recomp_0000.c          # Functions 0-999
├── recomp_0001.c          # Functions 1000-1999
├── ...                    # More splits
├── recomp_dispatch.c      # ICALL dispatch table (binary search)
├── recomp_funcs.h         # Forward declarations
└── recomp_stubs.c         # Stubs for unresolvable targets
```

`port/recomp/gen` is ignored by git: the generated code is made from the game, so it stays on your machine.

## After Step 5

JSRF-PC's own build takes it from here: [building-on-linux.md](building-on-linux.md), steps 5 and 6, and its Windows section. A crash or a wrong result is found as [pipeline/06-debugging.md](pipeline/06-debugging.md) says, and fixed where it starts ([project-rules.md](project-rules.md), Where code goes): the generated code is never edited, and nothing is stubbed to get past a crash.

For how the runtime works, read [technical/indirect-calls.md](technical/indirect-calls.md), [technical/memory-layout.md](technical/memory-layout.md) and [technical/kernel-replacement.md](technical/kernel-replacement.md), and the toolkit's [lessons-learned.md](https://github.com/sp00nznet/xboxrecomp/blob/main/docs/technical/lessons-learned.md), in its own repository.
