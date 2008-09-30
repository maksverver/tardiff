#include "common.h"
#include <sys/mman.h>

#define MAX_DIFF_FILES 100

/* A merged patch file is described by a sequence of block references (one for
   each block in the output file). The reference is made either to a block in
   the original file (if fp == NULL) in which case offset is a multiple of the
   block size, or a block stored at the specified offset and file.
*/
typedef struct BlockRef
{
    InputStream *is;
    off_t offset;
} BlockRef;

static InputStream *is_diff[MAX_DIFF_FILES];
static bool orig_digest_known;
static uint8_t orig_digest[DS];
static uint8_t last_digest[DS];
static size_t last_num_blocks;
static BlockRef *last_blocks;

/* Process the differences file in input stream, starting at offset 8 (the
   header has already been read and verified), creating a new block reference
   list. */
static void process_input(InputStream *is)
{
    FILE *fp;
    uint32_t S;
    uint16_t C, A;
    size_t num_blocks;
    off_t offset;
    BlockRef br;
    uint8_t digest1[DS], digest2[DS];

    fp = tmpfile();
    if (fp == NULL)
    {
        fprintf(stderr, "Couldn't open temporary file!\n");
        exit(1);
    }

    offset = 8;
    num_blocks = 0;

    for (;;)
    {
        S = read_uint32(is);
        C = read_uint16(is);
        A = read_uint16(is);
        offset += 8;

        /* Check for end-of-instructions. */
        if (S == 0xffffffffu && C == 0xffffu && A == 0xffffu) break;

        if ( (C > 0x7fffu || A > 0x7fffu) || (C > 0xffffffffu - S) ||
             (S == 0xffffffffu && C != 0) || (S != 0xffffffffu && C == 0) )
        {
            fprintf(stderr, "Invalid instruction in differences file!\n");
            exit(1);
        }

        while (C--)
        {
            if (last_blocks == NULL)
            {
                br.is = NULL;
                br.offset = BS*S++;
            }
            else
            {
                if (S >= last_num_blocks)
                {
                    fprintf(stderr, "Invalid block index in differences file!\n");
                    exit(1);
                }
                br = last_blocks[S++];
            }
            if (fwrite(&br, sizeof(br), 1, fp) != 1)
            {
                fprintf(stderr, "Write to temporary file failed!\n");
                exit(1);
            }
            ++num_blocks;
        }

        while (A--)
        {
            br.is = is;
            br.offset = offset;
            offset += BS;
            if (fwrite(&br, sizeof(br), 1, fp) != 1)
            {
                fprintf(stderr, "Write to temporary file failed!");
                exit(1);
            }
            ++num_blocks;
        }

        is->seek(is, offset);
    }

    /* Verify MD5 digests */
    read_data(is, digest2, DS);
    if (is->read(is, digest1, DS) == DS)
    {
        if (last_blocks == NULL)
        {
            orig_digest_known = true;
            memcpy(orig_digest, digest1, DS);
        }
        else
        if (memcmp(digest1, last_digest, DS) != 0)
        {
            fprintf(stderr, "Invalid sequence of differences files!\n");
            exit(1);
        }
    }
    else
    {
        fprintf(stderr, "WARNING: differences file is missing original file "
                        "digest; patch integrity cannot be guaranteed.\n");
    }
    memcpy(last_digest, digest2, DS);

    /* Unmap old block data */
    if (last_blocks != NULL)
    {
        if (munmap(last_blocks, last_num_blocks*sizeof(BlockRef)) != 0)
        {
            fprintf(stderr, "munmap() failed!\n");
            exit(1);
        }
    }

    /* Map new block data */
    fflush(fp); /* must flush to ensure all data can be mmap()ed */
    last_blocks = mmap( NULL, num_blocks*sizeof(BlockRef), PROT_READ,
                        MAP_SHARED, fileno(fp), 0 );
    if (last_blocks == NULL)
    {
        fprintf(stderr, "mmap() failed!\n");
        exit(1);
    }
    last_num_blocks = num_blocks;
    fclose(fp);
}

