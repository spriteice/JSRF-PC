/* Run with: python tests/missions/run.py
 *
 * Developer > Missions' rows (port/missions_menu.c), made from made-up
 * mission files served by a made-up kernel file layer, and made-up names in
 * made-up guest memory at the addresses the game has its tables: nothing of
 * the game's. xbox_MenuAdd and the restart are stood in for; the settings
 * file's code is the engine's own (run.py cuts it from nv2a_gl.c), with
 * developer.cfg and start-states.txt in the case's folder.
 *
 * Each row is a "row:" line, its kind, label, submenu and choices, with its
 * confirm; the argument picks the case: "menu" makes the rows, "pick" picks
 * Character's second choice then a mission, and the restart stands in by
 * printing the start's variables and ending the run, as the real one ends
 * it; "pick_fails" has the restart come back; "load" and "load_fails" do the
 * same with Load game; "no_folder" has no mission folder; "no_names" no
 * names or order of the characters in guest memory; "bad_order" an order
 * that names a character twice. A "check" line for each check. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel.h"
#include "menu.h"
#include "missions.h"

#define RAM          0x240000u   /* the made-up guest memory */
#define LEVEL_TABLE  0x21B988u   /* where the game has its tables */
#define CHAR_TABLE   0x1EFEF0u
#define ORDER_TABLE  0x21C450u
#define NAMES_AT     0x100000u   /* the made-up names */
#define FOLDER_HANDLE ((HANDLE)(uintptr_t)0x1000u)
#define MAX_FILES    32
#define MAX_ROWS     32

static _Alignas(16) uint8_t s_ram[RAM];
static const char *s_case;
static int s_failed;

static void check(const char *what, int ok)
{
    printf("check: %s %s\n", what, ok ? "ok" : "FAILED");
    s_failed |= !ok;
}

/* ---- made-up mission files ---- */

typedef struct { const char *name; uint8_t data[0x200]; uint32_t size; } File;
static File s_files[MAX_FILES];
static int s_nfiles, s_listed, s_open, s_opens, s_bad_open;

static void put32(uint8_t *d, uint32_t at, uint32_t v) { memcpy(d + at, &v, 4); }

/* A mission file: its set (stgXY: X, Y), its kind, and its cast, a type and
 * a number for each record. */
static void mission(const char *name, unsigned x, unsigned y, uint32_t kind, int n, const uint32_t *cast)
{
    File *f = &s_files[s_nfiles++];
    int i;
    f->name = name;
    put32(f->data, 0x00, (x << 16) | y);
    put32(f->data, 0x20, kind);
    put32(f->data, 0x34, 0x40);
    put32(f->data, 0x38, (uint32_t)n);
    for (i = 0; i < n; i++) {
        put32(f->data, 0x40 + 0x20u * (uint32_t)i, cast[2 * i]);
        put32(f->data, 0x44 + 0x20u * (uint32_t)i, cast[2 * i + 1]);
    }
    f->size = 0x40 + 0x20u * (uint32_t)n;
}

static int same_name(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if ((*a | 0x20) != (*b | 0x20)) return 0;
    return *a == *b;
}

