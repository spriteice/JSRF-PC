/* The follow camera's view kept out of walls (camera_walls.c).
 *
 * The game's follow camera tests one line, from what it looks at to its eye,
 * and puts the eye on the wall that line meets. But the view is not a point:
 * it starts at the near plane, ahead of the eye, as wide as the projection's
 * angle, which widescreen widens. This tests that view with the game's own
 * line test and moves the eye across, or in, until nothing cuts across it,
 * and holds and eases what it moved so a wall going by does not shake the
 * view. It does so for the game's own eye, where the game's wall test put
 * it, and for the eye the free camera (freecam.h) places, whose own line it
 * tests as well; for the game's, the eye's line counts only when what it
 * meets is wider than the view, as a wall or a half pipe's curve the eye
 * has gone behind is, where the game's test runs only when the eye's move
 * crosses a surface. The hooks are in recomp_manual.c: sub_000A6100, the
 * follow camera's post-update. */
#ifndef JSRF_CAMERA_WALLS_H
#define JSRF_CAMERA_WALLS_H

#include <stdint.h>

/* A line test against the world, as the follow camera's wall test makes it:
 * how much of the line from a to b is open, 0 to 1, up to the nearest hit.
 * ctx is the caller's. */
typedef double (*camwall_line_fn)(void *ctx, const double a[3], const double b[3]);

/* What set the share camwall_clear returns, for the log. */
enum {
    CAMWALL_CLEAR,     /* nothing: all of it */
    CAMWALL_EYE,       /* the eye's own line, as the game's */
    CAMWALL_CORNER,    /* a corner of the view, cleared by coming in */
    CAMWALL_NO_ROOM,   /* corners that neither clears: the eye's line alone */
    CAMWALL_ACROSS,    /* a corner of the view, cleared by moving across */
    CAMWALL_NEAREST    /* the eye's line meets a wall right by the target */
};

/* After a wall (camwall_share): game frames the eye stays where the wall put
 * it, once nothing brings it further in, before it goes back out -- a third
 * of a second, so a wall that comes and goes, as in a tight space, does not
 * shake it in and out -- and then the share of the way back out it closes
 * each frame. 1/10 brings it two thirds of the way in 11 frames, about a
 * fifth of a second, and nearly all of it in half a second. */
#define CAMWALL_HOLD 20
#define CAMWALL_EASE 10.0

/* One camera's eye after walls, as camwall_share and camwall_across keep it
 * from one frame to the next. */
struct camwall {
    double share;       /* the share of its distance the eye had last frame */
    int share_wait;     /* frames since a wall last brought it in, up to the hold */
    double across[3];   /* how far across the eye moved last frame */
    int across_wait;    /* frames since a wall last moved it across, up to the hold */
};
/* The eye all the way out and not moved across, with no wait: a camera no
 * wall has met. */
#define CAMWALL_START { 1.0, CAMWALL_HOLD, { 0, 0, 0 }, CAMWALL_HOLD }

/* CAMWALL_START: as a camera that starts again. */
void camwall_reset(struct camwall *w);

/* Each camera's: CAMWALL_CAMERAS at once, as many as split screen's
 * players' and a scene's besides. */
#define CAMWALL_CAMERAS 8

/* The state of camera cam (its guest address), for key: what places its eye
 * and how, as the caller tells them apart. A camera not seen before, or one
 * whose key changed since, starts again (CAMWALL_START); with more cameras
 * than CAMWALL_CAMERAS, a new one takes the place of the one seen longest
 * ago. Each camera keeps its own, so one player's walls don't move
 * another's view. */
struct camwall *camwall_for(uint32_t cam, uint32_t key);

/* How far from the target tgt the eye eye can be, as a share of its
 * distance, up to most (1 for all of it), and push, how far it moves across
 * there: its own line clear, with eye_line, and no surface across the view
 * where it starts, ahead of it (ahead, and half_w and half_h either side
 * there; 0s test the free camera's line alone, and leave the game's eye
 * where it is). Without eye_line, for the game's own eye, where the game's
 * wall test put it, what the eye's line meets brings it in only when it
 * hides the whole of where the view starts from beside tgt, as a surface
 * the eye has gone behind does: a pole between, narrower than the view, is
 * let be, as the game lets it. When a surface cuts across the view, the eye
 * moves across, away from it, or else comes in until lines from tgt to the
 * view's corners are clear, if either clears them. Never nearer tgt than
 * where the view starts in front of it. by says what set it. */
double camwall_clear(const float tgt[3], const float eye[3], double most, int eye_line,
                     double ahead, double half_w, double half_h, camwall_line_fn line,
                     void *ctx, double push[3], int *by);

/* After camwall_clear, each frame: open is the share it gave. Returns the
 * share to put the eye at: in at once when open is less than last frame's,
 * so the view never goes into a wall; then, once nothing has brought it in
 * for a moment, back out by degrees, so a wall going by, or one that comes
 * and goes, does not make the view jump or shake. Short of open, the view
 * there is not yet tested: camwall_clear up to it says, with how far across
 * the eye moves there, and when that is less, this again with what it gave
 * brings the eye in to it. */
double camwall_share(struct camwall *w, double open);

/* After that, each frame: how far across to move the eye (out), for the
 * eye at share s, where camwall_clear moved it need. To need at once when
 * last frame's move, where the eye is now, no longer keeps the view clear,
 * so the view never goes into a wall; else last frame's for a moment, then
 * toward need by degrees, as camwall_share goes back out, so the view does
 * not jump back across once a wall it moved away from ends. */
void camwall_across(struct camwall *w, const float tgt[3], const float eye[3], double s,
                    const double need[3], double ahead, double half_w, double half_h,
                    camwall_line_fn line, void *ctx, double out[3]);

#endif
