# src/: the engine's libraries

The engine, `jsrf_recomp`, is the game's own code recompiled to C (made from
the player's disc, and never in this repository), the game's own logic in
`port/`, and the libraries here: the console's kernel and devices, the
renderer, and the host layer under them. The root CMakeLists.txt builds each
folder as a library, and port/CMakeLists.txt links them into the engine
through the root's `xboxrecomp` target.

Most of this began as the runtime of the
[xboxrecomp](https://github.com/sp00nznet/xboxrecomp) toolkit by Ned Heller
(sp00nznet), with the sound chip's code from
[xemu](https://github.com/xemu-project/xemu). JSRF-PC has changed it since
(docs/upstream.md, and NOTICE for each file's origin).

## Each folder, and its layer

A fix goes in the layer that owns the behaviour (docs/project-rules.md,
Where code goes 2).

| Folder | Library | Layer | What it holds | Linked by |
|---|---|---|---|---|
| `platform/` | `platform` | The platform layer | The Win32 and NT types the kernel is written in. On Linux, the Win32 primitives it uses (threads, events, mutexes, atomics, file mappings, timers) on POSIX (`win32_compat.c`); on Windows, `<windows.h>` itself. Also `recomp_check.h`, the check that stays in release builds. | `xbox_kernel`, `xbox_apu`, `xbox_usb`, `xbox_input`, `xbox_d3d8` |
| `kernel/` | `xbox_kernel` | The kernel | The console kernel's exports the game imports, each routed from the game's thunk table to its `bridge_*` function (`kernel_bridge.c`), the guest's memory and heap, and the timer thread. Also where the game's loads and stores to device registers go (`kernel_mmio.c`), and the NV2A's interrupts and pushbuffer (`nv2a_intr.c`, `nv2a_pb_scan.c`, `nv2a_pb_exec.c`). Its [README](kernel/README.md). | `xboxrecomp` |
| `apu/` | `xbox_apu` | The device models | The MCPX APU, the console's sound chip: xemu's voice processor, JSRF-PC's own effects where the console's DSP would run them, and the output through SDL2's audio. Its [README](apu/README.md). | `xboxrecomp` |
| `usb/` | `xbox_usb` | The device models | The MCPX's OHCI USB host controllers, and a controller on them, for the game's own USB driver to find. | `xboxrecomp` |
| `input/` | `xbox_input` | The platform layer | The host's pads, through SDL2's GameController, as the Xbox gamepad's state, which `usb/`'s controller reports. Its [README](input/README.md). | `xbox_usb`, `xboxrecomp` |
| `nv2a_gl/` | `xbox_nv2a_gl` | The renderer | What the game sends the NV2A, the console's GPU, drawn with OpenGL: its methods, shaders and textures, and the window. | `xbox_kernel`, `xboxrecomp` |
| `menu/` | `xbox_menu` | The in-game menu | The menu (F1, or Back on a pad), with Dear ImGui over the renderer's window. Any part of the engine adds rows to it (`menu.h`). | `xbox_nv2a_gl` |
| `d3d/` | `xbox_d3d8`, headers only | The renderer | The Xbox's D3D8 types, and the NV2A's texture swizzling. | `xboxrecomp` |
| `nv2a/` | none, a header | The device models | `qemu_shim.h`, the QEMU types xemu's APU code is written against. Its [README](nv2a/README.md). | `apu/`'s include path |

## Outside src/

- **`port/`:** the game's own logic, in the engine: its entry point, the
  game function overrides and hooks, and JSRF-PC's features
  ([README](../port/README.md)).
- **`templates/runtime/`:** `recomp_types.h`, the header the generated code
  is built with: the guest's registers and memory, and the calls and checks
  the recompiler emits.
- **`tools/`:** the pipeline that makes the generated code from the game's
  XBE, the recompiler among it ([README](../tools/README.md)).
- **`launcher/`:** the launchers, and the scripts that build and package
  each platform's release (docs/building-on-linux.md).
