#include "common.h"

typedef struct Block
{
    uint32_t index;
    uint8_t  digest[DS];
    char     data[BS];
} Block;

typedef struct IndexNode
{
    struct IndexNode *next;
    uint8_t digest[DS];
    uint32_t index;
} IndexNode;

/* Hash table index of file 1 */
IndexNode *block_index[HT] = { };

/* Custom allocator data structures */
static char *xalloc_data = NULL;
static size_t xalloc_size = 0;

/* MD5 digest for file 2
  (useful to verify if a patch has been applied correctly) */
static MD5_CTX file2_md5_ctx;

/* Counts for patch instruction */
static uint32_t next_block = 0;     /* prefered next block */
static uint32_t S = 0xffffffffu;    /* seek to */
static uint16_t C = 0;              /* copy existing blocks*/
static uint16_t A = 0;              /* append new blocks */
static char new_blocks[NA][BS];

/* Allocate memory (that is never released).
   If memory cannot be allocated, execution is aborted, so this function
   never returns NULL. */
void *xalloc(size_t len)
{
    const size_t chunk_size = 1<<20;   /* 1 MB */
    void *buf;

    if (len >= chunk_size)
    {
        buf = malloc(len);
        assert(buf != NULL);
        return buf;
    }

    if (len < 1) len = 1;

    if (xalloc_size < len)
    {
        /* Allocate new chunk (1MB) */
        xalloc_size = chunk_size;
        xalloc_data = malloc(chunk_size);
        assert(xalloc_data != NULL);
    }

    buf = xalloc_data;
    xalloc_data += len;
    xalloc_size -= len;
    return buf;
}

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

/* Searches for and returns a pointer to a block in the index with the given
   digest, or NULL if it does not exist.

   If index is non-zero, the return value will point to a matching block with
   the given index if it exists. Otherwise the first matching block is returned.
*/
IndexNode *lookup(uint8_t digest[DS], uint32_t index)
{
    IndexNode *node, *first = NULL;

    node = block_index[*(uint32_t*)digest % HT];
    while (node != NULL)
    {
        if (memcmp(node->digest, digest, DS) == 0)
        {
            if (index == 0 || node->index == index) return node;
            if (first == NULL) first = node;
        }
        node = node->next;
    }

    return first;
}

/* Callback called while enumerating over file 1.
   Stores the digest and index of every block received in the hash table. */
void pass_1_callback(Block *block)
{
    IndexNode *node, **bucket;

    bucket = &block_index[*(uint32_t*)(block->digest) % HT];
    node = xalloc(sizeof(IndexNode));
    node->next = *bucket;
    memcpy(node->digest, block->digest, DS);
    node->index = block->index;
    *bucket = node;
}

/* Callback called while enumerating over file 1.
   Looks up blocks in the hash table and builds patch instructions according to
   wether or not the blocks were found. */
void pass_2_callback(Block *block)
{
    IndexNode *node = lookup(block->digest, next_block);
    if (node == NULL)
    {
        append_block(block->data);
        next_block = 0;
    }
    else
    {
        copy_block(node->index);
        next_block = node->index + 1;
    }
    MD5_Update(&file2_md5_ctx, block->data, BS);
}

void compute_digest(Block *block)
{
    MD5_CTX ctx;

    MD5_Init(&ctx);
    MD5_Update(&ctx, block->data, BS);
    MD5_Final(block->digest, &ctx);
}

void scan_file(const char *path, void (*callback)(Block *))
{
    InputStream *is;
    Block block;
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

        nread = is->read(is, block.data, BS);
        if (nread == 0) break;
        if (nread < BS)
        {
            fprintf(stderr, "WARNING: last block padded with zeroes\n");
            memset(block.data + nread, 0, BS - nread);
        }
        compute_digest(&block);
        callback(&block);
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

    if (argc != 4)
    {
        printf("Usage:\n"
               "    tardiff <file1> <file2> <diff>\n");
        return 0;
    }

    if (strcmp(argv[3], "-") != 0) redirect_stdout(argv[3]);

    write_header();
    scan_file(argv[1], &pass_1_callback);
    scan_file(argv[2], &pass_2_callback);
    write_footer();

    return 0;
}
