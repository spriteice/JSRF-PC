/*
 * kernel_hal.c - Hardware Abstraction Layer
 *
 * Implements IRQL simulation, performance counters, system time,
 * processor stalls, bug checks, floating point state, and hardware stubs.
 *
 * The Xbox HAL provides low-level hardware access that doesn't exist on
 * a standard Windows PC. Most of these functions are either:
 *   - Directly mappable (perf counters, system time)
 *   - Simulated (IRQL tracking via TLS)
 *   - Stubbed (PCI access, SMC, interrupts)
 */

#include "kernel.h"
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <intrin.h>
#endif

/* ============================================================================
 * IRQL Simulation
 *
 * Xbox uses IRQL (Interrupt Request Level) for synchronization:
 *   PASSIVE_LEVEL (0) - normal thread execution
 *   APC_LEVEL (1) - APC delivery
 *   DISPATCH_LEVEL (2) - scheduler/DPC level, no page faults allowed
 *
 * On Windows, we simulate IRQL with a thread-local variable. Raising to
 * DISPATCH_LEVEL doesn't actually prevent preemption, but the tracking
 * allows code that checks IRQL to function correctly.
 * ============================================================================ */

static XBOX_THREAD_LOCAL KIRQL g_current_irql = PASSIVE_LEVEL;

/* What this thread's IRQL is, for code outside this file.
 *
 * It exists because IRQL here is bookkeeping and not enforcement: raising to
 * DISPATCH_LEVEL records a number and prevents nothing. That is harmless
 * while nothing delivers interrupts to a guest thread, and not harmless once
 * something does -- a driver raises IRQL precisely to keep its own interrupt
 * handler out of a structure it is halfway through rewriting, and delivering
 * the handler anyway corrupts exactly what the raise was protecting. So
 * whoever delivers an interrupt has to ask first. */
KIRQL xbox_KeCurrentIrql(void);
KIRQL xbox_KeCurrentIrql(void) { return g_current_irql; }

/*
 * KfRaiseIrql - Raises IRQL to the specified level.
 * Returns the previous IRQL. Uses __fastcall (ECX = NewIrql).
 */
KIRQL __fastcall xbox_KfRaiseIrql(KIRQL NewIrql)
{
    KIRQL old = g_current_irql;

    if (NewIrql < old) {
        xbox_log(XBOX_LOG_WARN, XBOX_LOG_HAL,
            "KfRaiseIrql: attempt to lower IRQL from %d to %d (use KfLowerIrql)",
            old, NewIrql);
    }

    g_current_irql = NewIrql;
    return old;
}

/*
 * KfLowerIrql - Lowers IRQL to the specified level.
 * Uses __fastcall (ECX = NewIrql).
 */
VOID __fastcall xbox_KfLowerIrql(KIRQL NewIrql)
{
    if (NewIrql > g_current_irql) {
        xbox_log(XBOX_LOG_WARN, XBOX_LOG_HAL,
            "KfLowerIrql: attempt to raise IRQL from %d to %d (use KfRaiseIrql)",
            g_current_irql, NewIrql);
    }

    g_current_irql = NewIrql;
}

/*
 * KeRaiseIrqlToDpcLevel - Convenience function to raise to DISPATCH_LEVEL.
 */
KIRQL __stdcall xbox_KeRaiseIrqlToDpcLevel(void)
{
    KIRQL old = g_current_irql;
    g_current_irql = DISPATCH_LEVEL;
    return old;
}

/* ============================================================================
 * KeTickCount
 *
 * Exported as a data pointer, not a function. The Xbox kernel increments
 * this every ~1ms (approximating the Xbox tick interval).
 * Updated lazily when read, using GetTickCount.
 * ============================================================================ */

volatile ULONG xbox_KeTickCount = 0;

/* Call this periodically or on-demand to update KeTickCount */
static void xbox_update_tick_count(void)
{
    xbox_KeTickCount = GetTickCount();
}

/* ============================================================================
 * Performance Counters
 *
 * Direct 1:1 mapping to Win32 QueryPerformanceCounter/Frequency.
 * Both Xbox and Windows return LARGE_INTEGER.
 * ============================================================================ */

LARGE_INTEGER __stdcall xbox_KeQueryPerformanceCounter(void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return counter;
}

