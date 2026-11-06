/*
 * The internal resolution: its names and its range (nv2a_scale.h).
 */
#include "nv2a_scale.h"

#include <stdlib.h>

const char *const nv2a_scale_names[NV2A_SCALE_MAX] = {
    "1x  640x480", "2x  1280x960", "3x  1920x1440", "4x  2560x1920",
    "5x  3200x2400", "6x  3840x2880" };

uint32_t nv2a_scale_parse(const char *v)
{
    long n = v ? strtol(v, NULL, 0) : 1;
    return (uint32_t)(n < 1 ? 1 : n > NV2A_SCALE_MAX ? NV2A_SCALE_MAX : n);
}
