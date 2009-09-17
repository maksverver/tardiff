#include "common.h"

struct InvalidFile
{
    const char      *error;
};

struct DataFile
{
    uint8_t         digest[DS];
};

struct DiffFile
{
    uint8_t         digest1[DS];    /* input file digest */
    uint8_t         digest2[DS];    /* output file digest */
    uint32_t        copied;         /* number of blocks copied */
    uint32_t        added;          /* number of files added */
};

enum FileType { FILE_INVALID, FILE_DATA, FILE_DIFF };

struct File
{
    struct File     *next;
    char            *path;
    bool            usable;
    enum FileType   type;
    union {
        struct InvalidFile invalid;
        struct DataFile    data;
        struct DiffFile    diff;
    };
};

static struct File *files = NULL, **files_end = &files;
static struct File cur_file;

static void add_current_file()
{
    /* Create a copy of the file struct at the end of the list */
    *files_end = malloc(sizeof(struct File));
    assert(*files_end != NULL);
    memcpy(*files_end, &cur_file, sizeof(struct File));
    files_end = &(*files_end)->next;
}

static void mark_diffs_usable(const uint8_t digest[DS])
{
    struct File *file;
    for (file = files; file != NULL; file = file->next)
    {
        if (!file->usable && file->type == FILE_DIFF &&
            memcmp(file->diff.digest1, digest, DS) == 0)
        {
            file->usable = true;
            mark_diffs_usable(file->diff.digest2);
        }
    }
}

static void free_files()
{
    struct File *file, *next;
    for (file = files; file != NULL; file = next)
    {
        next = file->next;
        free(file->path);
        free(file);
    }
    files = NULL;
}

static bool process_diff(InputStream *is, const char **error)
{
    uint8_t     data[BS];
    uint32_t    S;
    uint16_t    C, A;
    char        digest1_str[2*DS + 1];
    char        digest2_str[2*DS + 1];
    uint32_t    TC = 0, TA = 0;

    for (;;)
    {
        if (is->read(is, data, 8) != 8)
        {
            *error = "read failed -- file truncated?";
            return false;
        }

        S = parse_uint32(data + 0);
        C = parse_uint16(data + 4);
        A = parse_uint16(data + 6);

        if (S == 0xffffffffu && C == 0xffffu && A == 0xffffu) break;

        if (C >= 0x8000 || A >= 0x8000)
        {
            *error = "invalid diff data";
            return false;
        }

        TC += C;
        TA += A;

        while (A > 0)
        {
            if (is->read(is, data, BS) != BS)
            {
                *error = "read failed -- file truncated?";
                return false;
            }
            A -= 1;
        }
    }

    if (is->read(is, cur_file.diff.digest2, DS) != DS)
    {
        *error = "read failed -- file truncated?";
        return false;
    }
    hexstring(digest2_str, cur_file.diff.digest2, DS);

    if (is->read(is, cur_file.diff.digest1, DS) == DS)
    {
        /* Version 1.1 file */
        hexstring(digest1_str, cur_file.diff.digest1, DS);
    }
    else
    {
        /* Version 1.0 file; no input file digest present */
        memset(cur_file.diff.digest1, 0, DS);
        strcpy(digest1_str, "?");
    }

    cur_file.type = FILE_DIFF;
    cur_file.diff.copied = TC;
    cur_file.diff.added  = TA;

    printf( "%s -> %s (%d blocks, %6.3f%% new)\n",
        digest1_str, digest2_str, TC + TA, 100.0*TA/(TC + TA) );
    return true;
}

static bool process_data(InputStream *is, char *buf, size_t buf_size,
                         size_t len, const char **error)
{
    MD5_CTX     md5_ctx;
    char        digest_str[2*DS + 1];
    size_t      total = 0;

    (void)error;  /* unused */

    /* Compute MD5 hash of contents */
    MD5_Init(&md5_ctx);
    while (len > 0)
    {
        total += len;
        MD5_Update(&md5_ctx, buf, len);
        len = is->read(is, buf, buf_size);
    }
    MD5_Final(cur_file.data.digest, &md5_ctx);

    hexstring(digest_str, cur_file.data.digest, DS);
    printf( "%s (%d blocks)\n",
            digest_str, (int)(total/BS + (bool)(total%BS)) );

    cur_file.type = FILE_DATA;

    return true;
}

static bool process_file(const char *path)
{
    bool        res, is_diff;
    const char  *error = NULL;
    InputStream *is;
    char        buf[512];
    size_t      len;

    assert(sizeof(buf) >= MAGIC_LEN);

    cur_file.next   = NULL;
    cur_file.path   = strdup(path);
    cur_file.usable = false;
    assert(cur_file.path != NULL);
    printf("%s: ", path);
    fflush(stdout);

    is = (strcmp(path, "-") == 0) ? OpenStdinInputStream()
                                  : OpenFileInputStream(path);
    if (is == NULL)
    {
        error = "failed to open file";
        res = false;
    }
    else
    {
        len = is->read(is, buf, MAGIC_LEN);
        is_diff = memcmp(buf, MAGIC_STR, len) == 0;
        if (is_diff)
        {
            /* File starts with prefix of signature */
            if (len < MAGIC_LEN)
            {
                if (len == 0)
                {
                    error = "unreadable or empty file";
                }
                else
                {
                    error = "incomplete signature -- file truncated?";
                }
                res = false;
            }
            else
            {
                /* Valid signature; process as differences file */
                printf("diff: ");
                fflush(stdout);
                res = process_diff(is, &error);
            }
        }
        else
        {
            /* File does NOT start with a prefix of the signature; assume this
               is not a differences file, but a regular data file instead. */
            printf("data: ");
            fflush(stdout);
            res = process_data(is, buf, sizeof(buf), len, &error);
        }
    }

    if (is != NULL) is->close(is);

    if (!res)
    {
        assert(error != NULL);
        cur_file.type = FILE_INVALID;
        cur_file.invalid.error = error;
        printf("%s\n", error);
    }

    add_current_file();

    return res;
}

bool write_usability_report(FILE *fp)
{
    static uint8_t zero_digest[DS];
    struct File *file;
    bool res;

    /* Mark all data files as usable, as well as all diff files that can be
       reach from a data file: */
    for (file = files; file != NULL; file = file->next)
    {
        if (file->type == FILE_DATA)
        {
            file->usable = true;
            mark_diffs_usable(file->data.digest);
        }
    }

    /* To avoid gratuitous errors when using v1.0 files, mark all v1.0 diffs
       usable and those that can be reached from them usable as well. */
    mark_diffs_usable(zero_digest);

    /* Warn about unusable files: */
    res = true;
    for (file = files; file != NULL; file = file->next)
    {
        if (!file->usable)
        {
            fprintf(fp, "UNUSABLE FILE: %s\n", file->path);
            res = false;
        }
    }
    return res;
}

int tardiffinfo(int argc, char *argv[])
{
    int n;
    bool success = true;

    for (n = 0; n < argc; ++n) success = process_file(argv[n]) && success;
    success = write_usability_report(stderr) && success;
    free_files();

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
