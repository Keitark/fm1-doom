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
    /* Optional persistent synchronous wire buffer supplied by the LCD owner. */
    uint8_t *strip_buffer;
    size_t strip_buffer_bytes;
    /* Optional sound-owner callback for E4 (slot 25), on press only. */
    void (*toggle_music_mode)(void *);
} fm1_doom_io;

typedef struct {
    fm1_doom_io io;
    uint16_t palette[256];
    /* 160x100 profile: 1 selects 8x8 LCD blocks; menus always stay detailed. */
    uint8_t coarse_gameplay;
    uint64_t reported_keys;
    uint64_t sampled_keys;
    uint64_t pending_keys;
    int menu_visible;
    uint8_t *strip;
#ifndef FM1_TARGET_PI32V2
    uint8_t host_strip[FM1_DOOM_WIDTH * FM1_DOOM_STRIP_ROWS * 2u];
#endif
} fm1_doom_port;

int fm1_doom_port_init(fm1_doom_port *port, const fm1_doom_io *io);
void fm1_doom_palette(fm1_doom_port *port, const uint8_t rgb[768]);
int fm1_doom_present(fm1_doom_port *port, const uint8_t indexed[FM1_DOOM_SOURCE_WIDTH * FM1_DOOM_SOURCE_HEIGHT]);
/* Returns one Doom key edge per call. Slot 24 toggles gameplay presentation;
   slot 25 toggles music through its owner callback. Both act only on press
   and never become engine events. Re-polls only after all prior
   edges were delivered, preserving simultaneous ordinary key edges. */
int fm1_doom_next_key(fm1_doom_port *port, int *pressed, uint8_t *key);

#endif
