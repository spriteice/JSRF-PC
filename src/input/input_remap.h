/* The layouts in use (input_layout.h), as a player sets them: the host's
 * pads' and the keyboard's (its keys and the mouse's buttons), kept in
 * remap.cfg beside the settings file, and the in-game menu's pages that set
 * them, Input > Controller layout and Input > Keyboard layout
 * (docs/menu-format.md). The menu changes them on the window's thread while
 * the game reads them on its own, so each read is a copy. No SDL in it, for
 * tests/input_layout: the inputs' names are xinput_device.c's. */
#ifndef XBOX_INPUT_REMAP_H
#define XBOX_INPUT_REMAP_H

#include "input_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The two layouts. */
#define INPUT_REMAP_PAD  0
#define INPUT_REMAP_KEYS 1

/* A copy of a layout as it is now: either may be NULL. At their defaults
 * until input_remap_start has read the settings. Any thread. */
void input_remap_layouts(InputLayout *pad, InputLayout *keys);

/* At start, on the main thread, before the game reads a pad: the layouts
 * from remap.cfg, each control that differs from its default said
 * in the log, and the menu's two pages (port/main.c, before the free
 * camera's, so they are Input's first submenus). */
void input_remap_start(void);

/* An input's name (xinput_device.c, from SDL's): as the menu shows it
 * ("LB", "Space", "Mouse left"), or as remap.cfg keeps it
 * ("leftshoulder", "Space", "Mouse left"). NULL for an input that has
 * none. And the input a name kept in the settings is, or INPUT_NONE. */
#define INPUT_NAME_SHOWN 0
#define INPUT_NAME_KEPT  1
const char *input_remap_name(int layout, int input, int how);
int input_remap_from_name(int layout, const char *name);

/* The port's own controls (input_layout.h, from INPUT_CTL_OWN), which only
 * the pad's layout has: what a press of one of a control's inputs runs, set
 * at start (port/freecam.c sets the free camera's switch). */
void input_remap_pad_hotkey(int control, void (*run)(void));

/* Each change of a host pad's button or trigger, from the window's thread
 * (nv2a_gl.c): pad, SDL's number for it; input, which (input_layout.h);
 * value, how far it is pressed, 0 to 255; shut, 1 while the in-game menu
 * is shut or there is none. A press, from below INPUT_LAYOUT_DOWN to at
 * least it, of an input a port's own control has in the pad's layout runs
 * that control's hotkey there, while shut. A press made while the menu is
 * open runs nothing, then or once it shuts. */
void input_remap_pad_event(int pad, int input, int value, int shut);

#ifdef __cplusplus
}
#endif

#endif
