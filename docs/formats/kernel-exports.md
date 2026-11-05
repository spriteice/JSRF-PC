# Xbox Kernel Exports

> **From the xboxrecomp toolkit,** by Ned Heller (sp00nznet), written for
> the toolkit's own work, much of it on Burnout 3: Takedown, on Windows with
> Direct3D 11. JSRF-PC keeps it for how the pipeline and runtime work. Where
> it differs from how JSRF-PC is built or changed,
> [building-on-linux.md](../building-on-linux.md) and
> [project-rules.md](../project-rules.md) are right, and this doc is out
> of date there: say so. The generated code is never edited, and a bug is
> fixed where it starts, not stubbed out or guarded around.

The Xbox kernel (`xboxkrnl.exe`) exports up to 366 functions and data objects. Games import a subset of these via the kernel thunk table in the XBE header. This document covers the most commonly imported exports (~147 that a typical game uses), organized by category.

Each entry shows: **ordinal**, function prototype and description, and each group says where JSRF-PC does it. Each export the game imports reaches its `bridge_*` function, in src/kernel/kernel_bridge.c unless a line says otherwise (src/kernel/README.md).

## Memory Management

### Contiguous Memory (GPU-accessible)

| Ord | Function | Description |
|-----|----------|-------------|
| 165 | `PVOID MmAllocateContiguousMemory(ULONG NumberOfBytes)` | Allocate physically contiguous memory. Used for GPU-accessible buffers (vertex buffers, textures, push buffers). |
| 166 | `PVOID MmAllocateContiguousMemoryEx(ULONG NumberOfBytes, ULONG_PTR LowestAddr, ULONG_PTR HighestAddr, ULONG Alignment, ULONG Protect)` | Extended contiguous allocation with address range and alignment constraints. |
| 171 | `VOID MmFreeContiguousMemory(PVOID BaseAddress)` | Free contiguous memory allocated by ordinals 165/166. |
| 178 | `VOID MmPersistContiguousMemory(PVOID BaseAddress, ULONG NumberOfBytes, BOOLEAN Persist)` | Mark memory as persistent across quick reboot. |

**In JSRF-PC:** `bridge_MmAllocateContiguousMemory` and its `Ex`, `bridge_MmFreeContiguousMemory` and `bridge_MmGetPhysicalAddress` hand out and take back the guest's physical memory, in the window at `XBOX_CONTIG_BASE` (0x80000000, src/kernel/kernel.h), where the APU and the NV2A read it.

### Virtual Memory

| Ord | Function | Description |
|-----|----------|-------------|
| 184 | `NTSTATUS NtAllocateVirtualMemory(PVOID* BaseAddress, ULONG_PTR ZeroBits, PSIZE_T RegionSize, ULONG AllocationType, ULONG Protect)` | Allocate or reserve virtual memory pages. |
| 199 | `NTSTATUS NtFreeVirtualMemory(PVOID* BaseAddress, PSIZE_T RegionSize, ULONG FreeType)` | Free virtual memory. |
| 217 | `NTSTATUS NtQueryVirtualMemory(PVOID BaseAddress, PVOID MemoryInformation, ULONG Length, PULONG ReturnLength)` | Query virtual memory region info. |

**In JSRF-PC:** `bridge_NtAllocateVirtualMemory`, `bridge_NtFreeVirtualMemory`, `bridge_NtQueryVirtualMemory` and `bridge_NtProtectVirtualMemory`, on the guest heap (src/kernel/xbox_heap.c).

### System/Pool Memory

