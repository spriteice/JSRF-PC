/* Run with: python tests/music_info/run.py
 *
 * Music info in pause without the game: port/music_info.c as it is, built with
 * the in-game menu, reading and keeping musicinpause= through the engine's own
 * settings code (taken from src/nv2a_gl/nv2a_gl.c by run.py). The file is
 * included, not linked, so the cases can walk its table of the street mixes.
 * The menu's xbox_MenuAdd is stood in for, to catch the row. Each settings
 * case is its own run, as the setting is read once; run.py writes the
 * settings file and checks the log and the file.
 *
 * The game's side is a guest memory of the test's own: the object manager,
 * the sound manager with its two music players, and the title and artist
 * tables with a few of their strings, laid out as music_info.h says the
 * game's are. ADXT_GetTime is the game's: here a mix's play time is given as
 * its count and rate, as ADXT_GetTime gives them.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "menu.h"
#include "music_info.c"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static XBOX_MENU_ITEM s_row;
static int s_rows;
void xbox_MenuAdd(const XBOX_MENU_ITEM *items, int count)
{
    if (count > 0) s_row = items[0];
    s_rows += count;
}

/* ---- the guest memory ------------------------------------------------------ */
#define TOP      0x00400000u
#define MANAGER  0x00300000u   /* the object manager */
#define SOUND    0x00310000u   /* the sound manager */
#define PLAYER_0 0x00320000u
#define PLAYER_1 0x00320100u
#define STRINGS  0x00330000u
static uint8_t *mem;

static void put32(uint32_t va, uint32_t v) { memcpy(mem + va, &v, 4); }
static uint32_t put_text(uint32_t *at, const char *text)
{
    uint32_t va = *at;
    memcpy(mem + va, text, strlen(text) + 1);
    *at += (uint32_t)strlen(text) + 1;
    return va;
}

/* The sound manager playing id on player current, whose status is status. */
static void playing(int id, uint32_t current, int status)
{
    put32(MUSIC_OBJECTS, MANAGER);
    put32(MANAGER + MUSIC_OBJECT_TABLE + 4 * MUSIC_SOUND_SLOT, SOUND);
    put32(SOUND + MUSIC_PLAYERS, PLAYER_0);
    put32(SOUND + MUSIC_PLAYERS + 4, PLAYER_1);
    put32(SOUND + MUSIC_CURRENT, current);
    put32(SOUND + MUSIC_ID, (uint32_t)id);
    mem[PLAYER_0 + ADXT_STATUS] = 3;
    mem[(current ? PLAYER_1 : PLAYER_0) + ADXT_STATUS] = (uint8_t)status;
}

/* ---- the cases -------------------------------------------------------------- */
static void check_now(void)
{
    MusicNow now;
    /* A mix on the second player, playing; then playing out its end. */
    playing(MUSIC_MIX_01A, 1, ADXT_PLAYING);
    CHECK(music_info_now(mem, TOP, &now) && now.id == MUSIC_MIX_01A && now.player == PLAYER_1);
    playing(MUSIC_MIX_01A, 1, ADXT_DECODED);
    CHECK(music_info_now(mem, TOP, &now));
    /* A song on the first. */
    playing(MUSIC_GRACE_AND_GLORY, 0, ADXT_PLAYING);
    CHECK(music_info_now(mem, TOP, &now) && now.id == MUSIC_GRACE_AND_GLORY && now.player == PLAYER_0);
    /* Not yet playing (status 1), ended (5) or none (0): nothing. */
    playing(MUSIC_MIX_01A, 1, 1);
    CHECK(!music_info_now(mem, TOP, &now));
    playing(MUSIC_MIX_01A, 1, 5);
    CHECK(!music_info_now(mem, TOP, &now));
    playing(MUSIC_MIX_01A, 1, 0);
    CHECK(!music_info_now(mem, TOP, &now));
    /* No music; and the music that shows nothing, which says nothing. */
    playing(MUSIC_NONE, 0, ADXT_PLAYING);
    CHECK(!music_info_now(mem, TOP, &now));
    playing(MUSIC_TITLE, 0, ADXT_PLAYING);
    CHECK(!music_info_now(mem, TOP, &now));
    playing(MUSIC_DJ_DEMO, 0, ADXT_PLAYING);
    CHECK(!music_info_now(mem, TOP, &now));
    playing(MUSIC_ENDING_LONG, 0, ADXT_PLAYING);
    CHECK(!music_info_now(mem, TOP, &now));
    /* What the game never holds: each a [CHECK] line, once (run.py), and
     * nothing shown. */
    playing(0x77, 0, ADXT_PLAYING);
    CHECK(!music_info_now(mem, TOP, &now));
    playing(MUSIC_MIX_01A, 2, ADXT_PLAYING);
    CHECK(!music_info_now(mem, TOP, &now));
    playing(MUSIC_MIX_01A, 0, ADXT_PLAYING);
    put32(SOUND + MUSIC_PLAYERS, TOP - 8);
    CHECK(!music_info_now(mem, TOP, &now));
    playing(MUSIC_MIX_01A, 0, ADXT_PLAYING);
    put32(MANAGER + MUSIC_OBJECT_TABLE + 4 * MUSIC_SOUND_SLOT, 0);
    CHECK(!music_info_now(mem, TOP, &now));
    put32(MUSIC_OBJECTS, 0);
    CHECK(!music_info_now(mem, TOP, &now));
    /* Guest memory too small to hold the object manager's pointer. */
    CHECK(!music_info_now(mem, MUSIC_OBJECTS, &now));
    printf("music_info: the sound manager read\n");
}

