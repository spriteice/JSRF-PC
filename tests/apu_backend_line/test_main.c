/* Run with: python tests/apu_backend_line/run.py
 *
 * The [APU] line that says which sound back end opened, written by
 * src/apu/apu_core.c's mcpx_apu_monitor_init. The back end is apu_sdl.c,
 * which needs SDL: this file stands in for it, and opens or not as the
 * command line says ("open" or "fail"). run.py checks the line.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "apu_state.h"

void mcpx_apu_monitor_init(MCPXAPUState *d, Error **errp);

static int s_open;

int xa2_init(void) { return s_open; }
void xa2_shutdown(void) {}
int xa2_is_active(void) { return s_open; }
int xa2_submit_samples(const int16_t *samples, int num_samples) { (void)samples; return num_samples; }
int xa2_get_buffer_size(void) { return 0; }
int xa2_ring_level(void) { return 0; }
int xa2_ms_since_pull(void) { return 0; }

int main(int argc, char **argv)
{
    static MCPXAPUState d;
    s_open = argc > 1 && !strcmp(argv[1], "open");
    mcpx_apu_monitor_init(&d, NULL);
    fflush(stderr);
    return 0;
}
