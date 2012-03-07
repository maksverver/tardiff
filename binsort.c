#include "common.h"
#include "binsort.h"
#include <sys/mman.h>

/* Defines how many files to merge at the same time. */
#define NWAY_MERGE 16

/* Maximum number of open files at a time. Since the number of blocks in a
   patch is limited to 32-bits numbers, this is more than sufficient: */
#define NFILES (32*NWAY_MERGE)

struct BinSort
{
    size_t   block_size;        /* Size of each block */
    size_t   cache_size;        /* Cache size (in number of blocks) */
    compar_t compar;            /* Block comparison funciton */

    size_t   nstored;           /* Number of blocks stored in total */
    size_t   ncached;           /* Number of blocks cached */

    char     *cache;            /* Block cache (size: cache_size*block_size) */

    int      nfiles;            /* Number of stored files */
    size_t   sizes[NFILES];     /* Temp file sizes (in number of blocks) */
    FILE     *files[NFILES];    /* Temp file pointers */

    void     *data;             /* mmap()ed data */
};

/* Insert block `i' into the sorted sequence of length `n'; indices of existing
   blocks are stored in `order', in decreasing order of block data, so that the
   index of the least block is stored at the end. */
static void insert_sorted(BinSort *bs, int *order, int n, int i)
{
    int m, lo = 0, hi = n;

    /* binary search for position of insertion */
    while (lo < hi)
    {
        int mid = (lo + hi)/2;
        int j = order[mid];
        if (bs->compar(bs->cache + j*bs->block_size,
                       bs->cache + i*bs->block_size) >= 0)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid;
        }
    }
    for (m = n; m > lo; --m) order[m] = order[m - 1];
    order[lo] = i;
}

/* Merges the last `k' files into one. Cache must be empty, because its memory
   is used to store blocks during the merge. */
static void merge_files(BinSort *bs, int k)
{
    FILE **files  = bs->files + bs->nfiles - k;
    size_t *sizes = bs->sizes + bs->nfiles - k;

    FILE *dst;
    size_t pos[NWAY_MERGE];
    int order[NWAY_MERGE], n;
    int i, x;

    assert(k <= NWAY_MERGE && k <= bs->nfiles);
    assert(bs->ncached == 0);

    /* Create destination file: */
    dst = tmpfile();
    assert(dst != NULL);

    n = 0;
    for (i = 0; i < k; ++i)
    {
        /* Read first block */
        rewind(files[i]);
        assert(sizes[i] > 0);
        x = fread(bs->cache + i*bs->block_size, bs->block_size, 1, files[i]);
        assert(x == 1);
        pos[i] = 1;
        insert_sorted(bs, order, n++, i);
    }
    while (n > 0)
    {
        /* Write out smallest block: */
        i = order[--n];
        x = fwrite(bs->cache + i*bs->block_size, bs->block_size, 1, dst);
        assert(x == 1);
        if (pos[i] < sizes[i])
        {
            /* Read next block: */
            x = fread(bs->cache + i*bs->block_size, bs->block_size, 1, files[i]);
            assert(x == 1);
            ++pos[i];
            insert_sorted(bs, order, n++, i);
        }
        else
        {
            fclose(files[i]);
            files[i] = NULL;
        }
    }

    /* Store merged file: */
    files[0] = dst;
    for (i = 1; i < k; ++i) sizes[0] += sizes[i];
    bs->nfiles -= NWAY_MERGE - 1;
}

/* Flushes all currently cached blocks to a new file on disk.
   Afterwards, some cached files may be merged. */
static void flush_cache(BinSort *bs)
{
    FILE *fp;
    size_t nwritten;

    if (bs->ncached == 0) return;

    /* Sort cached blocks */
    qsort(bs->cache, bs->ncached, bs->block_size, bs->compar);

    /* Write cached contents to a temporary file. */
    fp = tmpfile();
    assert(fp != NULL);
    nwritten = fwrite(bs->cache, bs->block_size, bs->ncached, fp);
    assert(nwritten == bs->ncached);

    assert(bs->nfiles < NFILES);
    bs->files[bs->nfiles] = fp;
    bs->sizes[bs->nfiles] = bs->ncached;

    bs->ncached = 0;
    bs->nfiles += 1;

    /* Merge equal-length files: */
    while ( bs->nfiles >= NWAY_MERGE &&
            bs->sizes[bs->nfiles - 1] == bs->sizes[bs->nfiles - NWAY_MERGE] )
    {
        merge_files(bs, NWAY_MERGE);
    }
}

/* Merges all data (whether cached or stored on disk) into a single file. */
static void flush_and_merge_all(BinSort *bs)
{
    flush_cache(bs);
    assert(bs->nstored > 0);
    if (bs->nfiles > 1)
    {
        while (bs->nfiles > NWAY_MERGE) merge_files(bs, NWAY_MERGE);
        merge_files(bs, bs->nfiles);
    }
    assert(bs->nstored == bs->sizes[0]);
}

BinSort *BinSort_create(size_t block_size, size_t cache_size, compar_t compar)
{
    BinSort *bs;

    if (cache_size < NWAY_MERGE) cache_size = NWAY_MERGE;
    assert(block_size > 0 && cache_size <= ~sizeof(BinSort)/block_size);
    bs = malloc(sizeof(BinSort) + block_size*cache_size);
    if (bs == NULL) return NULL;
    bs->block_size = block_size;
    bs->cache_size = cache_size;
    bs->compar     = compar;
    bs->nstored    = 0;
    bs->ncached    = 0;
    bs->cache      = (char*)bs + sizeof(BinSort);
    bs->nfiles     = 0;
    bs->data       = NULL;
    return bs;
}

void BinSort_add(BinSort *bs, const void *data)
{
    assert(bs->data == NULL);
    if (bs->ncached == bs->cache_size) flush_cache(bs);
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

    flush_and_merge_all(bs);

    /* Read file contents into memory. */
    rewind(bs->files[0]);
    nread = fread(data, bs->block_size, bs->nstored, bs->files[0]);
    assert(nread == bs->nstored);
}

void *BinSort_mmap(BinSort *bs)
{
    if (bs->nstored == 0) return NULL;

    flush_and_merge_all(bs);

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