NTSTATUS __stdcall xbox_NtCreateFile(PHANDLE h, ACCESS_MASK access, PXBOX_OBJECT_ATTRIBUTES oa,
                                     PXBOX_IO_STATUS_BLOCK ios, PLARGE_INTEGER alloc, ULONG attrs,
                                     ULONG share, ULONG disposition, ULONG options)
{
    char path[128];
    const char *folder = "D:\\Media\\Mission", *leaf;
    int i;
    (void)ios; (void)alloc; (void)attrs; (void)share;
    snprintf(path, sizeof path, "%.*s", (int)oa->ObjectName->Length, oa->ObjectName->Buffer);
    if (access != (XBOX_GENERIC_READ | XBOX_SYNCHRONIZE) || disposition != XBOX_FILE_OPEN) s_bad_open++;
    if (options & XBOX_FILE_DIRECTORY_FILE) {
        if (strcmp(path, folder) || !strcmp(s_case, "no_folder")) return STATUS_OBJECT_PATH_NOT_FOUND;
        *h = FOLDER_HANDLE;
        s_open++;
        return STATUS_SUCCESS;
    }
    if (strncmp(path, folder, strlen(folder)) || path[strlen(folder)] != '\\') return STATUS_OBJECT_PATH_NOT_FOUND;
    leaf = path + strlen(folder) + 1;
    for (i = 0; i < s_nfiles; i++)
        if (same_name(s_files[i].name, leaf)) {
            *h = (HANDLE)(uintptr_t)(i + 1);
            s_open++;
            s_opens++;
            return STATUS_SUCCESS;
        }
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

NTSTATUS __stdcall xbox_NtQueryDirectoryFile(HANDLE h, HANDLE event, PIO_APC_ROUTINE apc, PVOID context,
                                             PXBOX_IO_STATUS_BLOCK ios, PVOID info, ULONG length,
                                             PXBOX_ANSI_STRING name, BOOLEAN restart)
{
    PXBOX_FILE_DIRECTORY_INFORMATION e = info;
    size_t n;
    (void)event; (void)apc; (void)context;
    if (h != FOLDER_HANDLE) return STATUS_INVALID_HANDLE;
    if (restart) {
        check("the folder listed by the game's own names",
              name && name->Length == 9 && !memcmp(name->Buffer, "mssn*.bin", 9));
        s_listed = 0;
    }
    if (s_listed == s_nfiles) return STATUS_NO_MORE_FILES;
    n = strlen(s_files[s_listed].name);
    memset(e, 0, length);
    e->FileNameLength = (ULONG)n;
    memcpy(e->FileName, s_files[s_listed++].name, n);
    ios->Status = STATUS_SUCCESS;
    ios->Information = sizeof *e + n;
    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_NtReadFile(HANDLE h, HANDLE event, PIO_APC_ROUTINE apc, PVOID context,
                                   PXBOX_IO_STATUS_BLOCK ios, PVOID buf, ULONG length, PLARGE_INTEGER at)
{
    uintptr_t i = (uintptr_t)h - 1;
    uint32_t n = 0;
    (void)event; (void)apc; (void)context;
    if (i >= (uintptr_t)s_nfiles) return STATUS_INVALID_HANDLE;
    if ((uint64_t)at->QuadPart < s_files[i].size) {
        n = s_files[i].size - (uint32_t)at->QuadPart;
        if (n > length) n = length;
        memcpy(buf, s_files[i].data + at->QuadPart, n);
    }
    ios->Status = STATUS_SUCCESS;
    ios->Information = n;
    return STATUS_SUCCESS;
}

NTSTATUS __stdcall xbox_NtClose(HANDLE h)
{
    (void)h;
    s_open--;
    return STATUS_SUCCESS;
}

/* missions.c's path step, which this doesn't call. */
NTSTATUS __stdcall xbox_NtQueryFullAttributesFile(PXBOX_OBJECT_ATTRIBUTES oa,
                                                  PXBOX_FILE_NETWORK_OPEN_INFORMATION info)
{
    (void)oa; (void)info;
    return STATUS_SUCCESS;
}
void xbox_path_set_user_dir(const char *dir) { (void)dir; }

/* ---- the menu and the restart, stood in for ---- */

static XBOX_MENU_ITEM s_rows[MAX_ROWS];
static int s_nrows;

void xbox_MenuAdd(const XBOX_MENU_ITEM *items, int count)
{
    static const char *const kind[] = { "ACTION", "TOGGLE", "CHOICE", "INFO", "SEPARATOR", "NUMBER",
                                        "TOGGLES", "GROUP" };
    int i, k;
    for (i = 0; i < count && s_nrows < MAX_ROWS; i++) {
        const XBOX_MENU_ITEM *r = &items[i];
        const char *h = r->hint;
        s_rows[s_nrows++] = *r;
        printf("row: %s %s in %s", kind[r->kind], r->label ? r->label : "-", r->group ? r->group : "-");
        for (k = 0; k < r->nchoices; k++) printf("%s%s", k ? " | " : ": ", r->choices[k]);
        if (r->kind == XBOX_ITEM_CHOICE) printf(" [ticked %d]", r->state());
        if (r->confirm) printf("; asks \"%s\"", r->confirm);
        printf("\n");
        check("in Developer", r->menu == XBOX_MENU_DEVELOPER);
        if (r->kind != XBOX_ITEM_SEPARATOR)
            check("a hint, one sentence of 70 characters at most",
                  h && strlen(h) <= MENU_HINT_LONGEST && h[strlen(h) - 1] == '.');
        if (r->kind == XBOX_ITEM_CHOICE && r->confirm)
            check("a level's pick tells its row", r->pick_with && r->data && !r->pick);
        if (r->kind == XBOX_ITEM_ACTION)
            check("an action runs", r->run && !r->pick && !r->pick_with);
    }
}

void nv_window_restart(void)
{
    const char *m = getenv("JSRF_START_MISSION"), *k = getenv("JSRF_START_SKATER"),
               *l = getenv("JSRF_START_LOAD");
    printf("restart: JSRF_START_MISSION=%s JSRF_START_SKATER=%s JSRF_START_LOAD=%s\n", m ? m : "-",
           k ? k : "-", l ? l : "-");
    fflush(stdout);
    if (strcmp(s_case, "pick_fails") && strcmp(s_case, "load_fails"))
        exit(s_failed);                 /* a restart doesn't come back */
}

/* ---- made-up names in guest memory ---- */

static uint32_t s_text = NAMES_AT;

static void name_at(uint32_t table, int i, const char *name)
{
    size_t n = strlen(name) + 1;
    memcpy(s_ram + s_text, name, n);
    put32(s_ram, table + 4u * (uint32_t)i, s_text);
    s_text += (uint32_t)n;
}

static int row_named(const char *label)
{
    int i;
    for (i = 0; i < s_nrows; i++)
        if (s_rows[i].label && !strcmp(s_rows[i].label, label)) return i;
    return -1;
}

int main(int argc, char **argv)
{
    static const uint32_t race[] = { 5, 7, 2, 0 };                  /* an enemy, then character 0 */
    static const uint32_t battle[] = { 2, 10, 2, 12, 2, 10, 2, 40, 2, 8 }; /* 10 twice, 40 past the names, Galam */
    static const uint32_t five[] = { 2, 1, 2, 2, 2, 3, 2, 4, 2, 5 };
    static const uint32_t fight[] = { 2, 3, 5, 1 };
    char name[32];
    int i;
    if (argc != 2) return 2;
    s_case = argv[1];
    if (strcmp(s_case, "no_names")) {
        for (i = 0; i < 14; i++) {
            snprintf(name, sizeof name, "Name %d", i);
            name_at(LEVEL_TABLE, i, name);
        }
        for (i = 0; i < 24; i++) {
            snprintf(name, sizeof name, "Char%d", i);
            name_at(CHAR_TABLE, i, i == 8 ? "Galam" : name);  /* shown as Garam */
            /* The order: a made-up one, each character once, Char6 first. */
            put32(s_ram, ORDER_TABLE + 4u * (uint32_t)i, (uint32_t)(5 * i + 6) % 24u);
        }
        if (!strcmp(s_case, "bad_order")) put32(s_ram, ORDER_TABLE + 4u * 5u, 6);  /* Char6 twice */
    } else {
        name_at(CHAR_TABLE, 6, "Char6");
        put32(s_ram, CHAR_TABLE + 4u * 2u, RAM + 0x100u);   /* outside memory */
        put32(s_ram, CHAR_TABLE + 4u * 1u, NAMES_AT - 0x10u);   /* not text */
        s_ram[NAMES_AT - 0x10u] = 0x01;
    }
    /* Listed in no order: sorted by number, the levels by their lowest. */
    mission("mssn0371.bin", 1, 0, 6, 5, battle);   /* stg10, a tag battle */
    mission("mssn0100.bin", 0, 0, 0, 0, NULL);     /* the garage */
    mission("mssn0112.bin", 1, 2, 4, 2, race);     /* stg12, a race */
    mission("MSSN0230.BIN", 1, 1, 1, 0, NULL);     /* stg11, its name in capitals */
    mission("mssn0230.bin", 1, 1, 1, 0, NULL);     /* ... and again: once */
    mission("mssn0110.bin", 1, 2, 1, 0, NULL);
    mission("mssn0113.bin", 1, 2, 2, 2, fight);    /* a fight: no names */
    mission("mssn0296.bin", 0, 9, 0, 0, NULL);     /* stg09, no name in the table */
    mission("mssn0890.bin", 4, 0, 2, 0, NULL);     /* stg40, Gouji's Tower: named by us */
    mission("mssn0120.bin", 1, 0, 1, 0, NULL);
    mission("mssn0197.bin", 0, 0, 0, 0, NULL);     /* off the story's way (start-states.txt) */
    mission("mssn0252.bin", 3, 0, 3, 5, five);     /* stg30, a flag game: four names at most */
    mission("mssn0361.bin", 2, 1, 9, 0, NULL);     /* a kind with no name of ours */
    mission("mssn0090.bin", 1, 0, 0, 0, NULL);     /* chapter 0: not the story's */
    mission("mssn2010.bin", 1, 0, 8, 0, NULL);     /* chapter 20: not the story's */
    mission("mssn01100.bin", 1, 2, 1, 0, NULL);    /* not a mission's name */
    mission("mssnab10.bin", 1, 2, 1, 0, NULL);
    mission("mssn0130.bin", 1, 2, 1, 0, NULL);
    s_files[s_nfiles - 1].size = 0x10;             /* too short: unreadable */
    mission("mssn0140.bin", 12, 2, 1, 0, NULL);    /* a set that isn't one */

    missions_menu_start(s_ram, RAM);
    check("every file closed", s_open == 0);
    check("opened to read, never made", s_bad_open == 0);

    if (!strcmp(s_case, "pick") || !strcmp(s_case, "pick_fails")) {
        int c = row_named("Character"), l = row_named("Name 2 (stg12)");
        check("the rows there", c >= 0 && l >= 0);
        if (c < 0 || l < 0) return 1;
        s_rows[c].pick(1);
        check("Character's second choice ticked", s_rows[c].state() == 1);
        s_rows[l].pick_with(s_rows[l].data, 1);    /* 0112 */
        check("the start's variables taken out again",
              !getenv("JSRF_START_MISSION") && !getenv("JSRF_START_SKATER"));
    }
    if (!strcmp(s_case, "load") || !strcmp(s_case, "load_fails")) {
        int g = row_named("Load game");
        check("the row there", g >= 0);
        if (g < 0) return 1;
        s_rows[g].run();
        check("JSRF_START_LOAD taken out again", !getenv("JSRF_START_LOAD"));
    }
    printf("%s\n", s_failed ? "FAILED" : "passed");
    return s_failed;
}
