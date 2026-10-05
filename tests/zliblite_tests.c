#include "fm1_doom_archive.h"
#include "net/zliblite/zlib.h"
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int fm1_fmd_zliblite_inflate(void *, const uint8_t *, size_t, uint8_t *, size_t);
typedef int (__cdecl *init_fn)(z_streamp, int, const char *, int);
typedef int (__cdecl *inflate_fn)(z_streamp, int);
typedef int (__cdecl *end_fn)(z_streamp);
typedef int (__cdecl *compress_fn)(Bytef *, uLongf *, const Bytef *, uLong, int);
static init_fn real_init;
static inflate_fn real_inflate;
static end_fn real_end;

/* Forward the exact target binding to desktop zlib. Check the callbacks that
   the pinned SDK requires, and preserve its single-call Z_FINISH contract. */
int ZEXPORT inflateInit2_(z_streamp stream, int bits, const char *version, int size)
{
    if (!stream->zalloc || !stream->zfree || bits != 15) return Z_STREAM_ERROR;
    return real_init(stream, bits, version, size);
}
int ZEXPORT inflate(z_streamp stream, int flush)
{
    if (flush != Z_FINISH) return Z_STREAM_ERROR;
    return real_inflate(stream, flush);
}
int ZEXPORT inflateEnd(z_streamp stream) { return real_end(stream); }

static int archive_check(const char *path)
{
    FILE *file = fopen(path, "rb");
    uint8_t *image, *result, cache[4096];
    fm1_fmd_t archive;
    long length;
    CHECK(file && !fseek(file, 0, SEEK_END));
    length = ftell(file);
    CHECK(length > 16 && !fseek(file, 0, SEEK_SET));
    image = malloc((size_t)length);
    CHECK(image && fread(image, 1, (size_t)length, file) == (size_t)length);
    CHECK(!fclose(file));
    CHECK(!fm1_fmd_open(&archive, image, (size_t)length, cache, sizeof(cache),
                       fm1_fmd_zliblite_inflate, NULL));
    result = malloc(archive.wad_size);
    CHECK(result && fm1_fmd_read(&archive, 0, result, archive.wad_size) == archive.wad_size);
    CHECK(!memcmp(result, "IWAD", 4) && archive.block_decodes == archive.block_count);
    printf("Validated %u complete archive blocks with bounded inflater\n", archive.block_count);
    free(result);
    free(image);
    return 0;
}

int main(int argc, char **argv)
{
    HMODULE dll = LoadLibraryA("zlib1.dll");
    compress_fn compress;
    uint8_t source[4096], output[4096], encoded[8192];
    unsigned pattern, i;
    CHECK(dll);
    real_init = (init_fn)GetProcAddress(dll, "inflateInit2_");
    real_inflate = (inflate_fn)GetProcAddress(dll, "inflate");
    real_end = (end_fn)GetProcAddress(dll, "inflateEnd");
    compress = (compress_fn)GetProcAddress(dll, "compress2");
    CHECK(real_init && real_inflate && real_end && compress);
    for (pattern = 0; pattern < 3; ++pattern) {
        uLongf size = sizeof(encoded);
        uint32_t random = 12345;
        for (i = 0; i < sizeof(source); ++i) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            source[i] = pattern == 0 ? 'A' : pattern == 1 ? (uint8_t)i : (uint8_t)random;
        }
        CHECK(compress(encoded, &size, source, sizeof(source), 9) == Z_OK);
        CHECK(!fm1_fmd_zliblite_inflate(NULL, encoded, size, output, sizeof(output)));
        CHECK(!memcmp(source, output, sizeof(source)));
        CHECK(fm1_fmd_zliblite_inflate(NULL, encoded, size, output, sizeof(output) - 1) != 0);
        CHECK(fm1_fmd_zliblite_inflate(NULL, encoded, size - 1, output, sizeof(output)) != 0);
        encoded[size] = 0;
        CHECK(fm1_fmd_zliblite_inflate(NULL, encoded, size + 1, output, sizeof(output)) != 0);
        encoded[size - 1] ^= 1;
        CHECK(fm1_fmd_zliblite_inflate(NULL, encoded, size, output, sizeof(output)) != 0);
        encoded[size - 1] ^= 1;
        CHECK(!fm1_fmd_zliblite_inflate(NULL, encoded, size, output, sizeof(output)));
    }
    CHECK(fm1_fmd_zliblite_inflate(NULL, NULL, 1, output, 1) != 0);
    CHECK(fm1_fmd_zliblite_inflate(NULL, encoded, SIZE_MAX, output, 1) != 0);
    if (argc > 1) CHECK(!archive_check(argv[1]));
    puts("Bounded allocation, complete streams, corruption and failure recovery passed");
    CHECK(FreeLibrary(dll));
    return 0;
}
