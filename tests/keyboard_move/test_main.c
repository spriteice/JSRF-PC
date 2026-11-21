/* Run with: python tests/keyboard_move/run.py
 *
 * Keyboard moves without the game: port/keyboard_move.c as it is, with the
 * settings file, the menu's xbox_MenuAdd and the input layer's movement keys
 * stood in for. One case a run, as the setting is read once. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "menu.h"
#include "keyboard_move.h"
#include "xinput_xbox.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)
#define FULL 32767
#define DIAG 23170              /* full tilt, an eighth of a turn round */
#define CLOSE_TO(a, b) (abs((int)(a) - (int)(b)) <= 1)

static const double k_pi = 3.14159265358979323846;

/* The settings file: keyboard_moves= as the case sets it. */
static int    s_has;
static double s_value, s_kept = -1;
int nv_window_pref_num(const char *key, double *v)
{
    if (strcmp(key, "keyboard_moves") || !s_has) return 0;
    *v = s_value;
    return 1;
}
void nv_window_pref_set_num(const char *key, double v)
{
    CHECK(!strcmp(key, "keyboard_moves"));
    s_kept = v;
}

/* The rows it adds: Input's Behaviour submenu, then its row in it. */
static XBOX_MENU_ITEM s_group, s_row;
static int s_rows;
void xbox_MenuAdd(const XBOX_MENU_ITEM *items, int count)
{
    if (count > 0) s_group = items[0];
    if (count > 1) s_row = items[1];
    s_rows += count;
}

/* The keys held: D minus A, W minus S. */
static int s_kx, s_ky;
int xbox_InputKeyboardMove(DWORD port, int *x, int *y)
{
    *x = port == 0 ? s_kx : 0;
    *y = port == 0 ? s_ky : 0;
    return port == 0 && (s_kx || s_ky);
}

/* One steering: keys, the skater's facing (in quarter turns) and the
 * camera's look, against the stick it should make. */
static double s_held;
static int s_held_keys;
static void steer(int kx, int ky, double quarters, double lookx, double lookz, int wx, int wy)
{
    short lx = 1, ly = 1;
    kbmove_stick(kx, ky, quarters * k_pi / 2, lookx, lookz, &s_held, &s_held_keys, &lx, &ly);
    if (!CLOSE_TO(lx, wx) || !CLOSE_TO(ly, wy)) {
        fprintf(stderr, "FAIL: keys (%d, %d), skater %.2f turns round, look (%g, %g): stick (%d, %d), "
                "should be (%d, %d)\n", kx, ky, quarters / 4, lookx, lookz, lx, ly, wx, wy);
        exit(1);
    }
}

static void check_steering(void)
{
    /* The camera looks along +z, and the skater faces the same way: the
     * keys are what they were. */
    steer(0, 1, 0, 0, 1, 0, FULL);              /* W: straight on, up */
    steer(-1, 0, 0, 0, 1, -FULL, 0);            /* A: a quarter turn left */
    steer(1, 0, 0, 0, 1, FULL, 0);              /* D: right */
    steer(-1, 1, 0, 0, 1, -DIAG, DIAG);         /* W with A: an eighth */
    steer(1, 1, 0, 0, 1, DIAG, DIAG);
    /* The skater a quarter turn right of the camera (facing +x): W goes
     * that way, which on the stick is right; A is the camera's way, up. */
    steer(0, 1, 1, 0, 1, FULL, 0);
    steer(-1, 0, 1, 0, 1, 0, FULL);
    steer(1, 0, 1, 0, 1, 0, -FULL);
    /* Facing the camera: W is down on the stick, and A is right. */
    steer(0, 1, 2, 0, 1, 0, -FULL);
    steer(-1, 0, 2, 0, 1, FULL, 0);
    /* The camera along +x, the skater too: W up again; the look's length
     * doesn't matter. */
    steer(0, 1, 1, 5, 0, 0, FULL);
    steer(-1, 0, 1, 5, 0, -FULL, 0);
    /* A held: the way it gives turns with the skater, so the skater keeps
     * turning (the stick stays a quarter left of wherever it faces). */
    steer(-1, 0, 0.5, 0, 1, -DIAG, DIAG);
    steer(-1, 0, 1, 0, 1, 0, FULL);
    /* S: the way behind the skater as it goes down, kept while it is held,
     * however the skater turns; once it is let go and pressed again, the
     * way behind it then. */
    steer(0, -1, 0, 0, 1, 0, -FULL);
    steer(0, -1, 1, 0, 1, 0, -FULL);
    steer(0, -1, 2, 0, 1, 0, -FULL);
    steer(0, 1, 2, 0, 1, 0, -FULL);             /* let go: W, the skater facing the camera */
    steer(0, -1, 2, 0, 1, 0, FULL);             /* S again: behind it now is up */
    /* S with A: behind and to the left, taken again as the keys change. */
    steer(-1, -1, 0, 0, 1, -DIAG, -DIAG);
    steer(1, -1, 0, 0, 1, DIAG, -DIAG);
    /* No look on the ground (the camera straight above): no stick. */
    steer(0, 1, 0, 0, 0, 0, 0);
}

