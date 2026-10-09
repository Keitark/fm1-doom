#include "fm1_doom_archive.h"

#include <string.h>

#define FMD_HEADER 16u
#define FMD_ENTRY 12u
#define FMD_NO_BLOCK UINT32_MAX

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint32_t block_crc(const uint8_t *data, size_t len)
{
    static const uint32_t nibble[16] = {
        0x00000000u, 0x1db71064u, 0x3b6e20c8u, 0x26d930acu,
        0x76dc4190u, 0x6b6b51f4u, 0x4db26158u, 0x5005713cu,
        0xedb88320u, 0xf00f9344u, 0xd6d6a3e8u, 0xcb61b38cu,
        0x9b64c2b0u, 0x86d3d2d4u, 0xa00ae278u, 0xbdbdf21cu
    };
    uint32_t crc = UINT32_MAX;
    size_t i;
    for (i = 0; i < len; ++i) {
        crc ^= data[i];
        crc = (crc >> 4) ^ nibble[crc & 15u];
        crc = (crc >> 4) ^ nibble[crc & 15u];
    }
    return ~crc;
}

int fm1_fmd_open(fm1_fmd_t *fmd, const uint8_t *image, size_t image_len,
                 uint8_t *cache, size_t cache_len,
                 fm1_fmd_inflate_fn inflate, void *context)
{
    uint32_t block_size, wad_size, count, i;
    size_t cursor;
    if (!fmd || !image || !cache || !inflate || image_len < FMD_HEADER
        || memcmp(image, "FMD1", 4) != 0) {
        return -1;
    }
    block_size = le32(image + 4);
    wad_size = le32(image + 8);
    count = le32(image + 12);
    if (block_size < 1024 || block_size > 32768
        || (block_size & (block_size - 1)) != 0 || cache_len < block_size
        || wad_size < 12 || count != (uint64_t)(wad_size + (uint64_t)block_size - 1) / block_size
        || count > (image_len - FMD_HEADER) / FMD_ENTRY) {
        return -1;
    }
    cursor = FMD_HEADER + (size_t)count * FMD_ENTRY;
    for (i = 0; i < count; ++i) {
        const uint8_t *entry = image + FMD_HEADER + (size_t)i * FMD_ENTRY;
        size_t pos = le32(entry), size = le32(entry + 4);
        if (pos != cursor || size < 1 || size > image_len - cursor) {
            return -1;
        }
        cursor += size;
    }
    if (cursor != image_len) {
        return -1;
    }
    fmd->image = image;
    fmd->image_len = image_len;
    fmd->cache = cache;
    fmd->cache_len = cache_len;
    fmd->block_size = block_size;
    fmd->wad_size = wad_size;
    fmd->block_count = count;
    fmd->cached_block = FMD_NO_BLOCK;
    fmd->block_decodes = 0;
    fmd->compressed_bytes_decoded = 0;
    fmd->inflate = inflate;
    fmd->context = context;
    return 0;
}

size_t fm1_fmd_read(fm1_fmd_t *fmd, uint32_t offset, void *destination,
                    size_t length)
{
    uint8_t *out = destination;
    size_t copied = 0;
    if (!fmd || !destination || offset >= fmd->wad_size) {
        return 0;
    }
    if (length > fmd->wad_size - offset) {
        length = fmd->wad_size - offset;
    }
    while (copied < length) {
        uint32_t block = offset / fmd->block_size;
        size_t within = offset % fmd->block_size;
        size_t raw_len = fmd->wad_size - block * fmd->block_size;
        size_t take;
        if (raw_len > fmd->block_size) {
            raw_len = fmd->block_size;
        }
        if (fmd->cached_block != block) {
            const uint8_t *entry = fmd->image + FMD_HEADER + (size_t)block * FMD_ENTRY;
            size_t pos = le32(entry), size = le32(entry + 4);
            fmd->cached_block = FMD_NO_BLOCK;
            if (fmd->inflate(fmd->context, fmd->image + pos, size,
                             fmd->cache, raw_len) != 0
                || block_crc(fmd->cache, raw_len) != le32(entry + 8)) {
                return 0;
            }
            fmd->cached_block = block;
            ++fmd->block_decodes;
            fmd->compressed_bytes_decoded += size;
        }
        take = raw_len - within;
        if (take > length - copied) {
            take = length - copied;
        }
        memcpy(out + copied, fmd->cache + within, take);
        copied += take;
        offset += (uint32_t)take;
    }
    return copied;
}
