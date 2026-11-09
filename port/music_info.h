/* Music info in pause: the song playing now, in the pause menus.
 *
 * Display > Music info in pause, kept as musicinpause= in the settings file
 * (0, off, or 1, the default): both pause menus show the song that is
 * playing, its number, title and artist, in the game's font, in white, in
 * the free part of the menu's left column, above ROTATE MAP (and below
 * CHANGE MAP, in the street's menu). It changes at once, and the lines change with the song while the
 * menu is open, as the music plays on through the pause.
 *
 * In the garage a song plays from its own file, and the sound manager's
 * music id names it. In the streets a mix plays: one long file of several
 * songs, back to back, which loops. There the mix's play position, looked
 * up in the table of where each song starts in each street mix, names the
 * song. Anything else (the title, the DJ's scenes, the endings) shows
 * nothing.
 *
 * The hooks are in recomp_manual.c, inside the two pause menus' draws: they
 * read the game's position and draw the lines with the game's own font call.
 * The setting, the reading of the sound manager, the song lookup, the lines'
 * layout and the [MUSIC] log are here. */
#ifndef JSRF_MUSIC_INFO_H
#define JSRF_MUSIC_INFO_H

#include <stddef.h>
#include <stdint.h>

/* The game's music ids (the sound manager's +0x9C). The 30 songs, one file
 * each, in the order of the game's title and artist tables, so a song's
 * number is its index plus 1. */
#define MUSIC_FIRST_SONG 0x1E
#define MUSIC_SONGS      30
enum {
    MUSIC_HUMMING_THE_BASSLINE = MUSIC_FIRST_SONG, MUSIC_AISLE_10, MUSIC_THE_ANSWER,
    MUSIC_BABY_T, MUSIC_BIRTHDAY_CAKE, MUSIC_BOKFRESH, MUSIC_LATCH_BROTHER_BOUNCE,
    MUSIC_FLY_LIKE_A_BUTTERFLY, MUSIC_THE_CONCEPT_OF_LOVE, MUSIC_FUNKY_DEALER,
    MUSIC_SHAPE_DA_FUTURE, MUSIC_STATEMENT_OF_INTENT, MUSIC_KOTO_STOMP, MUSIC_COUNT_LATCHULA,
    MUSIC_LET_MOM_SLEEP, MUSIC_I_LOVE_LOVE_YOU, MUSIC_ROCKIN_THE_MIC, MUSIC_IM_NOT_A_MODEL,
    MUSIC_OLDIES_BUT_HAPPIES, MUSIC_ME_LIKEY_THE_POOM_POOM, MUSIC_ROCK_IT_ON, MUSIC_THE_SCRAPPY,
    MUSIC_SNEAKMAN, MUSIC_ILL_VICTORY_BEAT, MUSIC_WHAT_ABOUT_THE_FUTURE, MUSIC_TEKNOPATHETIC,
    MUSIC_LIKE_IT_LIKE_THIS_LIKE_THAT, MUSIC_SWEET_SOUL_BROTHER, MUSIC_THATS_ENOUGH,
    MUSIC_GRACE_AND_GLORY
};
/* The street mixes: an area's "a" and "b" mixes, by their files' names, and
 * set_04, a mix that is no area's. */
enum {
    MUSIC_MIX_04 = 0x1A,
    MUSIC_MIX_01A = 0x3C, MUSIC_MIX_01B, MUSIC_MIX_02A, MUSIC_MIX_02B, MUSIC_MIX_03A,
    MUSIC_MIX_05A, MUSIC_MIX_05B, MUSIC_MIX_06A, MUSIC_MIX_06B, MUSIC_MIX_07A, MUSIC_MIX_07B,
    MUSIC_MIX_08A, MUSIC_MIX_08B, MUSIC_MIX_09A, MUSIC_MIX_09B, MUSIC_MIX_03B
};
/* The music that shows nothing: the title, the clear and game over jingles,
 * the DJ's two scenes, the ending's copy of Grace & Glory, and the two
 * endings. */
enum {
    MUSIC_TITLE = 0x00, MUSIC_CLEAR = 0x0A, MUSIC_GAME_OVER = 0x0B, MUSIC_DJ_DEMO = 0x14,
    MUSIC_DJ_CLEAR = 0x15, MUSIC_ENDING_GRACE = 0x19, MUSIC_ENDING_SHORT = 0x1B,
    MUSIC_ENDING_LONG = 0x1C
};
#define MUSIC_NONE (-1)         /* the sound manager's id when nothing plays */
#define MUSIC_RATE 44100        /* every music file's rate: the table's samples */

