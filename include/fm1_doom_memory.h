#ifndef FM1_DOOM_MEMORY_H
#define FM1_DOOM_MEMORY_H

#include <stddef.h>
#include <stdint.h>

/* The direct-E1M1 FMD1 profile uses 4 KiB blocks. Caller owns the returned
 * cache for the lifetime of the archive and must not use it concurrently. */
uint8_t *fm1_doom_archive_cache(size_t *length);

#endif
