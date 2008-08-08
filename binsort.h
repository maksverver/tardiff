#ifndef BINSORT_H_INCLUDED
#define BINSORT_H_INCLUDED

typedef struct BinSort BinSort;

/* Creates a new bin sort object, with the given block size and cache size
   (the latter is specified in number of blocks).
   The data structure returned must be freed with BinSort_destroy. */
BinSort *BinSort_create(size_t block_size, size_t cache_size);

/* Add a block to be sorted. */
void BinSort_add(BinSort *bs, const void *data);

/* Returns the number of blocks stored. */
size_t BinSort_size(BinSort *bs);

/* Collects all stored blocks into the given array, which must be at least
   block_count*block_size bytes long. */
void BinSort_collect(BinSort *bs, void *data);

/* Collects blocks into a memory mapped file. */
void *BinSort_mmap(BinSort *bs);

/* Destroys the data structure and releases all associated resources. */
void BinSort_destroy(BinSort *bs);

#endif /* ndef BINSORT_H_INCLUDED */
