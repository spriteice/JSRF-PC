/* dav1d's config.h for JSRF-PC Tools (AVIF, through libavif), in place of
 * the one its Meson build writes: plain C, no assembly, on every host, as
 * dav1d builds for a processor it has no assembly for; 8-bit and 10/12-bit
 * pictures. Its file list is launcher/tools/graffiti_libs.txt. */
#ifndef DAV1D_CONFIG_H
#define DAV1D_CONFIG_H

#define ARCH_AARCH64 0
#define ARCH_ARM 0
#define ARCH_LOONGARCH 0
#define ARCH_LOONGARCH64 0
#define ARCH_PPC64LE 0
#define ARCH_RISCV 0
#define ARCH_X86 0
#define ARCH_X86_32 0
#define ARCH_X86_64 0
#define HAVE_ASM 0

#define CONFIG_8BPC 1
#define CONFIG_16BPC 1
#define CONFIG_LOG 0
#define TRIM_DSP_FUNCTIONS 0
#define ENDIANNESS_BIG 0

#ifndef _WIN32
#define HAVE_UNISTD_H 1
#define HAVE_POSIX_MEMALIGN 1
#define HAVE_DLSYM 0
#endif

#endif