LARGE_INTEGER __stdcall xbox_KeQueryPerformanceFrequency(void)
{
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    return freq;
}

/* ============================================================================
 * System Time
 *
 * KeQuerySystemTime returns the current time as a FILETIME (100ns since
 * January 1, 1601). Direct Win32 mapping.
 * ============================================================================ */

VOID __stdcall xbox_KeQuerySystemTime(PLARGE_INTEGER CurrentTime)
{
    if (CurrentTime)
        GetSystemTimeAsFileTime((LPFILETIME)CurrentTime);
}

/* ============================================================================
 * Processor Stall
 *
 * KeStallExecutionProcessor performs a busy-wait for the given number
 * of microseconds. Used for hardware timing (e.g., waiting for GPU).
 * ============================================================================ */

VOID __stdcall xbox_KeStallExecutionProcessor(ULONG MicroSeconds)
{
    LARGE_INTEGER freq, start, now;

    if (MicroSeconds == 0)
        return;

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    LONGLONG target_counts = (freq.QuadPart * MicroSeconds) / 1000000;

    do {
        QueryPerformanceCounter(&now);
    } while ((now.QuadPart - start.QuadPart) < target_counts);
}

/* ============================================================================
 * Floating Point State
 *
 * Xbox kernel requires saving/restoring FP state when kernel code uses
 * floating point. On Windows user-mode this is handled automatically by
 * the OS, so these are no-ops.
 * ============================================================================ */

