/*
 * The single-header libraries JSRF-PC Tools' graffiti page uses, built
 * once, here, as their headers ask, with the warnings they were written
 * for: stb_dxt (third_party/stb, public domain or MIT) for the colour half
 * of the wall texture's DXT3 blocks (graffiti_save.c); stb_image (the same)
 * for JPEG and PNG, and nanosvg (third_party/nanosvg, zlib) for SVG
 * (graffiti_image.c).
 */
#include <stdio.h>
#include <string.h>
#include <float.h>
#include <math.h>

#define STB_DXT_IMPLEMENTATION
#include "stb_dxt.h"

#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"