| Ord | Function | Description |
|-----|----------|-------------|
| 14 | `PVOID ExAllocatePool(ULONG NumberOfBytes)` | Allocate from kernel pool (NonPagedPool). |
| 15 | `PVOID ExAllocatePoolWithTag(ULONG NumberOfBytes, ULONG Tag)` | Pool allocation with debug tag. |
| 23 | `ULONG ExQueryPoolBlockSize(PVOID PoolBlock)` | Query size of a pool allocation. |
| 177 | `PVOID MmMapIoSpace(ULONG_PTR PhysicalAddress, ULONG NumberOfBytes, ULONG Protect)` | Map physical address to virtual. Used for NV2A GPU register access. |
| 173 | `ULONG_PTR MmGetPhysicalAddress(PVOID BaseAddress)` | Get physical address of virtual address. |
| 181 | `NTSTATUS MmQueryStatistics(PMM_STATISTICS MemoryStatistics)` | Get memory usage statistics. |
| 180 | `ULONG MmQueryAllocationSize(PVOID BaseAddress)` | Query allocation size. |
| 179 | `ULONG MmQueryAddressProtect(PVOID VirtualAddress)` | Query page protection. |
| 182 | `VOID MmSetAddressProtect(PVOID BaseAddress, ULONG NumberOfBytes, ULONG NewProtect)` | Change page protection. |
| 168 | `PVOID MmClaimGpuInstanceMemory(ULONG NumberOfBytes, PULONG NumberOfPaddingBytes)` | Claim GPU instance memory from the top of RAM. |
| 169 | `PVOID MmCreateKernelStack(ULONG NumberOfBytes, BOOLEAN DebuggerThread)` | Create a kernel thread stack. |
| 170 | `VOID MmDeleteKernelStack(PVOID StackBase, PVOID StackLimit)` | Delete a kernel thread stack. |
| 175 | `VOID MmLockUnlockBufferPages(PVOID BaseAddress, ULONG NumberOfBytes, BOOLEAN UnlockPages)` | Lock/unlock physical pages. |
| 176 | `VOID MmLockUnlockPhysicalPage(ULONG_PTR PhysicalAddress, BOOLEAN UnlockPage)` | Lock/unlock a single physical page. |

**In JSRF-PC:** the pool (`bridge_ExAllocatePool`, `bridge_ExAllocatePoolWithTag`, `bridge_ExFreePool`) and the Mm calls are on the guest heap (src/kernel/xbox_heap.c). `bridge_MmMapIoSpace` gives a block of it. The devices' registers themselves are reached by their addresses: each guest load and store there goes to the device that owns it (src/kernel/kernel_mmio.c).

## File I/O

| Ord | Function | Description |
|-----|----------|-------------|
| 190 | `NTSTATUS NtCreateFile(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, PLARGE_INTEGER AllocationSize, ULONG FileAttributes, ULONG ShareAccess, ULONG CreateDisposition, ULONG CreateOptions)` | Open or create a file. Primary file open API. |
| 202 | `NTSTATUS NtOpenFile(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, ULONG ShareAccess, ULONG OpenOptions)` | Open an existing file (simplified NtCreateFile). |
| 219 | `NTSTATUS NtReadFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID Buffer, ULONG Length, PLARGE_INTEGER ByteOffset)` | Read data from a file. |
| 236 | `NTSTATUS NtWriteFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID Buffer, ULONG Length, PLARGE_INTEGER ByteOffset)` | Write data to a file. |
| 187 | `NTSTATUS NtClose(HANDLE Handle)` | Close any kernel handle (files, events, semaphores, threads). |
| 195 | `NTSTATUS NtDeleteFile(POBJECT_ATTRIBUTES ObjectAttributes)` | Delete a file by name. |
| 211 | `NTSTATUS NtQueryInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass)` | Query file metadata (size, timestamps, attributes). |
| 226 | `NTSTATUS NtSetInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass)` | Set file metadata (position, size, disposition). |
| 210 | `NTSTATUS NtQueryFullAttributesFile(POBJECT_ATTRIBUTES ObjectAttributes, PFILE_NETWORK_OPEN_INFORMATION FileInformation)` | Query file attributes without opening. |
| 207 | `NTSTATUS NtQueryDirectoryFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, PANSI_STRING FileName, BOOLEAN RestartScan)` | Enumerate directory contents. |
| 218 | `NTSTATUS NtQueryVolumeInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FsInformation, ULONG Length, FS_INFORMATION_CLASS FsInformationClass)` | Query volume/filesystem info (free space, etc.). |
| 198 | `NTSTATUS NtFlushBuffersFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock)` | Flush file buffers to disk. |
| 200 | `NTSTATUS NtFsControlFile(...)` | File system control operations. |
| 196 | `NTSTATUS NtDeviceIoControlFile(...)` | Device I/O control. |

