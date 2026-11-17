/* Developer > Missions: the story's missions, to restart the game at one
 * (missions.h).
 *
 * The rows are made once, as the engine starts, once the game's program is in
 * guest memory and the path layer is up (port/main.c, step 4), from the
 * game's own files, so nothing of the game's is written into the repository:
 * no list of its missions, its levels or its characters.
 *
 * - The missions are the story's mission files, D:\Media\Mission\mssnCCNN.bin
 *   for chapters 1 to 9, listed and read through the kernel's file layer.
 *   Each one's first word is its set, two 16-bit numbers (0x00010002 is
 *   stg12); the word at +0x20 its kind, which the game reads at 0x51F14 to
 *   pick the mission's rules; and its cast, the table at +0x34, names a
 *   character by number in a record of type 2.
 * - The names are default.xbe's, read from guest memory: the levels' at
 *   0x21B988 (14 pointers, in the order of k_named_sets, then the same in
 *   other languages), and Sega's short names for the characters at 0x1EFEF0
 *   (24 pointers: 1 Corn, 2 Gum, 6 Yoyo ...), but Garam's, which the
 *   program spells Galam and is shown as the game's own text spells it. The
 *   characters' order is the game's too: its table of them at 0x21C450, a
 *   number each, which the garage's unlock reads (0x11AB30).
 *
 * Inside Missions (port/developer.c's submenu right below Performance):
 * Character, the skater a mission starts with -- any of the game's 24, Yoyo
 * to begin with, as New Game has him, kept in developer.cfg as
 * mission_skater= -- then, between two lines, Load game, which restarts the
 * game at the title's Load Game with the mission runs' saves; then each level
 * in story order (the order of its lowest mission number), as "99th Street
 * (stg30)", a choice of its missions in numerical order, each with its kind
 * and, for a race or a battle, who it is against: "0252 (flag game,
 * Rapid99)". Two names are ours, as the game's table has none for them: the
 * garage's, and stg40's, Gouji's Tower, the story's final fight (0890), a
 * level of its own as the player knows it. The other sets the table doesn't
 * name (the chapters' openings, and chapter 8's others) are gathered in
 * Other sets, at the foot, each with its set: "0296 (scene, stg09)". A mission start-states.txt marks as off the story's way
 * (missions_off_story) says so: "0497 (scene, not on the story's way)".
 *
 * Picking a mission asks first, then sets JSRF_START_MISSION and
 * JSRF_START_SKATER and restarts, as File > Restart does; missions.c starts
 * it. Load game asks first too, then sets JSRF_START_LOAD the same way. The
 * kinds' names are ours, as the game has none: what each rests on is in
 * mission-select/FINDINGS-3.md, section 11, in the project's shared folder. */
#define _POSIX_C_SOURCE 200112L    /* setenv, unsetenv */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel.h"
#include "missions.h"
#if defined(XBOXRECOMP_MENU)
#  include "menu.h"
#endif

#if defined(XBOXRECOMP_MENU) && defined(NV2A_GL_USE_SDL)

/* nv2a_gl.c: a file beside the settings file, and the restart. */
extern int  nv_window_file_get(const char *file, const char *key, int def);
extern void nv_window_file_set(const char *file, const char *key, int val);
extern void nv_window_restart(void);
#define MSN_CFG             "developer.cfg" /* the Developer menu's own file (port/developer.c) */
#define MSN_CFG_SKATER      "mission_skater"

/* ---- the game's mission files ---- */
#define MSN_FOLDER          "D:\\Media\\Mission"
#define MSN_PATTERN         "mssn*.bin"     /* mssnCCNN.bin, the game's own name (sub_00030490) */
#define MSN_NAME_LENGTH     12u             /* "mssnCCNN.bin" */
#define MSN_SHARE_READ      0x00000001u     /* FILE_SHARE_READ */
#define MF_SET              0x00u   /* two 16-bit numbers: the set's second digit, then its first */
#define MF_KIND             0x20u   /* the mission's kind */
#define MF_CAST             0x34u   /* the cast table's offset in the file ... */
#define MF_CAST_COUNT       0x38u   /* ... and its records */
#define MF_HEADER           0x3Cu
#define CAST_RECORD         0x20u
#define CAST_TYPE           0x00u
#define CAST_WHO            0x04u
#define CAST_CHARACTER      2u      /* a record naming a character by number */
#define CAST_MOST           256u    /* records read at most */

