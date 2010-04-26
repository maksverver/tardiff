#ifndef IDENTIFY_H_INCLUDED
#define IDENTIFY_H_INCLUDED

#include "common.h"

/* Common functions used to identify files passed on the command line. */

enum FileType { FILE_INVALID, FILE_DATA, FILE_DIFF };

struct InvalidFile
{
    const char      *error;         /* human-readable error message */
};

struct DataFile
{
    uint8_t         digest[DS];
};

struct DiffFile
{
    uint8_t         digest1[DS];    /* input file digest */
    uint8_t         digest2[DS];    /* output file digest */
    uint32_t        copied;         /* number of blocks copied */
    uint32_t        added;          /* number of blocks added */
};

struct File
{
    struct File     *next;
    char            *path;
    bool            usable;
    enum FileType   type;
    union {
        struct InvalidFile invalid;
        struct DataFile    data;
        struct DiffFile    diff;
    };
};

/* Identifies the files with the paths given on the command line.
   Human-readable informaiton is printed to `fp' if it is non-NULL.
   The resulting file list is written to `files'. This function returns false
   if some files could not be read; in that case, `files' still contains valid
   data that must be freed with free_files. */
bool identify_files(const char **paths, int npath, FILE *fp,
                    struct File **files);

/* Frees a file list as returned by identify_files. */
void free_files(struct File *files);

#endif /* ndef IDENTIFY_H_INCLUDED */
