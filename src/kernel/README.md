# xbox_kernel — the Xbox kernel

> **From the xboxrecomp toolkit,** by Ned Heller (sp00nznet), written for
> the toolkit's own work, much of it on Burnout 3: Takedown, on Windows with
> Direct3D 11. JSRF-PC keeps it for how the kernel works. Where it differs
> from how JSRF-PC is built or changed,
> [building-on-linux.md](../../docs/building-on-linux.md) and
> [project-rules.md](../../docs/project-rules.md) are right, and this doc is
> out of date there: say so. The generated code is never edited, and a bug
> is fixed where it starts, not stubbed out or guarded around.

Does on the host what the console's kernel does for the game, for each of
the kernel's exports the game imports: memory, files, threads,
synchronisation, timers, interrupts and DPCs, crypto, and the hardware
abstraction layer. The game reaches each through its kernel thunk table,
which `kernel_bridge.c` routes to the export's `bridge_*` function. This
library also takes the game's loads and stores to the devices' registers
(`kernel_mmio.c`), and runs the NV2A's interrupts and its pushbuffer.

It is written against the Win32 API. On Windows that is Windows itself; on
Linux, src/platform's `win32_compat.c` gives the same calls on POSIX.

## Files

| File | Purpose |
|------|---------|
| `kernel.h` | The public header: types, constants and prototypes |
| `kernel_bridge.c` | The bridge: each ordinal's `bridge_*` function, the game's thunk table routed to them, the kernel data exports, and indirect calls resolved |
| `kernel_bridge.h` | What kernel_bridge.c shares with the files split out of it |
| `kernel_init.c` | The kernel layer's start and end (`xbox_kernel_init`, `xbox_kernel_shutdown`), and its log (`xbox_log`) |
| `xbox_memory_layout.h`, `.c` | The guest's address space: the XBE's sections at their addresses, RAM's mirrors, the stack, and the guest's registers |
| `xbox_heap.c` | The guest heap, behind the kernel's memory calls and the runtime's own stacks |
| `kernel_memory.c` | Memory: MmQueryStatistics, NtFreeVirtualMemory, NtQueryVirtualMemory, page protection |
| `kernel_file.c` | File I/O: NtCreateFile, NtReadFile, and the rest |
| `kernel_path.c` | The Xbox's device paths (`\Device\CdRom0\`, `D:\`, `T:\` ...) as host paths |
| `kernel_apc.c` | The file requests' APCs, delivered at the thread's next alertable wait |
| `kernel_io.c` | The I/O manager: devices and IRPs |
| `kernel_thread.c` | Threads: priorities, suspend and resume, KeDelayExecutionThread |
| `kernel_sync.c` | Synchronisation: events, semaphores, mutexes |
| `kernel_timer.c` | Timers, and the timer thread: KeTickCount, the timers and DPCs that are due, and the devices' periodic work |
| `kernel_interrupt.c` | Interrupt objects, and the DPC queue |
| `kernel_rtl.c` | The runtime library: critical sections, counted strings, time fields |
| `kernel_hal.c` | The hardware abstraction layer: IRQLs, performance counters, system time, PCI |
| `kernel_crypto.c` | Crypto: SHA-1, RC4, HMAC |
| `kernel_ob.c` | The object manager: reference counts, lookup by name |
| `kernel_xbox.c` | The EEPROM's settings, and the network PHY |
| `kernel_mmio.c` | The devices' registers: each guest load and store in their range, sent to the device that owns it by address |
| `nv2a_intr.c` | The NV2A's interrupt registers, the display's vertical blank, and the GPU's software methods |
| `nv2a_pb_scan.c`, `nv2a_pb_exec.c` | The game's NV2A pushbuffer: a read-only survey of it, and the part of it that decides the surface drawn into and clears it |
| `icall_feedback.c`, `recomp_icall_feedback.h` | The indirect-call targets the game reaches, kept for the next run of the pipeline |
| `recomp_trace.c` | Function tracing, for the recompiler's `--trace-functions` (a diagnosis only: project-rules.md, Never do) |

## Memory Layout

The Xbox has 64 MB of unified RAM. `xbox_MemoryLayoutInit` maps it at the
guest's own addresses, with each section of the XBE at the address its
header gives, using `CreateFileMapping` + `MapViewOfFileEx`.

### Why CreateFileMapping?

The Xbox has **mirror regions** — the same physical memory appears at multiple virtual addresses (26-bit address bus wrapping). `CreateFileMapping` gives us true aliases where writes through one address are instantly visible at all mirrors. `VirtualAlloc` would give separate copies. On Linux, `win32_compat.c` backs the mapping with `memfd_create`.

We create 28 mirror views covering 1.75 GB of address space:

```c
// The core mapping
HANDLE h = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 64*1024*1024, NULL);

// Primary view at Xbox base (0x10000000 on host)
MapViewOfFileEx(h, FILE_MAP_ALL_ACCESS, 0, 0, XBOX_TOTAL_RAM, target_base);

// Mirror views every 64 MB
for (int i = 1; i < 28; i++)
    MapViewOfFileEx(h, FILE_MAP_ALL_ACCESS, 0, 0, XBOX_TOTAL_RAM, target_base + i * 64MB);
```