/* ---- default.xbe's names, in guest memory ---- */
#define XBE_LEVEL_NAMES     0x0021B988u /* a pointer to each level's name, in k_named_sets' order */
#define XBE_CHARACTER_NAMES 0x001EFEF0u /* a pointer to each character's short name, by number */
#define XBE_CHARACTER_ORDER 0x0021C450u /* the characters in the game's order, a number each */
#define LEVEL_NAMES         14
#define CHARACTER_NAMES     MISSIONS_CHARACTERS
/* The program's short name for character 8, the Rokkaku Group's Garam, is
 * spelled Galam; the game's own text, and the player, call him Garam. Only
 * that spelling is changed, so a program that names him otherwise is left
 * as it is. */
#define CHARACTER_GARAM     8
#define GARAM_PROGRAM       "Galam"
#define GARAM_SHOWN         "Garam"
#define NAME_LONGEST        48          /* a name's most characters */
#define GUEST_LOWEST        0x10000u    /* below it, no name */

/* The sets the game's table of level names names, in its order: the 14 the
 * story's missions use but the garage's and the scenes' own. That the order
 * is the levels' is reasoned (FINDINGS-3.md, section 11): 0110, the story's
 * first level, is stg12, Dogenzaka Hill, and 0510 to 0515, the Death Ball
 * chapter, stg32, the stadium. */
static const unsigned char k_named_sets[LEVEL_NAMES] = {
    10, 11, 12, 13, 20, 21, 22, 23, 24, 30, 31, 32, 33, 34
};
#define SET_OTHER           (-1)    /* a level row's set for Other sets */
/* The sets the game's table doesn't name that are a level of their own,
 * with our names: the garage, and stg40, where the story's final fight is
 * (0890), named as the player calls it. */
static const struct {
    unsigned char set;
    const char *name;
} k_own_sets[] = {
    { 0, "Garage" },
    { 40, "Gouji's Tower" },
};
#define OWN_SETS            ((int)(sizeof k_own_sets / sizeof k_own_sets[0]))

/* The kinds, by the file's number; kinds 3 to 8 are against someone, whom
 * the cast names. Multiplayer's 9 to 12 aren't in the story. */
static const char *const k_kinds[] = {
    "scene", "free play", "fight", "flag game", "race", "Death Ball", "tag battle", "challenge",
    "City Rush",
};
#define KINDS               ((uint32_t)(sizeof k_kinds / sizeof k_kinds[0]))
#define KIND_AGAINST_FIRST  3u
#define KIND_AGAINST_LAST   8u

/* The story's missions: chapters 1 to 9, the ones missions.c starts. */
#define MSN_FIRST           100
#define MSN_LAST            999
#define MSN_MAX             (MSN_LAST - MSN_FIRST + 1)
#define MSN_LEVELS          (LEVEL_NAMES + OWN_SETS + 1)   /* the game's named, ours, and Other sets */
#define MSN_AGAINST         4                   /* names in a caption at most */
#define MSN_CAPTION         96
#define MSN_LABEL           (NAME_LONGEST + 16)


typedef struct {
    int mission;                        /* CCNN */
    unsigned set;                       /* stg30 is 30 */
    uint32_t kind;
    int nagainst;
    unsigned char against[MSN_AGAINST];  /* characters, by number */
} MmFile;

typedef struct {
    int set;                            /* SET_OTHER for Other sets */
    int first;                          /* its lowest mission: the story's order */
    int at, n;                          /* its missions: s_mission[at] on */
    char label[MSN_LABEL];
} MmLevel;

