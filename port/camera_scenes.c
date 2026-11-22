/* The game's own camera shots (camera_scenes.h). */
#include "camera_scenes.h"

int camscene_shot(uint32_t mode)
{
    return mode == CAMSCENE_MODE_GRAFFITI;
}
