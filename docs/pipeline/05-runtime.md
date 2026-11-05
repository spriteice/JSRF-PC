# Step 5: Building the Runtime

> **From the xboxrecomp toolkit,** by Ned Heller (sp00nznet), written for
> the toolkit's own work, much of it on Burnout 3: Takedown, on Windows with
> Direct3D 11. JSRF-PC keeps it for how the pipeline and runtime work. Where
> it differs from how JSRF-PC is built or changed,
> [building-on-linux.md](../building-on-linux.md) and
> [project-rules.md](../project-rules.md) are right, and this doc is out
> of date there: say so. The generated code is never edited, and a bug is
> fixed where it starts, not stubbed out or guarded around.

## Overview

The lifted C code from Step 4 is just one piece of the puzzle. It expects to run in an environment that mimics the original Xbox: memory at specific addresses, kernel functions available for calling, a D3D8 graphics device, and input from Xbox controllers. The runtime provides all of this.

The runtime has four major subsystems:
1. **Memory Layout** -- map Xbox virtual addresses into Windows process space
2. **Kernel Shim** -- replace 147+ Xbox kernel functions with Win32 equivalents
3. **Graphics** -- draw what the game's own Direct3D 8 tells the NV2A to draw, with OpenGL
4. **Input** -- map the host's pads to the Xbox controller, through SDL2

## Memory Layout

### The Problem

Xbox games use hardcoded absolute addresses for all global variables. When the original code does `mov eax, [0x004D532C]`, it expects a specific variable at that exact address. On Windows, address space layout randomization (ASLR) and existing allocations mean address `0x004D532C` is not available by default.

### The Solution: CreateFileMapping + MapViewOfFileEx

The runtime creates a file mapping object backed by the page file and maps views of it at the exact Xbox virtual addresses:

```c
// Create a 64 MB file mapping (Xbox total RAM)
HANDLE hMapping = CreateFileMapping(
    INVALID_HANDLE_VALUE,   // page file backed
    NULL,                   // default security
    PAGE_READWRITE,         // read-write access
    0, 64 * 1024 * 1024,   // 64 MB
    NULL                    // unnamed
);

// Map the base view at address 0x00000000
void *base = MapViewOfFileEx(
    hMapping,
    FILE_MAP_ALL_ACCESS,
    0, 0,                   // offset 0
    64 * 1024 * 1024,       // 64 MB
    (void*)0x00000000       // requested base address
);
```

### Why Not VirtualAlloc?

An earlier approach used `VirtualAlloc` to reserve memory at the target addresses. This works for the basic case, but fails for **memory mirrors**. The Xbox has 64 MB of physical RAM that is identity-mapped, but some games access it through mirrored addresses (e.g., `0x80000000 + offset` is the same physical memory as `0x00000000 + offset`).

With `VirtualAlloc`, two allocations at different addresses are completely independent -- writing to one does not affect the other. With `CreateFileMapping`, two `MapViewOfFileEx` calls on the same mapping object create true aliases: writes to one view are instantly visible through the other, because they share the same physical pages.

### Mirror Views

The Xbox maps its 64 MB of RAM at multiple address ranges. The runtime pre-maps 28 mirror views:

```c
#define XBOX_NUM_MIRRORS 28

for (int i = 1; i <= XBOX_NUM_MIRRORS; i++) {
    uintptr_t mirror_addr = (uintptr_t)i * 64 * 1024 * 1024;
    g_mirror_views[i-1] = MapViewOfFileEx(
        hMapping,
        FILE_MAP_ALL_ACCESS,
        0, 0,
        64 * 1024 * 1024,
        (void*)mirror_addr
    );
    // Some mirrors will fail (address already in use) -- that's OK
}
```

Not all mirrors succeed -- Windows may have DLLs or other allocations at some addresses. For Burnout 3, 4 out of 33 attempted mirrors fail due to address conflicts. This is acceptable because the game primarily accesses RAM through the base view.

### Section Loading

After mapping, the runtime copies XBE section data to the correct addresses:

