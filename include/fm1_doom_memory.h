#ifndef FM1_DOOM_MEMORY_H
#define FM1_DOOM_MEMORY_H

#include <stddef.h>
#include <stdint.h>

/* The direct-E1M1 FMD1 profile uses 4 KiB blocks. Caller owns the returned
 * cache for the lifetime of the archive and must not use it concurrently. */
uint8_t *fm1_doom_archive_cache(size_t *length);

/* Dedicated synth delay storage partitioned from the fixed engine zone.
 * Bind once before enabling audio; only the audio renderer owns its contents. */
int16_t *fm1_doom_reverb_buffer(size_t *samples);

#endif
