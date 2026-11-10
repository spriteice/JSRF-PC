/* Music info in pause (music_info.h): musicinpause= in the settings file,
 * or Display > Music info in pause.
 *
 * Read once, at start (music_info_start). The menu's row changes it at once
 * and keeps it in the settings file; it runs on the window's thread, and the
 * pause menus' hooks read it on the game's. Plain C on guest memory, so a
 * test links it (tests/music_info). */
#include <stdio.h>
#include <string.h>
#include "music_info.h"
#include "platform/recomp_check.h"
#if defined(XBOXRECOMP_MENU)
#  include "menu.h"
#endif

#if defined(NV2A_GL_USE_SDL)
/* nv2a_gl.c: a number from the settings file the engine already keeps, and a
 * number kept there. */
extern int  nv_window_pref_num(const char *key, double *v);
extern void nv_window_pref_set_num(const char *key, double v);
#else
/* Without the SDL window (a headless build) there is no settings file: no
 * keys. */
static int nv_window_pref_num(const char *key, double *v)
{
    (void)key; (void)v;
    return 0;
}
#endif

/* The row goes right under Skip intro (port/intro.c). */
#define MUSIC_UNDER "Skip intro"

/* On with no musicinpause= in the settings. */
#define MUSIC_INFO_DEFAULT 1

static volatile int s_on;

#if defined(XBOXRECOMP_MENU)
static int  menu_on(void) { return s_on; }
static void menu_flip(void)
{
    s_on = !s_on;
    fprintf(stderr, "[MUSIC] info in pause %s (the menu)\n", s_on ? "on" : "off");
    nv_window_pref_set_num("musicinpause", s_on);
}
#endif

void music_info_start(void)
{
    double v;
    s_on = nv_window_pref_num("musicinpause", &v) > 0 ? v != 0 : MUSIC_INFO_DEFAULT;
    fprintf(stderr, "[MUSIC] info in pause %s (settings)\n", s_on ? "on" : "off");
#if defined(XBOXRECOMP_MENU)
    {
        const XBOX_MENU_ITEM row = {
            .menu = XBOX_MENU_DISPLAY, .kind = XBOX_ITEM_TOGGLE, .label = "Music info in pause",
            .state = menu_on, .run = menu_flip, .under = MUSIC_UNDER,
            .hint = "Shows the song title, artist, and track number in the pause menu." };
        xbox_MenuAdd(&row, 1);
    }
#endif
}

int music_info_on(void)
{
    return s_on;
}

/* ---- the street mixes ------------------------------------------------------
 * Where each song starts in each street mix, in samples at MUSIC_RATE: from
 * the moment it comes in, over the end of the one before, to the next one's.
 * Between two songs, where neither plays, the one before is still shown.
 * Most mixes open with a few seconds before their first song, which show
 * nothing; set_04 and set_06 open inside theirs. A mix loops from
 * loop_start, in that opening or its first song, to loop_end. */
typedef struct {
    int song;
    uint32_t at;
} MixSong;

typedef struct {
    int id;
    uint32_t loop_start, loop_end;
    const MixSong *songs;
    int n;
} Mix;

