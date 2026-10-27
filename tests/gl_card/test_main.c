/* Run with: python tests/gl_card/run.py
 *
 * The GPU time profile's line for the graphics card -- card_mark and
 * card_report, from src/nv2a_gl/nv2a_gl.c, put before this by run.py -- on a
 * headless EGL context (Mesa's llvmpipe will do). Frames a 60th of a second
 * apart, as pacing makes them, each with some drawing for the card, its
 * timestamp as it is shown and a flush where the window's swap would be, and
 * a report about once a second. Then more frames than there are queries with
 * no report between, so that some go untimed. run.py reads the lines. */

#define FRAMES 150
#define PERIOD_NS 16666667L

/* Some work for the card: a large target cleared a few times over. */
static void draw_frame(GLuint fbo)
{
    int i;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    for (i = 0; i < 8; i++) {
        glClearColor((float)i / 8.0f, 0.5f, 0.25f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
}

int main(void)
{
    GLuint fbo, rb;
    double report_at;
    int f;
    if (!gl_backend_context()) return 1;
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 1024, 1024);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    report_at = prof_now() + 1.0;
    for (f = 0; f < FRAMES; f++) {
        struct timespec period = { 0, PERIOD_NS };
        draw_frame(fbo);
        nanosleep(&period, NULL);          /* pacing */
        card_mark();
        glFlush();                         /* the swap */
        if (prof_now() >= report_at) {
            card_report();
            report_at += 1.0;
        }
    }
    glFinish();
    card_report();
    fprintf(stderr, "frames shown: %d\n", FRAMES);
    for (f = 0; f < CARD_QUERIES + 2; f++) {
        card_mark();
        glFlush();
    }
    glFinish();
    card_report();
    return 0;
}