**In JSRF-PC:** the `bridge_Nt*File` functions, with src/kernel/kernel_file.c. Xbox uses NT-style paths (`\Device\CdRom0\`, `\Device\Harddisk0\Partition1\`), in ANSI_STRINGs, not UNICODE_STRINGs as Windows NT does. src/kernel/kernel_path.c maps them to host paths:
- `D:\` (DVD drive) and `\Device\CdRom0\` to the game's folder
- `T:\` (title persistent storage) to the save folder's `TitleData`
- `U:\` (user data) to the save folder's `UserData`
- `Z:\` (utility partition) to the save folder's `Cache`

### I/O Manager

| Ord | Function | Description |
|-----|----------|-------------|
| 66 | `NTSTATUS IoCreateFile(...)` | Extended file creation (internal). |
| 61 | `NTSTATUS IoBuildDeviceIoControlRequest(...)` | Build an IRP for device I/O. |
| 84 | `NTSTATUS IoSynchronousDeviceIoControlRequest(...)` | Synchronous device I/O. |
| 85 | `NTSTATUS IoSynchronousFsdRequest(...)` | Synchronous FSD request. |
| 203 | `NTSTATUS NtOpenSymbolicLinkObject(PHANDLE LinkHandle, POBJECT_ATTRIBUTES ObjectAttributes)` | Open a symbolic link. |
| 215 | `NTSTATUS NtQuerySymbolicLinkObject(HANDLE LinkHandle, PANSI_STRING LinkTarget, PULONG ReturnedLength)` | Read symbolic link target. Games use this to resolve drive letters. |

**In JSRF-PC:** the `bridge_Io*` functions, with src/kernel/kernel_io.c: `bridge_IoCreateDevice` makes the Xbox's device object in guest memory. The symbolic links are `bridge_IoCreateSymbolicLink`, `bridge_IoDeleteSymbolicLink`, `bridge_NtOpenSymbolicLinkObject` and `bridge_NtQuerySymbolicLinkObject`.

## Threading

| Ord | Function | Description |
|-----|----------|-------------|
| 255 | `NTSTATUS PsCreateSystemThreadEx(PHANDLE ThreadHandle, ULONG ThreadExtraSize, ULONG KernelStackSize, ULONG TlsDataSize, PULONG ThreadId, PVOID StartContext1, PVOID StartContext2, BOOLEAN CreateSuspended, BOOLEAN DebugStack, PSYSTEM_ROUTINE StartRoutine)` | Create a kernel thread. Primary thread creation API. |
| 258 | `NTSTATUS PsTerminateSystemThread(NTSTATUS ExitStatus)` | Terminate the current thread. |
| 99 | `NTSTATUS KeDelayExecutionThread(KPROCESSOR_MODE WaitMode, BOOLEAN Alertable, PLARGE_INTEGER Interval)` | Sleep the current thread (negative = relative time in 100ns units). |
| 143 | `LONG KeSetBasePriorityThread(PVOID Thread, LONG Increment)` | Set thread base priority. |
| 124 | `LONG KeQueryBasePriorityThread(PVOID Thread)` | Get thread base priority. |
| 238 | `NTSTATUS NtYieldExecution(void)` | Yield CPU to another thread. |
| 197 | `NTSTATUS NtDuplicateObject(HANDLE SourceHandle, PHANDLE TargetHandle, ULONG Options)` | Duplicate a kernel handle. |
| 224 | `NTSTATUS NtResumeThread(HANDLE ThreadHandle, PULONG PreviousSuspendCount)` | Resume a suspended thread. Returns previous suspend count. |
| 231 | `NTSTATUS NtSuspendThread(HANDLE ThreadHandle, PULONG PreviousSuspendCount)` | Suspend a thread. Returns previous suspend count. |

**In JSRF-PC:** `bridge_PsCreateSystemThreadEx` and `bridge_PsTerminateSystemThread`, and the other thread calls with src/kernel/kernel_thread.c. The thread procs use `__stdcall`.

## Synchronization

### Events and Semaphores

| Ord | Function | Description |
|-----|----------|-------------|
| 189 | `NTSTATUS NtCreateEvent(PHANDLE EventHandle, POBJECT_ATTRIBUTES ObjectAttributes, ULONG EventType, BOOLEAN InitialState)` | Create a named or unnamed event. |
| 225 | `NTSTATUS NtSetEvent(HANDLE EventHandle, PLONG PreviousState)` | Signal an event. |
| 193 | `NTSTATUS NtCreateSemaphore(PHANDLE SemaphoreHandle, POBJECT_ATTRIBUTES ObjectAttributes, LONG InitialCount, LONG MaximumCount)` | Create a semaphore. |
| 222 | `NTSTATUS NtReleaseSemaphore(HANDLE SemaphoreHandle, LONG ReleaseCount, PLONG PreviousCount)` | Release a semaphore. |
| 233 | `NTSTATUS NtWaitForSingleObject(HANDLE Handle, BOOLEAN Alertable, PLARGE_INTEGER Timeout)` | Wait on a single sync object. |
| 235 | `NTSTATUS NtWaitForMultipleObjectsEx(ULONG Count, HANDLE Handles[], ULONG WaitType, BOOLEAN Alertable, PLARGE_INTEGER Timeout)` | Wait on multiple sync objects. |

**In JSRF-PC:** `bridge_NtCreateEvent`, `bridge_NtSetEvent`, `bridge_NtCreateSemaphore`, the `bridge_NtWait*` functions and the rest, with src/kernel/kernel_sync.c, on the host's events and semaphores (on Linux, src/platform's).

### Kernel-mode Sync (Dispatcher Objects)

| Ord | Function | Description |
|-----|----------|-------------|
| 145 | `LONG KeSetEvent(PVOID Event, LONG Increment, BOOLEAN Wait)` | Kernel-mode set event. |
| 159 | `NTSTATUS KeWaitForSingleObject(PVOID Object, ULONG WaitReason, KPROCESSOR_MODE WaitMode, BOOLEAN Alertable, PLARGE_INTEGER Timeout)` | Kernel-mode wait. |
| 158 | `NTSTATUS KeWaitForMultipleObjects(ULONG Count, PVOID Objects[], ULONG WaitType, ULONG WaitReason, KPROCESSOR_MODE WaitMode, BOOLEAN Alertable, PLARGE_INTEGER Timeout, PVOID WaitBlockArray)` | Kernel-mode multi-wait. |

**In JSRF-PC:** `bridge_KeWaitForSingleObject`, `bridge_KeWaitForMultipleObjects` and `bridge_KeSetEvent` which take a dispatcher object's guest address as well as a handle.

### Critical Sections

| Ord | Function | Description |
|-----|----------|-------------|
| 291 | `VOID RtlInitializeCriticalSection(PRTL_CRITICAL_SECTION CriticalSection)` | Initialize a critical section. |
| 277 | `VOID RtlEnterCriticalSection(PRTL_CRITICAL_SECTION CriticalSection)` | Enter (lock) a critical section. |
| 294 | `VOID RtlLeaveCriticalSection(PRTL_CRITICAL_SECTION CriticalSection)` | Leave (unlock) a critical section. |

**In JSRF-PC:** `bridge_RtlInitializeCriticalSection`, `bridge_RtlEnterCriticalSection` and `bridge_RtlLeaveCriticalSection`, on the CRITICAL_SECTION at the guest's address.

### Timers and DPCs

| Ord | Function | Description |
|-----|----------|-------------|
| 113 | `VOID KeInitializeTimerEx(PKTIMER Timer, TIMER_TYPE Type)` | Initialize a kernel timer. |
| 149 | `BOOLEAN KeSetTimer(PKTIMER Timer, LARGE_INTEGER DueTime, PKDPC Dpc)` | Set a one-shot timer. |
| 150 | `BOOLEAN KeSetTimerEx(PKTIMER Timer, LARGE_INTEGER DueTime, LONG Period, PKDPC Dpc)` | Set a periodic timer. |
| 97 | `BOOLEAN KeCancelTimer(PKTIMER Timer)` | Cancel a timer. |
| 107 | `VOID KeInitializeDpc(PKDPC Dpc, PKDEFERRED_ROUTINE DeferredRoutine, PVOID DeferredContext)` | Initialize a Deferred Procedure Call. |
| 119 | `BOOLEAN KeInsertQueueDpc(PKDPC Dpc, PVOID Arg1, PVOID Arg2)` | Queue a DPC for execution. |
| 137 | `BOOLEAN KeRemoveQueueDpc(PKDPC Dpc)` | Remove a queued DPC. |

**In JSRF-PC:** timers in src/kernel/kernel_timer.c (`bridge_KeInitializeTimerEx`, `bridge_KeSetTimer`, `bridge_KeSetTimerEx`), fired by the kernel's timer thread there, with `bridge_KeCancelTimer`; and DPCs in src/kernel/kernel_interrupt.c (`bridge_KeInitializeDpc`, `bridge_KeInsertQueueDpc`, `bridge_KeRemoveQueueDpc`), run by that thread.

## HAL & System

| Ord | Function | Description |
|-----|----------|-------------|
| 48 | `VOID HalRequestSoftwareInterrupt(KIRQL RequestIrql)` | Request a software interrupt. |
| 38 | `VOID HalClearSoftwareInterrupt(KIRQL RequestIrql)` | Clear a software interrupt. |
| 39 | `VOID HalDisableSystemInterrupt(ULONG BusInterruptLevel, KIRQL Irql)` | Disable a hardware interrupt. |
| 44 | `ULONG HalGetInterruptVector(ULONG BusInterruptLevel, PKIRQL Irql)` | Get interrupt vector for a bus IRQ. |
| 9 | `ULONG HalReadSMCTrayState(PULONG TrayState, PULONG ChangeCount)` | Read DVD tray state from SMC. |
| 46 | `VOID HalReadWritePCISpace(ULONG BusNumber, ULONG SlotNumber, ULONG RegisterNumber, PVOID Buffer, ULONG Length, BOOLEAN Write)` | Read/write PCI configuration space. |
| 126 | `LARGE_INTEGER KeQueryPerformanceCounter(void)` | High-resolution timer (same as QPC). |
| 127 | `LARGE_INTEGER KeQueryPerformanceFrequency(void)` | Performance counter frequency. |
| 128 | `VOID KeQuerySystemTime(PLARGE_INTEGER CurrentTime)` | Get current system time (100ns since 1601). |
| 125 | `ULONGLONG KeQueryInterruptTime(void)` | Get monotonic time in 100ns units. On Xbox, reads kernel shared data page updated at timer interrupt. |
| 151 | `VOID KeStallExecutionProcessor(ULONG MicroSeconds)` | Busy-wait for N microseconds. |
| 160 | `KIRQL KfRaiseIrql(KIRQL NewIrql)` | Raise IRQL (interrupt priority). |
| 161 | `VOID KfLowerIrql(KIRQL NewIrql)` | Lower IRQL. |
| 129 | `KIRQL KeRaiseIrqlToDpcLevel(void)` | Raise to DISPATCH_LEVEL. |
| 142 | `NTSTATUS KeSaveFloatingPointState(PVOID State)` | Save FPU/SSE state. |
| 139 | `NTSTATUS KeRestoreFloatingPointState(PVOID State)` | Restore FPU/SSE state. |
| 95 | `VOID KeBugCheck(ULONG BugCheckCode)` | Trigger kernel bugcheck (BSOD). |
| 96 | `VOID KeBugCheckEx(ULONG BugCheckCode, ULONG_PTR P1, ULONG_PTR P2, ULONG_PTR P3, ULONG_PTR P4)` | Extended bugcheck with parameters. |
| 358 | `BOOLEAN HalIsResetOrShutdownPending(void)` | Check if reset/shutdown requested. |
| 360 | `VOID HalInitiateShutdown(void)` | Initiate system shutdown. |

**In JSRF-PC:** the performance counter and the time (`bridge_KeQueryPerformanceCounter`, `bridge_KeQuerySystemTime` and the rest) from the host's. Each thread's IRQL is kept (`bridge_KfRaiseIrql`, `bridge_KfLowerIrql`), as the guest lock's hand-over depends on it. `bridge_KeBugCheck` and `bridge_KeBugCheckEx` log the code. `bridge_KeSaveFloatingPointState` and its restore leave the FPU as it is, as each host thread keeps its own.

### Data Exports

| Ord | Symbol | Description |
|-----|--------|-------------|
| 156 | `KeTickCount` | System tick count (increments every ~1ms). |
| 322 | `XboxHardwareInfo` | Hardware flags and revision info. |
| 324 | `XboxKrnlVersion` | Kernel version (major.minor.build.qfe). |
| 164 | `LaunchDataPage` | Pointer to launch data page (title ID, launch path). |
| 259 | `PsThreadObjectType` | Thread object type pointer. |
| 16 | `ExEventObjectType` | Event object type pointer. |
| 326 | `XeImageFileName` | ANSI_STRING with XBE filename. |
| 323 | `XboxHDKey` | 16-byte hard drive key. |
| 325 | `XboxSignatureKey` | 16-byte title signature key. |
| 353 | `XboxLANKey` | 16-byte LAN encryption key. |
| 354 | `XboxAlternateSignatureKeys` | 16x16-byte alternate signature keys. |
| 355 | `XePublicKeyData` | 284-byte RSA public key data. |
| 64 | `IoCompletionObjectType` | I/O completion object type. |
| 70 | `IoDeviceObjectType` | I/O device object type. |

**In JSRF-PC:** in the kernel data area at `XBOX_KERNEL_DATA_BASE` (src/kernel/xbox_memory_layout.h), each at its `KDATA_*` offset, set up by `xbox_kernel_bridge_init`, and found by `kernel_data_va_for_ordinal`. The kernel's timer thread moves `KeTickCount` (src/kernel/kernel_timer.c).

## Runtime Library (Rtl*)

| Ord | Function | Description |
|-----|----------|-------------|
| 289 | `VOID RtlInitAnsiString(PANSI_STRING Dest, const char* Src)` | Initialize ANSI_STRING from C string. |
| 260 | `NTSTATUS RtlAnsiStringToUnicodeString(PUNICODE_STRING Dest, PANSI_STRING Src, BOOLEAN Alloc)` | Convert ANSI to Unicode string. |
| 308 | `NTSTATUS RtlUnicodeStringToAnsiString(PANSI_STRING Dest, PUNICODE_STRING Src, BOOLEAN Alloc)` | Convert Unicode to ANSI string. |
| 279 | `BOOLEAN RtlEqualString(PANSI_STRING S1, PANSI_STRING S2, BOOLEAN CaseInsensitive)` | Compare two ANSI strings. |
| 269 | `ULONG RtlCompareMemoryUlong(PVOID Source, ULONG Length, ULONG Pattern)` | Scan memory for ULONG pattern. |
| 301 | `ULONG RtlNtStatusToDosError(NTSTATUS Status)` | Convert NTSTATUS to Win32 error code. |
| 304 | `BOOLEAN RtlTimeFieldsToTime(PTIME_FIELDS Fields, PLARGE_INTEGER Time)` | Convert time fields to NT time. |
| 305 | `VOID RtlTimeToTimeFields(PLARGE_INTEGER Time, PTIME_FIELDS Fields)` | Convert NT time to time fields. |
| 312 | `VOID RtlUnwind(PVOID TargetFrame, PVOID TargetIp, PVOID ExceptionRecord, PVOID ReturnValue)` | SEH stack unwinding. |
| 302 | `VOID RtlRaiseException(PVOID ExceptionRecord)` | Raise an exception. |
| 352 | `VOID RtlRip(PCHAR ApiName, PCHAR Expression, PCHAR Message)` | Debug assertion failure (debug builds). |

**In JSRF-PC:** the `bridge_Rtl*` functions, with src/kernel/kernel_rtl.c, on the guest's own strings and structures. `bridge_RtlUnwind` walks the guest's SEH chain and runs each handler's unwind.

## Object Manager

| Ord | Function | Description |
|-----|----------|-------------|
| 246 | `NTSTATUS ObReferenceObjectByHandle(HANDLE Handle, PVOID ObjectType, PVOID* Object)` | Get object pointer from handle. |
| 247 | `NTSTATUS ObReferenceObjectByName(PANSI_STRING Name, ULONG Attributes, PVOID ObjectType, PVOID ParseContext, PVOID* Object)` | Get object by name lookup. |
| 250 | `VOID ObfDereferenceObject(PVOID Object)` | Decrement object reference count. |

**In JSRF-PC:** `bridge_ObReferenceObjectByHandle` gives the handle back as the object, which the bridges that take an object resolve as they do a handle. `bridge_ObReferenceObjectByName`, `bridge_ObfReferenceObject` and `bridge_ObfDereferenceObject`, with src/kernel/kernel_ob.c.

## XBE Section Loading

| Ord | Function | Description |
|-----|----------|-------------|
| (varies) | `NTSTATUS XeLoadSection(PXBE_SECTION_HEADER Section)` | Demand-load an XBE section. |
| (varies) | `NTSTATUS XeUnloadSection(PXBE_SECTION_HEADER Section)` | Unload a demand-loaded section. |

**In JSRF-PC:** `bridge_XeLoadSection` and `bridge_XeUnloadSection` keep the section's reference count, its data being mapped at its VA with the rest of the XBE (src/kernel/xbox_memory_layout.c).

## Crypto

| Ord | Function | Description |
|-----|----------|-------------|
| 335 | `VOID XcSHAInit(PSHA_CONTEXT Context)` | Initialize SHA-1 context. |
| 336 | `VOID XcSHAUpdate(PSHA_CONTEXT Context, const UCHAR* Input, ULONG Length)` | Update SHA-1 hash. |
| 337 | `VOID XcSHAFinal(PSHA_CONTEXT Context, UCHAR* Digest)` | Finalize SHA-1 hash. |
| 338 | `VOID XcRC4Key(PRC4_CONTEXT Context, ULONG KeyLength, const UCHAR* Key)` | Initialize RC4 key schedule. |

**In JSRF-PC:** the `bridge_Xc*` functions, with SHA-1, RC4 and HMAC in software (src/kernel/kernel_crypto.c), on Linux and Windows.

## Debug

| Ord | Function | Description |
|-----|----------|-------------|
| 8 | `ULONG DbgPrint(const char* Format, ...)` | Debug printf (__cdecl, variadic). On real hardware, outputs to kernel debugger serial port. |
| 89 | `BOOLEAN KdDebuggerNotPresent` | Data export: TRUE on retail (no debugger attached). Game code checks this to skip debug output. |
| 8 | (unknown) | Unknown ordinal 8. Appears in some games' thunk tables. |

**In JSRF-PC:** `bridge_DbgBreakPoint`. `DbgPrint` uses the __cdecl calling convention (caller cleans stack), unlike most kernel functions, which use __stdcall.

## Network (Xbox Live)

| Ord | Function | Description |
|-----|----------|-------------|
| 252 | `ULONG PhyGetLinkState(BOOLEAN Verify)` | Get Ethernet link state. |
| 253 | `NTSTATUS PhyInitialize(BOOLEAN ForceReset, PVOID Param2)` | Initialize network PHY. |

**In JSRF-PC:** `bridge_PhyInitialize` and `bridge_PhyGetLinkState` (src/kernel/kernel_xbox.c) give the network PHY's state.

## Display / AV

| Ord | Function | Description |
|-----|----------|-------------|
| 1 | `ULONG AvGetSavedDataAddress(void)` | Get saved display configuration. |
| 2 | `VOID AvSendTVEncoderOption(PVOID RegisterBase, ULONG Option, ULONG Param, PULONG Result)` | Configure TV encoder (resolution, format). |
| 3 | `VOID AvSetDisplayMode(PVOID RegisterBase, ULONG Step, ULONG Mode, ULONG Format, ULONG Pitch, ULONG FrameBuffer)` | Set display mode. |
| 4 | `VOID AvSetSavedDataAddress(ULONG Address)` | Save display configuration. |

**In JSRF-PC:** the `bridge_Av*` functions. `bridge_AvSetDisplayMode` takes the framebuffer the title asks the display to show, and its format. The window is the renderer's (src/nv2a_gl, with SDL2 and OpenGL).

## Port I/O

| Ord | Function | Description |
|-----|----------|-------------|
| 333 | `VOID WRITE_PORT_BUFFER_USHORT(PUSHORT Port, PUSHORT Buffer, ULONG Count)` | Write to I/O port (16-bit). |
| 334 | `VOID WRITE_PORT_BUFFER_ULONG(PULONG Port, PULONG Buffer, ULONG Count)` | Write to I/O port (32-bit). |

**In JSRF-PC:** `bridge_WRITE_PORT_BUFFER_USHORT` and `bridge_WRITE_PORT_BUFFER_ULONG` take the call and write nothing: no device on those ports is modelled.