static MmFile  s_files[MSN_MAX];
static int     s_nfiles;
static MmLevel s_levels[MSN_LEVELS];
static int     s_nlevels;
static char    s_caption[MSN_MAX][MSN_CAPTION];
static const char *s_choice[MSN_MAX];    /* the levels' choices, a level after another */
static int     s_mission[MSN_MAX];       /* ... and their missions */
static char    s_level_name[LEVEL_NAMES][NAME_LONGEST + 1];
static char    s_character[CHARACTER_NAMES][NAME_LONGEST + 1];
static int     s_order[CHARACTER_NAMES];         /* Character's choices: the characters, in the game's order */
static const char *s_skater_choice[CHARACTER_NAMES];
static int     s_skater = MISSIONS_SKATER_YOYO;

static void set_env(const char *name, const char *val)
{
#if defined(_WIN32)
    _putenv_s(name, val);               /* "" takes it out */
#else
    if (*val) setenv(name, val, 1);
    else      unsetenv(name);
#endif
}

/* Entry i of a table of pointers to names in guest memory, into `out`: 0
 * when it isn't a name (a pointer outside memory, or no printable text
 * ending in time). */
static int guest_name(const uint8_t *mem, uint32_t size, uint32_t table, int i, char *out)
{
    uint32_t p, k;
    if (size < 4u * (uint32_t)(i + 1) || table > size - 4u * (uint32_t)(i + 1)) return 0;
    memcpy(&p, mem + table + 4u * (uint32_t)i, 4);
    if (p < GUEST_LOWEST || p >= size) return 0;
    for (k = 0; k <= NAME_LONGEST && p + k < size; k++) {
        unsigned char c = mem[p + k];
        out[k] = (char)c;
        if (!c) return k > 0;
        if (c < 0x20 || c > 0x7E) return 0;
    }
    return 0;
}

/* A table's names, each read or "#<number>" in its place. Returns how many
 * were read. */
static int guest_names(const uint8_t *mem, uint32_t size, uint32_t table, int count,
                       char (*out)[NAME_LONGEST + 1])
{
    int i, read = 0;
    for (i = 0; i < count; i++) {
        if (guest_name(mem, size, table, i, out[i])) read++;
        else snprintf(out[i], NAME_LONGEST + 1, "#%d", i);
    }
    return read;
}

/* The game's order of its characters into s_order: 1 when its table holds
 * each of them once; otherwise their numbers' order, and 0. */
static int guest_order(const uint8_t *mem, uint32_t size)
{
    unsigned seen = 0;
    uint32_t v;
    int i;
    for (i = 0; i < CHARACTER_NAMES; i++) {
        if (size < 4u * CHARACTER_NAMES || XBE_CHARACTER_ORDER > size - 4u * CHARACTER_NAMES) break;
        memcpy(&v, mem + XBE_CHARACTER_ORDER + 4u * (uint32_t)i, 4);
        if (v >= CHARACTER_NAMES || (seen >> v & 1u)) break;
        seen |= 1u << v;
        s_order[i] = (int)v;
    }
    if (i == CHARACTER_NAMES) return 1;
    for (i = 0; i < CHARACTER_NAMES; i++) s_order[i] = i;
    return 0;
}

/* ---- the mission files, through the kernel's file layer ---- */

static int file_open(const char *xbox_path, int folder, HANDLE *h)
{
    char path[64];
    XBOX_ANSI_STRING name;
    XBOX_OBJECT_ATTRIBUTES oa;
    XBOX_IO_STATUS_BLOCK ios;
    snprintf(path, sizeof path, "%s", xbox_path);
    name.Buffer = path;
    name.Length = (USHORT)strlen(path);
    name.MaximumLength = (USHORT)(name.Length + 1);
    oa.RootDirectory = NULL;
    oa.ObjectName = &name;
    oa.Attributes = 0;
    memset(&ios, 0, sizeof ios);
    return NT_SUCCESS(xbox_NtCreateFile(h, XBOX_GENERIC_READ | XBOX_SYNCHRONIZE, &oa, &ios, NULL, 0,
                                        MSN_SHARE_READ, XBOX_FILE_OPEN,
                                        (folder ? XBOX_FILE_DIRECTORY_FILE : XBOX_FILE_NON_DIRECTORY_FILE)
                                        | XBOX_FILE_SYNCHRONOUS_IO_NONALERT));
}

