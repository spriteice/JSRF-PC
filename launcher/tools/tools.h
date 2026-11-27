/*
 * JSRF-PC Tools: the shell (tools_main.cpp) and its pages. The saves page
 * (tools_saves.cpp, b3 row 36), the graffiti page (b3 row 35) and the
 * missions page (tools_missions.cpp, b3 row 38) each draw into the shell's
 * window, which finds the game, keeps the window's scale,
 * and hands each page what is dropped on the window.
 *
 * C++, as Dear ImGui is. Paths are UTF-8.
 */
#ifndef JSRF_TOOLS_H
#define JSRF_TOOLS_H

#include "save_sources.h"

struct tools_ctx {
    char  game[JSRF_PATH_MAX];      /* JSRF-PC's folder: the launcher's */
    char  saves[JSRF_PATH_MAX];     /* the folder the game's UDATA is in */
    char  title[JSRF_PATH_MAX];     /* its UDATA\5345000a */
    char  backups[JSRF_PATH_MAX];   /* where the dated backups go: <saves>\Save backups */
    char  game_files[JSRF_PATH_MAX];/* the game's own files (default.xbe's folder, or the disc image), "" unknown */
    char  settings[JSRF_PATH_MAX];  /* the folder JSRF-PC's settings file is in */
    float scale;                    /* the display's, for sizes a page sets itself */
    int   busy;                     /* set by a page, each frame, while it needs frames to keep
                                       coming (work on another thread, or one step a frame);
                                       otherwise the window draws only when something happens */
};

struct tools_page {
    const char *name;                                   /* its tab */
    void (*draw)(tools_ctx *t);
    void (*dropped)(tools_ctx *t, const char *path);    /* a file or folder dropped while it shows */
};

/* The saves page (tools_saves.cpp). */
extern const tools_page tools_saves_page;

/* The graffiti page (graffiti_page.cpp). */
extern const tools_page tools_graffiti_page;

/* The missions page (tools_missions.cpp). */
extern const tools_page tools_missions_page;

/* ---- for the pages: the shell's helpers ----------------------------------------- */

/* A button that goes on the line before when there is room for it, and
 * starts a line of its own when there isn't, so a row of them reflows as
 * the window narrows. first: the row's first button. */
bool tools_flow_button(const char *label, bool first);

/* The shell's warning before anything is written: a modal with text, and
 * go and Cancel. Open it with ImGui::OpenPopup(id); true the frame go is
 * pressed. */
bool tools_warning(const char *id, const char *text, const char *go);

#endif
