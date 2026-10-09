#include "fm1_doom_runtime.h"
#include "fm1_doom_wad_file.h"
#include "doomgeneric.h"
#include "z_zone.h"
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t frame[FM1_DOOM_WIDTH * FM1_DOOM_HEIGHT];
static unsigned strips;
static HMODULE zlib_module;
static uint64_t scripted_keys;
static uint8_t *archive_image;
static uint8_t *archive_cache;
static fm1_fmd_t archive;
static LARGE_INTEGER perf_frequency;
static LONGLONG inflate_ticks;
extern int menuactive;
void G_ExitLevel(void);

typedef int (__cdecl *uncompress_fn)(unsigned char *, unsigned long *,
                                      const unsigned char *, unsigned long);
static uncompress_fn host_uncompress;

static int host_inflate(void *unused, const uint8_t *src, size_t src_len,
                        uint8_t *dst, size_t dst_len)
{
    LARGE_INTEGER start, end;
    unsigned long actual = (unsigned long)dst_len;
    (void)unused;
    QueryPerformanceCounter(&start);
    {
        int result = host_uncompress(dst, &actual, src, (unsigned long)src_len);
        QueryPerformanceCounter(&end);
        inflate_ticks += end.QuadPart - start.QuadPart;
        return result == 0 && actual == dst_len ? 0 : -1;
    }
}

static int load_archive(FILE *file, const char *path)
{
    long length;
    uint8_t header[16];
    unsigned block_size;
    if (fseek(file, 0, SEEK_END) || (length = ftell(file)) < 16 ||
        fseek(file, 0, SEEK_SET) || fread(header, 1, sizeof(header), file) != sizeof(header))
        return -1;
    block_size = (unsigned)header[4] | (unsigned)header[5] << 8 |
                 (unsigned)header[6] << 16 | (unsigned)header[7] << 24;
    if (block_size < 1024 || block_size > 32768 || (block_size & (block_size - 1)))
        return -1;
    archive_image = malloc((size_t)length);
    archive_cache = malloc(block_size);
    zlib_module = LoadLibraryA("zlib1.dll");
    host_uncompress = zlib_module ? (uncompress_fn)GetProcAddress(zlib_module, "uncompress") : NULL;
    QueryPerformanceFrequency(&perf_frequency);
    if (!archive_image || !archive_cache || !host_uncompress || fseek(file, 0, SEEK_SET) ||
        fread(archive_image, 1, (size_t)length, file) != (size_t)length ||
        fm1_fmd_open(&archive, archive_image, (size_t)length,
                     archive_cache, block_size, host_inflate, NULL))
        return -1;
    fm1_doom_set_wad_archive(path, &archive);
    return 0;
}

static uint64_t host_keys(void *unused) { (void)unused; return scripted_keys; }
static int host_rows(void *unused, unsigned y, unsigned rows, const uint8_t *pixels)
{
    unsigned i;
    (void)unused;
    if (rows > FM1_DOOM_STRIP_ROWS || y + rows > FM1_DOOM_HEIGHT) return -1;
    for (i = 0; i < rows * FM1_DOOM_WIDTH; ++i)
        frame[y * FM1_DOOM_WIDTH + i] = (uint16_t)((pixels[i * 2u] << 8u) | pixels[i * 2u + 1u]);
    ++strips;
    return 0;
}
static uint32_t host_ticks(void *unused) { (void)unused; return GetTickCount(); }
static void host_sleep(void *unused, uint32_t ms) { (void)unused; Sleep(ms); }

static int save_ppm(const char *path)
{
    FILE *out = fopen(path, "wb");
    unsigned i;
    if (!out) return -1;
    fprintf(out, "P6\n%u %u\n255\n", FM1_DOOM_WIDTH, FM1_DOOM_HEIGHT);
    for (i = 0; i < FM1_DOOM_WIDTH * FM1_DOOM_HEIGHT; ++i) {
        uint16_t p = frame[i];
        uint8_t rgb[3] = {
            (uint8_t)(((p >> 11u) & 31u) * 255u / 31u),
            (uint8_t)(((p >> 5u) & 63u) * 255u / 63u),
            (uint8_t)((p & 31u) * 255u / 31u)
        };
        if (fwrite(rgb, 1, 3, out) != 3) { fclose(out); return -1; }
    }
    return fclose(out) ? -1 : 0;
}