/* Up to n bytes at `at`: how many were read. */
static uint32_t file_read(HANDLE h, uint32_t at, void *buf, uint32_t n)
{
    XBOX_IO_STATUS_BLOCK ios;
    LARGE_INTEGER off;
    off.QuadPart = at;
    memset(&ios, 0, sizeof ios);
    if (!NT_SUCCESS(xbox_NtReadFile(h, NULL, NULL, NULL, &ios, buf, n, &off))) return 0;
    return ios.Information < n ? (uint32_t)ios.Information : n;
}

static uint32_t word(const uint8_t *b, uint32_t at)
{
    uint32_t v;
    memcpy(&v, b + at, 4);
    return v;
}

/* Mission CCNN's set, kind and who it is against, into f: 0 when its file
 * can't be read as a mission file. */
static int read_mission_file(int mission, MmFile *f)
{
    static uint8_t cast[CAST_MOST * CAST_RECORD];
    uint8_t head[MF_HEADER];
    char path[64];
    HANDLE h;
    uint32_t i, n, got;
    int ok;
    snprintf(path, sizeof path, MSN_FOLDER "\\mssn%04d.bin", mission);
    if (!file_open(path, 0, &h)) return 0;
    memset(f, 0, sizeof *f);
    f->mission = mission;
    ok = file_read(h, 0, head, MF_HEADER) == MF_HEADER
      && (word(head, MF_SET) & 0xFFFFu) < 10u && (word(head, MF_SET) >> 16) < 10u;
    if (ok) {
        f->set = (word(head, MF_SET) >> 16) * 10u + (word(head, MF_SET) & 0xFFFFu);
        f->kind = word(head, MF_KIND);
        n = word(head, MF_CAST_COUNT);
        if (n > CAST_MOST) n = CAST_MOST;
        got = n ? file_read(h, word(head, MF_CAST), cast, n * CAST_RECORD) / CAST_RECORD : 0;
        for (i = 0; i < got; i++) {
            uint32_t who = word(cast, i * CAST_RECORD + CAST_WHO);
            int k;
            if (word(cast, i * CAST_RECORD + CAST_TYPE) != CAST_CHARACTER || who >= CHARACTER_NAMES) continue;
            for (k = 0; k < f->nagainst && f->against[k] != who; k++) { }
            if (k == f->nagainst && f->nagainst < MSN_AGAINST) f->against[f->nagainst++] = (unsigned char)who;
        }
    }
    xbox_NtClose(h);
    return ok;
}

/* CCNN from a mission file's name, "mssnCCNN.bin" in any case; -1 for any
 * other name. */
static int mission_of(const char *name, ULONG len)
{
    int i, m = 0;
    if (len != MSN_NAME_LENGTH) return -1;
    for (i = 0; i < 4; i++)
        if ((name[i] | 0x20) != "mssn"[i] || (name[8 + i] | 0x20) != ".bin"[i]) return -1;
    for (i = 4; i < 8; i++) {
        if (name[i] < '0' || name[i] > '9') return -1;
        m = m * 10 + name[i] - '0';
    }
    return m;
}

