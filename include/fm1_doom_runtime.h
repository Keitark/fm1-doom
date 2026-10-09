#ifndef FM1_DOOM_RUNTIME_H
#define FM1_DOOM_RUNTIME_H

#include "fm1_doom_port.h"

/* Bind before doomgeneric_Create. The engine is single-instance and owns its
   state until reset; the IO callbacks must remain valid throughout play. */
int fm1_doom_bind_io(const fm1_doom_io *io);
fm1_doom_port *fm1_doom_active_port(void);

#endif
