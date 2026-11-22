/* Keyboard moves (keyboard_move.h): keyboard_moves= in the settings file, 0
 * Camera (the default) or 1 Skater.
 *
 * Which way the stick points, and which way the game goes for it, are as
 * recomp_manual.c's autopilot finds them (auto_step): up on the stick is the
 * camera's look on the ground, from its eye (+0x44) to its target (+0x5C),
 * and right on it is that look turned a quarter to the right, (z, -x). The
 * skater faces the way sub_000A32B0 puts the camera back behind it: its turn
 * on the ground (+0xCC0, 0x10000 a turn) carries +z to (sin, cos). The game's
 * angles are atan2(x, z) in the same units (sub_0004A7A0), so a turn is a
 * heading too.
 *
 * The player manager's pad case (sub_000A76E0, 0x000A7940 to 0x000A7B16)
 * goes the way of the stick's angle from the heading the camera's controls
 * follow (its +0x228), but within 60 degrees of straight up it squares the
 * angle (kbmove_soft_centre), and after the camera changes mode it keeps the
 * old heading (+0x234) and the way it had while the stick stays near it. So
 * the angle that sends the skater a given way is worked out from what the
 * game will read, at its read (kbmove_angle). */
#include <math.h>
#include <stdio.h>
#include <time.h>
#include "keyboard_move.h"
#include "xinput_xbox.h"
#if defined(XBOXRECOMP_MENU)
#  include "menu.h"
#endif

/* The setting's choices, as keyboard_moves= numbers them and the row lists
 * them. */
enum { KBMOVE_CAMERA, KBMOVE_SKATER, KBMOVE_CHOICES };
#define KBMOVE_DEFAULT KBMOVE_CAMERA
static const char *const k_names[KBMOVE_CHOICES] = { "Camera", "Skater" };

#define KBMOVE_PORTS   4
#define KBMOVE_TURN    65536.0     /* the game's angles: this many a turn */
#define KBMOVE_FULL    32767.0     /* the stick at full tilt */
#define KBMOVE_FRESH_S 0.1         /* a view older than this is not steered by (no camera update: a scene, a menu) */
#define KBMOVE_LOOK_MIN 1e-3       /* a look shorter than this on the ground has no direction */
#define KBMOVE_SOFT    0x2AAA      /* the game's soft centre: within this of straight up (60 degrees), squared */
#define KBMOVE_SOFT_IN  (1.0f / 10922.0f)  /* its sums' constants, floats as the game keeps them (0x1CD50C) */
#define KBMOVE_SOFT_OUT (-10922.0f)        /* (0x1CD5C0) */

static const double k_pi = 3.14159265358979323846;

#if defined(NV2A_GL_USE_SDL)
extern int  nv_window_pref_num(const char *key, double *v);
extern void nv_window_pref_set_num(const char *key, double v);
#else
/* Without the SDL window (a headless build) there is no settings file, and
 * no keyboard. */
static int nv_window_pref_num(const char *key, double *v)
{
    (void)key; (void)v;
    return 0;
}
#endif

static volatile int s_mode = KBMOVE_DEFAULT;
static struct {
    double look_x, look_z, heading;   /* as kbmove_view last had them */
    double at;                        /* when, on kbmove_now's clock; 0: never */
    double held;                      /* the way S keeps (kbmove_way) */
    int held_keys;
    int steering;                     /* the keys steered at the last read */
    int keys_x, keys_y;               /* and the keys they steered with */
} s_port[KBMOVE_PORTS];

static double kbmove_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

/* The way the keys send the skater, radians as heading is. */
static double kbmove_way(int keys_x, int keys_y, double heading, double *held, int *held_keys)
{
    /* The keys as one number, so a change of any of them is seen. */
    int keys = (keys_x + 1) * 3 + (keys_y + 1) + 1;
    if (keys_y < 0) {
        /* S: the way behind the skater (or behind and to the side, with A or
         * D) as the keys went down, kept while they stay so; re-read each
         * frame it would turn round for ever. */
        if (*held_keys != keys) {
            *held = heading + atan2((double)keys_x, (double)keys_y);
            *held_keys = keys;
        }
        return *held;
    }
    /* W straight on; A and D a quarter turn to their side, so held they
     * turn the skater; W with A or D an eighth. Re-read each frame from
     * the way the skater faces now. */
    *held_keys = 0;
    return heading + atan2((double)keys_x, (double)keys_y);
}

