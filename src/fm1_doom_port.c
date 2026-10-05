#include "fm1_doom_port.h"
#include "doomkeys.h"
#include <string.h>

typedef struct { uint8_t slot, key; } key_binding;
#define PRESENTATION_KEY_BIT (UINT64_C(1) << 24)
#define MUSIC_MODE_KEY_BIT (UINT64_C(1) << 25)

/* Stock-derived slots, not MIDI note numbers. Directions use the leftmost
   F-to-A group; fire/use use the far-right F/G pair. */
static const key_binding bindings[] = {
    {14, KEY_LEFTARROW}, {15, KEY_RSHIFT}, {16, KEY_DOWNARROW},
    {17, KEY_UPARROW}, {18, KEY_RIGHTARROW},
    {19, KEY_ESCAPE},
    {20, KEY_RALT}, {21, '1'}, {22, KEY_ENTER},
    {23, '2'}, {38, KEY_USE}, {40, KEY_FIRE}
};

int fm1_doom_port_init(fm1_doom_port *port, const fm1_doom_io *io)
{
    if (!port || !io || !io->read_keys || !io->write_rows
        || !io->ticks_ms || !io->sleep_ms)
        return -1;
    if (io->strip_buffer && io->strip_buffer_bytes < FM1_DOOM_WIDTH * FM1_DOOM_STRIP_ROWS * 2u)
        return -1;
#ifdef FM1_TARGET_PI32V2
    if (!io->strip_buffer) return -1;
#endif
    memset(port, 0, sizeof(*port));
#if FM1_DOOM_SOURCE_WIDTH == 160 && FM1_DOOM_SOURCE_HEIGHT == 100
    port->coarse_gameplay = 1;
#endif
    port->io = *io;
    port->strip = io->strip_buffer;
#ifndef FM1_TARGET_PI32V2
    if (!port->strip) port->strip = port->host_strip;
#endif
    return 0;
}

void fm1_doom_palette(fm1_doom_port *port, const uint8_t rgb[768])
{
    unsigned i;
    if (!port || !rgb) return;
    for (i = 0; i != 256; ++i) {
        unsigned r = rgb[i * 3u], g = rgb[i * 3u + 1u], b = rgb[i * 3u + 2u];
        port->palette[i] = (uint16_t)(((r & 0xf8u) << 8u)
                        | ((g & 0xfcu) << 3u) | (b >> 3u));
    }
}