/* The story's mission files into s_files: how many. */
static int list_missions(int *unreadable)
{
    static unsigned char seen[MSN_MAX];
    union {
        XBOX_FILE_DIRECTORY_INFORMATION info;
        char room[sizeof(XBOX_FILE_DIRECTORY_INFORMATION) + 260];
    } e;
    char pattern[] = MSN_PATTERN;
    XBOX_ANSI_STRING pat;
    XBOX_IO_STATUS_BLOCK ios;
    HANDLE folder;
    int first = 1, m;
    *unreadable = 0;
    if (!file_open(MSN_FOLDER, 1, &folder)) return 0;
    pat.Buffer = pattern;
    pat.Length = (USHORT)strlen(pattern);
    pat.MaximumLength = (USHORT)(pat.Length + 1);
    for (;; first = 0) {
        memset(&ios, 0, sizeof ios);
        if (!NT_SUCCESS(xbox_NtQueryDirectoryFile(folder, NULL, NULL, NULL, &ios, &e, sizeof e,
                                                  first ? &pat : NULL, (BOOLEAN)first)))
            break;
        m = mission_of(e.info.FileName, e.info.FileNameLength);
        if (m < MSN_FIRST || m > MSN_LAST || seen[m - MSN_FIRST]) continue;
        seen[m - MSN_FIRST] = 1;
        if (read_mission_file(m, &s_files[s_nfiles])) s_nfiles++;
        else ++*unreadable;
    }
    xbox_NtClose(folder);
    return s_nfiles;
}

/* ---- the rows ---- */

static int named_set(unsigned set)
{
    int i;
    for (i = 0; i < LEVEL_NAMES; i++)
        if (k_named_sets[i] == set) return i;
    return -1;
}

static const char *own_set(unsigned set)
{
    int i;
    for (i = 0; i < OWN_SETS; i++)
        if (k_own_sets[i].set == set) return k_own_sets[i].name;
    return NULL;
}

/* A mission's level row: its set, or SET_OTHER. */
static int level_set(const MmFile *f)
{
    return named_set(f->set) >= 0 || own_set(f->set) ? (int)f->set : SET_OTHER;
}

static int by_mission(const void *a, const void *b)
{
    return ((const MmFile *)a)->mission - ((const MmFile *)b)->mission;
}

/* The story's order: by the lowest mission, Other sets last. */
static int by_story(const void *a, const void *b)
{
    const MmLevel *x = a, *y = b;
    if ((x->set == SET_OTHER) != (y->set == SET_OTHER)) return x->set == SET_OTHER ? 1 : -1;
    return x->first - y->first;
}

static void append(char *out, size_t n, size_t *at, const char *fmt, ...)
{
    va_list ap;
    int w;
    if (*at + 1 >= n) return;
    va_start(ap, fmt);
    w = vsnprintf(out + *at, n - *at, fmt, ap);
    va_end(ap);
    if (w > 0) *at += (size_t)w < n - *at ? (size_t)w : n - *at - 1;
}

/* "0252 (flag game, Rapid99)": the number, the kind, who it is against,
 * the set where its level has no name, and whether it is off the story's
 * way. */
static void caption(const MmFile *f, int other, char *out, size_t n)
{
    size_t at = 0;
    int i;
    append(out, n, &at, "%04d (", f->mission);
    if (f->kind < KINDS) append(out, n, &at, "%s", k_kinds[f->kind]);
    else                 append(out, n, &at, "kind %u", (unsigned)f->kind);
    if (f->kind >= KIND_AGAINST_FIRST && f->kind <= KIND_AGAINST_LAST)
        for (i = 0; i < f->nagainst; i++) append(out, n, &at, ", %s", s_character[f->against[i]]);
    if (other) append(out, n, &at, ", stg%02u", f->set);
    if (missions_off_story(f->mission)) append(out, n, &at, ", not on the story's way");
    append(out, n, &at, ")");
}

/* The levels, in story order, each with its missions in numerical order and
 * their captions. */