/* The game's tables of the 30 songs' titles and artists: a pointer to each
 * string, in song order. The strings break their lines with "$n". */
#define MUSIC_TITLES  0x002254B0u
#define MUSIC_ARTISTS 0x00225528u

/* Where the sound manager is, and what it holds. */
#define MUSIC_OBJECTS      0x0022FCE0u  /* the pointer to the game's object manager */
#define MUSIC_OBJECT_TABLE 0x98u        /* its table of objects, by slot (sub_000128C0) */
#define MUSIC_SOUND_SLOT   5u           /* the sound manager's slot */
#define MUSIC_SOUND_SIZE   0x110u       /* the sound manager's fields read here end before this */
#define MUSIC_PLAYERS      0x68u        /* its two music players (ADXT handles), 4 bytes apart */
#define MUSIC_CURRENT      0x78u        /* which of the two is heard, 0 or 1 */
#define MUSIC_ID           0x9Cu        /* the music id on it, or MUSIC_NONE */
#define ADXT_STATUS        0x01u        /* a player's status byte */
#define ADXT_SIZE          0x100u       /* a player's fields read here end before this */
#define ADXT_PLAYING       3            /* status: playing */
#define ADXT_DECODED       4            /* status: decoded to the end, still playing out */

/* Where the lines go, in the menu's 640x480 space: the left column's x,
 * with CHANGE MAP and ROTATE MAP, from below CHANGE MAP's line to above the
 * button over ROTATE MAP. The game's text is drawn from its top. A line's
 * advance is the font's at scale 1 times the scale; a block that wouldn't
 * fit at MUSIC_INFO_SCALE is drawn smaller, all of it at one scale. */
#define MUSIC_INFO_X       48.0f
#define MUSIC_INFO_TOP     172.0f
#define MUSIC_INFO_BOTTOM  294.0f
#define MUSIC_INFO_SCALE   0.6f   /* as CHANGE MAP's and ROTATE MAP's */
#define MUSIC_INFO_ADVANCE 30.0f  /* a line's advance at scale 1, to be set from the test build */
#define MUSIC_INFO_GAP     4.0f   /* between the number, the title and the artist */
#define MUSIC_INFO_SPACING 1.1f   /* as every menu label passes */
#define MUSIC_INFO_WHITE   0xFFFFFFFFu
#define MUSIC_INFO_LINES   3      /* the number, the title, the artist */
#define MUSIC_INFO_TEXT    128    /* the most of a title or artist read for its breaks */

/* The two pause menus, for the log. */
enum { MUSIC_MENU_STREET, MUSIC_MENU_OTHER, MUSIC_MENUS };

/* At start, before the game runs: reads the setting and adds its row to the
 * in-game menu's Display page, under Skip intro. */
void music_info_start(void);

/* Whether the menus show the song. */
int music_info_on(void);

/* What the sound manager plays now: the music id on the player that is
 * heard, and that player. */
typedef struct {
    int id;
    uint32_t player;
} MusicNow;

/* Reads it from the game's memory mem, whose guest addresses run below top.
 * 1 when that player plays (or plays out) a song or a mix, so there is
 * something to show; 0 otherwise. */
int music_info_now(const uint8_t *mem, uint32_t top, MusicNow *now);

/* Whether id is a mix, whose song needs its play position. */
int music_info_is_mix(int id);

/* The song to show for music id id, at play position ncount / tscale
 * seconds (ADXT_GetTime's) for a mix: its index (0 to MUSIC_SONGS - 1), or
 * -1 for nothing. A mix's position past its end is folded into its loop. */
int music_info_song(int id, int64_t ncount, int64_t tscale);

/* The same, for a pause menu drawing at time now: the [MUSIC] log says when
 * each menu first shows the music, and each change of the song shown. */
int music_info_pick(int menu, int id, int64_t ncount, int64_t tscale, double now);

/* One line to draw: where, at what scale, and its text, a guest address, or
 * 0 for the number's text. */
typedef struct {
    float x, y, scale;
    uint32_t text;
} MusicLine;

/* The lines for song song (0 to MUSIC_SONGS - 1), the title and artist read
 * from the game's tables in mem, and the number's text, written to number
 * (n bytes). How many lines: MUSIC_INFO_LINES, or 0 when the tables don't
 * hold the song's strings. */
int music_info_lines(const uint8_t *mem, uint32_t top, int song, MusicLine line[MUSIC_INFO_LINES],
                     char *number, size_t n);

#endif
