/* 30 seconds of frames at 60 a second through the probe, 300 draws each: a
 * skinned batch every 50th draw, a small quad every 100th, and the frame's
 * second surface from its 250th. Then what vc_debug_switch says of the
 * environment. With "switch", only the latter. */
int main(int argc, char **argv)
{
    const char *why;
    uint32_t frame, k;
    if (argc < 2)
        for (frame = 0; frame < 30 * 60; frame++) {
            S.frames++;
            g_probe_now = frame / 60.0;
            for (k = 0; k < 300; k++) {
                S.draws++;
                S.color_offset = k < 250 ? 0x00084000u : 0x001B2000u;
                probe(k % 50 == 25, (int)(k % 7), k % 100 == 99 ? 6u : 120u, 0);
            }
        }
    why = vc_debug_switch();
    fprintf(stderr, "[TEST] vc_debug_switch: %s\n", why ? why : "none");
    return 0;
}
