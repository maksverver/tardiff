#ifndef COMMON_H_INCLUDED
#define COMMON_H_INCLUDED

#define _FILE_OFFSET_BITS 64

#include <assert.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <openssl/md5.h>

#define BS 512          /* block size (512 bytes for TAR) */
#define DS 16           /* digest size (16 bytes for MD5) */
#define HT 1000001      /* hash table index size (entries) */
#define NC 32767        /* max. number of blocks to copy per instruction */
#define NA 2048         /* max. number of blocks to append per instruction */

#define MAGIC_LEN 8
#define MAGIC_STR "tardiff0"

typedef struct InputStream
{
    size_t ( *read  )(struct InputStream *is, void *buf, size_t len);
    bool   ( *seek  )(struct InputStream *is, off_t pos);
    void   ( *close )(struct InputStream *is);
} InputStream;

InputStream *OpenStdinInputStream();
InputStream *OpenFileInputStream(const char *path);

void redirect_stdout(const char *path);

#endif /* ndef COMMON_H_INCLUDED */
