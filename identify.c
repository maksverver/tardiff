#include "identify.h"

static bool process_diff(InputStream *is, struct File *file,
                         FILE *fp, const char **error)
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

    if (is->read(is, file->diff.digest2, DS) != DS)
    {
        *error = "read failed -- file truncated?";
        return false;
    }
    hexstring(digest2_str, file->diff.digest2, DS);

    if (is->read(is, file->diff.digest1, DS) == DS)
    {
        /* Version 1.1 file */
        hexstring(digest1_str, file->diff.digest1, DS);
    }
    else
    {
        /* Version 1.0 file; no input file digest present */
        memset(file->diff.digest1, 0, DS);
        strcpy(digest1_str, "?");
    }

    file->type = FILE_DIFF;
    file->diff.copied = TC;
    file->diff.added  = TA;

    if (fp != NULL)
    {
        fprintf(fp, "%s -> %s (%d blocks, %6.3f%% new)\n",
            digest1_str, digest2_str, TC + TA, 100.0*TA/(TC + TA) );
    }

    return true;
}

static bool process_data(
    InputStream *is, char *buf, size_t buf_size, size_t len,
    struct File *file, FILE *fp, const char **error)
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
    MD5_Final(file->data.digest, &md5_ctx);

    hexstring(digest_str, file->data.digest, DS);
    if (fp != NULL)
    {
        fprintf(fp, "%s (%d blocks)\n",
                digest_str, (int)(total/BS + (bool)(total%BS)) );
    }

    file->type = FILE_DATA;

    return true;
}

static bool process_file(const char *path, FILE *fp, struct File ***files)
{
    struct File *file;
    bool        res, is_diff;
    const char  *error = NULL;
    InputStream *is;
    char        buf[512];
    size_t      len;

    assert(sizeof(buf) >= MAGIC_LEN);

    /* Allocate file entry: */
    file = malloc(sizeof(struct File));
    assert(file != NULL);
    file->next   = NULL;
    file->path   = strdup(path);
    file->usable = false;
    assert(file->path != NULL);
    **files = file;
    *files = &file->next;

    if (fp != NULL)
    {
        fprintf(fp, "%s: ", path);
        fflush(fp);
    }

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
                if (fp != NULL)
                {
                    fprintf(fp, "diff: ");
                    fflush(stdout);
                }
                res = process_diff(is, file, fp, &error);
            }
        }
        else
        {
            /* File does NOT start with a prefix of the signature; assume this
               is not a differences file, but a regular data file instead. */
            if (fp != NULL)
            {
                fprintf(fp, "data: ");
                fflush(stdout);
            }
            res = process_data(is, buf, sizeof(buf), len, file, fp, &error);
        }
    }

    if (is != NULL) is->close(is);

    if (!res)
    {
        assert(error != NULL);
        file->type = FILE_INVALID;
        file->invalid.error = error;
        if (fp != NULL) fprintf(fp, "%s\n", error);
    }

    return res;
}

bool identify_files(const char **paths, int npath, FILE *fp,
                    struct File **files)
{
    int i;
    bool res = true;

    *files = NULL;
    for (i = 0; i < npath; ++i)
    {
        if (!process_file(paths[i], fp, &files)) res = false;
    }
    return res;
}

void free_files(struct File *files)
{
    struct File *file, *next;
    for (file = files; file != NULL; file = next)
    {
        next = file->next;
        free(file->path);
        free(file);
    }
}