int main(int argc, char **argv)
{
    fm1_doom_io io = {0, host_keys, host_rows, host_ticks, host_sleep};
    char *doom_argv[13];
    long ticks, zone_mb;
    unsigned i;
    const char *menu_script = getenv("FM1_DOOM_HOST_MENU_SCRIPT");
    FILE *wad;
    if ((argc != 4 && argc != 5) || (ticks = strtol(argv[2], NULL, 10)) < 1
        || ticks > 10000 || (zone_mb = argc == 5 ? strtol(argv[4], NULL, 10) : 6) < 1
        || zone_mb > 64) {
        fprintf(stderr, "usage: fm1_doom_host IWAD.WAD ticks output.ppm [zone_MiB]\n");
        return 2;
    }
    wad = fopen(argv[1], "rb");
    if (!wad) { perror(argv[1]); return 2; }
    if (menu_script && strcmp(menu_script, "open") && strcmp(menu_script, "options")
        && strcmp(menu_script, "detail")
        && strcmp(menu_script, "restart") && strcmp(menu_script, "resume")) {
        fprintf(stderr, "FM1_DOOM_HOST_MENU_SCRIPT must be open, options, detail, restart, or resume\n");
        fclose(wad);
        return 2;
    }
    {
        const char *setting = getenv("FM1_DOOM_HOST_KEYS");
        if (setting && *setting) {
            char *end;
            scripted_keys = _strtoui64(setting, &end, 0);
            if (*end || (scripted_keys >> 41u)) {
                fprintf(stderr, "FM1_DOOM_HOST_KEYS must be a 41-bit numeric mask\n");
                fclose(wad);
                return 2;
            }
        }
    }
    {
        char magic[4];
        if (fread(magic, 1, 4, wad) != 4) { fclose(wad); return 2; }
        if (memcmp(magic, "FMD1", 4) == 0 && load_archive(wad, argv[1])) {
            fprintf(stderr, "FMD1 setup failed (check archive and zlib1.dll on PATH)\n");
            fclose(wad);
            return 2;
        }
    }
    fclose(wad);
    if (fm1_doom_bind_io(&io)) return 2;
    doom_argv[0] = argv[0]; doom_argv[1] = "-iwad"; doom_argv[2] = argv[1];
    doom_argv[3] = "-warp"; doom_argv[4] = "1";
    doom_argv[5] = "-skill"; doom_argv[6] = "1";
    doom_argv[7] = "-nomusic"; doom_argv[8] = "-nosfx";
    doom_argv[9] = "-mb"; doom_argv[10] = argc == 5 ? argv[4] : "6";
    doom_argv[11] = "-nogui"; doom_argv[12] = NULL;
    if (getenv("FM1_DOOM_HOST_VIRTUAL_IWAD")) {
        if (!archive_image) {
            fprintf(stderr, "virtual IWAD smoke requires an FMD1 archive\n");
            return 2;
        }
        fm1_doom_set_wad_archive("doom1.wad", &archive);
        doom_argv[2] = "doom1.wad";
    }
    doomgeneric_Create(12, doom_argv);
    for (i = 0; i < (unsigned)ticks; ++i) {
        if (i == 20 && getenv("FM1_DOOM_HOST_EXIT_TEST")) G_ExitLevel();
        if (menu_script) {
            scripted_keys = i >= 4 && i < 15 ? UINT64_C(1) << 19 : 0;
            if (strcmp(menu_script, "open")) {
                if (i >= 22 && i < 33 && (!strcmp(menu_script, "options")
                    || !strcmp(menu_script, "detail")))
                    scripted_keys = UINT64_C(1) << 16;
                if (i >= 40 && i < 51 && (!strcmp(menu_script, "options")
                    || !strcmp(menu_script, "detail")
                    || !strcmp(menu_script, "restart")))
                    scripted_keys = UINT64_C(1) << 22;
                if (i >= 58 && i < 69 && !strcmp(menu_script, "detail"))
                    scripted_keys = UINT64_C(1) << 40;
                if (i >= 22 && i < 33 && !strcmp(menu_script, "restart"))
                    scripted_keys = UINT64_C(1) << 22;
                if (i >= 22 && i < 33 && !strcmp(menu_script, "resume"))
                    scripted_keys = UINT64_C(1) << 19;
            }
        }
        doomgeneric_Tick();
    }
    if (menu_script && (menuactive != (!strcmp(menu_script, "open")
                                 || !strcmp(menu_script, "options")
                                 || !strcmp(menu_script, "detail")))) {
        fprintf(stderr, "Menu script did not reach the expected open/closed state\n");
        return 1;
    }
    if (!strips || save_ppm(argv[3])) return 1;
    printf("Rendered %u LCD strips over %ld engine ticks to %s\n", strips, ticks, argv[3]);
    printf("Purgeable/free Doom zone bytes after run: %d\n", Z_FreeMemory());
    printf("Menu active after run: %d\n", menuactive);
    if (archive_image) {
        printf("FMD1 decode: %u blocks, %llu compressed bytes, %.3f ms host inflate time\n",
               archive.block_decodes, (unsigned long long)archive.compressed_bytes_decoded,
               perf_frequency.QuadPart ? 1000.0 * (double)inflate_ticks / (double)perf_frequency.QuadPart : 0.0);
    }
    {
        const char *dump_path = getenv("FM1_DOOM_HEAP_DUMP");
        if (dump_path && *dump_path) {
            FILE *dump = fopen(dump_path, "wb");
            if (!dump) return 1;
            Z_FileDumpHeap(dump);
            if (fclose(dump)) return 1;
        }
    }
    return 0;
}
