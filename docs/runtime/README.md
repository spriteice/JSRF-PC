# Runtime Implementation Guide

> **From the xboxrecomp toolkit,** by Ned Heller (sp00nznet), written for
> the toolkit's own work, much of it on Burnout 3: Takedown, on Windows with
> Direct3D 11. JSRF-PC keeps it for how the pipeline and runtime work. Where
> it differs from how JSRF-PC is built or changed,
> [building-on-linux.md](../building-on-linux.md) and
> [project-rules.md](../project-rules.md) are right, and this doc is out
> of date there: say so. The generated code is never edited, and a bug is
> fixed where it starts, not stubbed out or guarded around.

The recompiled C code needs a host environment to run. This page names the four major runtime subsystems, and where each is in JSRF-PC.

## The Four Pillars

### 1. Memory Layout (`src/kernel/xbox_memory_layout.c`)

Reproduce the Xbox's 64 MB address space at the correct virtual addresses. This is the most critical piece — get it wrong and nothing else works.

See: [../technical/memory-layout.md](../technical/memory-layout.md)

Template: [xbox_memory.h](https://github.com/sp00nznet/xboxrecomp/blob/main/templates/runtime/xbox_memory.h), in the toolkit's repository

### 2. Kernel Shim (`src/kernel/`)

Replace the Xbox kernel imports with host equivalents, each doing what the console's kernel does for the game ([project-rules.md](../project-rules.md), Where code goes 2). A stub that returns success hides what is missing. The critical ones are memory allocation, file I/O and threads.

See: [../technical/kernel-replacement.md](../technical/kernel-replacement.md)

Template: [kernel_stubs.h](https://github.com/sp00nznet/xboxrecomp/blob/main/templates/runtime/kernel_stubs.h), in the toolkit's repository

### 3. Graphics (`src/nv2a_gl/`)

JSRF's own Direct3D 8 runs recompiled and builds NV2A push buffers, as on the console. src/nv2a_gl draws them with OpenGL. This is the largest runtime component.

See: [../pipeline/05-runtime.md](../pipeline/05-runtime.md) (Graphics) and [../technical/nv2a-shaders.md](../technical/nv2a-shaders.md)

### 4. Input System (`src/input/`)

Fill the Xbox controller's state from the host's pads, through SDL2's GameController API, on Linux and Windows. This is usually the simplest piece.

## Build System

JSRF-PC builds with CMake from `port/`, with GCC or Clang on Linux and llvm-mingw for Windows ([../building-on-linux.md](../building-on-linux.md)). The runtime's libraries are in `src/` ([src/README.md](../../src/README.md)).

## Game-Specific Components

Beyond the four pillars, JSRF-PC has:

- **Game function overrides**, in `port/`, where the game function's own logic is the cause ([project-rules.md](../project-rules.md), Where code goes 6)
- **Its own features and settings**, such as the free camera and the intro in `port/`, and the menus in `src/menu/`