/* The pad the camera update finds: the main player's manager names the
 * game's pad 4, whose port the game keeps apart (0x251F20). */
static void check_port(void)
{
    short lx, ly;
    unsigned p, main_port;
    for (p = 0; p < 4; p++) CHECK(kbmove_game_port(p, 2) == (int)p);
    for (main_port = 0; main_port < 4; main_port++)
        CHECK(kbmove_game_port(4, main_port) == (int)main_port);
    CHECK(kbmove_game_port(4, 4) == -1 && kbmove_game_port(4, 0xFFFFFFFFu) == -1);
    CHECK(kbmove_game_port(0xFFFFFFFFu, 0) == -1 && kbmove_game_port(5, 0) == -1);
    /* The pad number itself is no port: a view for 4 is kept for none, and
     * the read on port 0 is left as the keys made it (the first build's
     * fault: the keys never steered). */
    s_kx = 0; s_ky = 1;
    lx = 0; ly = FULL;
    kbmove_view(4, 0, 1, 0x4000u);
    kbmove_pad(0, &lx, &ly);
    CHECK(lx == 0 && ly == FULL);
    /* Through the main port it is, and W steers the skater on port 0. */
    lx = 0; ly = FULL;
    kbmove_view((unsigned)kbmove_game_port(4, 0), 0, 1, 0x4000u);
    kbmove_pad(0, &lx, &ly);
    CHECK(CLOSE_TO(lx, FULL) && CLOSE_TO(ly, 0));
    s_ky = 0;
}

/* The game's soft centre as its own code does it (0x000A7ABF), the x87's
 * sums in double as the recompiler lifts them, from its two floats: an
 * angle from the heading within 0x2AAA of straight up, times 1/10922,
 * squared with its sign, times -10922, truncated, and taken from the
 * heading; any other angle as it is. */
#define SOFT 0x2AAA
static const float k_soft_in = 1.0f / 10922.0f, k_soft_out = -10922.0f;
static int game_soft(int a)
{
    double x, y;
    if (!(a > -SOFT && a < SOFT)) return a;
    x = (double)a * (double)k_soft_in;
    y = x * x;
    if (x < 0) y = -y;
    return -(int)(y * (double)k_soft_out);
}

/* The game's angles, wrapped to -0x8000..0x7FFF. */
static int wrap16(long a)
{
    a &= 0xFFFF;
    return (int)(a >= 0x8000 ? a - 0x10000 : a);
}

/* Where the player manager's pad case sends the skater for an angle in the
 * pad's entry (0x000A7940 to 0x000A7B16), the camera not holding a heading:
 * the heading plus the angle, softened near straight up. */
static int game_way(uint32_t angle, uint32_t heading)
{
    int d = wrap16((long)angle);
    return wrap16((long)heading + (d > -SOFT && d < SOFT ? game_soft(d) : d));
}

static void check_soft(void)
{
    static int image[2 * SOFT];
    int a, way, n = 0;
    /* kbmove_soft_centre is the game's, at every angle it softens, and
     * leaves the rest. */
    for (a = -SOFT + 1; a < SOFT; a++) {
        if (kbmove_soft_centre(a) != game_soft(a)) {
            fprintf(stderr, "FAIL: soft centre of %d: %d, the game's %d\n", a, kbmove_soft_centre(a), game_soft(a));
            exit(1);
        }
        image[n++] = game_soft(a);
    }
    CHECK(kbmove_soft_centre(SOFT) == SOFT && kbmove_soft_centre(-SOFT) == -SOFT);
    CHECK(kbmove_soft_centre(0x7FFF) == 0x7FFF && kbmove_soft_centre(-0x8000) == -0x8000);
    CHECK(kbmove_soft_centre(0x1000) < 0x1000 && kbmove_soft_centre(-0x1000) > -0x1000);
    /* Its undoing: for every way the game can soften to, the angle found
     * lands as near it as any angle the game softens can (the softened
     * angles rise, so the nearest is either side of where it would go). */
    for (way = -SOFT + 1; way < SOFT; way++) {
        int lo = 0, hi = n, best, got = kbmove_soft_centre_undo(way);
        CHECK(got > -SOFT && got < SOFT);
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (image[mid] < way) lo = mid + 1; else hi = mid;
        }
        best = 0x10000;
        if (lo < n) best = abs(image[lo] - way);
        if (lo > 0 && abs(image[lo - 1] - way) < best) best = abs(image[lo - 1] - way);
        if (abs(game_soft(got) - way) != best || best > 1) {
            fprintf(stderr, "FAIL: undoing %d: %d, which the game takes to %d; the nearest it can is %d off\n",
                    way, got, game_soft(got), best);
            exit(1);
        }
    }
    /* Outside it, as it is. */
    CHECK(kbmove_soft_centre_undo(SOFT) == SOFT && kbmove_soft_centre_undo(-0x6000) == -0x6000);
}

