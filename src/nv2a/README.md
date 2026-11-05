# src/nv2a — the QEMU shim

This folder holds one file, `qemu_shim.h`, which the MCPX APU's code
(`src/apu`) is built against: the code extracted from
[xemu](https://github.com/xemu-project/xemu) is written for QEMU's types,
and the shim gives it those. `src/apu/apu_shim.h` includes it, and
`src/apu/CMakeLists.txt` and the APU's tests put this folder on the include
path.

The folder is named for what it first held: xemu's register-level NV2A GPU
code (`nv2a_core.c`, `nv2a_state.h` and its register definitions,
`nv2a_regs.h`). No build linked that code, so it was removed. The engine
draws what the game sends the NV2A in `src/kernel/nv2a_pb_exec.c` (the
pushbuffer, in software) and `src/nv2a_gl` (OpenGL), and models its
interrupt registers in `src/kernel/nv2a_intr.c`.

## What the shim provides

`qemu_shim.h` replaces QEMU types with C11 equivalents, and with the
Win32 primitives (`src/platform` provides them on Linux), so the xemu code
compiles standalone:

| QEMU Type | Replacement | Notes |
|-----------|-------------|-------|
| `hwaddr` | `uint64_t` | Hardware address |
| `MemoryRegion` | Struct with size + opaque | MMIO region stub |
| `PCIDevice` | Struct with irq + bus | PCI device stub |
| `QemuMutex` | `CRITICAL_SECTION` | Win32 mutex |
| `QemuCond` | `CONDITION_VARIABLE` | Win32 condition variable |
| `QemuThread` | `HANDLE` | Win32 thread |
| `qemu_irq` | `void*` | Interrupt stub |

Helper macros:
```c
muldiv64(a, b, c)    // 128-bit multiply-divide (uses _umul128 on MSVC)
ctz32(val)            // Count trailing zeros
GET_MASK(v, mask)     // Extract masked field
SET_MASK(v, mask, val)// Set masked field
```
