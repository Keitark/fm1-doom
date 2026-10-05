#ifndef FM1_DOOM_WAD_FILE_H
#define FM1_DOOM_WAD_FILE_H

#include "fm1_doom_archive.h"

/* Register the archive behind a filename ending in .wad. The path must also
 * satisfy Doom's IWAD discovery (a real host file or target filesystem shim).
 * Archive and path remain owned by the caller until Doom exits. */
void fm1_doom_set_wad_archive(const char *path, fm1_fmd_t *archive);

#endif