NTSTATUS __stdcall xbox_KeSaveFloatingPointState(PVOID FloatingPointState)
{
    (void)FloatingPointState;
    /* No-op: Windows user-mode preserves FP state across context switches */
    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_KeRestoreFloatingPointState(PVOID FloatingPointState)
{
    (void)FloatingPointState;
    return STATUS_SUCCESS;
}

/* ============================================================================
 * Bug Check (Blue Screen of Death)
 *
 * KeBugCheck/KeBugCheckEx are the Xbox equivalent of BSOD. In our
 * recompilation, we log the error and terminate the process.
 * ============================================================================ */

VOID __stdcall xbox_KeBugCheck(ULONG BugCheckCode)
{
    xbox_log(XBOX_LOG_ERROR, XBOX_LOG_HAL,
        "*** KeBugCheck: code=0x%08X ***", BugCheckCode);

#ifdef _DEBUG
    DebugBreak();
#endif

    ExitProcess(BugCheckCode);
}

VOID __stdcall xbox_KeBugCheckEx(
    ULONG BugCheckCode,
    ULONG_PTR Param1,
    ULONG_PTR Param2,
    ULONG_PTR Param3,
    ULONG_PTR Param4)
{
    xbox_log(XBOX_LOG_ERROR, XBOX_LOG_HAL,
        "*** KeBugCheckEx: code=0x%08X, params=(0x%p, 0x%p, 0x%p, 0x%p) ***",
        BugCheckCode, (void*)Param1, (void*)Param2, (void*)Param3, (void*)Param4);

#ifdef _DEBUG
    DebugBreak();
#endif

    ExitProcess(BugCheckCode);
}

/* ============================================================================
 * HAL PCI Access
 *
 * HalReadWritePCISpace reads/writes PCI configuration space. The Xbox uses
 * this for GPU and southbridge setup. Not needed on Windows - stub it.
 * ============================================================================ */

VOID __stdcall xbox_HalReadWritePCISpace(
    ULONG BusNumber,
    ULONG SlotNumber,
    ULONG RegisterNumber,
    PVOID Buffer,
    ULONG Length,
    BOOLEAN WritePCISpace)
{
    (void)BusNumber;
    (void)SlotNumber;
    (void)RegisterNumber;
    (void)Length;
    (void)WritePCISpace;

    /* Return zeroed buffer for reads */
    if (!WritePCISpace && Buffer)
        memset(Buffer, 0, Length);

    xbox_log(XBOX_LOG_TRACE, XBOX_LOG_HAL,
        "HalReadWritePCISpace: bus=%u slot=%u reg=0x%X len=%u %s (stubbed)",
        BusNumber, SlotNumber, RegisterNumber, Length,
        WritePCISpace ? "WRITE" : "READ");
}

/* ============================================================================
 * HAL Firmware & Shutdown
 *
 * HalReturnToFirmware returns to the Xbox dashboard. For us, this means
 * exit the game cleanly.
 * ============================================================================ */

VOID __stdcall xbox_HalReturnToFirmware(ULONG Routine)
{
    xbox_log(XBOX_LOG_INFO, XBOX_LOG_HAL,
        "HalReturnToFirmware: routine=%u (exiting)", Routine);
    ExitProcess(0);
}

VOID __stdcall xbox_HalInitiateShutdown(void)
{
    xbox_log(XBOX_LOG_INFO, XBOX_LOG_HAL, "HalInitiateShutdown (exiting)");
    ExitProcess(0);
}

BOOLEAN __stdcall xbox_HalIsResetOrShutdownPending(void)
{
    return FALSE;
}

/* ============================================================================
 * SMC (System Management Controller)
 *
 * HalReadSMCTrayState reads the DVD tray state. No disc tray on PC.
 * ============================================================================ */

ULONG __stdcall xbox_HalReadSMCTrayState(PULONG TrayState, PULONG TrayStateChangeCount)
{
    /* Tray state: 0x10 = media detected (disc present) */
    if (TrayState)
        *TrayState = 0x10;
    if (TrayStateChangeCount)
        *TrayStateChangeCount = 0;
    return 0; /* Success */
}

/* ============================================================================
 * Software Interrupts
 *
 * Used for APC/DPC delivery on Xbox. Stubbed since we don't have real
 * interrupt-driven DPC delivery.
 * ============================================================================ */

VOID __fastcall xbox_HalClearSoftwareInterrupt(KIRQL RequestIrql)
{
    (void)RequestIrql;
}

VOID __fastcall xbox_HalRequestSoftwareInterrupt(KIRQL RequestIrql)
{
    (void)RequestIrql;
}

VOID __stdcall xbox_HalDisableSystemInterrupt(ULONG BusInterruptLevel, KIRQL Irql)
{
    (void)BusInterruptLevel;
    (void)Irql;
}

ULONG __stdcall xbox_HalGetInterruptVector(ULONG BusInterruptLevel, PKIRQL Irql)
{
    (void)BusInterruptLevel;
    if (Irql)
        *Irql = PASSIVE_LEVEL;
    return 0;
}

/* ============================================================================
 * Interrupt Objects
 *
 * Used by DSOUND and other drivers for hardware interrupt handling.
 * Since we replace the audio/graphics subsystems entirely, these are stubs.
 * ============================================================================ */

VOID __stdcall xbox_KeInitializeInterrupt(
    PXBOX_KINTERRUPT Interrupt,
    PVOID ServiceRoutine,
    PVOID ServiceContext,
    ULONG Vector,
    KIRQL Irql,
    ULONG InterruptMode,
    BOOLEAN ShareVector)
{
    (void)Vector;
    (void)InterruptMode;
    (void)ShareVector;

    if (!Interrupt)
        return;

    Interrupt->ServiceRoutine = ServiceRoutine;
    Interrupt->ServiceContext = ServiceContext;
    Interrupt->Irql = Irql;
    Interrupt->Connected = FALSE;

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_HAL,
        "KeInitializeInterrupt: interrupt=%p, routine=%p, vector=%u",
        Interrupt, ServiceRoutine, Vector);
}

BOOLEAN __stdcall xbox_KeConnectInterrupt(PXBOX_KINTERRUPT Interrupt)
{
    if (!Interrupt)
        return FALSE;

    Interrupt->Connected = TRUE;

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_HAL,
        "KeConnectInterrupt: interrupt=%p (stubbed - no real HW interrupts)",
        Interrupt);

    return TRUE;
}

BOOLEAN __stdcall xbox_KeDisconnectInterrupt(PXBOX_KINTERRUPT Interrupt)
{
    BOOLEAN was_connected;

    if (!Interrupt)
        return FALSE;

    /* Returns the PREVIOUS connected state, not success. */
    was_connected = Interrupt->Connected;
    Interrupt->Connected = FALSE;

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_HAL,
        "KeDisconnectInterrupt: interrupt=%p was_connected=%d",
        Interrupt, (int)was_connected);

    return was_connected;
}

/* ============================================================================
 * Miscellaneous Port I/O Stubs
 * ============================================================================ */

