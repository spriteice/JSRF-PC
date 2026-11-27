/*
 * libavif's picture scaling, for JSRF-PC Tools, in place of its scale.c,
 * which needs libyuv. Reading a picture at its own size never scales; a
 * file whose parts would need it (a gain map, or a tile of another size) is
 * refused with this, and the graffiti page says it couldn't be read.
 */
#include "avif/internal.h"

avifResult avifImageScaleWithLimit(avifImage *image, uint32_t dstWidth, uint32_t dstHeight,
                                   uint32_t imageSizeLimit, uint32_t imageDimensionLimit, avifDiagnostics *diag)
{
    (void)imageSizeLimit;
    (void)imageDimensionLimit;
    if (image->width == dstWidth && image->height == dstHeight)
        return AVIF_RESULT_OK;
    avifDiagnosticsPrintf(diag, "Scaling isn't built into JSRF-PC Tools");
    return AVIF_RESULT_NOT_IMPLEMENTED;
}

avifResult avifImageScale(avifImage *image, uint32_t dstWidth, uint32_t dstHeight, avifDiagnostics *diag)
{
    return avifImageScaleWithLimit(image, dstWidth, dstHeight, AVIF_DEFAULT_IMAGE_SIZE_LIMIT,
                                   AVIF_DEFAULT_IMAGE_DIMENSION_LIMIT, diag);
}
