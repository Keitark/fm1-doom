#include "fm1_doom_target_io.h"

uint64_t fm1_doom_debounce_keys(fm1_doom_key_filter *filter,
                                uint64_t sampled, uint32_t now_ms)
{
    if (!filter) return 0;
    if (sampled != filter->candidate) {
        filter->candidate = sampled;
        filter->changed_ms = now_ms;
    } else if ((uint32_t)(now_ms - filter->changed_ms) >= 10u) {
        filter->stable = sampled;
    }
    return filter->stable;
}

static int command(fm1_doom_lcd_write_fn write, uint8_t cmd,
                   const uint8_t *parameters, size_t length)
{
    if (write(0, &cmd, 1)) return -1;
    if (length && write(1, parameters, length)) return -1;
    return 0;
}

int fm1_doom_lcd_rows(fm1_doom_lcd_write_fn write,
                      unsigned y, unsigned rows, const uint8_t *pixels)
{
    const uint8_t columns[4] = {0, 0, 0, 239};
    uint8_t bounds[4];
    unsigned first, last;
    if (!write || !pixels || !rows || rows > 8u || y > 240u - rows
        || y % 8u || rows != 8u) return -1;
    if (y == 0 && command(write, 0x2a, columns, sizeof(columns))) return -1;
    /* The qualified NES stock sequence displays RAM rows 0..239. */
    first = y;
    last = first + rows - 1u;
    bounds[0] = (uint8_t)(first >> 8); bounds[1] = (uint8_t)first;
    bounds[2] = (uint8_t)(last >> 8); bounds[3] = (uint8_t)last;
    if (command(write, 0x2b, bounds, sizeof(bounds)) || command(write, 0x2c, 0, 0)
        || write(1, pixels, rows * 240u * 2u)) return -1;
    return 0;
}
