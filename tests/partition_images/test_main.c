/* Run with: python tests/partition_images/run.py
 *
 * The emulated hard disk's partition devices. Each is backed by an image file
 * in the save folder, and the images used to be sized to the retail geometry --
 * close to 10 GB of apparent size, which a file manager reports and a copy that
 * does not keep holes writes out in full. Now each image holds only what has
 * been written, and the device's length comes from the geometry instead.
 *
 * What a title can see must not change with that: the device is as long as
 * the partition, reads anywhere inside it succeed, and what was never written
 * reads as zero. An ordinary file's short read stays short.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "kernel/kernel.h"

static const char *s_save;

/* Stubs for what the file layer reaches outside itself. */
void xbox_log(int level, const char *subsystem, const char *fmt, ...)
{
    (void)level; (void)subsystem; (void)fmt;
}
const char *xbox_LookupSymbolicLink(const char *link)
{
    (void)link;
    return NULL;
}

static long long image_size(int p)
{
    char path[512];
    struct stat st;
    snprintf(path, sizeof(path), "%s/Partition%d.img", s_save, p);
    return stat(path, &st) == 0 ? (long long)st.st_size : -1;
}

static HANDLE open_xbox(const char *xbox_path, ULONG disposition)
{
    XBOX_ANSI_STRING name;
    XBOX_OBJECT_ATTRIBUTES oa;
    XBOX_IO_STATUS_BLOCK ios;
    HANDLE h = NULL;
    name.Buffer = (PCHAR)xbox_path;
    name.Length = (USHORT)strlen(xbox_path);
    name.MaximumLength = (USHORT)(name.Length + 1);
    oa.RootDirectory = NULL;
    oa.ObjectName = &name;
    oa.Attributes = 0;
    assert(xbox_NtCreateFile(&h, XBOX_GENERIC_READ | XBOX_GENERIC_WRITE, &oa, &ios, NULL, 0, 0,
                             disposition, XBOX_FILE_SYNCHRONOUS_IO_NONALERT) == STATUS_SUCCESS);
    return h;
}

/* Reads `want` bytes at `at` (sequentially when at < 0) into a buffer filled
 * with a marker; returns the status and the count. */
static NTSTATUS read_at(HANDLE h, long long at, unsigned char *buf, ULONG want, ULONG *got)
{
    XBOX_IO_STATUS_BLOCK ios;
    LARGE_INTEGER off;
    NTSTATUS st;
    memset(buf, 0xAA, want);
    off.QuadPart = at;
    st = xbox_NtReadFile(h, NULL, NULL, NULL, &ios, buf, want, at < 0 ? NULL : &off);
    *got = (ULONG)ios.Information;
    return st;
}

static int all_zero(const unsigned char *p, ULONG n)
{
    while (n--)
        if (*p++)
            return 0;
    return 1;
}

int main(int argc, char **argv)
{
    static unsigned char buf[4096], data[4096];
    const ULONGLONG part5 = 0x00177000ull * 512, part0 = 0x009C4400ull * 512;
    char path[512];
    XBOX_IO_STATUS_BLOCK ios;
    LARGE_INTEGER off;
    HANDLE h, h0, file;
    ULONG got;
    FILE *f;

    assert(argc == 3);
    s_save = argv[2];

    /* An image an earlier build sized to the whole partition. */
    snprintf(path, sizeof(path), "%s/Partition5.img", s_save);
    f = fopen(path, "wb");
    assert(f && fseek(f, 1L << 20, SEEK_SET) == 0 && fputc(1, f) == 1);
    fclose(f);

    xbox_path_init(argv[1], argv[2]);

    /* Recreated empty: only the partition table, at sector 4, is in one. */
    assert(image_size(0) == 0x800 + 512);
    for (int p = 1; p <= 5; p++)
        assert(image_size(p) == 0);

    /* The devices are as long as the partitions. */
    h0 = open_xbox("\\Device\\Harddisk0\\Partition0", XBOX_FILE_OPEN);
    h = open_xbox("\\Device\\Harddisk0\\Partition5", XBOX_FILE_OPEN);
    assert(xbox_partition_length(h0) == part0);
    assert(xbox_partition_length(h) == part5);

    /* The table reads back where XAPI looks for it. */
    assert(read_at(h0, 0x800, buf, 512, &got) == STATUS_SUCCESS && got == 512);
    assert(!memcmp(buf, "****PARTINFO****", 16));

    /* Never written: zeros, and the whole read, as on a blank disk. */
    assert(read_at(h, 0, buf, 512, &got) == STATUS_SUCCESS && got == 512 && all_zero(buf, 512));
    assert(read_at(h, (long long)part5 - 4096, buf, 4096, &got) == STATUS_SUCCESS
           && got == 4096 && all_zero(buf, 4096));
    /* ...up to the end of the partition, and no further. */
    assert(read_at(h, (long long)part5 - 256, buf, 512, &got) == STATUS_SUCCESS
           && got == 256 && all_zero(buf, 512));
    assert(read_at(h, (long long)part5, buf, 512, &got) == STATUS_END_OF_FILE && got == 0);

    /* Writing grows the image by what was written, not by the partition. */
    for (int i = 0; i < (int)sizeof(data); i++)
        data[i] = (unsigned char)(i * 7 + 1);
    off.QuadPart = 0x1000;
    assert(xbox_NtWriteFile(h, NULL, NULL, NULL, &ios, data, sizeof(data), &off) == STATUS_SUCCESS);
    assert(image_size(5) == 0x2000);
    /* A read that runs off what was written gets the rest as zeros. */
    assert(read_at(h, 0x1800, buf, 4096, &got) == STATUS_SUCCESS && got == 4096);
    assert(!memcmp(buf, data + 0x800, 0x800) && all_zero(buf + 0x800, 0x800));

    /* Sequential reads go on from where the last one stopped, past the end
     * of what was written: 0x2800 here, then 0x3800. */
    assert(read_at(h, -1, buf, 4096, &got) == STATUS_SUCCESS && got == 4096 && all_zero(buf, 4096));
    assert(read_at(h, -1, buf, 16, &got) == STATUS_SUCCESS && got == 16 && all_zero(buf, 16));
    assert(xbox_NtClose(h) == STATUS_SUCCESS && xbox_NtClose(h0) == STATUS_SUCCESS);

    /* An ordinary file is not a device: its short read stays short, the rest
     * of the buffer zeroed, and reading at its end is the end. */
    snprintf(path, sizeof(path), "%s/plain.bin", argv[1]);
    f = fopen(path, "wb");
    assert(f && fwrite(data, 1, 100, f) == 100);
    fclose(f);
    file = open_xbox("D:\\plain.bin", XBOX_FILE_OPEN);
    assert(xbox_partition_length(file) == 0);
    assert(read_at(file, 0, buf, 512, &got) == STATUS_SUCCESS && got == 100);
    assert(!memcmp(buf, data, 100) && all_zero(buf + 100, 412));
    assert(read_at(file, 100, buf, 512, &got) == STATUS_END_OF_FILE && got == 0);
    assert(xbox_NtClose(file) == STATUS_SUCCESS);

    /* The next start begins again from empty images. */
    xbox_path_init(argv[1], argv[2]);
    assert(image_size(5) == 0);

    printf("partition_images: ok\n");
    return 0;
}