/* The angle for the player manager's update: with the keys steering, the
 * game goes the keys' way, from the skater's turn, whatever the camera's
 * heading. */
static void angle_case(uint32_t turn, uint32_t heading, int kx, int ky, long want)
{
    uint32_t a = 0x12345;
    short lx = 0, ly = 0;
    s_kx = kx; s_ky = ky;
    kbmove_view(0, 0, 1, turn);
    kbmove_pad(0, &lx, &ly);
    CHECK(kbmove_angle(0, turn, heading, 0, 0, &a) == 1);
    CHECK(a <= 0xFFFFu);
    if (abs(wrap16((long)game_way(a, heading) - want)) > 1) {
        fprintf(stderr, "FAIL: keys (%d, %d), turn %04X, heading %04X: angle %04X goes %04X, should go %04X\n",
                kx, ky, turn, heading, a, game_way(a, heading) & 0xFFFF, (unsigned)(want & 0xFFFF));
        exit(1);
    }
}

static void check_angle(void)
{
    uint32_t a = 0, turn, heading;
    short lx = 0, ly = 0;
    /* Not steering: no keys, no view, or another port. */
    s_kx = s_ky = 0;
    kbmove_view(0, 0, 1, 0);
    kbmove_pad(0, &lx, &ly);
    CHECK(kbmove_angle(0, 0, 0, 0, 0, &a) == 0);
    CHECK(kbmove_angle(1, 0, 0, 0, 0, &a) == 0 && kbmove_angle(4, 0, 0, 0, 0, &a) == 0);
    /* W, A, D and W with A, with the skater and the camera's heading each
     * way round, the camera near the skater's way (where the game softens)
     * and far from it: W straight on, A and D a quarter turn to their side,
     * W with A an eighth. */
    for (turn = 0; turn < 0x10000u; turn += 0x0B00u)
        for (heading = 0; heading < 0x10000u; heading += 0x0700u) {
            angle_case(turn, heading, 0, 1, (long)turn);
            angle_case(turn, heading, -1, 0, (long)turn - 0x4000);
            angle_case(turn, heading, 1, 0, (long)turn + 0x4000);
            angle_case(turn, heading, -1, 1, (long)turn - 0x2000);
        }
    /* The camera a little off the skater's way, either side, where the
     * soft centre pulls hardest towards it (the second build's fault: W
     * turned the skater with the camera). */
    for (heading = 0xFF00u; heading != 0x0100u; heading = (heading + 1) & 0xFFFFu)
        angle_case(0, heading, 0, 1, 0);
    /* S: the way behind the skater as it went down, kept as it turns. */
    s_kx = 0; s_ky = 0;
    kbmove_view(0, 0, 1, 0x1000u);
    kbmove_pad(0, &lx, &ly);
    s_ky = -1;
    kbmove_view(0, 0, 1, 0x1000u);
    kbmove_pad(0, &lx, &ly);
    CHECK(kbmove_angle(0, 0x1000u, 0x3000u, 0, 0, &a) == 1);
    CHECK(abs(wrap16((long)game_way(a, 0x3000u) - 0x9000)) <= 1);
    CHECK(kbmove_angle(0, 0x5000u, 0x3000u, 0, 0, &a) == 1);   /* the skater turning: kept */
    CHECK(abs(wrap16((long)game_way(a, 0x3000u) - 0x9000)) <= 1);
    /* The camera holding a heading (after it changed mode): the angle from
     * that heading is the keys' way, which the game compares with the way
     * it kept to let it go. */
    s_kx = 0; s_ky = 1;
    kbmove_view(0, 0, 1, 0x2000u);
    kbmove_pad(0, &lx, &ly);
    CHECK(kbmove_angle(0, 0x2000u, 0x7000u, 1, 0xE000u, &a) == 1);
    CHECK(((a + 0xE000u) & 0xFFFFu) == 0x2000u);
    s_kx = s_ky = 0;
    kbmove_pad(0, &lx, &ly);
    CHECK(kbmove_angle(0, 0x2000u, 0x7000u, 1, 0xE000u, &a) == 0);
}

