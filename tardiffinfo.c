#include "common.h"
#include "identify.h"

static void mark_diffs_usable(struct File *files, const uint8_t digest[DS])
{
    struct File *file;
    for (file = files; file != NULL; file = file->next)
    {
        if (!file->usable && file->type == FILE_DIFF &&
            memcmp(file->diff.digest1, digest, DS) == 0)
        {
            file->usable = true;
            mark_diffs_usable(files, file->diff.digest2);
        }
    }
}

bool write_usability_report(struct File *files, FILE *fp)
{
    static uint8_t zero_digest[DS];
    struct File *file;
    bool res;

    /* Mark all data files as usable, as well as all diff files that can be
       reach from a data file: */
    for (file = files; file != NULL; file = file->next)
    {
        if (file->type == FILE_DATA)
        {
            file->usable = true;
            mark_diffs_usable(files, file->data.digest);
        }
    }

    /* To avoid gratuitous errors when using v1.0 files, mark all v1.0 diffs
       usable and those that can be reached from them usable as well. */
    mark_diffs_usable(files, zero_digest);

    /* Warn about unusable files: */
    res = true;
    for (file = files; file != NULL; file = file->next)
    {
        if (!file->usable)
        {
            fprintf(fp, "UNUSABLE FILE: %s\n", file->path);
            res = false;
        }
    }
    return res;
}

int tardiffinfo(int argc, char *argv[])
{
    struct File *files;
    bool success;

    success = identify_files((const char**)argv, argc, stdout, &files) &&
              write_usability_report(files, stderr);
    free_files(files);

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
