#include "fm1_doom_archive.h"
#include "net/zliblite/zlib.h"
#include <limits.h>
#include <string.h>

/* The pinned SDK requires allocation callbacks. Complete FMD1 blocks fit in
 * one Z_FINISH call, so inflate needs its state, without a sliding window.
 * The SDK state is 7,120 bytes; leave room for alignment in this fixed arena.
 * This binding is used synchronously by the single Doom task. */
static union {
    uint64_t alignment;
    uint8_t bytes[7168];
} inflate_arena;
static int inflate_busy;

static voidpf block_alloc(voidpf opaque, uInt count, uInt size)
{
    size_t *used = opaque;
    size_t bytes, start = (*used + 7u) & ~(size_t)7u;
    if (size && (size_t)count > SIZE_MAX / size) return Z_NULL;
    bytes = (size_t)count * size;
    if (!bytes || start > sizeof(inflate_arena.bytes)
        || bytes > sizeof(inflate_arena.bytes) - start) return Z_NULL;
    *used = start + bytes;
    memset(inflate_arena.bytes + start, 0, bytes);
    return inflate_arena.bytes + start;
}

static void block_free(voidpf opaque, voidpf address)
{
    /* All allocations are released together after inflateEnd. */
    (void)opaque;
    (void)address;
}

int fm1_fmd_zliblite_inflate(void *context, const uint8_t *src, size_t src_len,
                             uint8_t *dst, size_t dst_len)
{
    z_stream stream;
    size_t used = 0;
    int rc, complete;
    (void)context;
    if (!src || !dst || !src_len || !dst_len || inflate_busy
        || src_len > UINT_MAX || dst_len > UINT_MAX) {
        return -1;
    }
    inflate_busy = 1;
    memset(&stream, 0, sizeof(stream));
    stream.next_in = (Bytef *)src;
    stream.avail_in = (uInt)src_len;
    stream.next_out = dst;
    stream.avail_out = (uInt)dst_len;
    stream.zalloc = block_alloc;
    stream.zfree = block_free;
    stream.opaque = &used;
    /* The library supports the archive's standard 15-bit zlib window even
       though the SDK zconf.h advertises a smaller MAX_WBITS constant. */
    rc = inflateInit2(&stream, 15);
    if (rc != Z_OK) {
        inflate_busy = 0;
        return -1;
    }
    rc = inflate(&stream, Z_FINISH);
    complete = rc == Z_STREAM_END && stream.total_out == dst_len
        && stream.avail_in == 0;
    rc = inflateEnd(&stream);
    inflate_busy = 0;
    return complete && rc == Z_OK ? 0 : -1;
}
