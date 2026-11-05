/*
 * kernel_memory.c - Xbox Memory Management
 *
 * Implements Mm* and NtAllocateVirtualMemory/NtFreeVirtualMemory/NtQueryVirtualMemory
 * using Win32 VirtualAlloc/VirtualFree/VirtualQuery.
 *
 * Xbox contiguous memory (MmAllocateContiguousMemory) is used for GPU-accessible
 * buffers. The GPU's own resources are the renderer's (nv2a_gl); these
 * allocations just need to return valid CPU-accessible memory.
 */

#include "kernel.h"
#include <string.h>
#if defined(_WIN32)
/* _aligned_malloc/_aligned_free; POSIX gets them from win32_compat.h */
#include <malloc.h>
#include "xbox_memory_layout.h"   /* the guest view, for MEM_DECOMMIT */
#endif

/* ============================================================================
 * Helper: Xbox protect flags → Win32 protect flags
 * ============================================================================ */

static DWORD xbox_protect_to_win32(ULONG xbox_protect)
{
    /* Xbox uses the same PAGE_* constants as Windows NT */
    switch (xbox_protect & 0xFF) {
        case 0x01: return PAGE_NOACCESS;
        case 0x02: return PAGE_READONLY;
        case 0x04: return PAGE_READWRITE;
        case 0x08: return PAGE_WRITECOPY;
        case 0x10: return PAGE_EXECUTE;
        case 0x20: return PAGE_EXECUTE_READ;
        case 0x40: return PAGE_EXECUTE_READWRITE;
        default:   return PAGE_READWRITE;
    }
}

/* ============================================================================
 * Memory Query & Protection
 * ============================================================================ */

NTSTATUS __stdcall xbox_MmQueryStatistics(PXBOX_MM_STATISTICS MemoryStatistics)
{
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);

    if (!MemoryStatistics)
        return STATUS_INVALID_PARAMETER;

    if (!GlobalMemoryStatusEx(&ms))
        return STATUS_UNSUCCESSFUL;

    memset(MemoryStatistics, 0, sizeof(XBOX_MM_STATISTICS));
    MemoryStatistics->Length = sizeof(XBOX_MM_STATISTICS);

    /* Xbox has 64MB RAM. Report plausible values. */
    ULONG page_size = 4096;
    MemoryStatistics->TotalPhysicalPages = 64 * 1024 * 1024 / page_size; /* 16384 pages */
    MemoryStatistics->AvailablePages = (ULONG)(ms.ullAvailPhys / page_size);
    if (MemoryStatistics->AvailablePages > MemoryStatistics->TotalPhysicalPages)
        MemoryStatistics->AvailablePages = MemoryStatistics->TotalPhysicalPages / 2;

    return STATUS_SUCCESS;
}

VOID __stdcall xbox_MmUnmapIoSpace(PVOID BaseAddress, ULONG NumberOfBytes)
{
    XBOX_TRACE(XBOX_LOG_MEM, "MmUnmapIoSpace(%p, %u)", BaseAddress, NumberOfBytes);
    if (BaseAddress)
        VirtualFree(BaseAddress, 0, MEM_RELEASE);
}

ULONG_PTR __stdcall xbox_MmGetPhysicalAddress(PVOID BaseAddress)
{
    /* No physical address translation on Windows - return the VA as a placeholder */
    return (ULONG_PTR)BaseAddress;
}

ULONG __stdcall xbox_MmQueryAddressProtect(PVOID VirtualAddress)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(VirtualAddress, &mbi, sizeof(mbi))) {
        /* Return the Xbox-equivalent protection */
        return mbi.Protect;
    }
    return PAGE_NOACCESS;
}

VOID __stdcall xbox_MmSetAddressProtect(PVOID BaseAddress, ULONG NumberOfBytes, ULONG NewProtect)
{
    DWORD old_protect;
    VirtualProtect(BaseAddress, NumberOfBytes, xbox_protect_to_win32(NewProtect), &old_protect);
    XBOX_TRACE(XBOX_LOG_MEM, "MmSetAddressProtect(%p, %u, 0x%X)", BaseAddress, NumberOfBytes, NewProtect);
}

ULONG __stdcall xbox_MmQueryAllocationSize(PVOID BaseAddress)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(BaseAddress, &mbi, sizeof(mbi))) {
        return (ULONG)mbi.RegionSize;
    }
    return 0;
}