int fm1_doom_present(fm1_doom_port *port, const uint8_t indexed[FM1_DOOM_SOURCE_WIDTH * FM1_DOOM_SOURCE_HEIGHT])
{
    unsigned y, row, x, previous_sy = (unsigned)-1;
    if (!port || !indexed || !port->strip) return -1;
    for (y = 0; y < FM1_DOOM_HEIGHT; y += FM1_DOOM_STRIP_ROWS) {
#if FM1_DOOM_SOURCE_WIDTH == 160 && FM1_DOOM_SOURCE_HEIGHT == 100
        if (port->coarse_gameplay && !port->menu_visible && y < 224u) {
            unsigned sy = (y / 8u) * 93u / 28u;
            const uint8_t *src = indexed + sy * FM1_DOOM_SOURCE_WIDTH;
            /* One 30-sample row supplies all eight rows of the LCD strip. */
            for (x = 0; x < 30u; ++x) {
                unsigned sx = x * FM1_DOOM_SOURCE_WIDTH / 30u;
                uint16_t pixel = port->palette[src[sx]];
                unsigned column;
                for (column = 0; column < 8u; ++column) {
                    unsigned offset = (x * 8u + column) * 2u;
                    port->strip[offset] = (uint8_t)(pixel >> 8u);
                    port->strip[offset + 1u] = (uint8_t)pixel;
                }
            }
            for (row = 1; row < FM1_DOOM_STRIP_ROWS; ++row)
                memcpy(port->strip + row * FM1_DOOM_WIDTH * 2u,
                       port->strip, FM1_DOOM_WIDTH * 2u);
            previous_sy = sy;
        } else
#endif
        {
            for (row = 0; row < FM1_DOOM_STRIP_ROWS; ++row) {
                unsigned display_y = y + row;
                unsigned sy = (display_y * FM1_DOOM_SOURCE_HEIGHT) / FM1_DOOM_HEIGHT;
#if FM1_DOOM_SOURCE_WIDTH == 160 && FM1_DOOM_SOURCE_HEIGHT == 100
                if (!port->menu_visible) {
                    /* Keep all 160 source columns and 93 gameplay rows.
                       The final 16 LCD rows contain the seven-row HUD. */
                    sy = display_y < 224u
                       ? display_y * 93u / 224u
                       : 93u + (display_y - 224u) * 7u / 16u;
                }
#endif
                const uint8_t *src = indexed + sy * FM1_DOOM_SOURCE_WIDTH;
                uint8_t *dst = port->strip + row * FM1_DOOM_WIDTH * 2u;
                if (sy == previous_sy) {
                    unsigned previous_row = row ? row - 1u : FM1_DOOM_STRIP_ROWS - 1u;
                    memcpy(dst, port->strip + previous_row * FM1_DOOM_WIDTH * 2u,
                           FM1_DOOM_WIDTH * 2u);
                    continue;
                }
                for (x = 0; x < FM1_DOOM_WIDTH; ++x) {
                    unsigned sx = x * (FM1_DOOM_SOURCE_WIDTH - 1u) / (FM1_DOOM_WIDTH - 1u);
                    uint16_t pixel = port->palette[src[sx]];
                    dst[x * 2u] = (uint8_t)(pixel >> 8u);
                    dst[x * 2u + 1u] = (uint8_t)pixel;
                }
                previous_sy = sy;
            }
        }
        if (port->io.write_rows(port->io.context, y, FM1_DOOM_STRIP_ROWS, port->strip))
            return -1;
    }
    return 0;
}

int fm1_doom_next_key(fm1_doom_port *port, int *pressed, uint8_t *key)
{
    unsigned i;
    uint64_t mapped = PRESENTATION_KEY_BIT | MUSIC_MODE_KEY_BIT;
    if (!port || !pressed || !key) return 0;
    for (i = 0; i < sizeof(bindings) / sizeof(bindings[0]); ++i)
        mapped |= UINT64_C(1) << bindings[i].slot;
    if (!port->pending_keys) {
        port->sampled_keys = port->io.read_keys(port->io.context) & mapped;
        port->pending_keys = port->sampled_keys ^ port->reported_keys;
    }
    if (port->pending_keys & PRESENTATION_KEY_BIT) {
        port->pending_keys &= ~PRESENTATION_KEY_BIT;
        if (port->sampled_keys & PRESENTATION_KEY_BIT) {
            port->reported_keys |= PRESENTATION_KEY_BIT;
            port->coarse_gameplay ^= 1u;
        } else {
            port->reported_keys &= ~PRESENTATION_KEY_BIT;
        }
    }
    if (port->pending_keys & MUSIC_MODE_KEY_BIT) {
        port->pending_keys &= ~MUSIC_MODE_KEY_BIT;
        if (port->sampled_keys & MUSIC_MODE_KEY_BIT) {
            port->reported_keys |= MUSIC_MODE_KEY_BIT;
            if (port->io.toggle_music_mode)
                port->io.toggle_music_mode(port->io.context);
        } else {
            port->reported_keys &= ~MUSIC_MODE_KEY_BIT;
        }
    }
    if (!port->pending_keys) return 0;
    for (i = 0; i < sizeof(bindings) / sizeof(bindings[0]); ++i) {
        uint64_t bit = UINT64_C(1) << bindings[i].slot;
        if (port->pending_keys & bit) {
            port->pending_keys &= ~bit;
            if (port->sampled_keys & bit) port->reported_keys |= bit;
            else port->reported_keys &= ~bit;
            *pressed = (port->sampled_keys & bit) != 0;
            *key = bindings[i].key;
            return 1;
        }
    }
    return 0;
}