```c
// Copy .rdata (read-only data: strings, vtables, constants)
memcpy((void*)XBOX_RDATA_VA, xbe_data + RDATA_RAW_OFFSET, XBOX_RDATA_SIZE);

// Copy initialized .data (global variables with initial values)
memcpy((void*)XBOX_DATA_VA, xbe_data + DATA_RAW_OFFSET, XBOX_DATA_INIT_SIZE);

// Zero-fill BSS (uninitialized globals)
memset((void*)(XBOX_DATA_VA + XBOX_DATA_INIT_SIZE), 0,
       XBOX_DATA_SIZE - XBOX_DATA_INIT_SIZE);

// Copy XDK library data sections
for (int i = 0; i < NUM_EXTRA_SECTIONS; i++) {
    memcpy((void*)g_extra_sections[i].va,
           xbe_data + g_extra_sections[i].raw_offset,
           g_extra_sections[i].size);
}
```

The .text section is NOT copied -- the recompiled code is native Windows code in the compiled binary, not the original x86 instructions. Only data sections need to be at their original addresses.

### NV2A GPU Registers

The Xbox GPU (NV2A) has memory-mapped registers starting at `0xFD000000`. Some game code spin-waits on GPU status registers:

```asm
loop:
    mov eax, [0xFD400700]    ; read GPU status register
    test eax, 0x100
    jnz loop                 ; wait until bit clears
```

In JSRF-PC the generated code sends every load and store in the device registers' range to src/kernel/kernel_mmio.c (`RECOMP_MMIO_LO` to `RECOMP_MMIO_HI` in recomp_types.h), which hands it to the device that owns the address, as the NV2A's interrupt registers or the audio processor's. So a register that a device models reads what the hardware would show, and a wait on it ends as it does on the console. A range no device claims behaves as plain memory.

The toolkit first answered these reads, for Burnout 3, with a zero page mapped on demand, which only works for a title that never needs the hardware's answer: JSRF's music player, for one, takes a silent audio processor for a dirty disc.

### Heap Allocator

The runtime provides a bump allocator for Xbox heap allocations (MmAllocateContiguousMemory, etc.):

```c
#define XBOX_HEAP_BASE  0x00880000   // above stack
#define XBOX_HEAP_SIZE  (~55.5 MB)   // fills remaining 64 MB

static uint32_t heap_cursor = XBOX_HEAP_BASE;

uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment) {
    heap_cursor = (heap_cursor + alignment - 1) & ~(alignment - 1);
    uint32_t result = heap_cursor;
    heap_cursor += size;
    if (heap_cursor >= XBOX_HEAP_BASE + XBOX_HEAP_SIZE)
        return 0;  // out of memory
    return result;
}
```

A bump allocator is sufficient because Xbox games rarely free memory -- they allocate during loading and hold everything for the level's duration.

## Kernel Shim

The Xbox kernel provides 366 possible exports. Games import a subset. Burnout 3 uses 147.

### Architecture

Each kernel function needs a **bridge** that:
1. Reads arguments from the simulated Xbox stack (g_esp)
2. Translates Xbox pointers to native pointers (adding g_xbox_mem_offset)
3. Calls the Win32 equivalent
4. Writes the return value to g_eax
5. Handles calling convention differences (Xbox kernel uses stdcall)

### Synthetic VA Scheme

Kernel thunk entries in the XBE contain ordinals (`0x80000000 | ordinal`). At initialization, the runtime replaces each with a synthetic VA:

```c
#define KERNEL_VA_BASE  0xFE000000

// For each imported ordinal:
MEM32(thunk_table_addr + slot * 4) = KERNEL_VA_BASE + slot * 4;
```

When `RECOMP_ICALL` encounters a VA in `[0xFE000000, 0xFE000600)`, it routes to `recomp_lookup_kernel()` which dispatches to the per-ordinal bridge function.

### Bridge Function Example

```c
// Ordinal 116: MmAllocateContiguousMemory
static void bridge_MmAllocateContiguousMemory(void) {
    // stdcall: args on stack, callee cleans
    uint32_t size = MEM32(g_esp + 4);     // arg 1: size
    // (arg 2: alignment - ignored for bump allocator)

    uint32_t result = xbox_HeapAlloc(size, 16);

    g_eax = result;  // return value in eax
    g_esp += 4;      // pop return address
    g_esp += 8;      // pop 2 args (stdcall)
}
```

### Kernel Function Categories