VOID __stdcall xbox_WRITE_PORT_BUFFER_ULONG(PULONG Port, PULONG Buffer, ULONG Count)
{
    (void)Port;
    (void)Buffer;
    (void)Count;
}

VOID __stdcall xbox_WRITE_PORT_BUFFER_USHORT(PUSHORT Port, PUSHORT Buffer, ULONG Count)
{
    (void)Port;
    (void)Buffer;
    (void)Count;
}

/* ============================================================================
 * System Time (Set)
 *
 * NtSetSystemTime - we don't actually change the system clock, just log it.
 * ============================================================================ */

NTSTATUS __stdcall xbox_NtSetSystemTime(PLARGE_INTEGER SystemTime, PLARGE_INTEGER PreviousTime)
{
    if (PreviousTime)
        GetSystemTimeAsFileTime((LPFILETIME)PreviousTime);

    xbox_log(XBOX_LOG_WARN, XBOX_LOG_HAL,
        "NtSetSystemTime: ignored (not setting system clock)");

    return STATUS_SUCCESS;
}

/* ============================================================================
 * Display / AV
 *
 * These are declared in kernel.h for the thunk table but will be fully
 * implemented by the D3D replacement layer. We provide realistic AV pack
 * detection so games can query display capabilities (480p, 720p, widescreen).
 * ============================================================================ */

static ULONG g_av_saved_data_address = 0;
static ULONG g_av_display_mode = 0;

ULONG __stdcall xbox_AvGetSavedDataAddress(void)
{
    return g_av_saved_data_address;
}

VOID __stdcall xbox_AvSendTVEncoderOption(
    PVOID RegisterBase, ULONG Option, ULONG Param, PULONG Result)
{
    (void)RegisterBase;
    (void)Param;

    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_HAL,
        "AvSendTVEncoderOption: option=0x%02X param=0x%X", Option, Param);

    if (!Result)
        return;

    switch (Option) {
    case AV_OPTION_QUERY_AVPACK:
        /* Pack type, video standard and refresh rate, in one word.
         *
         * D3D keys its display-mode table on all three: the row flags carry
         * the pack in 0x000000FF, the standard in 0x0000FF00 and the refresh
         * in 0x00C00000 (Half-Life 2's 640x480 60Hz row is 0x00480104).
         * Returning the pack alone left the standard as 0, which matches no
         * row, so the mode scan ran off the end of the table and device
         * creation failed with E_FAIL.
         *
         * HDTV pack keeps 480p/720p available to titles that offer them;
         * NTSC-M and 60Hz are the North American retail default, and match
         * the region reported by ExQueryNonVolatileSetting. */
        *Result = AV_PACK_HDTV
                | (AV_STANDARD_NTSC_M << AV_STANDARD_SHIFT)
                | AV_REFRESH_60Hz;
        break;

    case AV_OPTION_QUERY_MODE:
        /* Return current display mode */
        *Result = g_av_display_mode;
        break;

    case AV_OPTION_QUERY_AV_CAPABILITIES:
        /* Report support for 480i, 480p, 720p, and widescreen */
        *Result = AV_FLAGS_HDTV_480i | AV_FLAGS_HDTV_480p
                | AV_FLAGS_HDTV_720p | AV_FLAGS_WIDESCREEN
                | AV_FLAGS_60Hz;
        break;

    case AV_OPTION_QUERY_ENCODER_TYPE:
        /* Conexant CX25871 (common in retail Xboxes) */
        *Result = 4;
        break;

    case AV_OPTION_QUERY_MODE_CAPS:
        /* Same as capabilities for our purposes */
        *Result = AV_FLAGS_HDTV_480i | AV_FLAGS_HDTV_480p
                | AV_FLAGS_HDTV_720p | AV_FLAGS_WIDESCREEN
                | AV_FLAGS_60Hz;
        break;

    case AV_OPTION_SET_MODE:
        g_av_display_mode = Param;
        *Result = 0;
        break;

    case AV_OPTION_BLANK_SCREEN:
    case AV_OPTION_MACROVISION_MODE:
    case AV_OPTION_FLICKER_FILTER:
    case AV_OPTION_ZERO_MODE:
        *Result = 0;
        break;

    default:
        xbox_log(XBOX_LOG_WARN, XBOX_LOG_HAL,
            "AvSendTVEncoderOption: unknown option 0x%02X", Option);
        *Result = 0;
        break;
    }
}

