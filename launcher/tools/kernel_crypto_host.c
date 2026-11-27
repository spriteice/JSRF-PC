/*
 * The kernel's HMAC-SHA1 (src/kernel/kernel_crypto.c, the one the game signs
 * its saves with) for JSRF-PC Tools, a program outside the engine: the two
 * things kernel_crypto.c takes from the engine given here, its log and, off
 * Windows, SecureZeroMemory (src/platform/win32_compat.c's, which brings the
 * engine's threads with it).
 */
#include "kernel.h"
#include "jsrf_save.h"

/* kernel_crypto.c logs only from the public-key calls, which the tool never
 * makes. */
void xbox_log(int level, const char *subsystem, const char *fmt, ...)
{
    (void)level; (void)subsystem; (void)fmt;
}

#ifndef _WIN32
VOID SecureZeroMemory(PVOID ptr, SIZE_T cnt)
{
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (cnt--) *p++ = 0;
}
#endif

void jsrf_hmac_sha1(const uint8_t *key, size_t kn, const void *data, size_t n, uint8_t out[JSRF_SIG_BYTES])
{
    xbox_XcHMAC(key, (ULONG)kn, (const UCHAR *)data, (ULONG)n, NULL, 0, out);
}
