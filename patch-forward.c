#include "common.h"

void patch_forward(InputStream *is_file1, InputStream *is_diff,
                   uint8_t digest_out[DS])
{
    MD5_CTX file2_md5_ctx;
    char data[BS];
    uint32_t S;
    uint16_t C, A;

    MD5_Init(&file2_md5_ctx);

    for (;;)
    {
        S = read_uint32(is_diff);
        C = read_uint16(is_diff);
        A = read_uint16(is_diff);

        if (S == 0xffffffffu && C == 0xffffu && A == 0xffffu) break;

        if (C > 0x7fff || A > 0x7fff || (S < 0xffffffffu) != (C > 0))
        {
            fprintf(stderr, "Invalid diff data.\n");
            abort();
        }

        if (S < 0xffffffffu)
        {
            if (!is_file1->seek(is_file1, (off_t)BS*S))
            {
                fprintf(stderr, "Seek failed.\n");
                abort();
            }

            while (C-- > 0)
            {
                read_data(is_file1, data, BS);
                write_data(data, BS);
                MD5_Update(&file2_md5_ctx, data, BS);
            }
        }

        while (A-- > 0)
        {
            read_data(is_diff, data, BS);
            write_data(data, BS);
            MD5_Update(&file2_md5_ctx, data, BS);
        }
    }

    MD5_Final(digest_out, &file2_md5_ctx);
}