VOID __stdcall xbox_AvSetSavedDataAddress(ULONG Address)
{
    g_av_saved_data_address = Address;
}

VOID __stdcall xbox_AvSetDisplayMode(
    PVOID RegisterBase, ULONG Step, ULONG Mode,
    ULONG Format, ULONG Pitch, ULONG FrameBuffer)
{
    (void)RegisterBase;
    (void)Step;
    (void)Format;
    (void)Pitch;
    (void)FrameBuffer;

    g_av_display_mode = Mode;

    xbox_log(XBOX_LOG_INFO, XBOX_LOG_HAL,
        "AvSetDisplayMode: step=%u mode=0x%X format=0x%X pitch=%u fb=0x%X",
        Step, Mode, Format, Pitch, FrameBuffer);
}

/* ============================================================================
 * SMBus - HalReadSMBusValue / HalWriteSMBusValue
 *
 * The Xbox SMBus connects the CPU to the System Management Controller (SMC),
 * EEPROM, temperature sensor, and TV encoder. Games use these to detect
 * AV pack type, read EEPROM settings, and check hardware state.
 *
 * We simulate responses for the most commonly queried devices:
 *   - SMC (0x20): firmware version, tray state, AV pack, temperatures
 *   - EEPROM (0xA8): handled separately via ExQueryNonVolatileSetting
 *   - Temperature sensor (0x98): CPU/board temperatures
 * ============================================================================ */

NTSTATUS __stdcall xbox_HalReadSMBusValue(
    UCHAR SlaveAddress, UCHAR CommandCode, BOOLEAN ReadWordValue, PULONG DataValue)
{
    if (!DataValue)
        return STATUS_INVALID_PARAMETER;

    *DataValue = 0;

    switch (SlaveAddress) {
    case SMC_SLAVE_ADDRESS:  /* 0x20 - System Management Controller */
        switch (CommandCode) {
        case SMC_CMD_FIRMWARE_VER:
            /* "P01" = production SMC, return 'P' for first byte.
             * Games read version byte-by-byte: P(0x50), 0(0x30), 1(0x31) */
            *DataValue = 0x50; /* 'P' */
            break;
        case SMC_CMD_TRAY_STATE:
            /* 0x60 = media present, tray closed */
            *DataValue = 0x60;
            break;
        case SMC_CMD_AV_PACK:
            /* HDTV/Component pack */
            *DataValue = AV_PACK_HDTV;
            break;
        case SMC_CMD_CPU_TEMP:
            *DataValue = 40; /* 40 degrees C */
            break;
        case SMC_CMD_MB_TEMP:
            *DataValue = 35; /* 35 degrees C */
            break;
        case SMC_CMD_FAN_SPEED:
            *DataValue = 50; /* ~50% fan speed */
            break;
        case SMC_CMD_INTERRUPT_REASON:
            *DataValue = 0;  /* No pending interrupt */
            break;
        case SMC_CMD_ERROR_CODE:
            *DataValue = 0;  /* No error */
            break;
        default:
            xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_HAL,
                "HalReadSMBusValue: SMC unknown cmd=0x%02X", CommandCode);
            break;
        }
        break;

    case TEMP_SLAVE_ADDRESS:  /* 0x98 - ADM1032 temperature sensor */
        /* CommandCode 0x00 = local temp, 0x01 = remote temp */
        if (CommandCode == 0x00)
            *DataValue = 35;  /* Board: 35C */
        else if (CommandCode == 0x01)
            *DataValue = 40;  /* CPU: 40C */
        else
            *DataValue = 30;
        break;

    case ENCODER_SLAVE_ADDRESS:  /* 0xD4 - TV encoder */
        /* Return 0 for most encoder register reads */
        *DataValue = 0;
        break;

    default:
        xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_HAL,
            "HalReadSMBusValue: unknown slave=0x%02X cmd=0x%02X",
            SlaveAddress, CommandCode);
        break;
    }

    xbox_log(XBOX_LOG_TRACE, XBOX_LOG_HAL,
        "HalReadSMBusValue: slave=0x%02X cmd=0x%02X word=%d -> 0x%X",
        SlaveAddress, CommandCode, ReadWordValue, *DataValue);

    (void)ReadWordValue;
    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_HalWriteSMBusValue(
    UCHAR SlaveAddress, UCHAR CommandCode, BOOLEAN WriteWordValue, ULONG DataValue)
{
    (void)WriteWordValue;

    xbox_log(XBOX_LOG_TRACE, XBOX_LOG_HAL,
        "HalWriteSMBusValue: slave=0x%02X cmd=0x%02X word=%d val=0x%X (ignored)",
        SlaveAddress, CommandCode, WriteWordValue, DataValue);

    /* Writes to SMC (LED control, fan speed, etc.) are silently accepted */
    return STATUS_SUCCESS;
}

