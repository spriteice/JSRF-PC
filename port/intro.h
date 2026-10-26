/* The intro: the cards the game opens with, four logos and then the graffiti
 * disclaimer, before the title screen.
 *
 * Display > Skip intro, kept as skipintro= in the settings file, leaves them
 * out: the game opens at the title screen's loading. Off (skipintro=0, the
 * default), they play as on the Xbox. The hook is in recomp_manual.c:
 * sub_0007E360, the cards' update. */
#ifndef JSRF_INTRO_H
#define JSRF_INTRO_H

/* At start, before the game runs: reads the setting and adds its row to the
 * in-game menu's Display page. */
void intro_start(void);

/* Whether the intro is left out. */
int intro_skip(void);

#endif