/* Emits an instruction to generate the last (C+A) blocks before position n. */
static void emit_instruction(size_t n, uint16_t C, uint16_t A)
{
    uint8_t block[BS];
    size_t a;
    InputStream *is;

    assert(C + A <= n && n <= last_num_blocks);

    /* Write instruction */
    write_uint32((C == 0) ? 0xffffffffu : last_blocks[n - A - C].offset/BS);
    write_uint16(C);
    write_uint16(A);

    /* Add instruction data */
    for (a = 0; a < A; ++a)
    {
        is = last_blocks[n - A + a].is;
        is->seek(is, last_blocks[n - A + a].offset);
        read_data(is, block, BS);
        write_data(block, BS);
    }
}

static void generate_output()
{
    size_t n;
    uint16_t C, A;

    /* Write header */
    write_data(MAGIC_STR, MAGIC_LEN);

    /* Generate instructions */
    C = A = 0;
    for (n = 0; n < last_num_blocks; ++n)
    {
        if (last_blocks[n].is == NULL)
        {
            /* Check to see if we must start a new instruction */
            if ( (C > 0 && last_blocks[n].offset !=
                           last_blocks[n - 1].offset + BS ) ||
                 (C == 0x7fffu) || (A > 0) )
            {
                emit_instruction(n, C, A);
                C = A = 0;
            }
            ++C;
        }
        else
        {
            if (A == 0x7fffu)
            {
                emit_instruction(n, C, A);
                C = A = 0;
            }
            ++A;
        }
    }

    /* Emit final instruction (if necessary) */
    if (C > 0 || A > 0) emit_instruction(last_num_blocks, C, A);

    /* Write end-of-instructions */
    write_uint32(0xffffffffu);
    write_uint16(0xffffu);
    write_uint16(0xffffu);

    /* Add last file digest */
    write_data(last_digest, DS);

    /* Add first file digest (if known) */
    if (!orig_digest_known)
    {
        fprintf(stderr, "WARNING: original file digest unknown; "
                        "generating version 1.0 differences file.\n");
    }
    else
    {
        write_data(orig_digest, DS);
    }
}

int main(int argc, char *argv[])
{
    int n, num_diffs;

    if (argc < 4)
    {
        printf("Usage:\n"
               "    tardiffmerge <diff1> <diff2> [..] <diff>\n");
        return 0;
    }

    num_diffs = argc - 2;
    if (num_diffs > MAX_DIFF_FILES)
    {
        fprintf( stderr, "Too many difference files supplied "
                         "(maximum is %d)!\n", MAX_DIFF_FILES );
        exit(1);
    }

    /* Open all input files first. */
    for (n = 0; n < num_diffs; ++n)
    {
        char magic[MAGIC_LEN];

        is_diff[n] = OpenFileInputStream(argv[1 + n]);
        if (is_diff[n] == NULL)
        {
            fprintf(stderr, "Cannot open difference file %d (%s) "
                            "for reading!\n", n, argv[1 + n] );
            exit(1);
        }
        read_data(is_diff[n], magic, MAGIC_LEN);
        if (memcmp(magic, MAGIC_STR, MAGIC_LEN) != 0)
        {
            fprintf(stderr, "File %d (%s) is not a difference file! "
                            " (invalid magic string)\n", n, argv[1 + n]);
            exit(1);
        }
    }

    /* Redirect output (if necessary) */
    if (strcmp(argv[argc - 1], "-") != 0) redirect_stdout(argv[argc - 1]);

    /* Process input files. */
    for (n = 0; n < num_diffs; ++n)
    {
        process_input(is_diff[n]);
    }

    /* Generate output */
    generate_output();

    munmap(last_blocks, last_num_blocks*sizeof(BlockRef));

    return 0;
}