| Category | Count | Approach |
|----------|-------|----------|
| Memory (MmAlloc, NtAllocateVirtualMemory, ...) | 12 | Bump allocator |
| File I/O (NtCreateFile, NtReadFile, NtClose, ...) | 8 | Win32 CreateFile/ReadFile |
| Threading (PsCreateSystemThreadEx, KeWaitFor...) | 15 | Mostly stubbed (single-threaded) |
| Synchronization (RtlEnterCriticalSection, ...) | 10 | Win32 CriticalSection |
| String/RTL (RtlInitAnsiString, RtlCompare...) | 20 | Direct reimplementation |
| AV/Video (AvSetDisplayMode, ...) | 5 | Stubbed (using our own D3D) |
| Hardware (HalReadSMBus, HalReturnToFirmware, ...) | 8 | Stubbed |
| Network/Xbox Live (XNetStartup, ...) | 15 | Stubbed |
| Data exports (KeTickCount, HardwareInfo, ...) | 12 | Static data in kernel data area |
| Other | 42 | Case-by-case |

For Burnout 3, approximately 68 kernel functions are bridged to actual Win32 implementations, and 79 are stubbed (return success without doing anything).

### Data Exports

Some kernel ordinals are data exports, not functions. The thunk entry must point to actual readable memory:

```c
// Ordinal 156: KeTickCount - pointer to tick counter
#define KDATA_TICK_COUNT  (XBOX_KERNEL_DATA_BASE + 0x020)
MEM32(thunk_slot_156) = KDATA_TICK_COUNT;

// In the update loop:
MEM32(KDATA_TICK_COUNT) = GetTickCount();
```

The kernel data area is a 4 KB block at `0x00740000` that holds all data exports.

## Graphics

JSRF links its own Direct3D 8, from the XDK, so it runs recompiled like the rest of the game's code: it builds NV2A commands in a push buffer in guest memory, as on the console. The runtime consumes that push buffer (src/kernel/nv2a_pb_exec.c, with the NV2A's registers modelled in src/nv2a), and [src/nv2a_gl](../../src/nv2a_gl) draws it with OpenGL: the register combiners become GLSL fragment shaders, and the vertex programs GLSL vertex shaders ([technical/nv2a-shaders.md](../technical/nv2a-shaders.md)). src/d3d holds only the D3D8 types and the NV2A's texture swizzling.

## Input System

### Xbox Controller Mapping

[src/input](../../src/input/README.md) fills the Xbox controller's state from the host's pads, through SDL2's GameController API, on Linux and Windows.

## Game-Specific: Manual Function Overrides

Not all translated functions work correctly. The runtime supports **manual overrides** -- hand-written C functions that replace specific translated functions:

```c
// recomp_manual.c
recomp_func_t recomp_lookup_manual(uint32_t xbox_va) {
    switch (xbox_va) {
        case 0x000636D0: return manual_sub_000636D0;  // physics
        case 0x000110E0: return manual_sub_000110E0;  // frame pump
        case 0x00011240: return manual_sub_00011240;  // resource loader
        // ... 30+ overrides ...
        default: return NULL;
    }
}
```

In JSRF-PC an override is the right place only when the game function's own logic is the cause ([project-rules.md](../project-rules.md), Where code goes 6). It reimplements that logic, and is named for what it does. It never stubs a function to get past a crash, and a trace while looking for a cause comes from `--trace-functions` (tools/recomp), not an override.

For Burnout 3, approximately 33 functions have manual overrides, including the physics engine, frame pump, resource loader, and several RenderWare initialization functions.

## Build System

JSRF-PC is built with CMake from `port/` ([building-on-linux.md](../building-on-linux.md), step 5), into one executable, `jsrf_recomp`, that links the generated code, the hand-written code in `port/`, and the runtime's libraries in `src/`: the kernel, the device models, the OpenGL renderer and input. The Windows build is cross-compiled on Linux with llvm-mingw (building-on-linux.md, Windows).

## Startup Sequence

1. Load the XBE file from disk (keep in memory for .rdata string fallback)
2. Call `xbox_MemoryLayoutInit()` to map sections to Xbox VAs
3. Call `xbox_kernel_init()` to fill the kernel thunk table
4. Call `xbox_kernel_bridge_init()` to wire synthetic VAs
5. Initialize g_esp to XBOX_STACK_TOP
6. Create the window and its OpenGL context, with SDL2 (src/nv2a_gl)
7. Call the generated entry point, `xbe_entry_point()`
8. The game runs from there, on that thread and the threads it starts