static void check_pad(int camera)
{
    short lx, ly;
    /* No key held: the stick as it was. */
    s_kx = s_ky = 0;
    lx = 100; ly = 200;
    kbmove_view(0, 0, 1, 0x4000u);              /* the skater facing +x */
    kbmove_pad(0, &lx, &ly);
    CHECK(lx == 100 && ly == 200);
    /* W held: Skater steers it (right on the stick, as the skater faces
     * right of the camera); Camera leaves it as the keys made it. */
    s_ky = 1;
    lx = 0; ly = FULL;
    kbmove_view(0, 0, 1, 0x4000u);
    kbmove_pad(0, &lx, &ly);
    if (camera) CHECK(lx == 0 && ly == FULL);
    else        CHECK(CLOSE_TO(lx, FULL) && CLOSE_TO(ly, 0));
    /* Only the game's turn's low 16 bits count, as in its own rotation. */
    lx = 0; ly = FULL;
    kbmove_view(0, 0, 1, 0x14000u);
    kbmove_pad(0, &lx, &ly);
    if (!camera) CHECK(CLOSE_TO(lx, FULL) && CLOSE_TO(ly, 0));
    /* A port with no view from the camera update (no skater the pad moves
     * there): left alone. */
    lx = 0; ly = FULL;
    kbmove_pad(1, &lx, &ly);
    CHECK(lx == 0 && ly == FULL);
    s_ky = 0;
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    if (!strcmp(c, "skater") || !strcmp(c, "port") || !strcmp(c, "angle")) { s_has = 1; s_value = 1; }
    if (!strcmp(c, "unknown")) { s_has = 1; s_value = 7; }
    kbmove_start();
    /* Input's Behaviour submenu, with its hint, then the row in it: a
     * choice, Camera then Skater, with a hint the strip can hold, that File >
     * Presets set to Camera in each preset. */
    CHECK(s_rows == 2);
    CHECK(s_group.menu == XBOX_MENU_INPUT && s_group.kind == XBOX_ITEM_GROUP);
    CHECK(!strcmp(s_group.label, XBOX_GROUP_BEHAVIOUR) && !strcmp(s_group.label, "Behaviour"));
    CHECK(s_group.hint && !strcmp(s_group.hint, "Sets how the camera, the keys and the sticks act."));
    CHECK(s_row.menu == XBOX_MENU_INPUT && s_row.kind == XBOX_ITEM_CHOICE);
    CHECK(s_row.group && !strcmp(s_row.group, XBOX_GROUP_BEHAVIOUR) && !s_row.under && !s_row.last);
    CHECK(!strcmp(s_row.label, "Keyboard moves") && s_row.nchoices == 2);
    CHECK(!strcmp(s_row.choices[0], "Camera") && !strcmp(s_row.choices[1], "Skater"));
    CHECK(s_row.hint && strlen(s_row.hint) <= MENU_HINT_LONGEST && s_row.hint[strlen(s_row.hint) - 1] == '.');
    CHECK(s_row.presets == XBOX_PRESETS_SET && !s_row.keep);
    CHECK(s_row.preset[XBOX_PRESET_XBOX] == 0 && s_row.preset[XBOX_PRESET_JSRF_PC] == 0
          && s_row.preset[XBOX_PRESET_MAX] == 0);
    /* Camera, the default, with no keyboard_moves= or one it doesn't know:
     * JSRF-PC default's, as a start with no settings gives. */
    if (!strcmp(c, "default") || !strcmp(c, "unknown")) {
        CHECK(s_row.state() == 0 && s_row.preset[XBOX_PRESET_JSRF_PC] == s_row.state());
        check_pad(1);
    }
    if (!strcmp(c, "default")) {
        /* The row: Skater at once, and kept. */
        s_row.pick(1);
        CHECK(s_row.state() == 1 && s_kept == 1);
        check_pad(0);
        s_row.pick(0);
        CHECK(s_row.state() == 0 && s_kept == 0);
        check_pad(1);
    }
    if (!strcmp(c, "skater")) {
        CHECK(s_row.state() == 1);
        check_pad(0);
    }
    if (!strcmp(c, "steer")) check_steering();
    if (!strcmp(c, "port"))  check_port();
    if (!strcmp(c, "soft"))  check_soft();
    if (!strcmp(c, "angle")) check_angle();
    printf("keyboard_move %s: pass\n", c);
    return 0;
}
