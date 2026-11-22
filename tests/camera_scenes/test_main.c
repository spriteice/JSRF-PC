/* Run with: python tests/camera_scenes/run.py
 *
 * The game's own camera shots without the game: port/camera_scenes.c as it
 * is. One case a run, named by the first argument. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "camera_scenes.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    if (!strcmp(c, "modes")) {
        /* Of the follow camera's post-update's modes, only graffiti's is
         * one of the game's own shots: the game aims it at the spot being
         * sprayed, and it is left as the game frames it. */
        static const uint32_t follow[] = { 3, 4, 5, 9 };
        uint32_t i;
        for (i = 0; i < sizeof follow / sizeof follow[0]; i++) CHECK(!camscene_shot(follow[i]));
        CHECK(CAMSCENE_MODE_GRAFFITI == 13);
        CHECK(camscene_shot(CAMSCENE_MODE_GRAFFITI));
    } else {
        fprintf(stderr, "no case %s\n", c);
        return 2;
    }
    printf("ok %s\n", c);
    return 0;
}
