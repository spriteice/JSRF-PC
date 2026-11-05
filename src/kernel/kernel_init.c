/*
 * kernel_init.c - The kernel layer's start and end, and its log
 *
 * This file provides:
 *   - xbox_kernel_init() - opens the log, before any game code runs
 *   - xbox_kernel_shutdown() - closes it
 *   - xbox_log() - the log itself (xbox_kernel.log, or stderr)
 *
 * The game's kernel imports are kernel_bridge.c's: xbox_kernel_bridge_init()
 * routes each slot of the game's thunk table to its bridge_* function.
 */

#include "kernel.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>

/* ============================================================================
 * Logging Implementation
 * ============================================================================ */

static FILE* g_log_file = NULL;
static int   g_log_level = XBOX_LOG_INFO;
static CRITICAL_SECTION g_log_cs;
static BOOL  g_log_cs_init = FALSE;

static const char* xbox_log_level_str(int level)
{
    switch (level) {
        case XBOX_LOG_ERROR: return "ERROR";
        case XBOX_LOG_WARN:  return "WARN ";
        case XBOX_LOG_INFO:  return "INFO ";
        case XBOX_LOG_DEBUG: return "DEBUG";
        case XBOX_LOG_TRACE: return "TRACE";
        default:             return "?????";
    }
}

void xbox_log(int level, const char* subsystem, const char* fmt, ...)
{
    va_list args;
    char timestamp[32];
    SYSTEMTIME st;

    if (level > g_log_level)
        return;

    GetLocalTime(&st);
    snprintf(timestamp, sizeof(timestamp), "%02d:%02d:%02d.%03d",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

    if (g_log_cs_init)
        EnterCriticalSection(&g_log_cs);

    FILE* out = g_log_file ? g_log_file : stderr;
    fprintf(out, "[%s] %s [%-6s] ", timestamp, xbox_log_level_str(level), subsystem);

    va_start(args, fmt);
    vfprintf(out, fmt, args);
    va_end(args);

    fprintf(out, "\n");
    fflush(out);

    if (g_log_cs_init)
        LeaveCriticalSection(&g_log_cs);
}

/* ============================================================================
 * xbox_kernel_init - Initialize the kernel replacement layer
 *
 * Must be called before any game code runs. Opens the log (xbox_kernel.log,
 * at the level XBOX_LOG_LEVEL names) and says the layer is ready.
 * ============================================================================ */

void xbox_kernel_init(void)
{
    /* Initialize logging */
    InitializeCriticalSection(&g_log_cs);
    g_log_cs_init = TRUE;

    /* Try to open log file, fall back to stderr */
    g_log_file = fopen("xbox_kernel.log", "w");

    /* Set log level from environment variable if present */
    const char* log_env = getenv("XBOX_LOG_LEVEL");
    if (log_env) {
        g_log_level = atoi(log_env);
        if (g_log_level < XBOX_LOG_ERROR) g_log_level = XBOX_LOG_ERROR;
        if (g_log_level > XBOX_LOG_TRACE) g_log_level = XBOX_LOG_TRACE;
    }

    xbox_log(XBOX_LOG_INFO, XBOX_LOG_THUNK,
        "=== Xbox Kernel Replacement Layer initializing ===");
    xbox_log(XBOX_LOG_INFO, XBOX_LOG_THUNK,
        "Kernel version: %u.%u.%u.%u (emulated)",
        xbox_KrnlVersion.Major, xbox_KrnlVersion.Minor,
        xbox_KrnlVersion.Build, xbox_KrnlVersion.Qfe);

    xbox_log(XBOX_LOG_INFO, XBOX_LOG_THUNK,
        "=== Xbox Kernel Replacement Layer ready ===");
}

/* ============================================================================
 * xbox_kernel_shutdown - Clean up the kernel replacement layer
 * ============================================================================ */

void xbox_kernel_shutdown(void)
{
    xbox_log(XBOX_LOG_INFO, XBOX_LOG_THUNK,
        "=== Xbox Kernel Replacement Layer shutting down ===");

    /* Close log file */
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }

    if (g_log_cs_init) {
        DeleteCriticalSection(&g_log_cs);
        g_log_cs_init = FALSE;
    }
}