void kbmove_stick(int keys_x, int keys_y, double heading, double look_x, double look_z,
                  double *held, int *held_keys, short *lx, short *ly)
{
    double way = kbmove_way(keys_x, keys_y, heading, held, held_keys);
    double len = sqrt(look_x * look_x + look_z * look_z), fx, fz, dx, dz;
    if (!(len > KBMOVE_LOOK_MIN)) { *lx = *ly = 0; return; }
    fx = look_x / len;
    fz = look_z / len;
    dx = sin(way);
    dz = cos(way);
    /* Up on the stick is the look, right on it the look turned right. */
    *lx = (short)lround((dx * fz - dz * fx) * KBMOVE_FULL);
    *ly = (short)lround((dx * fx + dz * fz) * KBMOVE_FULL);
}

/* The game's angles, wrapped to -0x8000..0x7FFF as sub_0004A7F0 does. */
static int kbmove_wrap(long a)
{
    a &= 0xFFFF;
    return (int)(a >= 0x8000 ? a - 0x10000 : a);
}

int kbmove_soft_centre(int angle)
{
    double x;
    if (angle <= -KBMOVE_SOFT || angle >= KBMOVE_SOFT) return angle;
    /* 0x000A7ABF: the angle times 1/10922, squared with its sign, times
     * -10922 to a whole number by truncation (0x0017C3E8), taken from the
     * heading; the floats' sums in double, as the recompiled x87 does. */
    x = (double)angle * (double)KBMOVE_SOFT_IN;
    return -(int)(x * fabs(x) * (double)KBMOVE_SOFT_OUT);
}

int kbmove_soft_centre_undo(int way)
{
    int a, best, d, best_d, i;
    if (way <= -KBMOVE_SOFT || way >= KBMOVE_SOFT) return way;
    /* The square root, then whichever whole angle nearby the game's own
     * sums take nearest to the way. */
    a = (int)lround((way < 0 ? -1.0 : 1.0) * sqrt(fabs((double)way) * KBMOVE_SOFT));
    best = a;
    best_d = 0x10000;
    for (i = a - 2; i <= a + 2; i++) {
        if (i <= -KBMOVE_SOFT || i >= KBMOVE_SOFT) continue;
        d = kbmove_soft_centre(i) - way;
        if (d < 0) d = -d;
        if (d < best_d) { best_d = d; best = i; }
    }
    return best;
}

int kbmove_game_port(uint32_t pad, uint32_t main_port)
{
    if (pad == 4) pad = main_port;
    return pad < KBMOVE_PORTS ? (int)pad : -1;
}

void kbmove_view(unsigned port, double look_x, double look_z, uint32_t turn)
{
    if (port >= KBMOVE_PORTS) return;
    s_port[port].look_x = look_x;
    s_port[port].look_z = look_z;
    s_port[port].heading = (double)(turn & 0xFFFFu) * (2.0 * k_pi / KBMOVE_TURN);
    s_port[port].at = kbmove_now();
}

void kbmove_pad(unsigned port, short *lx, short *ly)
{
    int kx, ky, steer;
    if (port >= KBMOVE_PORTS || !lx || !ly) return;
    steer = s_mode == KBMOVE_SKATER && xbox_InputKeyboardMove(port, &kx, &ky)
         && s_port[port].at > 0.0 && kbmove_now() - s_port[port].at < KBMOVE_FRESH_S;
    if (steer) {
        kbmove_stick(kx, ky, s_port[port].heading, s_port[port].look_x, s_port[port].look_z,
                     &s_port[port].held, &s_port[port].held_keys, lx, ly);
        s_port[port].keys_x = kx;
        s_port[port].keys_y = ky;
    } else
        s_port[port].held_keys = 0;
    if (steer != s_port[port].steering) {
        static int said;
        s_port[port].steering = steer;
        if (said < 4) {
            said++;
            fprintf(stderr, "[KEYS] the keys %s the skater on port %u%s\n",
                    steer ? "steer" : "no longer steer", port, said == 4 ? " (no more of these)" : "");
        }
    }
}

