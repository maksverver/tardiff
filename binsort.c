#include "common.h"
#include "binsort.h"
#include <sys/mman.h>

struct BinSort
{
    size_t block_size;      /* Size of each block */
    size_t cache_size;      /* Cache size (in number of blocks) */

    size_t nstored;         /* Number of blocks stored in total */
    size_t ncached;         /* Number of blocks cached */

    char   *cache;          /* Block cache (size: cache_size*block_size) */

    int    nfiles;
    size_t sizes[64];       /* Temp file sizes (in number of blocks) */
    FILE   *files[64];      /* Temp file pointers */

    void   *data;           /* mmap()ed data */
};

static void merge_files(BinSort *bs)
{
    FILE *f1, *f2, *fp;
    char *data1, *data2;
    size_t size1, size2, nx, w;

    assert(bs->nfiles >= 2);
    assert(bs->ncached == 0);

    w = bs->block_size;
    data1 = bs->cache - w;
    data2 = bs->cache;

    /* Open target file fp and rewind input files f1 and f2 */
    fp = tmpfile();
    assert(fp != NULL);
    f1 = bs->files[bs->nfiles - 2];
    f2 = bs->files[bs->nfiles - 1];
    size1 = bs->sizes[bs->nfiles - 2];
    size2 = bs->sizes[bs->nfiles - 1];
    assert(size1 > 0);
    assert(size2 > 0);
    rewind(f1);
    rewind(f2);

    /* Read initial blocks from both files */
    nx = fread(data1, w, 1, f1);
    assert(nx == 1);
    nx = fread(data2, w, 1, f2);
    assert(nx == 1);

    while (size1 > 0 || size2 > 0)
    {
        if (size2 == 0 || (size1 > 0 && memcmp(data1, data2, w) <= 0))
        {   /* Copy a block from file 1 */
            nx = fwrite(data1, w, 1, fp);
            assert(nx == 1);
            if(--size1 > 0)
            {
                nx = fread(data1, w, 1, f1);
                assert(nx == 1);
            }
        }
        else
        {   /* Copy a block from file 2 */
            nx = fwrite(data2, w, 1, fp);
            assert(nx == 1);
            if (--size2 > 0)
            {
                nx = fread(data2, w, 1, f2);
                assert(nx == 1);
            }
        }
    }

    fclose(f1);
    fclose(f2);

    bs->files[bs->nfiles - 2] = fp;
    bs->sizes[bs->nfiles - 2] += bs->sizes[bs->nfiles - 1];
    bs->nfiles -= 1;
}

static void sort_cached(BinSort *bs, size_t i, size_t j)
{
    size_t k, l, w;

    for (;;)
    {
        if (j - i <= 1) break;

        w = bs->block_size;
        k = i + 1, l = j;       /* pivot=i; unsorted window: [k:l) */
        while (k < l)
        {
            if (memcmp(bs->cache + i*w, bs->cache + k*w, w) >= 0)
            {
                /* Pivot is larger or equal; keep element on the left side */
                k += 1;
            }
            else
            {
                /* Pivot is smaller; swap element to the right. */
                l -= 1;
                memcpy(bs->cache - 1*w, bs->cache + k*w, w);
                memcpy(bs->cache + k*w, bs->cache + l*w, w);
                memcpy(bs->cache + l*w, bs->cache - 1*w, w);
            }
        }

        if (k > i + 1)
        {
            /* Move pivot to the middle */
            memcpy(bs->cache - 1*w,     bs->cache + i*w,     w);
            memcpy(bs->cache + i*w,     bs->cache + (k-1)*w, w);
            memcpy(bs->cache + (k-1)*w, bs->cache - 1*w,     w);
        }

        /* Unsorted ranges: [i:k-1) [k:j) */
        if (k - 1 - i < j - k)
        {
            sort_cached(bs, i, k - 1);
            i = k;
        }
        else
        {
            sort_cached(bs, k, j);
            j = k - 1;
        }
    }
}

static void purge_cached(BinSort *bs)
{
    FILE *fp;
    size_t nwritten;

    if (bs->ncached == 0) return;

    /* Sort cached blocks */
    sort_cached(bs, 0, bs->ncached);

    /* Write cached contents to a temporary file. */
    fp = tmpfile();
    assert(fp != NULL);
    nwritten = fwrite(bs->cache, bs->block_size, bs->ncached, fp);
    assert(nwritten == bs->ncached);

    assert(bs->nfiles < 64);
    bs->files[bs->nfiles] = fp;
    bs->sizes[bs->nfiles] = bs->ncached;

    bs->ncached = 0;
    bs->nfiles += 1;

    /* Merge files of equal-ish length */
    while ( bs->nfiles >= 2 &&
            bs->sizes[bs->nfiles - 1] >= bs->sizes[bs->nfiles - 2] )
    {
        merge_files(bs);
    }
}

BinSort *BinSort_create(size_t block_size, size_t cache_size)
{
    BinSort *bs;

    assert(block_size > 0 && cache_size > 0);
    /* FIXME: check for overflow in malloc below */
    bs = malloc(sizeof(BinSort) + (1 + block_size)*cache_size);
    if (bs == NULL) return NULL;
    bs->block_size = block_size;
    bs->cache_size = cache_size;
    bs->nstored    = 0;
    bs->ncached    = 0;
    bs->cache      = (char*)bs + sizeof(BinSort) + block_size;
    bs->nfiles     = 0;
    bs->data       = NULL;
    return bs;
}

void BinSort_add(BinSort *bs, const void *data)
{
    assert(bs->data == NULL);
    if (bs->ncached == bs->cache_size) purge_cached(bs);
    memcpy(bs->cache + bs->block_size * bs->ncached, data, bs->block_size);
    bs->nstored += 1;
    bs->ncached += 1;
}

size_t BinSort_size(BinSort *bs)
{
    return bs->nstored;
}

void BinSort_collect(BinSort *bs, void *data)
{
    size_t nread;

    if (bs->nstored == 0) return;

    purge_cached(bs);
    while (bs->nfiles > 1) merge_files(bs);

    /* Read file contents into memory. */
    rewind(bs->files[0]);
    nread = fread(data, bs->block_size, bs->nstored, bs->files[0]);
    assert(nread == bs->nstored);
}

void *BinSort_mmap(BinSort *bs)
{
    if (bs->nstored == 0) return NULL;

    purge_cached(bs);
    while (bs->nfiles > 1) merge_files(bs);
    assert(bs->nstored == bs->sizes[0]);

    /* Flushing is necessary here, to ensure no data is buffered in user-space,
       in which case mmap() would not be able to map the entire file into
       memory. */
    fflush(NULL);

    bs->data = mmap(NULL, bs->nstored * bs->block_size, PROT_READ, MAP_SHARED,
                    fileno(bs->files[0]), 0);
    assert(bs->data != NULL);
    return bs->data;
}

void BinSort_destroy(BinSort *bs)
{
    int n;
    if (bs->data != NULL) munmap(bs->data, bs->nstored * bs->block_size);
    for (n = 0; n < bs->nfiles; ++n) fclose(bs->files[n]);
    free(bs);
}
