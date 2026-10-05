#ifndef FM1_DOOM_ARCHIVE_H
#define FM1_DOOM_ARCHIVE_H

#include <stddef.h>
#include <stdint.h>

/* Return 0 only after decoding exactly dst_len bytes. An FM-1 binding may
 * call SDK zliblite uncompress; source must remain accessible during decode. */
typedef int (*fm1_fmd_inflate_fn)(void *context, const uint8_t *src,
                                   size_t src_len, uint8_t *dst, size_t dst_len);

typedef struct {
    const uint8_t *image;
    size_t image_len;
    uint8_t *cache;
    size_t cache_len;
    uint32_t block_size;
    uint32_t wad_size;
    uint32_t block_count;
    uint32_t cached_block;
    fm1_fmd_inflate_fn inflate;
    void *context;
} fm1_fmd_t;

/* Image is a contiguous, memory-mapped FMD1 archive. cache_len must be at
 * least the archive's block size. Returns 0 for a validated header/index. */
int fm1_fmd_open(fm1_fmd_t *fmd, const uint8_t *image, size_t image_len,
                 uint8_t *cache, size_t cache_len,
                 fm1_fmd_inflate_fn inflate, void *context);

/* Read virtual WAD bytes. Returns the number copied, or 0 on error/EOF.
 * A decompression/CRC failure invalidates the cache and returns 0. */
size_t fm1_fmd_read(fm1_fmd_t *fmd, uint32_t offset, void *destination,
                    size_t length);

#endif