### Global State

```c
// Defined in xbox_memory_layout.c, used everywhere
extern ptrdiff_t g_xbox_mem_offset;  // Add to Xbox VA → get native pointer

// Simulated x86 registers (the register model)
extern uint32_t g_eax, g_ecx, g_edx, g_esp;  // Volatile
extern uint32_t g_ebx, g_esi, g_edi;          // Callee-saved
extern uint32_t g_seh_ebp;                     // SEH frame pointer
```

### API

```c
// Initialize — call FIRST before anything else
BOOL xbox_MemoryLayoutInit(const void *xbe_data, size_t xbe_size);
void xbox_MemoryLayoutShutdown(void);

// Query
void     *xbox_GetMemoryBase(void);    // Native pointer to Xbox VA 0
ptrdiff_t xbox_GetMemoryOffset(void);  // g_xbox_mem_offset value
BOOL      xbox_IsXboxAddress(uintptr_t address);
HANDLE    xbox_GetMappingHandle(void); // For creating additional mirror views

// The guest heap (xbox_heap.c)
uint32_t xbox_HeapAlloc(uint32_t size, uint32_t alignment);
void     xbox_HeapFree(uint32_t xbox_va);
```

## Kernel Thunk Table

The Xbox kernel exposes functions via ordinal numbers. Games import them through a thunk table in the XBE, at the address its header gives. `xbox_kernel_bridge_init()` (kernel_bridge.c) reads each slot's ordinal from that table in Xbox memory, writes the slot's synthetic VA or kernel data address there, and routes each ordinal to its `bridge_*` function (`bridge_for_ordinal`). [docs/technical/kernel-replacement.md](../../docs/technical/kernel-replacement.md) has the whole scheme.

### Initialization

```c
xbox_kernel_init();         // Open the kernel log
xbox_kernel_bridge_init();  // Route the game's thunk table to the bridge, set up kernel data area
```

### Kernel Data Exports

Some kernel ordinals are data exports (not functions). They are kept in a
kernel data area at `XBOX_KERNEL_DATA_BASE` (0x00740000), each at its
`KDATA_*` offset in xbox_memory_layout.h, and `kernel_data_va_for_ordinal`
(kernel_bridge.c) gives each ordinal's address.

## File I/O

Xbox file I/O uses NT-style APIs (NtCreateFile, NtReadFile). `kernel_path.c` translates the Xbox's paths to the host's:

```
Xbox Path          → Host Path
───────────────────────────────────────
\Device\CdRom0\    → <game_dir>/
D:\                → <game_dir>/
T:\                → <save_dir>/TitleData/
U:\                → <save_dir>/UserData/
Z:\                → <save_dir>/Cache/
```

## Threading

Xbox threads use `PsCreateSystemThreadEx` with NT-style parameters. We create Win32 threads with a wrapper that sets up the register context:

```c
// The kernel bridge's bridge_PsCreateSystemThreadEx creates a thread that:
// 1. Looks up the start routine via recomp_lookup()
// 2. Sets up ESP for the new thread
// 3. Calls the recompiled function
NTSTATUS PsCreateSystemThreadEx(
    PHANDLE ThreadHandle,
    ULONG   ThreadExtraSize,
    ULONG   KernelStackSize,
    ULONG   TlsDataSize,
    PULONG  ThreadId,
    PVOID   StartContext1,
    PVOID   StartContext2,
    BOOLEAN CreateSuspended,
    BOOLEAN DebuggerThread,
    PVOID   StartRoutine        // Xbox VA → resolved via recomp_lookup()
);
```

## Synchronization

Full implementations of:

| Xbox API | Win32 Implementation |
|----------|---------------------|
| NtCreateEvent | CreateEvent |
| NtSetEvent / KeSetEvent | SetEvent |
| NtWaitForSingleObject | WaitForSingleObject |
| NtWaitForMultipleObjectsEx | WaitForMultipleObjects |
| NtCreateSemaphore | CreateSemaphore |
| NtReleaseSemaphore | ReleaseSemaphore |
| RtlInitializeCriticalSection | InitializeCriticalSection |
| RtlEnterCriticalSection | EnterCriticalSection |
| RtlLeaveCriticalSection | LeaveCriticalSection |

## NTSTATUS Codes

```c
#define STATUS_SUCCESS                0x00000000
#define STATUS_TIMEOUT                0x00000102
#define STATUS_PENDING                0x00000103
#define STATUS_NO_MEMORY              0xC0000017
#define STATUS_ACCESS_DENIED          0xC0000022
#define STATUS_OBJECT_NAME_NOT_FOUND  0xC0000034
#define STATUS_OBJECT_NAME_COLLISION  0xC0000035
// ... full list in kernel.h

#define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)
```

## Calling Conventions

| Convention | Used By | Example |
|-----------|---------|---------|
| `__stdcall` | Most kernel functions | `xbox_NtCreateFile(...)` |
| `__fastcall` | Kf* and ObfDereference | `xbox_KfRaiseIrql(NewIrql)` |