/* ============================================================================
 * HAL Data Exports
 *
 * Ordinals 40, 41 and 42 are variables, not functions. Games read them
 * directly through the thunk table, so the thunk must hand back the address
 * of real storage -- pointing these at a function is what produced garbage
 * disk metadata before.
 *
 * The strings are counted (Length/MaximumLength), not NUL-terminated, matching
 * the kernel's STRING type. Values describe the virtual disk we present; no
 * real hardware is queried.
 * ============================================================================ */

ULONG xbox_HalDiskCachePartitionCount = 3;

static char g_disk_model[]  = "XBOXRECOMP VIRTUAL HDD";
static char g_disk_serial[] = "XR0000000000";

XBOX_ANSI_STRING xbox_HalDiskModelNumber = {
    sizeof(g_disk_model) - 1,
    sizeof(g_disk_model) - 1,
    g_disk_model
};

XBOX_ANSI_STRING xbox_HalDiskSerialNumber = {
    sizeof(g_disk_serial) - 1,
    sizeof(g_disk_serial) - 1,
    g_disk_serial
};

/*
 * Video mode the SMC reported at boot. 0 lets title code fall back to querying
 * the AV pack, which we answer properly in AvGetSavedDataAddress/SMBus.
 */
ULONG xbox_HalBootSMCVideoMode = 0;

/*
 * IDE channel object. Real kernels export a device object for the ATA channel;
 * drivers only ever pass it back to us, so identity is all that is required.
 */
static ULONG g_idex_channel_data = 0x49444558; /* 'IDEX' */
PVOID xbox_IdexChannelObject = &g_idex_channel_data;

/* ============================================================================
 * Shutdown Notification
 * ============================================================================ */

VOID __stdcall xbox_HalRegisterShutdownNotification(
    PVOID ShutdownRegistration,
    BOOLEAN Register)
{
    /*
     * Registers a callback to run on reboot/shutdown. We never initiate an
     * Xbox-style shutdown -- HalReturnToFirmware terminates the process -- so
     * the callback would never fire. Recorded in the log so a title relying on
     * shutdown cleanup is visible rather than silently ignored.
     */
    xbox_log(XBOX_LOG_DEBUG, XBOX_LOG_HAL,
        "HalRegisterShutdownNotification: %s registration=%p (never invoked)",
        Register ? "register" : "unregister", ShutdownRegistration);
}

/* ============================================================================
 * Unknown Ordinal Stubs
 * ============================================================================ */

VOID __stdcall xbox_Unknown_8(void)
{
    xbox_log(XBOX_LOG_WARN, XBOX_LOG_HAL, "Unknown ordinal 8 called (stubbed)");
}

VOID __stdcall xbox_Unknown_23(void)
{
    xbox_log(XBOX_LOG_WARN, XBOX_LOG_HAL, "Unknown ordinal 23 called (stubbed)");
}

VOID __stdcall xbox_Unknown_42(void)
{
    xbox_log(XBOX_LOG_WARN, XBOX_LOG_HAL, "Unknown ordinal 42 called (stubbed)");
}

/* ============================================================================
 * Debug / Timing
 * ============================================================================ */

VOID __stdcall xbox_DbgBreakPoint(void)
{
    /*
     * Titles call this from assertion paths. Under a debugger this should
     * break; without one, raising a breakpoint exception would terminate the
     * process on a condition the title may well survive. Log loudly and
     * continue, and only actually break when a debugger is attached to catch it.
     */
    xbox_log(XBOX_LOG_WARN, XBOX_LOG_HAL, "DbgBreakPoint called by title");

    if (IsDebuggerPresent())
        DebugBreak();
}

