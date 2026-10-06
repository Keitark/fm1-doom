/* Fixed target memory for the 160x100 E1M1 profile. No heap allocation
 * or peripheral access is performed here. Link placement is the first gate. */
#include "fm1_doom_port.h"
#include "fm1_doom_memory.h"
#include "i_system.h"

#if FM1_DOOM_SOURCE_WIDTH != 160 || FM1_DOOM_SOURCE_HEIGHT != 100
#error "The fixed FM-1 zone is sized only for the 160x100 direct-E1M1 build"
#endif

enum { FM1_DOOM_ZONE_BYTES = 294 * 1024, FM1_DOOM_CACHE_BYTES = 4096 };
static struct {
    byte doom_zone[FM1_DOOM_ZONE_BYTES];
    int16_t reverb[1024];
} memory __attribute__((aligned(16)));
static uint8_t archive_cache[FM1_DOOM_CACHE_BYTES] __attribute__((aligned(16)));

byte *I_ZoneBase(int *size)
{
    if (!size) return 0;
    *size = sizeof(memory.doom_zone);
    return memory.doom_zone;
}

uint8_t *fm1_doom_archive_cache(size_t *length)
{
    if (length) *length = sizeof(archive_cache);
    return archive_cache;
}

int16_t *fm1_doom_reverb_buffer(size_t *samples)
{
    if (samples) *samples = sizeof(memory.reverb) / sizeof(memory.reverb[0]);
    return memory.reverb;
}
