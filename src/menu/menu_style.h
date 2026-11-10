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

struct ImGuiStyle;

/* Sets st to the style, at Dear ImGui's own sizes: the caller scales it to
 * its window afterwards (ImGuiStyle::ScaleAllSizes), as before. */
void menu_style_apply(ImGuiStyle *st);

#endif /* MENU_STYLE_H */
