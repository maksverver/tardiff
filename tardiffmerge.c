#include "common.h"
#include "identify.h"
#include <sys/mman.h>

#define MAX_DIFF_FILES 1000

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

/* Given a list of differences files, marks all files usable that can be
   applied to another differences file. This should leave exactly one unusable
   file that is the starting point for a diff sequence. */
static void mark_usable(struct File *files)
{
    struct File *f, *g;
    for (f = files; f != NULL; f = f->next)
    {
        assert(f->type == FILE_DIFF);
        for (g = files; g != NULL; g = g->next)
        {
            assert(g->type == FILE_DIFF);
            if (memcmp(f->diff.digest2, g->diff.digest1, DS) == 0)
            {
                g->usable = true;
            }
        }
    }
}

/* Returns the only unusable file (if there is exactly one): */
static struct File *find_first(struct File *files)
{
    struct File *file, *res = NULL;
    for (file = files; file != NULL; file = file->next)
    {
        if (!file->usable)
        {
            if (res != NULL) return NULL;
            res = file;
        }
    }
    return res;
}

/* Returns the only file with the given source digest: */
static struct File *find_digest(struct File *files, uint8_t digest[DS])
{
    struct File *file, *res = NULL;
    for (file = files; file != NULL; file = file->next)
    {
        if (memcmp(file->diff.digest1, digest, DS) == 0)
        {
            if (res != NULL) return NULL;
            res = file;
        }
    }
    return res;
}

/* Moves the target file to the front of the list: */
static struct File *move_to_front(struct File *files, struct File *target)
{
    struct File *file, *list, **ptr = &list;
    for (file = files; file != NULL; file = file->next)
    {
        if (file != target)
        {
            *ptr = file;
            ptr = &file->next;
        }
    }
    *ptr = NULL;
    target->next = list;
    return target;
}

/* Order the list of input files so each next file applies to the previous: */
static bool order_input(struct File **files)
{
    struct File *cur, *succ;

    /* Determine first file */
    cur = find_first(*files);
    if (cur == NULL) return false;
    *files = move_to_front(*files, cur);

    /* Order remaining files */
    while (cur->next)
    {
        succ = find_digest(cur->next, cur->diff.digest2);
        if (succ == NULL) return false;
        cur->next = move_to_front(cur->next, succ);
        cur = cur->next;
    }

    return true;
}

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
        exit(EXIT_FAILURE);
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
            exit(EXIT_FAILURE);
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
                    exit(EXIT_FAILURE);
                }
                br = last_blocks[S++];
            }
            if (fwrite(&br, sizeof(br), 1, fp) != 1)
            {
                fprintf(stderr, "Write to temporary file failed!\n");
                exit(EXIT_FAILURE);
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
                exit(EXIT_FAILURE);
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
            exit(EXIT_FAILURE);
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
            exit(EXIT_FAILURE);
        }
    }

    /* Map new block data */
    fflush(fp); /* must flush to ensure all data can be mmap()ed */
    last_blocks = mmap( NULL, num_blocks*sizeof(BlockRef), PROT_READ,
                        MAP_SHARED, fileno(fp), 0 );
    if (last_blocks == NULL)
    {
        fprintf(stderr, "mmap() failed!\n");
        exit(EXIT_FAILURE);
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

static bool generate_output()
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

    return true;
}

int tardiffmerge(int argc, char *argv[], char *flags)
{
    int n, num_diffs;
    struct File *files, *file;
    bool input_ok, output_ok, order_files;

    num_diffs = argc - 1;
    input_ok = output_ok = false;
    order_files = (strchr(flags, 'f') == NULL);

    /* Verify arguments are all diff files: */
    input_ok = identify_files((const char**)argv, num_diffs, NULL, &files);
    for (file = files; file != NULL; file = file->next)
    {
        if (file->type == FILE_INVALID)
        {
            fprintf(stderr, "%s: %s\n", file->path, file->invalid.error);
        }
        else
        if (file->type != FILE_DIFF)
        {
            fprintf(stderr, "%s: not a differences file\n", file->path);
            input_ok = false;
        }
        else  /* file->type == FILE_DIFF */
        {
            static uint8_t zero_digest[DS];
            if (order_files &&
                memcmp(file->diff.digest1, zero_digest, DS) == 0)
            {
                fprintf(stderr, "Input contains version 1.0 difference files; "
                                "merge order cannot be determined.\n");
                input_ok = false;
            }
        }
    }

    if (input_ok && order_files)
    {
        mark_usable(files);
        if (!order_input(&files))
        {
            fprintf(stderr, "Input files could not be ordered!\n");
            input_ok = false;
        }
    }

    if (input_ok)
    {
        /* Redirect output (if necessary) */
        if (strcmp(argv[argc - 1], "-") != 0) redirect_stdout(argv[argc - 1]);

        n = 0;
        for (file = files; file != NULL; file = file->next)
        {
            InputStream *is;
            char magic[MAGIC_LEN];

            /* Try to open again */
            is = OpenFileInputStream(file->path);
            if (is == NULL)
            {
                fprintf(stderr, "%s: could not be opened.\n", file->path);
                break;
            }

            /* Save pointer here, so we can close it later. */
            is_diff[n++] = is;

            /* Verify magic again */
            read_data(is, magic, MAGIC_LEN);
            if (memcmp(magic, MAGIC_STR, MAGIC_LEN) != 0)
            {
                fprintf(stderr, "%s: not a differences file\n", file->path);
                break;
            }

            /* Process entire file */
            process_input(is);
        }

        if (file == NULL)
        {
            output_ok = generate_output();
        }

        /* Close open streams: */
        while (n-- > 0) is_diff[n]->close(is_diff[n]);

        munmap(last_blocks, last_num_blocks*sizeof(BlockRef));
    }
    free_files(files);

    return (input_ok && output_ok) ? EXIT_SUCCESS : EXIT_FAILURE;
}