static void make_levels(int names)
{
    int i, l, k = 0;
    qsort(s_files, (size_t)s_nfiles, sizeof s_files[0], by_mission);
    for (i = 0; i < s_nfiles; i++) {
        const int named = named_set(s_files[i].set), set = level_set(&s_files[i]);
        for (l = 0; l < s_nlevels && s_levels[l].set != set; l++) { }
        if (l < s_nlevels) continue;
        s_levels[l].set = set;
        s_levels[l].first = s_files[i].mission;
        if (set == SET_OTHER)
            snprintf(s_levels[l].label, MSN_LABEL, "Other sets");
        else if (named < 0)
            snprintf(s_levels[l].label, MSN_LABEL, "%s (stg%02d)", own_set((unsigned)set), set);
        else if (names)
            snprintf(s_levels[l].label, MSN_LABEL, "%s (stg%02d)", s_level_name[named], set);
        else
            snprintf(s_levels[l].label, MSN_LABEL, "stg%02d", set);
        s_nlevels++;
    }
    qsort(s_levels, (size_t)s_nlevels, sizeof s_levels[0], by_story);
    for (l = 0; l < s_nlevels; l++) {
        s_levels[l].at = k;
        for (i = 0; i < s_nfiles; i++) {
            const int set = level_set(&s_files[i]);
            if (set != s_levels[l].set) continue;
            caption(&s_files[i], set == SET_OTHER, s_caption[k], MSN_CAPTION);
            s_choice[k] = s_caption[k];
            s_mission[k++] = s_files[i].mission;
        }
        s_levels[l].n = k - s_levels[l].at;
    }
}

static int skater_now(void)
{
    int i;
    for (i = 0; i < CHARACTER_NAMES; i++)
        if (s_order[i] == s_skater) return i;
    return 0;
}

static void skater_pick(int i)
{
    if (i < 0 || i >= CHARACTER_NAMES) return;
    s_skater = s_order[i];
    nv_window_file_set(MSN_CFG, MSN_CFG_SKATER, s_skater);
    fprintf(stderr, "[MISSION] Developer > Missions > Character: %s (skater %d), from the next mission"
            " picked\n", s_character[s_skater], s_skater);
}

static int level_none(void) { return -1; }     /* a level's choices aren't ticked */

/* A mission picked, once confirmed: the start's variables set, and the game
 * restarted with them. */
static void level_pick(const void *data, int i)
{
    const MmLevel *lv = data;
    char mission[16], skater[16];
    if (i < 0 || i >= lv->n) return;
    snprintf(mission, sizeof mission, "%04d", s_mission[lv->at + i]);
    snprintf(skater, sizeof skater, "%d", s_skater);
    set_env("JSRF_START_MISSION", mission);
    set_env("JSRF_START_SKATER", skater);
    fprintf(stderr, "[MISSION] %s picked in Developer > Missions, skater %s: restarting\n", mission, skater);
    nv_window_restart();                /* does not return when it restarts */
    set_env("JSRF_START_MISSION", "");
    set_env("JSRF_START_SKATER", "");
    fprintf(stderr, "[MISSION] %s: the game couldn't restart; nothing started\n", mission);
}

/* Load game, once confirmed: the title's Load Game, with the mission runs'
 * saves. */
static void load_pick(void)
{
    set_env("JSRF_START_MISSION", "");
    set_env("JSRF_START_SKATER", "");
    set_env("JSRF_START_LOAD", "1");
    fprintf(stderr, "[MISSION] Load game picked in Developer > Missions: restarting\n");
    nv_window_restart();                /* does not return when it restarts */
    set_env("JSRF_START_LOAD", "");
    fprintf(stderr, "[MISSION] Load game: the game couldn't restart; nothing started\n");
}

static void separator(XBOX_MENU_ITEM *row)
{
    row->menu = XBOX_MENU_DEVELOPER;
    row->kind = XBOX_ITEM_SEPARATOR;
    row->group = XBOX_GROUP_MISSIONS;
}

#endif