static void check_songs(void)
{
    int id;
    /* Each song: its index, whatever the position. */
    for (id = MUSIC_FIRST_SONG; id < MUSIC_FIRST_SONG + MUSIC_SONGS; id++) {
        CHECK(music_info_song(id, 0, 0) == id - MUSIC_FIRST_SONG);
        CHECK(!music_info_is_mix(id));
    }
    CHECK(music_info_song(MUSIC_HUMMING_THE_BASSLINE, 0, 0) == 0);
    CHECK(music_info_song(MUSIC_GRACE_AND_GLORY, 0, 0) == MUSIC_SONGS - 1);
    /* What shows nothing. */
    CHECK(music_info_song(MUSIC_TITLE, 0, MUSIC_RATE) == -1);
    CHECK(music_info_song(MUSIC_ENDING_GRACE, 0, MUSIC_RATE) == -1);
    CHECK(music_info_song(MUSIC_NONE, 0, MUSIC_RATE) == -1);
    CHECK(music_info_song(0x77, 0, MUSIC_RATE) == -1);
    printf("music_info: the songs\n");
}

/* Every mix in the table: its rows in order, inside its loop's end, each a
 * song, no song twice in a row; and the song at and just before each row, at
 * the files' rate and at another, then a lap of the loop later. */
static void check_mixes(void)
{
    static const int ids[] = { MUSIC_MIX_04, MUSIC_MIX_01A, MUSIC_MIX_01B, MUSIC_MIX_02A,
        MUSIC_MIX_02B, MUSIC_MIX_03A, MUSIC_MIX_05A, MUSIC_MIX_05B, MUSIC_MIX_06A, MUSIC_MIX_06B,
        MUSIC_MIX_07A, MUSIC_MIX_07B, MUSIC_MIX_08A, MUSIC_MIX_08B, MUSIC_MIX_09A, MUSIC_MIX_09B,
        MUSIC_MIX_03B };
    const int n_ids = (int)(sizeof ids / sizeof ids[0]);
    int i, k, rows = 0, opens_inside = 0;
    CHECK((int)(sizeof k_mixes / sizeof k_mixes[0]) == n_ids);
    for (i = 0; i < n_ids; i++) {
        const Mix *m = find_mix(ids[i]);
        int64_t lap;
        CHECK(m && music_info_is_mix(ids[i]));
        CHECK(m->n >= 3 && m->loop_start < m->songs[1].at && m->loop_start < m->loop_end);
        lap = (int64_t)m->loop_end - m->loop_start;
        CHECK(m->songs[m->n - 1].at < m->loop_end);
        /* The opening shows nothing, the first time, or the first song, in a
         * mix that opens inside it. */
        if (m->songs[0].at > 0) {
            CHECK(music_info_song(ids[i], 0, MUSIC_RATE) == -1);
            CHECK(music_info_song(ids[i], m->songs[0].at - 1, MUSIC_RATE) == -1);
        } else {
            CHECK(music_info_song(ids[i], 0, MUSIC_RATE) == m->songs[0].song - MUSIC_FIRST_SONG);
            opens_inside++;
        }
        for (k = 0; k < m->n; k++) {
            int want = m->songs[k].song - MUSIC_FIRST_SONG;
            int64_t at = m->songs[k].at;
            CHECK(want >= 0 && want < MUSIC_SONGS);
            if (k) CHECK(at > (int64_t)m->songs[k - 1].at && m->songs[k].song != m->songs[k - 1].song);
            CHECK(music_info_song(ids[i], at, MUSIC_RATE) == want);
            if (k) CHECK(music_info_song(ids[i], at - 1, MUSIC_RATE)
                         == m->songs[k - 1].song - MUSIC_FIRST_SONG);
            /* At the output's rate, 48 kHz: the count rounded up still
             * lands on the row. */
            CHECK(music_info_song(ids[i], (at * 48000 + MUSIC_RATE - 1) / MUSIC_RATE, 48000) == want);
            /* A lap later, for a row inside the loop. */
            if (at >= (int64_t)m->loop_start)
                CHECK(music_info_song(ids[i], at + lap, MUSIC_RATE) == want
                      && music_info_song(ids[i], at + 3 * lap, MUSIC_RATE) == want);
        }
        /* The last row runs to the loop's end, and after a lap, what is left
         * of the opening still shows it, until the first song comes in. */
        CHECK(music_info_song(ids[i], m->loop_end - 1, MUSIC_RATE)
              == m->songs[m->n - 1].song - MUSIC_FIRST_SONG);
        CHECK(music_info_song(ids[i], m->loop_end, MUSIC_RATE)
              == (m->loop_start < m->songs[0].at ? m->songs[m->n - 1].song : m->songs[0].song)
                 - MUSIC_FIRST_SONG);
        if (m->songs[0].at >= m->loop_start)
            CHECK(music_info_song(ids[i], (int64_t)m->songs[0].at + lap, MUSIC_RATE)
                  == m->songs[0].song - MUSIC_FIRST_SONG);
        rows += m->n;
    }
    /* A rate of 0, or a count below 0: nothing, and a [CHECK] line once. */
    CHECK(music_info_song(MUSIC_MIX_01A, MUSIC_RATE, 0) == -1);
    CHECK(music_info_song(MUSIC_MIX_01A, -1, MUSIC_RATE) == -1);
    printf("music_info: %d mixes, %d rows, %d opening inside their first song\n", n_ids, rows,
           opens_inside);
}

