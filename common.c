#include "common.h"
#include <zlib.h>

typedef struct FileStream
{
    InputStream is;
    gzFile file;
} FileStream;

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
    fs->is.seek  = (void*)FS_seek;
    fs->is.close = (void*)FS_close;
    fs->file = file;

    return &fs->is;
}


static size_t stdin_read(InputStream *is, void *buf, size_t len)
{
    (void)is;
    return fread(buf, 1, len, stdin);
}

static bool stdin_seek(InputStream *is, off_t pos)
{   /* seeking not supported */
    (void)is;
    (void)pos;
    return false;
}

static void stdin_close(InputStream *is)
{   /* keep stdin open until process terminates. */
    (void)is;
}

InputStream *OpenStdinInputStream()
{
    static InputStream is = { stdin_read, stdin_seek, stdin_close };
    return &is;
}

void redirect_stdout(const char *path)
{
    FILE *fp;

    fp = freopen(path, "ab", stdout);
    if (fp == NULL)
    {
        fprintf(stderr, "Could not open %s for writing!\n", path);
        exit(1);
    }

    if (ftello(fp) != 0)
    {
        fprintf(stderr, "Output file %s exists! (Not overwritten.)\n", path);
        exit(1);
    }
}
