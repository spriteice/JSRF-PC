/*
 * menu_sdl.h -- the in-game menu, drawn over an SDL window with an OpenGL
 * context: nv2a_gl.c's. Its contents are menu.h's.
 *
 * Plain C, so nv2a_gl.c stays C; the Dear ImGui side is menu_sdl.cpp.
 */
#ifndef XBOX_MENU_SDL_H
#define XBOX_MENU_SDL_H

#include <SDL.h>    /* SDL2::SDL2 adds include/SDL2 only, on MinGW */

#ifdef __cplusplus
extern "C" {
#endif

/* Once, on the GL thread, with the window's context current. about_gl: one
 * line saying which OpenGL this is, for File > About. 0 if Dear ImGui could
 * not start; the game then runs without the menu. */
int  xbox_MenuInit(SDL_Window *window, void *gl_context, const char *about_gl);

/* Each event, from sdl_pump, before the game's own handling. 1: it was the
 * menu's (F1, a pad's Back, or a key or click while it is open) and the game
 * must not see it. */
int  xbox_MenuEvent(const SDL_Event *ev);

/* Once a frame on the GL thread, after the events: builds the menu. */
void xbox_MenuFrame(void);

/* Just before each swap, on whichever thread swaps, with the window's
 * framebuffer bound: draws what xbox_MenuFrame built. */
void xbox_MenuDraw(void);

/* 1 while the menu has the controls, 0 while it is shut, -1 when there is
 * none (Dear ImGui did not start). Any thread. */
int  xbox_MenuIsOpen(void);

#ifdef __cplusplus
}
#endif

#endif /* XBOX_MENU_SDL_H */