static void check_lines(void)
{
    uint32_t at = STRINGS;
    MusicLine line[MUSIC_INFO_LINES];
    char number[24];
    uint32_t aisle_title, aisle_artist, love_title, love_artist;
    float bottom;
    int k;
    /* Song 2, a line each; song 16, three lines each, as the game's. */
    aisle_title = put_text(&at, "AISLE 10");
    aisle_artist = put_text(&at, "SCAPEGOAT WAX");
    love_title = put_text(&at, "I LOVE LOVE YOU$n(Love Love Super$nDimension Mix)");
    love_artist = put_text(&at, "GUITAR VADER$nremixed by$nHIDEKI NAGANUMA");
    put32(MUSIC_TITLES + 4 * 1, aisle_title);
    put32(MUSIC_ARTISTS + 4 * 1, aisle_artist);
    put32(MUSIC_TITLES + 4 * 15, love_title);
    put32(MUSIC_ARTISTS + 4 * 15, love_artist);

    CHECK(music_info_lines(mem, TOP, 1, line, number, sizeof number) == MUSIC_INFO_LINES);
    CHECK(!strcmp(number, "No. 2"));
    CHECK(line[0].text == 0 && line[1].text == aisle_title && line[2].text == aisle_artist);
    for (k = 0; k < MUSIC_INFO_LINES; k++)
        CHECK(line[k].x == MUSIC_INFO_X && line[k].scale == MUSIC_INFO_SCALE);
    CHECK(line[0].y == MUSIC_INFO_TOP);
    CHECK(fabsf(line[1].y - (line[0].y + MUSIC_INFO_ADVANCE * MUSIC_INFO_SCALE + MUSIC_INFO_GAP)) < 1e-3f);
    CHECK(fabsf(line[2].y - (line[1].y + MUSIC_INFO_ADVANCE * MUSIC_INFO_SCALE + MUSIC_INFO_GAP)) < 1e-3f);

    /* Seven lines: one scale for all, no more than the labels', and the
     * artist's last line ends above the bottom. */
    CHECK(music_info_lines(mem, TOP, 15, line, number, sizeof number) == MUSIC_INFO_LINES);
    CHECK(!strcmp(number, "No. 16"));
    CHECK(line[1].text == love_title && line[2].text == love_artist);
    CHECK(line[0].scale <= MUSIC_INFO_SCALE && line[0].scale > 0.3f);
    CHECK(line[1].scale == line[0].scale && line[2].scale == line[0].scale);
    CHECK(fabsf(line[2].y - (line[1].y + 3 * MUSIC_INFO_ADVANCE * line[1].scale + MUSIC_INFO_GAP)) < 1e-3f);
    bottom = line[2].y + 3 * MUSIC_INFO_ADVANCE * line[2].scale;
    CHECK(bottom <= MUSIC_INFO_BOTTOM + 1e-3f);
    printf("music_info: seven lines at scale %.3f, to y %.1f\n", line[0].scale, bottom);

    /* A song whose strings aren't there (a [CHECK] line, once), or no song. */
    put32(MUSIC_TITLES + 4 * 4, 0);
    CHECK(music_info_lines(mem, TOP, 4, line, number, sizeof number) == 0);
    CHECK(music_info_lines(mem, TOP, -1, line, number, sizeof number) == 0);
    CHECK(music_info_lines(mem, TOP, MUSIC_SONGS, line, number, sizeof number) == 0);
    /* A string with no end within the read: not a string. */
    {
        uint32_t va = at, i;
        for (i = 0; i < MUSIC_INFO_TEXT + 8; i++) mem[va + i] = 'A';
        put32(MUSIC_TITLES + 4 * 5, va);
        put32(MUSIC_ARTISTS + 4 * 5, aisle_artist);
        CHECK(music_info_lines(mem, TOP, 5, line, number, sizeof number) == 0);
    }
    printf("music_info: the lines\n");
}

