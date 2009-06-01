#include "common.h"

static bool process_file(const char *path)
{
    InputStream *is;
    char        magic_buf[MAGIC_LEN];
    uint8_t     data[BS];
    uint32_t    S;
    uint16_t    C, A;
    uint8_t     digest1[DS];
    uint8_t     digest2[DS];
    char        digest1_str[2*DS + 1];
    char        digest2_str[2*DS + 1];

    printf("%s: ", path);
    fflush(stdout);

    is = (strcmp(path, "-") == 0) ? OpenStdinInputStream()
                                  : OpenFileInputStream(path);
    if (is == NULL)
    {
        printf("failed to open file\n");
        goto failed;
    }

    if ( is->read(is, magic_buf, MAGIC_LEN) != MAGIC_LEN ||
         memcmp(magic_buf, MAGIC_STR, MAGIC_LEN) != 0 )
    {
        printf("missing/invalid file signature\n");
        goto failed;
    }

    for (;;)
    {
        if (is->read(is, data, 8) != 8)
        {
            printf("read failed -- file truncated?\n");
            goto failed;
        }

        S = parse_uint32(data + 0);
        C = parse_uint16(data + 4);
        A = parse_uint16(data + 6);

        if (S == 0xffffffffu && C == 0xffffu && A == 0xffffu) break;

        if (C >= 0x8000 || A >= 0x8000)
        {
            printf("invalid diff data\n");
            goto failed;
        }

        while (A > 0)
        {
            if (is->read(is, data, BS) != BS)
            {
                printf("read failed -- file truncated?\n");
                goto failed;
            }
            A -= 1;
        }
    }

    if (is->read(is, digest1, DS) != DS)
    {
        printf("read failed -- file truncated?\n");
        goto failed;
    }
    hexstring(digest1_str, digest1, DS);

    if (is->read(is, digest2, DS) == DS)
    {
        /* Version 1.1 file */
        hexstring(digest2_str, digest2, DS);
    }
    else
    {
        strcpy(digest2_str, "?");
    }

    printf("%s -> %s\n", digest2_str, digest1_str);

    /* successfully processed: */
    is->close(is);
    return true;

failed:
    if (is != NULL) is->close(is);
    return false;
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