VOID __stdcall xbox_MmLockUnlockBufferPages(PVOID BaseAddress, ULONG NumberOfBytes, BOOLEAN UnlockPages)
{
    /* Page locking is not meaningful in user mode. No-op. */
    XBOX_TRACE(XBOX_LOG_MEM, "MmLockUnlockBufferPages(%p, %u, %d) - stub", BaseAddress, NumberOfBytes, UnlockPages);
}

VOID __stdcall xbox_MmLockUnlockPhysicalPage(ULONG_PTR PhysicalAddress, BOOLEAN UnlockPage)
{
    XBOX_TRACE(XBOX_LOG_MEM, "MmLockUnlockPhysicalPage(0x%08X, %d) - stub", (ULONG)PhysicalAddress, UnlockPage);
}

/* ============================================================================
 * Virtual Memory (Nt API)
 * ============================================================================ */

NTSTATUS __stdcall xbox_NtFreeVirtualMemory(
    PVOID* BaseAddress,
    PSIZE_T RegionSize,
    ULONG FreeType)
{
    if (!BaseAddress || !*BaseAddress)
        return STATUS_INVALID_PARAMETER;

    SIZE_T size = (FreeType & MEM_RELEASE) ? 0 : (RegionSize ? *RegionSize : 0);

#if defined(_WIN32)
    /* Guest RAM is one view of a section (xbox_memory_layout.c), and
     * VirtualFree decommits only what VirtualAlloc made: on a view it fails
     * with ERROR_INVALID_PARAMETER, every time -- 7,229 times in a 110 s run
     * of JSRF, each one a warning below. Do what the POSIX shim's VirtualFree
     * does instead: take access away from the whole pages inside the range.
     * Committing them again (NtAllocateVirtualMemory with MEM_COMMIT, or the
     * MEM_RELEASE of a heap block) already gives it back. */
    if ((FreeType & MEM_DECOMMIT) && !(FreeType & MEM_RELEASE)) {
        uintptr_t view = (uintptr_t)xbox_GetMemoryBase();
        uintptr_t a = (uintptr_t)*BaseAddress;
        if (view && a >= view && a + size <= view + xbox_GetMappedSize()) {
            uintptr_t lo = (a + 0xFFF) & ~(uintptr_t)0xFFF;
            uintptr_t hi = (a + size) & ~(uintptr_t)0xFFF;
            DWORD old;
            if (hi > lo && !VirtualProtect((void *)lo, hi - lo, PAGE_NOACCESS, &old)) {
                xbox_log(XBOX_LOG_WARN, XBOX_LOG_MEM,
                    "NtFreeVirtualMemory: decommit of %p (%u bytes) in the guest view failed: err=%u",
                    *BaseAddress, (ULONG)size, GetLastError());
                return STATUS_UNSUCCESSFUL;
            }
            XBOX_TRACE(XBOX_LOG_MEM, "NtFreeVirtualMemory(%p, 0x%X) in the guest view",
                *BaseAddress, FreeType);
            return STATUS_SUCCESS;
        }
    }
#endif

    if (!VirtualFree(*BaseAddress, size, FreeType)) {
        xbox_log(XBOX_LOG_WARN, XBOX_LOG_MEM,
            "NtFreeVirtualMemory failed: base=%p type=0x%X err=%u",
            *BaseAddress, FreeType, GetLastError());
        return STATUS_UNSUCCESSFUL;
    }

    XBOX_TRACE(XBOX_LOG_MEM, "NtFreeVirtualMemory(%p, 0x%X)", *BaseAddress, FreeType);

    if (FreeType & MEM_RELEASE)
        *BaseAddress = NULL;

    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_NtQueryVirtualMemory(
    PVOID BaseAddress,
    PVOID MemoryInformation,
    ULONG MemoryInformationLength,
    PULONG ReturnLength)
{
    MEMORY_BASIC_INFORMATION mbi;

    if (!VirtualQuery(BaseAddress, &mbi, sizeof(mbi)))
        return STATUS_INVALID_PARAMETER;

    /*
     * Xbox NtQueryVirtualMemory returns a MEMORY_BASIC_INFORMATION-like struct.
     * Copy what fits into the caller's buffer.
     */
    ULONG copy_size = (MemoryInformationLength < sizeof(mbi)) ? MemoryInformationLength : (ULONG)sizeof(mbi);
    memcpy(MemoryInformation, &mbi, copy_size);

    if (ReturnLength)
        *ReturnLength = copy_size;

    return STATUS_SUCCESS;
}
