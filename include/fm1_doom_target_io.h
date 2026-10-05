#ifndef FM1_DOOM_TARGET_IO_H
#define FM1_DOOM_TARGET_IO_H

#include <stddef.h>
#include <stdint.h>

typedef int (*fm1_doom_lcd_write_fn)(int data, const uint8_t *bytes, size_t length);

typedef struct {
    uint64_t candidate, stable;
    uint32_t changed_ms;
} fm1_doom_key_filter;

uint64_t fm1_doom_debounce_keys(fm1_doom_key_filter *filter,
                                uint64_t sampled, uint32_t now_ms);

/* Send one big-endian RGB565 strip to the stock 240x240 panel window. */
int fm1_doom_lcd_rows(fm1_doom_lcd_write_fn write,
                      unsigned y, unsigned rows, const uint8_t *pixels);

#endif
