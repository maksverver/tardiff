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

/* MD5 digests for tar files
   (used to detect errors when merging and applying patches) */
static MD5_CTX file1_md5_ctx, file2_md5_ctx;

/* Counts for patch instruction */
static uint32_t S = 0xffffffffu;    /* seek to */
static uint16_t C = 0;              /* copy existing blocks*/
static uint16_t A = 0;              /* append new blocks */
static char new_blocks[NA][BS];

static int compar_block_info(const void *a_in, const void *b_in)
{
    const BlockInfo *a = a_in, *b = b_in;
    int d = memcmp(a->digest, b->digest, DS);
    if (d != 0) return d;
    if (a->index < b->index) return -1;
    if (a->index > b->index) return +1;
    return 0;
}

static void emit_instruction()
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

static void append_block(char data[BS])
{
    memcpy(new_blocks[A++], data, BS);
    if (A == NA) emit_instruction();
}

static void copy_block(uint32_t index)
{
    if (A != 0 || index != S + C) emit_instruction();
    if (C == 0) S = index;
    C += 1;
    if (C == NC) emit_instruction();
}

/* Returns a pointer to a block in file 1 matching the given `digest', or NULL
   if none exist.  If possible, the block returned has index one greater than
   the last-found block. */
static BlockInfo *lookup(uint8_t digest[DS])
{
    static size_t next_index;
    BlockInfo *lo, *hi;
    int d;

    /* Binary search for matching block: */
    lo = blocks;
    hi = blocks + nblocks;
    while (lo < hi)
    {
        BlockInfo *p = lo + (hi - lo)/2;
        d = memcmp(p->digest, digest, DS);
        if (d == 0 && p->index == next_index)
        {
            ++next_index;
            return p;
        }
        if (d < 0 || (d == 0 && p->index < next_index)) lo = p + 1; else hi = p;
    }
    if (lo < blocks + nblocks && memcmp(lo->digest, digest, DS) == 0)
    {
        next_index = lo->index + 1;
        return lo;
    }
    if (lo > blocks && memcmp((lo - 1)->digest, digest, DS) == 0)
    {
        next_index = (lo - 1)->index + 1;
        return lo - 1;
    }
    return NULL;
}

/* Callback called while enumerating over file 1. */
static void pass_1_callback(BlockInfo *block, char data[BS])
{
    (void)data;
    BinSort_add(bs, block);
    MD5_Update(&file1_md5_ctx, data, BS);
}

/* Callback called while enumerating over file 1.
   Searches for blocks in the index and builds patch instructions according to
   wether or not the blocks were found. */
static void pass_2_callback(BlockInfo *block, char data[BS])
{
    BlockInfo *bi = lookup(block->digest);
    if (bi == NULL) append_block(data);
    if (bi != NULL) copy_block(bi->index);
    MD5_Update(&file2_md5_ctx, data, BS);
}

static void scan_file(const char *path, void (*callback)(BlockInfo *, char*))
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
        fprintf(stderr, "Cannot open '%s' for reading!\n", path);
        exit(EXIT_FAILURE);
    }

    for (block.index = 0; ; ++block.index)
    {
        if (block.index == 0xffffffffu)
        {
            /* This is not very likely to happen, but ok. */
            fprintf(stderr, "File '%s' too large!\n", path);
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

static void write_header()
{
    write_data(MAGIC_STR, MAGIC_LEN);
}

static void write_footer()
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

    /* append MD5 digest of file 1 (new in version 1.1) */
    MD5_Final(digest, &file1_md5_ctx);
    write_data(digest, DS);
}

int tardiff(int argc, char *argv[], const char *flags)
{
    assert(MD5_DIGEST_LENGTH == DS);
    assert(sizeof(BlockInfo) == 20);

    if (strcmp(argv[2], "-") != 0) redirect_stdout(argv[2]);

    bs = BinSort_create(sizeof(BlockInfo), 65536, compar_block_info);
    assert(bs != NULL);

    /* Scan file 1 and gather block info */
    MD5_Init(&file1_md5_ctx);
    scan_file(argv[0], &pass_1_callback);

    /* Obtain sorted list of blocks */
    nblocks = BinSort_size(bs);
    assert((nblocks*sizeof(BlockInfo))/sizeof(BlockInfo) == nblocks);
    blocks = BinSort_mmap(bs);
    assert(blocks != NULL);

    /* Scan file 2 and generate diff */
    write_header();
    MD5_Init(&file2_md5_ctx);
    scan_file(argv[1], &pass_2_callback);
    write_footer();

    BinSort_destroy(bs);

    return EXIT_SUCCESS;
}