int kbmove_angle(unsigned port, uint32_t turn, uint32_t heading, int holds, uint32_t held,
                 uint32_t *angle)
{
    double way, s_held;
    long way16;
    int a, s_keys;
    if (port >= KBMOVE_PORTS || !angle || !s_port[port].steering) return 0;
    /* The way S keeps is the pad read's (kbmove_pad), taken as S went down:
     * read here, not kept, as the pad reads on a thread of its own. */
    s_held = s_port[port].held;
    s_keys = s_port[port].held_keys;
    way = kbmove_way(s_port[port].keys_x, s_port[port].keys_y,
                     (double)(turn & 0xFFFFu) * (2.0 * k_pi / KBMOVE_TURN), &s_held, &s_keys);
    way16 = lround(way * (KBMOVE_TURN / (2.0 * k_pi)));
    if (holds) {
        /* The game goes the way it kept, and compares the stick from the
         * heading it kept with that way (0x000A79B5): the way the keys
         * mean, from that heading, lets it go when they turn from it. */
        a = kbmove_wrap(way16 - (long)held);
    } else {
        a = kbmove_soft_centre_undo(kbmove_wrap(way16 - (long)heading));
    }
    *angle = (uint32_t)a & 0xFFFFu;
    {
        static int said;
        if (!said) {
            said = 1;
            fprintf(stderr, "[KEYS] the skater's update takes the keys' way on port %u\n", port);
        }
    }
    return 1;
}

#if defined(XBOXRECOMP_MENU)
/* Input > Behaviour's row: at once, kept in the settings file. On the
 * window's thread. */
static int  menu_mode(void)       { return s_mode; }
static void menu_mode_pick(int i) { s_mode = i; nv_window_pref_set_num("keyboard_moves", i); }
#endif

void kbmove_start(void)
{
    double v;
    int r = nv_window_pref_num("keyboard_moves", &v);
    s_mode = KBMOVE_DEFAULT;
    if (r > 0 && (v == KBMOVE_SKATER || v == KBMOVE_CAMERA))
        s_mode = (int)v;
    else if (r != 0)
        fprintf(stderr, "[KEYS] settings: keyboard_moves is not 0 or 1; using %d\n", KBMOVE_DEFAULT);
    fprintf(stderr, "[KEYS] keyboard moves: %s\n", k_names[s_mode]);
#if defined(XBOXRECOMP_MENU)
    {
        /* A row of Input > Behaviour, after the Camera ticks
         * (port/camera_scenes.c): it adds the submenu too, which adds nothing
         * once that has (menu.h). File > Presets: Camera in Xbox mode, the
         * way the console's stick steers, and the default in the others. */
        const XBOX_MENU_ITEM rows[] = {
            { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_GROUP, .label = XBOX_GROUP_BEHAVIOUR,
              .hint = XBOX_GROUP_BEHAVIOUR_HINT },
            { .menu = XBOX_MENU_INPUT, .kind = XBOX_ITEM_CHOICE, .label = "Keyboard moves",
              .group = XBOX_GROUP_BEHAVIOUR, .state = menu_mode, .pick = menu_mode_pick,
              .choices = k_names, .nchoices = KBMOVE_CHOICES,
              .presets = XBOX_PRESETS_SET, .preset = { KBMOVE_CAMERA, KBMOVE_DEFAULT, KBMOVE_DEFAULT },
              .hint = "Sets whether the keyboard steers from the camera or the skater." },
        };
        xbox_MenuAdd(rows, (int)(sizeof rows / sizeof rows[0]));
    }
#endif
}
