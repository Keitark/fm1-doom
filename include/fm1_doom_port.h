#ifndef FM1_DOOM_PORT_H
#define FM1_DOOM_PORT_H

#include <stddef.h>
#include <stdint.h>

#define FM1_DOOM_WIDTH 240u
#define FM1_DOOM_HEIGHT 240u
#ifndef FM1_DOOM_SOURCE_WIDTH
#define FM1_DOOM_SOURCE_WIDTH 320u
#endif
#ifndef FM1_DOOM_SOURCE_HEIGHT
#define FM1_DOOM_SOURCE_HEIGHT 200u
#endif
#define FM1_DOOM_STRIP_ROWS 8u

/* read_keys returns the 41 decoded stock scanner slots. write_rows consumes
   big-endian RGB565 bytes synchronously; it must not retain the strip. */
typedef struct {
    void *context;
    uint64_t (*read_keys)(void *);
    int (*write_rows)(void *, unsigned y, unsigned rows, const uint8_t *pixels);
    uint32_t (*ticks_ms)(void *);
    void (*sleep_ms)(void *, uint32_t);
} fm1_doom_io;

typedef struct {
    fm1_doom_io io;
    uint16_t palette[256];
    uint64_t reported_keys;
    uint64_t sampled_keys;
    uint64_t pending_keys;
    int menu_visible;
    uint8_t strip[FM1_DOOM_WIDTH * FM1_DOOM_STRIP_ROWS * 2u];
} fm1_doom_port;

int fm1_doom_port_init(fm1_doom_port *port, const fm1_doom_io *io);
void fm1_doom_palette(fm1_doom_port *port, const uint8_t rgb[768]);
int fm1_doom_present(fm1_doom_port *port, const uint8_t indexed[FM1_DOOM_SOURCE_WIDTH * FM1_DOOM_SOURCE_HEIGHT]);
/* Returns one Doom key edge per call. Re-polls only after all prior edges were
   delivered, so simultaneous presses and releases are not lost. */
int fm1_doom_next_key(fm1_doom_port *port, int *pressed, uint8_t *key);

#endif