void missions_menu_start(const uint8_t *mem, uint32_t size)
{
#if defined(XBOXRECOMP_MENU) && defined(NV2A_GL_USE_SDL)
    XBOX_MENU_ITEM rows[4 + MSN_LEVELS];
    int i, n = 0, names, characters, unreadable, off = 0, kept;
    names = guest_names(mem, size, XBE_LEVEL_NAMES, LEVEL_NAMES, s_level_name) == LEVEL_NAMES;
    characters = guest_names(mem, size, XBE_CHARACTER_NAMES, CHARACTER_NAMES, s_character);
    if (!strcmp(s_character[CHARACTER_GARAM], GARAM_PROGRAM))
        snprintf(s_character[CHARACTER_GARAM], NAME_LONGEST + 1, GARAM_SHOWN);
    if (!names)
        fprintf(stderr, "[MISSION] Developer > Missions: the game's level names aren't at 0x%08X;"
                " levels by their sets alone\n", XBE_LEVEL_NAMES);
    if (characters < CHARACTER_NAMES)
        fprintf(stderr, "[MISSION] Developer > Missions: %d of the game's %d character names at 0x%08X;"
                " the rest by number\n", characters, CHARACTER_NAMES, XBE_CHARACTER_NAMES);
    if (!guest_order(mem, size))
        fprintf(stderr, "[MISSION] Developer > Missions: the game's order of its characters isn't at 0x%08X;"
                " Character by number\n", XBE_CHARACTER_ORDER);
    if (!list_missions(&unreadable)) {
        fprintf(stderr, "[MISSION] Developer > Missions: no story mission files in %s%s; not shown\n",
                MSN_FOLDER, unreadable ? " that could be read" : "");
        return;
    }
    make_levels(names);
    for (i = 0; i < s_nfiles; i++) off += missions_off_story(s_files[i].mission);
    kept = nv_window_file_get(MSN_CFG, MSN_CFG_SKATER, MISSIONS_SKATER_YOYO);
    if (kept >= 0 && kept < CHARACTER_NAMES) s_skater = kept;
    else fprintf(stderr, "[MISSION] " MSN_CFG "'s " MSN_CFG_SKATER "=%d isn't one of the game's characters"
                 " (0 to %d); Yoyo instead\n", kept, CHARACTER_NAMES - 1);
    for (i = 0; i < CHARACTER_NAMES; i++) s_skater_choice[i] = s_character[s_order[i]];

    memset(rows, 0, sizeof rows);
    rows[n].menu = XBOX_MENU_DEVELOPER;
    rows[n].kind = XBOX_ITEM_CHOICE;
    rows[n].label = "Character";
    rows[n].group = XBOX_GROUP_MISSIONS;
    rows[n].state = skater_now;
    rows[n].pick = skater_pick;
    rows[n].choices = s_skater_choice;
    rows[n].nchoices = CHARACTER_NAMES;
    rows[n++].hint = "Who you start a mission as, unless the story picks for you.";
    separator(&rows[n++]);
    rows[n].menu = XBOX_MENU_DEVELOPER;
    rows[n].kind = XBOX_ITEM_ACTION;
    rows[n].label = "Load game";
    rows[n].group = XBOX_GROUP_MISSIONS;
    rows[n].run = load_pick;
    rows[n].confirm = "Restart at Load Game with the mission saves, losing unsaved progress?";
    rows[n++].hint = "Restarts the game at Load Game, with the saves from mission runs.";
    separator(&rows[n++]);
    for (i = 0; i < s_nlevels; i++, n++) {
        rows[n].menu = XBOX_MENU_DEVELOPER;
        rows[n].kind = XBOX_ITEM_CHOICE;
        rows[n].label = s_levels[i].label;
        rows[n].group = XBOX_GROUP_MISSIONS;
        rows[n].state = level_none;
        rows[n].pick_with = level_pick;
        rows[n].data = &s_levels[i];
        rows[n].choices = s_choice + s_levels[i].at;
        rows[n].nchoices = s_levels[i].n;
        rows[n].confirm = "Restart the game at this mission, losing unsaved progress?";
        rows[n].hint = s_levels[i].set == SET_OTHER
                     ? "Missions on sets the game has no name for, each with its set."
                     : "This level's missions in story order; one picked restarts the game.";
    }
    xbox_MenuAdd(rows, n);
    fprintf(stderr, "[MISSION] Developer > Missions: %d story missions from %s, in %d levels%s;"
            " %d off the story's way (" MISSIONS_LINES "); %d unreadable; Character %s\n",
            s_nfiles, MSN_FOLDER, s_nlevels - (s_levels[s_nlevels - 1].set == SET_OTHER),
            s_levels[s_nlevels - 1].set == SET_OTHER ? " and Other sets" : "", off, unreadable,
            s_character[s_skater]);
#else
    (void)mem;
    (void)size;
#endif
}