#define S(song, at) { MUSIC_##song, at }
#define MIX(id, loop_start, loop_end, rows) \
    { MUSIC_##id, loop_start, loop_end, rows, (int)(sizeof rows / sizeof rows[0]) }
static const MixSong k_mix_04[] = {
    S(THE_SCRAPPY, 0), S(KOTO_STOMP, 7254450), S(ROCKIN_THE_MIC, 12568500),
    S(LATCH_BROTHER_BOUNCE, 20396250), S(THE_ANSWER, 31465350), S(AISLE_10, 42887250),
};
static const MixSong k_mix_01a[] = {
    S(FUNKY_DEALER, 507150), S(I_LOVE_LOVE_YOU, 8974350), S(LET_MOM_SLEEP, 19800900),
    S(FLY_LIKE_A_BUTTERFLY, 28951650), S(WHAT_ABOUT_THE_FUTURE, 37639350),
    S(ROCK_IT_ON, 47076750),
};
static const MixSong k_mix_01b[] = {
    S(FLY_LIKE_A_BUTTERFLY, 551250), S(WHAT_ABOUT_THE_FUTURE, 9261000), S(ROCK_IT_ON, 18676350),
    S(FUNKY_DEALER, 36250200), S(I_LOVE_LOVE_YOU, 44717400), S(LET_MOM_SLEEP, 55543950),
};
static const MixSong k_mix_02a[] = {
    S(STATEMENT_OF_INTENT, 418950), S(ME_LIKEY_THE_POOM_POOM, 7519050), S(AISLE_10, 16140600),
    S(ROCKIN_THE_MIC, 25335450), S(OLDIES_BUT_HAPPIES, 33273450), S(BIRTHDAY_CAKE, 41387850),
    S(LIKE_IT_LIKE_THIS_LIKE_THAT, 48598200), S(SWEET_SOUL_BROTHER, 56249550),
};
static const MixSong k_mix_02b[] = {
    S(OLDIES_BUT_HAPPIES, 418950), S(BIRTHDAY_CAKE, 8467200),
    S(LIKE_IT_LIKE_THIS_LIKE_THAT, 15699600), S(SWEET_SOUL_BROTHER, 23328900),
    S(STATEMENT_OF_INTENT, 32170950), S(ME_LIKEY_THE_POOM_POOM, 39535650), S(AISLE_10, 47892600),
    S(ROCKIN_THE_MIC, 57087450),
};
static const MixSong k_mix_03a[] = {
    S(SNEAKMAN, 352800), S(TEKNOPATHETIC, 9966600), S(SHAPE_DA_FUTURE, 19293750),
    S(LIKE_IT_LIKE_THIS_LIKE_THAT, 29502900), S(LET_MOM_SLEEP, 36779400), S(BABY_T, 45180450),
};
static const MixSong k_mix_05a[] = {
    S(BABY_T, 352800), S(IM_NOT_A_MODEL, 4564350), S(THATS_ENOUGH, 11620350),
    S(LIKE_IT_LIKE_THIS_LIKE_THAT, 20947500), S(SWEET_SOUL_BROTHER, 28532700),
    S(BIRTHDAY_CAKE, 36360450), S(LATCH_BROTHER_BOUNCE, 43328250),
    S(OLDIES_BUT_HAPPIES, 51839550),
};
static const MixSong k_mix_05b[] = {
    S(SWEET_SOUL_BROTHER, 441000), S(BIRTHDAY_CAKE, 8224650), S(LATCH_BROTHER_BOUNCE, 15214500),
    S(OLDIES_BUT_HAPPIES, 23703750), S(BABY_T, 32170950), S(IM_NOT_A_MODEL, 36382500),
    S(THATS_ENOUGH, 43482600), S(LIKE_IT_LIKE_THIS_LIKE_THAT, 52765650),
};
static const MixSong k_mix_06a[] = {
    S(COUNT_LATCHULA, 352800), S(BOKFRESH, 11091150), S(ME_LIKEY_THE_POOM_POOM, 20043450),
    S(COUNT_LATCHULA, 28841400),
};
static const MixSong k_mix_06b[] = {
    S(COUNT_LATCHULA, 0), S(BOKFRESH, 10760400), S(ME_LIKEY_THE_POOM_POOM, 19778850),
    S(COUNT_LATCHULA, 28532700),
};
static const MixSong k_mix_07a[] = {
    S(TEKNOPATHETIC, 793800), S(WHAT_ABOUT_THE_FUTURE, 10120950),
    S(FLY_LIKE_A_BUTTERFLY, 21013650), S(SNEAKMAN, 29767500), S(SHAPE_DA_FUTURE, 40417650),
    S(ROCK_IT_ON, 48201300),
};
static const MixSong k_mix_07b[] = {
    S(SNEAKMAN, 352800), S(SHAPE_DA_FUTURE, 10980900), S(ROCK_IT_ON, 18786600),
    S(TEKNOPATHETIC, 36316350), S(WHAT_ABOUT_THE_FUTURE, 45643500),
    S(FLY_LIKE_A_BUTTERFLY, 56448000),
};
static const MixSong k_mix_08a[] = {
    S(THE_CONCEPT_OF_LOVE, 507150), S(IM_NOT_A_MODEL, 9569700), S(KOTO_STOMP, 16272900),
    S(THATS_ENOUGH, 21829500), S(SHAPE_DA_FUTURE, 32126850), S(BOKFRESH, 42247800),
    S(COUNT_LATCHULA, 51222150),
};
static const MixSong k_mix_08b[] = {
    S(SHAPE_DA_FUTURE, 463050), S(BOKFRESH, 10584000), S(COUNT_LATCHULA, 19536300),
    S(THE_CONCEPT_OF_LOVE, 30539250), S(IM_NOT_A_MODEL, 39623850), S(KOTO_STOMP, 46305000),
    S(THATS_ENOUGH, 51883650),
};
static const MixSong k_mix_09a[] = {
    S(FUNKY_DEALER, 352800), S(TEKNOPATHETIC, 8820000), S(SNEAKMAN, 18058950),
    S(FLY_LIKE_A_BUTTERFLY, 27297900), S(I_LOVE_LOVE_YOU, 36360450), S(THE_SCRAPPY, 45511200),
    S(STATEMENT_OF_INTENT, 51574950), S(THE_CONCEPT_OF_LOVE, 58741200),
};
static const MixSong k_mix_09b[] = {
    S(I_LOVE_LOVE_YOU, 815850), S(THE_SCRAPPY, 9944550), S(STATEMENT_OF_INTENT, 16030350),
    S(THE_CONCEPT_OF_LOVE, 23196600), S(FUNKY_DEALER, 33030900), S(TEKNOPATHETIC, 41476050),
    S(SNEAKMAN, 50737050), S(FLY_LIKE_A_BUTTERFLY, 59976000),
};
static const MixSong k_mix_03b[] = {
    S(LIKE_IT_LIKE_THIS_LIKE_THAT, 396900), S(LET_MOM_SLEEP, 7673400), S(BABY_T, 16074450),
    S(SNEAKMAN, 20638800), S(TEKNOPATHETIC, 30252600), S(SHAPE_DA_FUTURE, 39579750),
};
static const Mix k_mixes[] = {
    MIX(MIX_04, 657824, 52394528, k_mix_04),
    MIX(MIX_01A, 326784, 64461361, k_mix_01a),
    MIX(MIX_01B, 326816, 64461929, k_mix_01b),
    MIX(MIX_02A, 326496, 64992462, k_mix_02a),
    MIX(MIX_02B, 326496, 64992462, k_mix_02b),
    MIX(MIX_03A, 326304, 49719652, k_mix_03a),
    MIX(MIX_05A, 326304, 60262567, k_mix_05a),
    MIX(MIX_05B, 326304, 60262567, k_mix_05b),
    MIX(MIX_06A, 651232, 29242507, k_mix_06a),
    MIX(MIX_06B, 335072, 28926347, k_mix_06b),
    MIX(MIX_07A, 326560, 65275913, k_mix_07a),
    MIX(MIX_07B, 326304, 65275657, k_mix_07b),
    MIX(MIX_08A, 326304, 62034842, k_mix_08a),
    MIX(MIX_08B, 326304, 62034842, k_mix_08b),
    MIX(MIX_09A, 326496, 68560882, k_mix_09a),
    MIX(MIX_09B, 326784, 68561170, k_mix_09b),
    MIX(MIX_03B, 326304, 49719652, k_mix_03b),
};
#undef S
#undef MIX

static const Mix *find_mix(int id)
{
    unsigned i;
    for (i = 0; i < sizeof k_mixes / sizeof k_mixes[0]; i++)
        if (k_mixes[i].id == id) return &k_mixes[i];
    return NULL;
}

int music_info_is_mix(int id)
{
    return find_mix(id) != NULL;
}

/* A music id the sound manager can hold: a song, a mix, or one of those
 * that show nothing. */
static int known_id(int id)
{
    switch (id) {
    case MUSIC_TITLE: case MUSIC_CLEAR: case MUSIC_GAME_OVER: case MUSIC_DJ_DEMO:
    case MUSIC_DJ_CLEAR: case MUSIC_ENDING_GRACE: case MUSIC_ENDING_SHORT:
    case MUSIC_ENDING_LONG:
        return 1;
    default:
        return (id >= MUSIC_FIRST_SONG && id < MUSIC_FIRST_SONG + MUSIC_SONGS)
            || music_info_is_mix(id);
    }
}

static uint32_t guest32(const uint8_t *mem, uint32_t va)
{
    uint32_t v;
    memcpy(&v, mem + va, sizeof v);
    return v;
}

/* Whether size bytes at va are guest memory, below top and above the
 * first page the game's image starts at. */
static int in_guest(uint32_t va, uint32_t size, uint32_t top)
{
    return va >= 0x10000u && size <= top && va <= top - size;
}

int music_info_now(const uint8_t *mem, uint32_t top, MusicNow *now)
{
    uint32_t objects, sound, current, player;
    int status;
    now->id = MUSIC_NONE;
    now->player = 0;
    if (!in_guest(MUSIC_OBJECTS, 4, top)) return 0;
    objects = guest32(mem, MUSIC_OBJECTS);
    if (!RECOMP_CHECK(in_guest(objects + MUSIC_OBJECT_TABLE, 4 * (MUSIC_SOUND_SLOT + 1), top),
                      "the object manager at %08X", objects))
        return 0;
    sound = guest32(mem, objects + MUSIC_OBJECT_TABLE + 4 * MUSIC_SOUND_SLOT);
    if (!RECOMP_CHECK(in_guest(sound, MUSIC_SOUND_SIZE, top), "the sound manager at %08X", sound))
        return 0;
    now->id = (int)guest32(mem, sound + MUSIC_ID);
    if (now->id == MUSIC_NONE) return 0;
    if (!RECOMP_CHECK(known_id(now->id), "music id %X, none the game's file table has", now->id))
        return 0;
    current = guest32(mem, sound + MUSIC_CURRENT);
    if (!RECOMP_CHECK(current <= 1, "the music player heard is %u, not 0 or 1", current))
        return 0;
    player = guest32(mem, sound + MUSIC_PLAYERS + 4 * current);
    if (!RECOMP_CHECK(in_guest(player, ADXT_SIZE, top), "music player %u at %08X", current, player))
        return 0;
    now->player = player;
    /* Before the player is playing (its first frames), or once it has
     * ended, nothing is shown. */
    status = (int)(int8_t)mem[player + ADXT_STATUS];
    if (status != ADXT_PLAYING && status != ADXT_DECODED) return 0;
    return (now->id >= MUSIC_FIRST_SONG && now->id < MUSIC_FIRST_SONG + MUSIC_SONGS)
        || music_info_is_mix(now->id);
}

int music_info_song(int id, int64_t ncount, int64_t tscale)
{
    const Mix *mix;
    int64_t p;
    int looped = 0, k, song = -1;
    if (id >= MUSIC_FIRST_SONG && id < MUSIC_FIRST_SONG + MUSIC_SONGS) return id - MUSIC_FIRST_SONG;
    mix = find_mix(id);
    if (!mix) return -1;
    if (!RECOMP_CHECK(tscale > 0 && ncount >= 0, "mix %X's play time %lld / %lld", id,
                      (long long)ncount, (long long)tscale))
        return -1;
    p = ncount * MUSIC_RATE / tscale;
    if (p >= (int64_t)mix->loop_end) {
        looped = 1;
        p = mix->loop_start + (p - mix->loop_start) % (int64_t)(mix->loop_end - mix->loop_start);
    }
    for (k = 0; k < mix->n && (int64_t)mix->songs[k].at <= p; k++) song = mix->songs[k].song;
    /* Before its first song: the mix's opening the first time, or, once it
     * has looped, still the song its end played. */
    if (song < 0 && looped) song = mix->songs[mix->n - 1].song;
    return song < 0 ? -1 : song - MUSIC_FIRST_SONG;
}

static void log_time(char *buf, size_t n, int64_t ncount, int64_t tscale)
{
    double s = tscale > 0 ? (double)ncount / (double)tscale : 0.0;
    snprintf(buf, n, "%d:%04.1f", (int)(s / 60.0), s - 60.0 * (int)(s / 60.0));
}

int music_info_pick(int menu, int id, int64_t ncount, int64_t tscale, double now)
{
    static const char *const names[MUSIC_MENUS] = { "the street pause menu", "the other pause menu" };
    static unsigned char told[MUSIC_MENUS];
    static int shown = -2, shown_id = -2;
    int song = music_info_song(id, ncount, tscale), mix = music_info_is_mix(id);
    char at[32] = "";
    if (mix) log_time(at, sizeof at, ncount, tscale);
    if (menu >= 0 && menu < MUSIC_MENUS && !told[menu]) {
        told[menu] = 1;
        if (mix)
            fprintf(stderr, "[MUSIC] t=%.1f %s shows the music: mix %X at %s (%lld / %lld)\n",
                    now, names[menu], id, at, (long long)ncount, (long long)tscale);
        else
            fprintf(stderr, "[MUSIC] t=%.1f %s shows the music: song id %X\n", now, names[menu], id);
    }
    if (song != shown || id != shown_id) {
        shown = song;
        shown_id = id;
        if (song < 0)
            fprintf(stderr, "[MUSIC] t=%.1f showing nothing: mix %X at %s (%lld / %lld), before "
                    "its first song\n", now, id, at, (long long)ncount, (long long)tscale);
        else if (mix)
            fprintf(stderr, "[MUSIC] t=%.1f showing No. %d: mix %X at %s (%lld / %lld)\n", now,
                    song + 1, id, at, (long long)ncount, (long long)tscale);
        else
            fprintf(stderr, "[MUSIC] t=%.1f showing No. %d: song id %X\n", now, song + 1, id);
    }
    return song;
}

/* How many lines a game string at va takes: one, and one more for each
 * "$n" in it; 0 when it isn't a string within top. */
static int text_lines(const uint8_t *mem, uint32_t top, uint32_t va)
{
    int lines = 1, i;
    if (!in_guest(va, 1, top)) return 0;
    for (i = 0; i < MUSIC_INFO_TEXT && va + (uint32_t)i < top; i++) {
        if (!mem[va + i]) return lines;
        if (mem[va + i] == '$' && va + (uint32_t)i + 1 < top && mem[va + i + 1] == 'n') lines++;
    }
    return 0;
}

int music_info_lines(const uint8_t *mem, uint32_t top, int song, MusicLine line[MUSIC_INFO_LINES],
                     char *number, size_t n)
{
    uint32_t title, artist;
    int title_lines, artist_lines;
    float scale = MUSIC_INFO_SCALE, height, room = MUSIC_INFO_BOTTOM - MUSIC_INFO_TOP, y;
    if (song < 0 || song >= MUSIC_SONGS || !in_guest(MUSIC_ARTISTS, 4 * MUSIC_SONGS, top))
        return 0;
    title = guest32(mem, MUSIC_TITLES + 4 * (uint32_t)song);
    artist = guest32(mem, MUSIC_ARTISTS + 4 * (uint32_t)song);
    title_lines = text_lines(mem, top, title);
    artist_lines = text_lines(mem, top, artist);
    if (!RECOMP_CHECK(title_lines > 0 && artist_lines > 0, "song %d's title at %08X or artist "
                      "at %08X isn't a string", song + 1, title, artist))
        return 0;
    snprintf(number, n, "No. %d", song + 1);
    /* All of it at one scale, smaller if it wouldn't fit. */
    height = (float)(1 + title_lines + artist_lines) * MUSIC_INFO_ADVANCE * scale
           + 2.0f * MUSIC_INFO_GAP;
    if (height > room)
        scale *= (room - 2.0f * MUSIC_INFO_GAP) / (height - 2.0f * MUSIC_INFO_GAP);
    y = MUSIC_INFO_TOP;
    line[0] = (MusicLine){ MUSIC_INFO_X, y, scale, 0 };
    y += MUSIC_INFO_ADVANCE * scale + MUSIC_INFO_GAP;
    line[1] = (MusicLine){ MUSIC_INFO_X, y, scale, title };
    y += (float)title_lines * MUSIC_INFO_ADVANCE * scale + MUSIC_INFO_GAP;
    line[2] = (MusicLine){ MUSIC_INFO_X, y, scale, artist };
    return MUSIC_INFO_LINES;
}
