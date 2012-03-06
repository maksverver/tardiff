#include "common.h"
#include "binsort.h"

struct CopyBlock
{
    uint32_t S;  /* source block index */
    uint32_t T;  /* target block index */
};

static int cb_compare(const void *a, const void *b)
{
    const struct CopyBlock *p = a, *q = b;
    if (p->S < q->S) return -1;
    if (p->S > q->S) return +1;
    if (p->T < q->T) return -1;
    if (p->T > q->T) return +1;
    return 0;
}

void patch_backward(InputStream *is_file1, InputStream *is_diff,
                   uint8_t digest_out[DS])
{
    BinSort *bs = BinSort_create(sizeof(struct CopyBlock), 1<<20, cb_compare);
    uint32_t T = 0;
    char data[BS];

    /* Process differences file and copy new blocks into output: */
    for (;;)
    {
        uint32_t S = read_uint32(is_diff);
        uint16_t C = read_uint16(is_diff);
        uint16_t A = read_uint16(is_diff);

        if (S == 0xffffffffu && C == 0xffffu && A == 0xffffu) break;

        if (C > 0x7fff || A > 0x7fff || (S < 0xffffffffu) != (C > 0))
        {
            fprintf(stderr, "Invalid diff data.\n");
            abort();
        }

        assert(T + C + A >= T);  /* detect overflow */

        while (C-- > 0)
        {
            static char zeroes[BS];
            struct CopyBlock cb = { S++, T++ };
            BinSort_add(bs, &cb);
            write_data(zeroes, BS);
        }

        while (A-- > 0)
        {
            read_data(is_diff, data, BS);
            write_data(data, BS);
            T++;
        }
    }

    /* Process file 1 in sequence: */
    {
        uint32_t s = 0, t = T;
        struct CopyBlock *cb  = BinSort_mmap(bs), *end = cb + BinSort_size(bs);
        for ( ; cb != end; ++cb)
        {
            for ( ; s <= cb->S; ++s) read_data(is_file1, data, BS);
            assert(s == cb->S + 1);
            if (cb->T != t && fseeko(stdout, (off_t)cb->T*BS, SEEK_SET) != 0)
            {
                fprintf(stderr, "Seek failed.\n");
                abort();
            }
            write_data(data, BS);
            t = cb->T + 1;
        }
    }

    BinSort_destroy(bs);

    /* Calculcate checksum of output file: */
    {
        MD5_CTX md5_ctx;
        uint32_t t;

        if (fseeko(stdout, 0, SEEK_SET) != 0)
        {
            fprintf(stderr, "Seek failed.\n");
            abort();
        }
        MD5_Init(&md5_ctx);
        for (t = 0; t < T; ++t)
        {
            if (fread(data, BS, 1, stdout) != 1)
            {
                fprintf(stderr, "Read failed.\n");
                abort();
            }
            MD5_Update(&md5_ctx, data, BS);
        }
        MD5_Final(digest_out, &md5_ctx);
    }
}
