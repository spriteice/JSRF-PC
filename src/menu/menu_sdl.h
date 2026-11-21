/*
 * menu_sdl.h -- the in-game menu, drawn over an SDL window with an OpenGL
 * context: nv2a_gl.c's. Its contents are menu.h's.
 *
 * Plain C, so nv2a_gl.c stays C; the Dear ImGui side is menu_sdl.cpp.
 */
#ifndef XBOX_MENU_SDL_H
#define XBOX_MENU_SDL_H

#include <SDL.h>    /* SDL2::SDL2 adds include/SDL2 only, on MinGW */
#include "menu.h"   /* XBOX_MENU_BOX */

#ifdef __cplusplus
extern "C" {
#endif

/* Once, on the GL thread, with the window's context current. about_gpu and
 * about_gl: the graphics card and the OpenGL version, for File > About; kept,
 * not copied. 0 if Dear ImGui could not start; the game then runs without the
 * menu. */
int  xbox_MenuInit(SDL_Window *window, void *gl_context, const char *about_gpu,
                   const char *about_gl);

/* Each event, from sdl_pump, before the game's own handling. 1: it was the
 * menu's (F1, a pad's Back, a key or click while it or a box is open, F9
 * while a box is, or a press a bind row's box takes: menu.h,
 * XBOX_ITEM_BIND) and the game must not see it. */
int  xbox_MenuEvent(const SDL_Event *ev);

/* Once a frame on the GL thread, after the events: builds the menu, as it
 * fades in or out (menu_fade). */
void xbox_MenuFrame(void);

/* Just before each swap, on whichever thread swaps, with the window's
 * framebuffer bound: draws what xbox_MenuFrame built. */
void xbox_MenuDraw(void);

/* A box over the window while the game waits for something, on the GL
 * thread: `what`, done of total (menu_build_busy), from the next
 * xbox_MenuFrame until total is 0. */
void xbox_MenuBusy(const char *what, int done, int total);

/* A box of the window's own over the game (menu.h, XBOX_MENU_BOX): F9's,
 * put up from the next xbox_MenuFrame, unless the menu or a box is up
 * already, and kept as a copy. The pad starts on the choice in use, which
 * is marked. A choice is handed to its pick and shuts it; Cancel, Esc and
 * B shut it with nothing chosen, and so do F9, F1 and the pad's Back. While
 * it is up the game runs on behind it, and the keys, the mouse and the pad
 * are the box's (xbox_MenuIsOpen). On the GL thread, from sdl_pump. */
void xbox_MenuBox(const XBOX_MENU_BOX *box);

/* 1 while the menu or a box has the controls, 0 while both are shut, -1
 * when there is no menu (Dear ImGui did not start). Any thread. */
int  xbox_MenuIsOpen(void);

#ifdef __cplusplus
}
#endif

#endif /* XBOX_MENU_SDL_H */
