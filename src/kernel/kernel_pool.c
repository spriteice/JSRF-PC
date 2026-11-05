/*
 * kernel_pool.c - Xbox Pool Allocator
 *
 * Implements ExAllocatePool, ExAllocatePoolWithTag, ExFreePool,
 * ExQueryPoolBlockSize using the Win32 process heap.
 *
 * On Xbox, pool memory is kernel-mode nonpaged/paged pool.
 * Since we run entirely in user mode on Windows, we just use HeapAlloc.
 */

#include "kernel.h"

VOID __stdcall xbox_ExFreePool(PVOID P)
{
    XBOX_TRACE(XBOX_LOG_POOL, "ExFreePool(%p)", P);
    if (P) {
        HeapFree(GetProcessHeap(), 0, P);
    }
}
