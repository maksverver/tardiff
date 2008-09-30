#include "common.h"

static InputStream *is_file1, *is_diff;
static MD5_CTX file2_md5_ctx;

FILE *open_input_file(const char *path)
{
    FILE *fp;

    fp = fopen(path, "rb");
    if (fp == NULL)
    {
        fprintf(stderr, "Cannot open \"%s\" for reading!\n", path);
        exit(1);
    }

    return fp;
}

void process_diff()
{
    char magic_buf[MAGIC_LEN];
    char data[BS];
    uint8_t digest1[DS], digest2[DS];
    uint32_t S;
    uint16_t C, A;

    MD5_Init(&file2_md5_ctx);

    if ( is_diff->read(is_diff, magic_buf, MAGIC_LEN) != MAGIC_LEN ||
         memcmp(magic_buf, MAGIC_STR, MAGIC_LEN) != 0 )
    {
        fprintf(stderr, "Not a diff file!\n");
        exit(1);
    }

    for (;;)
    {
        S = read_uint32(is_diff);
        C = read_uint16(is_diff);
        A = read_uint16(is_diff);

        if (S == 0xffffffffu && C == 0xffffu && A == 0xffffu) break;

        if (C >= 0x8000 || A >= 0x8000)
        {
            fprintf(stderr, "Invalid diff data.\n");
            abort();
        }

        if (S < 0xffffffffu)
        {
            assert(((off_t)BS*S)/BS == S);    /* detect overflow */
            if (!is_file1->seek(is_file1, (off_t)BS*S))
            {
                fprintf(stderr, "Seek failed.\n");
                abort();
            }

            while (C > 0)
            {
                read_data(is_file1, data, BS);
                write_data(data, BS);
                MD5_Update(&file2_md5_ctx, data, BS);
                C -= 1;
            }
        }

        while (A > 0)
        {
            read_data(is_diff, data, BS);
            write_data(data, BS);
            MD5_Update(&file2_md5_ctx, data, BS);
            A -= 1;
        }
    }

    read_data(is_diff, digest1, DS);
    MD5_Final(digest2, &file2_md5_ctx);

    if (memcmp(digest1, digest2, DS) != 0)
    {
        fprintf(stderr,
            "Output file verification failed!\n"
            "Stored (original) file hash:  %08x%08x%08x%08x\n"
            "Computed (new) file hash:     %08x%08x%08x%08x\n"
            ,
            ((uint32_t*)digest1)[0], ((uint32_t*)digest1)[1],
            ((uint32_t*)digest1)[2], ((uint32_t*)digest1)[3],
            ((uint32_t*)digest2)[0], ((uint32_t*)digest2)[1],
            ((uint32_t*)digest2)[2], ((uint32_t*)digest2)[3] );
        exit(1);
    }
}

int main(int argc, char *argv[])
{
    assert(MD5_DIGEST_LENGTH == DS);

    if (argc != 4)
    {
        printf("Usage:\n"
               "    tarpatch <file1> <diff> <file2>\n");
        return 0;
    }

    /* Open file 1 */
    is_file1 = OpenFileInputStream(argv[1]);
    if (is_file1 == NULL)
    {
        fprintf(stderr, "Cannot open file 1 (%s) for reading!\n", argv[1]);
        exit(1);
    }

    /* Open diff file */
    is_diff  = (strcmp(argv[2], "-") == 0) ? OpenStdinInputStream()
                                           : OpenFileInputStream(argv[2]);
    if (is_diff == NULL)
    {
        fprintf(stderr, "Cannot open diff file (%s) for reading!\n", argv[2]);
        exit(1);
    }

    /* Redirect output (if necessary) */
    if (strcmp(argv[3], "-") != 0) redirect_stdout(argv[3]);

    process_diff();

    return 0;
}
