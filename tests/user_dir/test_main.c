/* Run with: python tests/user_dir/run.py
 *
 * Where a title's saves go. XAPI keeps them in E:\UDATA and E:\TDATA -- it
 * links U: and T: there -- and E: is Partition1, the game dir, so they land
 * among the game's files. A launcher can give the player's own folder for
 * them instead (JSRF_USER_DIR, the Linux launcher's portable mode). Given one,
 * the saves go there and nothing else moves; without one, nothing moves.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "kernel/kernel.h"

/* Stubs for what the file layer reaches outside itself. */
void xbox_log(int level, const char *subsystem, const char *fmt, ...)
{
    (void)level; (void)subsystem; (void)fmt;
}
/* U: as the title links it: to its own folder under E:\UDATA. */
const char *xbox_LookupSymbolicLink(const char *link)
{
    return strcmp(link, "\\??\\U:") ? NULL : "\\Device\\Harddisk0\\Partition1\\UDATA\\5345000a";
}

/* Opens what the title names, creating it if need be, and closes it. */
static void make(const char *xbox_path, ULONG options)
{
    XBOX_ANSI_STRING name;
    XBOX_OBJECT_ATTRIBUTES oa;
    XBOX_IO_STATUS_BLOCK ios;
    HANDLE h = NULL;
    ACCESS_MASK access = (options & XBOX_FILE_DIRECTORY_FILE)
                       ? XBOX_GENERIC_READ : XBOX_GENERIC_READ | XBOX_GENERIC_WRITE;
    name.Buffer = (PCHAR)xbox_path;
    name.Length = (USHORT)strlen(xbox_path);
    name.MaximumLength = (USHORT)(name.Length + 1);
    oa.RootDirectory = NULL;
    oa.ObjectName = &name;
    oa.Attributes = 0;
    assert(xbox_NtCreateFile(&h, access, &oa, &ios, NULL, 0, 0, XBOX_FILE_OPEN_IF,
                             options | XBOX_FILE_SYNCHRONOUS_IO_NONALERT) == STATUS_SUCCESS);
    assert(xbox_NtClose(h) == STATUS_SUCCESS);
}

static int exists(const char *dir, const char *rest)
{
    char path[512];
    struct stat st;
    snprintf(path, sizeof(path), "%s/%s", dir, rest);
    return stat(path, &st) == 0;
}

/* What the title does on its first start (as a run's log shows it): makes its
 * folders under E:\TDATA and E:\UDATA one level at a time, then saves through
 * U:. Then a name that only starts like a save folder, and a disc file. */
static void play(void)
{
    make("\\Device\\Harddisk0\\partition1\\TDATA", XBOX_FILE_DIRECTORY_FILE);
    make("\\Device\\Harddisk0\\partition1\\TDATA\\5345000a", XBOX_FILE_DIRECTORY_FILE);
    make("\\Device\\Harddisk0\\partition1\\UDATA", XBOX_FILE_DIRECTORY_FILE);
    make("\\Device\\Harddisk0\\partition1\\UDATA\\5345000a", XBOX_FILE_DIRECTORY_FILE);
    make("U:\\99271B32E8BB", XBOX_FILE_DIRECTORY_FILE);
    make("U:\\99271B32E8BB\\JSRFDATA.SAV", 0);
    make("\\Device\\Harddisk0\\partition1\\UDATAX", XBOX_FILE_DIRECTORY_FILE);
    make("D:\\plain.bin", 0);
}

int main(int argc, char **argv)
{
    static char env[600];
    const char *game, *save, *user;

    assert(argc == 4);
    game = argv[1];
    save = argv[2];
    user = argv[3];

    /* Given a user dir (not made yet): the saves go there, and only they do. */
    snprintf(env, sizeof(env), "JSRF_USER_DIR=%s", user);
    putenv(env);
    xbox_path_init(game, save);
    play();
    assert(exists(user, "TDATA/5345000a"));
    assert(exists(user, "UDATA/5345000a/99271B32E8BB/JSRFDATA.SAV"));
    assert(!exists(game, "TDATA") && !exists(game, "UDATA"));
    assert(exists(game, "UDATAX") && !exists(user, "UDATAX"));
    assert(exists(game, "plain.bin") && !exists(user, "plain.bin"));

    /* Without one: in the game dir, as Partition1 always was. */
    putenv("JSRF_USER_DIR=");
    xbox_path_init(game, save);
    play();
    assert(exists(game, "TDATA/5345000a"));
    assert(exists(game, "UDATA/5345000a/99271B32E8BB/JSRFDATA.SAV"));

    printf("user_dir: ok\n");
    return 0;
}
