/*
 * kernel_ob.c - Object Manager
 *
 * Implements object reference counting and handle-to-object resolution.
 *
 * The Xbox kernel uses a simplified NT object manager. Objects have reference
 * counts and can be looked up by handle or name. Since we use Win32 HANDLEs
 * directly, most of these are thin wrappers.
 *
 * ObfReferenceObject/ObfDereferenceObject use __fastcall on Xbox (parameter
 * in ECX register).
 */

#include "kernel.h"

/* ============================================================================
 * Reference Counting
 *
 * On Xbox, kernel objects have embedded reference counts. On Windows,
 * HANDLEs manage their own lifetime, so we track references loosely.
 *
 * ObfDereferenceObject: Decrements ref count. If it reaches zero on Xbox,
 * the object is freed. We approximate by closing the handle on deref.
 * However, since game code may hold multiple references, we only close
 * when explicitly told via NtClose. These become approximate no-ops.
 * ============================================================================ */

VOID __fastcall xbox_ObfReferenceObject(PVOID Object)
{
    /*
     * Increment reference count. With Win32 handles, the OS manages
     * the underlying reference count via DuplicateHandle/CloseHandle.
     * We don't need to track this separately for correctness.
     */
    (void)Object;
    XBOX_TRACE(XBOX_LOG_OB, "ObfReferenceObject: %p", Object);
}

VOID __fastcall xbox_ObfDereferenceObject(PVOID Object)
{
    /*
     * Decrement reference count. We don't close the handle here because
     * game code may still use it. The handle is closed via NtClose.
     */
    (void)Object;
    XBOX_TRACE(XBOX_LOG_OB, "ObfDereferenceObject: %p", Object);
}

/* ============================================================================
 * ObReferenceObjectByName
 *
 * Looks up a named kernel object. On Xbox, named objects include devices,
 * symbolic links, etc. Since we don't maintain a kernel object namespace,
 * this is stubbed to return not-found for most cases.
 * ============================================================================ */

NTSTATUS __stdcall xbox_ObReferenceObjectByName(
    PXBOX_ANSI_STRING ObjectName,
    ULONG Attributes,
    PVOID ObjectType,
    PVOID ParseContext,
    PVOID* Object)
{
    (void)Attributes;
    (void)ObjectType;
    (void)ParseContext;

    if (!Object)
        return STATUS_INVALID_PARAMETER;

    *Object = NULL;

    xbox_log(XBOX_LOG_WARN, XBOX_LOG_OB,
        "ObReferenceObjectByName: '%.*s' (not found - stubbed)",
        ObjectName ? ObjectName->Length : 0,
        ObjectName ? ObjectName->Buffer : "<null>");

    return STATUS_OBJECT_NAME_NOT_FOUND;
}