ULONGLONG __stdcall xbox_KeQueryInterruptTime(void)
{
    /*
     * Time since boot in NT 100ns units. GetTickCount64 is milliseconds, so
     * scale by 10,000. Resolution is coarser than the real kernel's, but it is
     * monotonic, which is the property callers actually depend on.
     */
    return (ULONGLONG)GetTickCount64() * 10000ULL;
}

/* ============================================================================
 * Time stamp counter
 *
 * Xbox's QueryPerformanceCounter is a bare `rdtsc`, and its
 * QueryPerformanceFrequency returns the CPU clock as a constant the title
 * compiles in: Half-Life 2's is 0x2BB5C755 (733,333,333 Hz) at 0x0059C6C7.
 * So a frame timer computes seconds as counter / 733333333.
 *
 * Returning the host's own TSC would make that division wrong by the ratio of
 * the two clocks -- a 3.5 GHz host would have the guest believe nearly five
 * seconds had passed for every real one. Scaling the host's performance
 * counter to the console's rate keeps the guest's arithmetic honest.
 *
 * Monotonic and shared by every thread, which is what a TSC is. The first
 * call establishes the origin so the counter starts near zero rather than at
 * whatever the host had been running for.
 * ========================================================================= */
#define XBOX_TSC_HZ 733333333ull

uint64_t xbox_ReadTimeStampCounter(void)
{
    static LARGE_INTEGER freq;
    static LARGE_INTEGER origin;
    LARGE_INTEGER now;

    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&origin);
        if (freq.QuadPart == 0)
            freq.QuadPart = 1;
    }
    QueryPerformanceCounter(&now);

    {
        uint64_t ticks = (uint64_t)(now.QuadPart - origin.QuadPart);
        /* Split the scaling so a long run cannot overflow: whole seconds
         * first, then the remainder. */
        uint64_t secs = ticks / (uint64_t)freq.QuadPart;
        uint64_t rem  = ticks % (uint64_t)freq.QuadPart;
        return secs * XBOX_TSC_HZ
             + (rem * XBOX_TSC_HZ) / (uint64_t)freq.QuadPart;
    }
}

/* =========================================================================
 * x86 port I/O
 *
 * The lifter turns `in`/`out` into these. The MCPX decodes I/O space: its
 * ACPI/PM block sits at 0x8000 and SMBus at 0xC000, and the kernel exports
 * cover SMBus, so a title touches ports directly only for the odd pin.
 *
 * 0x80C0 is the first GPIO register of the ACPI block. Bit 5 is the field pin
 * of the video encoder: D3D's vblank ISR stores !bit5 in [device+0x1DF8], and
 * D3DDevice_GetDisplayFieldStatus reports D3DFIELD_ODD while that is set.
 * Interlaced output alternates fields every vblank, so the pin follows the
 * parity of the vblank count. A constant answer says "odd field" for ever,
 * and the XDK's XMV player only starts its clock on an even one: Burnout 3's
 * EA intro waited on its loading screen indefinitely. JSRF's ISR reads it at
 * 0x0018E500.
 *
 * The count is g_vblank_count (kernel_bridge.c), bumped once per tick by the
 * timer thread. Two regimes follow from it:
 *  - tick-driven (RECOMP_VBLANK=1, once the first vblank is counted): the pin
 *    is the count's parity and holds still between vblanks, however often
 *    it is read;
 *  - no tick (before the first vblank, or the whole run with RECOMP_VBLANK
 *    off): the count is 0, so the pin toggles on every read instead. A
 *    reader waiting for either field still sees it; nothing is implied
 *    about vblank delivery.
 *
 * Every other port reads 0 and is logged on first use, once per port.
 * ========================================================================= */
extern unsigned long g_vblank_count;

static int port_first_use(uint32_t port, int is_out)
{
    static uint32_t seen[32];
    static volatile LONG nseen;
    uint32_t key = (port & 0xFFFFu) | (is_out ? 0x10000u : 0u);
    LONG i, n = nseen;

    for (i = 0; i < n && i < 32; i++)
        if (seen[i] == key)
            return 0;
    i = InterlockedIncrement(&nseen) - 1;
    if (i < 32)
        seen[i] = key;
    return i < 32;
}

