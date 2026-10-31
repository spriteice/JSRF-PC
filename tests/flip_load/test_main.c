/* JSRF's frames through the M_FLIP_STALL case: display buffers A and B, which
 * it alternates, and an off-screen surface in another format. Each frame
 * binds the next display buffer and flips, which shows the one copied into on
 * the frame before, then copies the off-screen frame into the bound buffer,
 * clears the off-screen surface and, unless it is loading, draws the scene. */
enum { A, B, SCRATCH };
static const uint32_t display_fmt = 0x0007u, scratch_fmt = 0x0003u;
static int failed;

static void check(int ok, const char *what)
{
    if (!ok) { fprintf(stderr, "[FAIL] %s\n", what); failed = 1; }
}

static void draw(int slot)
{
    S.draws++;
    S.since_present++;
    g_surf[slot].last_draw = S.draws;
}

/* do_clear: a full-surface clear ends the pass, and the frame is captured
 * (present(0)) only if something was drawn since the last one. */
static void clear_scratch(void)
{
    if (S.since_present) present(0);
    S.clear_color = 0;
}

/* Flip with buf bound; say what the window was given. */
static void flip(int buf, int *was_shown, int *slot, int *was_paced)
{
    int s0 = shown, p0 = paced;
    S.color_offset = g_surf[buf].offset;
    S.surf_format = display_fmt;
    flip_stall();
    *was_shown = shown > s0;
    *slot = shown_slot;
    *was_paced = paced > p0;
}

static void frame(int buf, int scene, int *was_shown, int *slot, int *was_paced)
{
    int k;
    flip(buf, was_shown, slot, was_paced);
    draw(buf);                              /* the copy into the display buffer */
    clear_scratch();
    if (scene)
        for (k = 0; k < 300; k++) draw(SCRATCH);
}

int main(void)
{
    static const uint32_t offset[] = { 0x00100000u, 0x00200000u, 0x00300000u };
    int k, was_shown, slot, was_paced;
    for (k = A; k <= SCRATCH; k++) {
        g_surf[k].used = 1;
        g_surf[k].offset = offset[k];
        g_surf[k].w = 640; g_surf[k].h = 480;
        g_surf[k].fmt = k == SCRATCH ? scratch_fmt : display_fmt;
    }
    S.clip_w = 640; S.clip_h = 480;

    /* Frame 0's flip: nothing drawn yet, not even a copy. Held, and paced. */
    frame(A, 1, &was_shown, &slot, &was_paced);
    check(!was_shown && was_paced, "the first flip, with nothing drawn, was not held and paced");
    /* Frames 1 and 2 normal, 3 to 5 loading (copy, clear, no scene), 6 to 8 normal. */
    for (k = 1; k <= 8; k++) {
        int loading = k >= 3 && k <= 5;
        int copied = (k - 1) % 2 ? B : A;   /* the buffer the frame before copied into */
        frame(k % 2 ? B : A, !loading, &was_shown, &slot, &was_paced);
        if (k >= 4 && k <= 6)
            check(was_shown && slot == copied,
                  "a flip after a loading frame did not show the buffer it copied into");
        else
            check(was_shown && slot == copied,
                  "a flip after a normal frame did not show the buffer it copied into");
    }
    /* Frame 9's flip shows frame 8's copy; a second flip straight after it
     * has nothing drawn at all since. Held, and paced. */
    flip(B, &was_shown, &slot, &was_paced);
    check(was_shown && slot == A, "the flip after frame 8 did not show the buffer it copied into");
    flip(A, &was_shown, &slot, &was_paced);
    check(!was_shown && was_paced, "a flip with nothing drawn since the last was not held and paced");
    return failed;
}
