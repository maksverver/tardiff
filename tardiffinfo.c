#include "common.h"

static bool process_diff(InputStream *is)
{
    uint8_t     data[BS];
    uint32_t    S;
    uint16_t    C, A;
    uint8_t     digest1[DS];
    uint8_t     digest2[DS];
    char        digest1_str[2*DS + 1];
    char        digest2_str[2*DS + 1];

    for (;;)
    {
        if (is->read(is, data, 8) != 8)
        {
            printf("read failed -- file truncated?\n");
            return false;
        }

        S = parse_uint32(data + 0);
        C = parse_uint16(data + 4);
        A = parse_uint16(data + 6);

        if (S == 0xffffffffu && C == 0xffffu && A == 0xffffu) break;

        if (C >= 0x8000 || A >= 0x8000)
        {
            printf("invalid diff data\n");
            return false;
        }

        while (A > 0)
        {
            if (is->read(is, data, BS) != BS)
            {
                printf("read failed -- file truncated?\n");
                return false;
            }
            A -= 1;
        }
    }

    if (is->read(is, digest1, DS) != DS)
    {
        printf("read failed -- file truncated?\n");
        return false;
    }
    hexstring(digest1_str, digest1, DS);

    if (is->read(is, digest2, DS) == DS)
    {
        /* Version 1.1 file */
        hexstring(digest2_str, digest2, DS);
    }
    else
    {
        /* Version 1.0 file; no input file digest present */
        strcpy(digest2_str, "?");
    }

    printf("%s -> %s\n", digest2_str, digest1_str);
    return true;
}

static bool process_file(const char *path)
{
    bool        res;
    InputStream *is;
    char        buf[512];
    size_t      len;

    assert(sizeof(buf) >= MAGIC_LEN);

    printf("%s: ", path);
    fflush(stdout);

    is = (strcmp(path, "-") == 0) ? OpenStdinInputStream()
                                  : OpenFileInputStream(path);
    if (is == NULL)
    {
        printf("failed to open file\n");
        return false;
    }

    len = is->read(is, buf, MAGIC_LEN);
    if (memcmp(buf, MAGIC_STR, len) == 0)
    {
        /* File starts with prefix of signature */
        if (len < MAGIC_LEN)
        {
            if (len == 0)
            {
                printf("empty file\n");
            }
            else
            {
                printf("incomplete signature -- file truncated?\n");
            }
            return false;
        }
        else
        {
            /* Valid signature; process as differences file */
            printf("diff: ");
            fflush(stdout);
            res = process_diff(is);
        }
    }
    else
    {
        /* File does NOT start with a prefix of the signature; assume this is
           not a differences file, but a regular data file instead. */
        MD5_CTX     md5_ctx;
        uint8_t     digest[DS];
        char        digest_str[2*DS + 1];

        printf("data: ");
        fflush(stdout);

        /* Compute MD5 hash of contents */
        MD5_Init(&md5_ctx);
        while (len > 0)
        {
            MD5_Update(&md5_ctx, buf, len);
            len = is->read(is, buf, sizeof(buf));
        }
        MD5_Final(digest, &md5_ctx);

        hexstring(digest_str, digest, DS);
        printf("%s\n", digest_str);

        res = true;
    }

    is->close(is);
    return res;
}

int main(int argc, char *argv[])
{
    int n, failed;

    if (argc < 2)
    {
        printf("Usage:\n"
               "    tardiffinfo <diff1> [..]\n");
        return 0;
    }

    failed = 0;
    for (n = 1; n < argc; ++n) failed += !process_file(argv[n]);

    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
