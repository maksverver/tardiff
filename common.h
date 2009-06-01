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

/* Redirects standard output to a file at the given path, or aborts if the file
   cannot be opened, or if it exists and is not empty (in case the file will be
   closed leaving the contents intact). */
void redirect_stdout(const char *path);

/* Reads data from the given input stream into a buffer or aborts on failure. */
void read_data(InputStream *is, void *buf, size_t len);

/* Interprets the first four bytes in `buf' as a 32-bit big-endian integer. */
uint32_t parse_uint32(uint8_t *buf);

/* Interprets the first two bytes in `buf' as a 16-bit big-endian integer. */
uint16_t parse_uint16(uint8_t *buf);

/* Reads a big-endian 32-bit unsigned integer or aborts. */
uint32_t read_uint32(InputStream *is);

/* Reads a big-endian 16-bit unsigned integer or aborts. */
uint16_t read_uint16(InputStream *is);

/* Writes data from the given buffer to standard output or aborts on failure. */
void write_data(void *buf, size_t len);

/* Writes a big-endian 32-bit unsigned integer to standard output or aborts. */
void write_uint32(uint32_t i);

/* Writes a big-endian 16-bit unsigned integer to standard output or aborts. */
void write_uint16(uint16_t i);

/* Write the hexidecimal representation of the `size` bytes pointed to by `data`
   to the buffer `str` which must have room for at least 2*size + 1 bytes. */
void hexstring(char *str, uint8_t *data, size_t size);

#endif /* ndef COMMON_H_INCLUDED */
