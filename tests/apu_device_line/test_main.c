/* Run with: python tests/apu_device_line/run.py
 *
 * The [APU] line that says what the sound device took (src/apu/apu_device_line.h),
 * made for each device the arguments describe: the driver ("-" for none),
 * then what SDL hands the ring (rate, channels, format, frames a pull,
 * bytes a pull), then whether SDL said what the device itself mixes at and,
 * if it did, its rate, channels, format and frames a buffer. Formats are
 * SDL 2's AUDIO_* words, in hex.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "apu_device_line.h"

int main(int argc, char **argv)
{
    char line[400];
    if (argc != 12) return 2;
    apu_device_line(line, sizeof line, strcmp(argv[1], "-") ? argv[1] : NULL,
                    atoi(argv[2]), atoi(argv[3]), (unsigned)strtoul(argv[4], NULL, 16),
                    atoi(argv[5]), (unsigned)atoi(argv[6]), atoi(argv[7]), atoi(argv[8]),
                    atoi(argv[9]), (unsigned)strtoul(argv[10], NULL, 16), atoi(argv[11]));
    printf("%s\n", line);
    return 0;
}
