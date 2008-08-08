#include "common.h"
#include "binsort.h"

typedef struct BlockInfo
{
    uint8_t  digest[DS];
    uint32_t index;
} BlockInfo;

/* Block sorting */
static BinSort *bs;
static BlockInfo *blocks;
static size_t nblocks;

/* MD5 digest for file 2
  (useful to verify if a patch has been applied correctly) */
static MD5_CTX file2_md5_ctx;

/* Counts for patch instruction */
static uint32_t S = 0xffffffffu;    /* seek to */
static uint16_t C = 0;              /* copy existing blocks*/
static uint16_t A = 0;              /* append new blocks */
static char new_blocks[NA][BS];

/* Writes the given data (or aborts if the write fails) */
void write_data(void *buf, size_t len)
{
    if (fwrite(buf, 1, len, stdout) != len)
    {
        fprintf(stderr, "Write failed!\n");
        abort();
    }
}

/* Writes a 32-bit unsigned integer in big-endian notation */
void write_uint32(uint32_t i)
{
    uint8_t buf[4];
    buf[3] = i&255;
    i >>= 8;
    buf[2] = i&255;
    i >>= 8;
    buf[1] = i&255;
    i >>= 8;
    buf[0] = i&255;
    write_data(buf, 4);
}

/* Writes a 16-bit unsigned integer in big-endian notation */
void write_uint16(uint16_t i)
{
    uint8_t buf[2];
    buf[1] = i&255;
    i >>= 8;
    buf[0] = i&255;
    write_data(buf, 2);
}

void emit_instruction()
{
    if (C == 0 && A == 0) return;   /* empty instruction */

    /* Output current instruction */
    write_uint32(S);
    write_uint16(C);
    write_uint16(A);

    /* Append new data blocks */
    write_data(new_blocks, BS*A);

    /* Reset instruction */
    S = 0xffffffffu;
    C = A = 0;
}

void append_block(char data[BS])
{
    memcpy(new_blocks[A++], data, BS);
    if (A == NA) emit_instruction();
}

void copy_block(uint32_t index)
{
    if (A != 0 || index != S + C) emit_instruction();
    if (C == 0) S = index;
    C += 1;
    if (C == NC) emit_instruction();
}

/* Searches for a block in the list of blocks from file 1.
   If no block has a matching digest, NULL is returned. Otherwise, a pointer
   is returned to the block with matching index (if it exists) or the lowest
   index (otherwise).
*/
BlockInfo *lookup(uint8_t digest[DS], size_t index)
{
    BlockInfo *lo, *mid, *hi, *first;
    int d;

    /* Binary search for first block with digest >= required digest */
    lo = blocks;
    hi = blocks + nblocks;
    while (lo < hi)
    {
        mid = lo + (hi - lo)/2;
        d = memcmp(mid->digest, digest, DS);
        if (d <  0) lo = mid + 1;
        if (d >= 0) hi = mid;
    }

    if (lo == blocks + nblocks || memcmp(lo->digest, digest, DS) > 0)
    {   /* No block with matching digest found */
        return NULL;
    }

    first = lo;

    /* Now search for the first block with digest > required digest */
    lo = hi = first + 1;
    while (hi < blocks + nblocks)
    {
        if (memcmp(hi->digest, digest, DS) > 0) break;
        hi += hi - first;
    }
    if (hi > blocks + nblocks) hi = blocks + nblocks;
    while (lo < hi)
    {
        mid = lo + (hi - lo)/2;
        d = memcmp(mid->digest, digest, DS);
        if (d <= 0) lo = mid + 1;
        if (d >  0) hi = mid;
    }

    /* All blocks with matching digest are in range [first:lo) */
    if (index > 0)
    {
        /* Binary search for the right index. */
        hi = lo;
        lo = first;
        while (lo < hi)
        {
            mid = lo + (hi - lo)/2;
            d = memcmp(&mid->index, &index, sizeof(uint32_t));
            if (d == 0) return mid;
            if (d < 0) lo = mid + 1;
            if (d > 0) hi = mid;
        }
    }

    return first;
}

/* Callback called while enumerating over file 1. */
void pass_1_callback(BlockInfo *block, char data[DS])
{
    (void)data;
    BinSort_add(bs, block);
}

/* Callback called while enumerating over file 1.
   Searches for blocks in the index and builds patch instructions according to
   wether or not the blocks were found. */
void pass_2_callback(BlockInfo *block, char data[DS])
{
    BlockInfo *bi;

    bi = lookup(block->digest, (C > 0 && A == 0) ? S + C : 0);
    if (bi == NULL) append_block(data);
    if (bi != NULL) copy_block(bi->index);
    MD5_Update(&file2_md5_ctx, data, BS);
}

void scan_file(const char *path, void (*callback)(BlockInfo *, char*))
{
    MD5_CTX md5_ctx;
    InputStream *is;
    BlockInfo block;
    char block_data[BS];
    size_t nread;

    is = (strcmp(path, "-") == 0) ? OpenStdinInputStream()
                                  : OpenFileInputStream(path);
    if (is == NULL)
    {
        fprintf(stderr, "Cannot open \"%s\" for reading!\n", path);
        exit(1);
    }

    for (block.index = 0; true; ++block.index)
    {
        if (block.index == 0xffffffffu)
        {
            /* This is not very likely to happen, but ok. */
            fprintf(stderr, "File \"%s\" too large!\n", path);
            abort();
        }

        nread = is->read(is, block_data, BS);
        if (nread == 0) break;
        if (nread < BS)
        {
            fprintf(stderr, "WARNING: last block padded with zeroes\n");
            memset(block_data + nread, 0, BS - nread);
        }

        /* Compute digest */
        MD5_Init(&md5_ctx);
        MD5_Update(&md5_ctx, block_data, BS);
        MD5_Final(block.digest, &md5_ctx);

        callback(&block, block_data);

        if (nread < BS) break;
    }

    is->close(is);
}

void write_header()
{
    write_data(MAGIC_STR, MAGIC_LEN);
    MD5_Init(&file2_md5_ctx);
}

void write_footer()
{
    uint8_t digest[DS];

    /* emit final instruction (if any) */
    emit_instruction();

    /* write special EOF instruction S=C=A=-1 */
    write_uint32(0xffffffffu);
    write_uint32(0xffffffffu);

    /* append MD5 digest of file 2 */
    MD5_Final(digest, &file2_md5_ctx);
    write_data(digest, DS);
}

int main(int argc, char *argv[])
{
    assert(MD5_DIGEST_LENGTH == DS);
    assert(sizeof(BlockInfo) == 20);

    if (argc != 4)
    {
        printf("Usage:\n"
               "    tardiff <file1> <file2> <diff>\n");
        return 0;
    }

    if (strcmp(argv[3], "-") != 0) redirect_stdout(argv[3]);

    bs = BinSort_create(sizeof(BlockInfo), 4096);
    assert(bs != NULL);

    /* Scan file 1 and gather block info */
    scan_file(argv[1], &pass_1_callback);

    /* Obtain sorted list of blocks */
    nblocks = BinSort_size(bs);
    assert((nblocks*sizeof(BlockInfo))/sizeof(BlockInfo) == nblocks);
    blocks = BinSort_mmap(bs);
    assert(blocks != NULL);

    /* Scan file 2 and generate diff */
    write_header();
    scan_file(argv[2], &pass_2_callback);
    write_footer();

    BinSort_destroy(bs);

    return 0;
}