/* The log: each menu's first showing, then each change of the song. */
static void check_pick(void)
{
    const Mix *m = find_mix(MUSIC_MIX_01A);
    CHECK(music_info_pick(MUSIC_MENU_STREET, MUSIC_MIX_01A, 0, MUSIC_RATE, 1.0) == -1);
    CHECK(music_info_pick(MUSIC_MENU_STREET, MUSIC_MIX_01A, 1, MUSIC_RATE, 1.5) == -1);
    CHECK(music_info_pick(MUSIC_MENU_STREET, MUSIC_MIX_01A, m->songs[0].at, MUSIC_RATE, 2.0)
          == m->songs[0].song - MUSIC_FIRST_SONG);
    CHECK(music_info_pick(MUSIC_MENU_OTHER, MUSIC_MIX_01A, m->songs[1].at, MUSIC_RATE, 3.0)
          == m->songs[1].song - MUSIC_FIRST_SONG);
    CHECK(music_info_pick(MUSIC_MENU_OTHER, MUSIC_HUMMING_THE_BASSLINE, 0, 0, 4.0) == 0);
    printf("music_info: the log\n");
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    music_info_start();
    /* One row, a toggle in Display > Enhancements, showing the setting. */
    CHECK(s_rows == 1);
    CHECK(s_row.menu == XBOX_MENU_DISPLAY && s_row.kind == XBOX_ITEM_TOGGLE && !s_row.last && !s_row.under);
    CHECK(!strcmp(s_row.label, "Music info in pause") && !s_row.confirm);
    CHECK(s_row.group && !strcmp(s_row.group, XBOX_GROUP_ENHANCEMENTS));
    /* A hint, a sentence short enough for the strip (menu.h). */
    CHECK(s_row.hint && strlen(s_row.hint) <= MENU_HINT_LONGEST && s_row.hint[strlen(s_row.hint) - 1] == '.');
    CHECK(s_row.state() == music_info_on());
    /* File > Presets: off in Xbox mode, and JSRF-PC default's and Max
     * quality's are what a start with no musicinpause= gives. */
    CHECK(s_row.presets == XBOX_PRESETS_SET && !s_row.keep && s_row.preset[XBOX_PRESET_XBOX] == 0);
    CHECK(s_row.preset[XBOX_PRESET_MAX] == s_row.preset[XBOX_PRESET_JSRF_PC]);
    if (!strcmp(c, "default")) CHECK(s_row.preset[XBOX_PRESET_JSRF_PC] == music_info_on());
    if (!strcmp(c, "off")) CHECK(!music_info_on());
    if (!strcmp(c, "on") || !strcmp(c, "default")) CHECK(music_info_on());   /* on by default */
    if (!strcmp(c, "flip")) {
        /* Off, then the row: on at once (and kept, which run.py checks). */
        CHECK(!music_info_on());
        s_row.run();
        CHECK(music_info_on() && s_row.state());
    }
    if (!strcmp(c, "game")) {
        mem = calloc(1, TOP);
        CHECK(mem != NULL);
        check_now();
        check_songs();
        check_mixes();
        check_lines();
        check_pick();
        free(mem);
    }
    printf("music_info: %s\n", c);
    return 0;
}
