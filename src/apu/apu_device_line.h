/*
 * apu_device_line.h - the [APU] line that says what the sound device took.
 *
 * apu_sdl.c opens the system's default device through SDL asking for 48 kHz
 * stereo s16 in 512-frame pulls, and lets SDL convert to whatever the device
 * really runs at, so what SDL hands back is always what was asked and the
 * back-end line read "48000 Hz stereo, 512-frame pulls" on every device (the
 * sound audit's O4, row 12b's G10). This line adds what the device itself
 * mixes at, as SDL says of the default device, once at the open:
 *
 *   [APU] the sound device as opened: the wasapi driver; the device mixes at
 *   48000 Hz, 2 channels, f32; the ring is pulled at 48000 Hz, 2 channels,
 *   s16, 512 frames (2048 bytes) a pull, SDL converting between them
 *
 * The device's real period isn't given by SDL 2: the pulls a second and the
 * longest time between two, under RECOMP_APU_STATS, are how it shows.
 * Plain values in, so tests/apu_device_line makes the line without SDL.
 */
#ifndef APU_DEVICE_LINE_H
#define APU_DEVICE_LINE_H

#include <stdio.h>

/* SDL 2's AUDIO_* format words (SDL_audio.h), by name for the line. */
static inline const char *apu_device_format(unsigned format, char *spare, size_t n)
{
    switch (format) {
    case 0x0008: return "u8";
    case 0x8008: return "s8";
    case 0x8010: return "s16";
    case 0x9010: return "s16 big-endian";
    case 0x8020: return "s32";
    case 0x8120: return "f32";
    default:
        snprintf(spare, n, "format 0x%04X", format);
        return spare;
    }
}

/* The line, without its newline. driver may be NULL (not named). The device's own
 * spec is known when dev_known; its samples are left out when 0. */
static inline void apu_device_line(char *out, size_t n, const char *driver,
                                   int freq, int channels, unsigned format,
                                   int samples, unsigned size, int dev_known,
                                   int dev_freq, int dev_channels,
                                   unsigned dev_format, int dev_samples)
{
    char f1[24], f2[24], dev[160], drv[64];
    if (dev_known) {
        int k = snprintf(dev, sizeof dev, "the device mixes at %d Hz, %d channels, %s",
                         dev_freq, dev_channels,
                         apu_device_format(dev_format, f2, sizeof f2));
        if (dev_samples > 0 && k > 0 && (size_t)k < sizeof dev)
            snprintf(dev + k, sizeof dev - (size_t)k, ", %d frames a buffer", dev_samples);
    } else {
        snprintf(dev, sizeof dev, "the device's own format not said by SDL");
    }
    if (driver) snprintf(drv, sizeof drv, "the %s driver", driver);
    else snprintf(drv, sizeof drv, "the driver not named");
    snprintf(out, n, "[APU] the sound device as opened: %s; %s; the ring is "
             "pulled at %d Hz, %d channels, %s, %d frames (%u bytes) a pull%s",
             drv, dev, freq, channels,
             apu_device_format(format, f1, sizeof f1), samples, size,
             dev_known && (dev_freq != freq || dev_channels != channels
                           || dev_format != format)
                 ? ", SDL converting between them" : "");
}

#endif /* APU_DEVICE_LINE_H */