uint32_t xbox_PortIn(uint32_t port, int width)
{
    uint32_t v = 0;

    port &= 0xFFFFu;
    if (port == 0x80C0u) {
        static volatile LONG reads;
        uint32_t n = (uint32_t)*(volatile unsigned long *)&g_vblank_count;
        if (!n)
            n = (uint32_t)InterlockedIncrement(&reads);
        v = (n & 1u) << 5;
        return v;
    }
    if (port_first_use(port, 0))
        xbox_log(XBOX_LOG_WARN, XBOX_LOG_HAL, "in%c 0x%04X -> 0 (not modelled)",
            width == 1 ? 'b' : width == 2 ? 'w' : 'd', port);
    return v;
}

void xbox_PortOut(uint32_t port, uint32_t value, int width)
{
    port &= 0xFFFFu;
    if (port_first_use(port, 1))
        xbox_log(XBOX_LOG_WARN, XBOX_LOG_HAL, "out%c 0x%04X = 0x%X (ignored)",
            width == 1 ? 'b' : width == 2 ? 'w' : 'd', port, value);
}

/* =========================================================================
 * cpuid
 *
 * The Xbox CPU is a 733 MHz Coppermine-based Pentium III with MMX, FXSR and
 * SSE, and that is what is reported by default. Leaf 1 eax 0x683 is family
 * 6, model 8 (Coppermine), stepping 3: one of the Coppermine signatures in
 * Intel's Pentium III Processor Specification Update. Which stepping the
 * console ships is not documented, so software should key on family and
 * model only. The max basic leaf is 2 and there are no extended leaves.
 * Leaf 1's edx is the P3 feature set (FPU..CMOV, PAT, PSE-36, MMX, FXSR,
 * SSE).
 *
 * Leaf 2 gives the P6 TLB and 16 KB 4-way L1 descriptors (0x01-0x04, 0x08,
 * 0x0C). The L2 byte is 0, the null descriptor: the console's 128 KB L2 is
 * reported to be 8-way, and the SDM's 128 KB 32-byte-line descriptor (0x41)
 * says 4-way, so no L2 is described rather than a wrong one.
 *
 * A leaf above the maximum, extended ones included, returns the highest
 * basic leaf's data: Intel SDM vol. 2A, CPUID, "If a value entered for
 * CPUID.EAX is higher than the maximum input value for basic or extended
 * function for that processor then the data for the highest basic
 * information leaf is returned." So 0x80000000 reads eax 0x03020101, below
 * 0x80000001, which is how software learns there are no extended leaves.
 *
 * RECOMP_CPUID_NO_SIMD=1 is an opt-out for a title whose MMX or SSE paths
 * do not lift correctly: it clears MMX, FXSR and SSE together (bits 23-25),
 * so the title takes its scalar path. Masking only MMX would describe a CPU
 * with SSE but no MMX, which never existed: SSE's integer extensions use
 * the MMX registers, and FXSAVE/FXRSTOR arrived with SSE to save that state.
 * ========================================================================= */
void xbox_Cpuid(uint32_t leaf, uint32_t subleaf, uint32_t out[4])
{
    static int no_simd = -1;

    if (no_simd < 0) {
        const char *e = getenv("RECOMP_CPUID_NO_SIMD");
        no_simd = e && *e && *e != '0';
    }
    (void)subleaf;
    out[0] = out[1] = out[2] = out[3] = 0;
    if (leaf > 2)
        leaf = 2;
    switch (leaf) {
    case 0:
        out[0] = 2;
        out[1] = 0x756E6547u;   /* "Genu" */
        out[3] = 0x49656E69u;   /* "ineI" */
        out[2] = 0x6C65746Eu;   /* "ntel" */
        break;
    case 1:
        out[0] = 0x00000683u;
        out[3] = 0x0383F9FFu;
        if (no_simd)
            out[3] &= ~0x03800000u;     /* MMX, FXSR, SSE */
        break;
    default:
        out[0] = 0x03020101u;
        out[3] = 0x0C040800u;   /* L1 D, DTLB 4M, L1 I, no L2 */
        break;
    }
}
