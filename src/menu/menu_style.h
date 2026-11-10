/*
 * menu_style.h -- how the in-game menu (menu_sdl.cpp) and the first-start
 * setup window (launcher/setup/setup_ui.cpp) look: Dear ImGui's dark style,
 * in the original Xbox's greens, with rounded corners and room for a pad.
 *
 * C++, as both its callers are. It needs Dear ImGui and nothing else, so
 * the launcher builds it beside its own copy of Dear ImGui
 * (launcher/build_launcher.sh), and the two windows look the same.
 */
#ifndef MENU_STYLE_H
#define MENU_STYLE_H

#include "imgui.h"

/* Sets st to the style, at Dear ImGui's own sizes: the caller scales it to
 * its window afterwards (ImGuiStyle::ScaleAllSizes), as before. */
void menu_style_apply(ImGuiStyle *st);

/* The caps the strip along the bottom of the menu (menu.cpp) shows the
 * pad's buttons and the keys on, each behind its name in white: A in the
 * dark green and B in a red as dark, as the pad colours them, and the rest
 * -- the pad's Back, and the keys -- in grey. */
enum menu_cap { MENU_CAP_A, MENU_CAP_B, MENU_CAP_KEY, MENU_CAP_COUNT };
ImVec4 menu_style_cap(menu_cap cap);

#endif /* MENU_STYLE_H */
