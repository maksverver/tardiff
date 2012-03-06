#include "common.h"

/* Generates file 2 on-line, but requires seeking in file 1.  This is the
   preferred method of applying patches when the input file is seekable, since
   it only takes linear time. */
extern void patch_forward(InputStream *is_file1, InputStream *is_diff,
                          uint8_t digest[DS]);

/* Generates file 2 out of order, but reads through file 1 only once.  This
   requires re-ordering the diff instructions which may be time consuming,
   but has the advantage of working when file 1 is non-seekable. */
extern void patch_backward(InputStream *is_file1, InputStream *is_diff,
                           uint8_t digest[DS]);

int tarpatch(int argc, char *argv[], const char *flags)
{
    InputStream *is_file1, *is_diff;
    void (*patch_func)(InputStream *, InputStream *, uint8_t[DS]);
    char magic_buf[MAGIC_LEN];
    uint8_t digest_expected[DS], digest_computed[DS];

    assert(MD5_DIGEST_LENGTH == DS);
    assert(argc == 3);

    /* Open file 1 */
    is_file1 = (strcmp(argv[0], "-") == 0) ? OpenStdinInputStream()
                                           : OpenFileInputStream(argv[0]);
    if (is_file1 == NULL)
    {
        fprintf(stderr, "Cannot open file 1 (%s) for reading!\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    /* Open diff file */
    is_diff  = (strcmp(argv[1], "-") == 0) ? OpenStdinInputStream()
                                           : OpenFileInputStream(argv[1]);
    if (is_diff == NULL)
    {
        fprintf(stderr, "Cannot open diff file (%s) for reading!\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    /* Redirect output (if necessary) */
    if (strcmp(argv[2], "-") != 0) redirect_stdout(argv[2]);

    if (is_file1->seek(is_file1, 0))
        patch_func = patch_forward;
    else
    if (fseeko(stdout, 0, SEEK_SET) == 0)
        patch_func = patch_backward;
    else
    {
        fprintf(stderr, "Neither file 1 nor file 2 is seekable!\n");
        exit(EXIT_FAILURE);
    }

    /* Read and verify file magic number */
    if ( is_diff->read(is_diff, magic_buf, MAGIC_LEN) != MAGIC_LEN ||
         memcmp(magic_buf, MAGIC_STR, MAGIC_LEN) != 0 )
    {
        fprintf(stderr, "Not a diff file!\n");
        exit(EXIT_FAILURE);
    }

    patch_func(is_file1, is_diff, digest_computed);

    /* Read and compare output file digest */
    read_data(is_diff, digest_expected, DS);
    if (memcmp(digest_expected, digest_computed, DS) != 0)
    {
        char expected_str[2*DS + 1],
             computed_str[2*DS + 1];

        hexstring(expected_str, digest_expected, DS);
        hexstring(computed_str, digest_computed, DS);

        fprintf(stderr, "Output file verification failed!\n"
                        "Original file hash:  %s (expected)\n"
                        "New file hash:       %s (computed)\n",
                        expected_str, computed_str );
        exit(EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}
