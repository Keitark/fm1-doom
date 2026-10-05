#ifndef FM1_DOOM_WAD_FILE_H
#define FM1_DOOM_WAD_FILE_H

#include "fm1_doom_archive.h"

/* Register the archive behind a filename ending in .wad. The generated
 * low-memory engine recognizes this registered path without filesystem I/O.
 * Archive and path remain owned by the caller until Doom exits. */
void fm1_doom_set_wad_archive(const char *path, fm1_fmd_t *archive);
int fm1_doom_archive_matches(const char *path);

#endif
