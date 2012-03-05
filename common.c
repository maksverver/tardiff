#include "common.h"
#include <zlib.h>

typedef struct FileStream
{
    InputStream is;
    gzFile file;
} FileStream;

static bool no_seek(InputStream *is, off_t pos)
{   /* seeking not supported */
    (void)is;
    (void)pos;
    return false;
}

static size_t FS_read(FileStream *fs, void *buf, size_t len)
{
    int res;
    assert((size_t)(int)len == len);
    res = gzread(fs->file, buf, len);
    return (res < 0) ? 0 : (size_t)res;
}

static bool FS_seek(FileStream *fs, off_t pos)
{
    assert((off_t)(z_off_t)pos == pos);
    return gzseek(fs->file, (z_off_t)pos, SEEK_SET) != (z_off_t)-1;
}

static void FS_close(FileStream *fs)
{
    assert(fs->file != NULL);
    gzclose(fs->file);
    fs->file = NULL;
    free(fs);
}

InputStream *OpenFileInputStream(const char *path)
{
    FileStream *fs;
    gzFile file;

    /* Open (possible gzipped) file */
    file = gzopen(path, "rb");
    if (file == NULL) return NULL;

    /* Initialize stream data structure */
    fs = malloc(sizeof(FileStream));
    if (fs == NULL)
    {
        gzclose(file);
        return NULL;
    }
    fs->is.read  = (void*)FS_read;
    fs->is.seek  = gzdirect(file) ? (void*)FS_seek : no_seek;
    fs->is.close = (void*)FS_close;
    fs->file = file;

    return &fs->is;
}


static size_t stdin_read(InputStream *is, void *buf, size_t len)
{
    (void)is;
    return fread(buf, 1, len, stdin);
}

static void stdin_close(InputStream *is)
{   /* keep stdin open until process terminates. */
    (void)is;
}

InputStream *OpenStdinInputStream()
{
    static InputStream is = { stdin_read, no_seek, stdin_close };
    return &is;
}

void redirect_stdout(const char *path)
{
    FILE *fp;

    fp = freopen(path, "ab", stdout);
    if (fp == NULL)
    {
        fprintf(stderr, "Cannot not open '%s' for writing!\n", path);
        exit(1);
    }

    if (ftello(fp) != 0)
    {
        fprintf(stderr, "Output file '%s' exists! (Not overwritten.)\n", path);
        exit(1);
    }
}

void read_data(InputStream *is, void *buf, size_t len)
{
    if (is->read(is, buf, len) != len)
    {
        fprintf(stderr, "Read failed!\n");
        abort();
    }
}

uint32_t parse_uint32(uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) |
           ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] <<  8) |
           ((uint32_t)buf[3] <<  0);
}

uint16_t parse_uint16(uint8_t *buf)
{
    return ((uint16_t)buf[0] <<  8) |
           ((uint16_t)buf[1] <<  0);
}

uint32_t read_uint32(InputStream *is)
{
    uint8_t buf[4];
    read_data(is, buf, 4);
    return parse_uint32(buf);
}

uint16_t read_uint16(InputStream *is)
{
    uint8_t buf[2];
    read_data(is, buf, 2);
    return parse_uint16(buf);
}

void write_data(void *buf, size_t len)
{
    if (fwrite(buf, 1, len, stdout) != len)
    {
        fprintf(stderr, "Write failed!\n");
        abort();
    }
}

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

void write_uint16(uint16_t i)
{
    uint8_t buf[2];
    buf[1] = i&255;
    i >>= 8;
    buf[0] = i&255;
    write_data(buf, 2);
}

void hexstring(char *str, uint8_t *data, size_t size)
{
    static const char *hexdigits = "0123456789abcdef";

    while (size-- > 0)
    {
        *str++ = hexdigits[*data/16];
        *str++ = hexdigits[*data%16];
        ++data;
    }

    *str = '\0';
}
