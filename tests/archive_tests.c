#include "fm1_doom_archive.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static int calls;
static void put32(uint8_t *p, uint32_t n)
{
    p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8);
    p[2] = (uint8_t)(n >> 16); p[3] = (uint8_t)(n >> 24);
}
static int decode(void *context, const uint8_t *src, size_t src_len,
                  uint8_t *dst, size_t dst_len)
{
    (void)context;
    ++calls;
    if (src_len != 1 || (*src != 'A' && *src != 'B')) return -1;
    memset(dst, *src, dst_len);
    return 0;
}
int main(void)
{
    uint8_t image[42] = {0}, cache[1024], result[20];
    fm1_fmd_t fmd;
    memcpy(image, "FMD1", 4);
    put32(image + 4, 1024);
    put32(image + 8, 1036);
    put32(image + 12, 2);
    put32(image + 16, 40); put32(image + 20, 1); put32(image + 24, 0xb737fb1a);
    put32(image + 28, 41); put32(image + 32, 1); put32(image + 36, 0x8393769f);
    image[40] = 'A'; image[41] = 'B';
    CHECK(fm1_fmd_open(&fmd, image, sizeof(image), cache, sizeof(cache), decode, NULL) == 0);
    CHECK(fmd.wad_size == 1036);
    CHECK(fm1_fmd_read(&fmd, 1016, result, sizeof(result)) == sizeof(result));
    CHECK(memcmp(result, "AAAAAAAA", 8) == 0 && memcmp(result + 8, "BBBBBBBBBBBB", 12) == 0);
    CHECK(calls == 2);
    CHECK(fm1_fmd_read(&fmd, 0, result, 5) == 5 && calls == 3);
    CHECK(fm1_fmd_read(&fmd, 2, result, 5) == 5 && calls == 3);
    CHECK(fm1_fmd_read(&fmd, 1036, result, 1) == 0);
    CHECK(fm1_fmd_read(&fmd, 1034, result, 5) == 2);
    CHECK(fm1_fmd_open(&fmd, image, sizeof(image), cache, 1023, decode, NULL) != 0);
    put32(image + 28, 40);
    CHECK(fm1_fmd_open(&fmd, image, sizeof(image), cache, sizeof(cache), decode, NULL) != 0);
    put32(image + 28, 41);
    put32(image + 24, 0);
    CHECK(fm1_fmd_open(&fmd, image, sizeof(image), cache, sizeof(cache), decode, NULL) == 0);
    CHECK(fm1_fmd_read(&fmd, 0, result, 1) == 0);
    puts("FMD1 seek, block crossing, cache reuse, bounds, index and CRC passed");
    return 0;
}
