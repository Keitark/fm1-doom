#include "fm1_doom_port.h"
#include "doomkeys.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static uint64_t keys;
static unsigned calls;
static uint16_t first, last, center;
static uint64_t read_keys(void *u) { (void)u; return keys; }
static uint32_t ticks(void *u) { (void)u; return 123u; }
static void sleep_ms(void *u, uint32_t ms) { (void)u; (void)ms; }
static int rows(void *u, unsigned y, unsigned count, const uint8_t *p)
{
    (void)u;
    if (count != 4u || y != calls * 4u) return -1;
    if (!calls) first = (uint16_t)((p[0] << 8u) | p[1]);
    if (y <= 120u && y + count > 120u) {
        unsigned offset = (120u - y) * FM1_DOOM_WIDTH * 2u + 120u * 2u;
        center = (uint16_t)((p[offset] << 8u) | p[offset + 1u]);
    }
    if (y + count == FM1_DOOM_HEIGHT) {
        unsigned offset = (count * FM1_DOOM_WIDTH - 1u) * 2u;
        last = (uint16_t)((p[offset] << 8u) | p[offset + 1u]);
    }
    ++calls;
    return 0;
}

int main(void)
{
    fm1_doom_port port;
    fm1_doom_io io = {0, read_keys, rows, ticks, sleep_ms};
    uint8_t palette[768] = {0};
    uint8_t screen[FM1_DOOM_SOURCE_WIDTH * FM1_DOOM_SOURCE_HEIGHT] = {0};
    int pressed; uint8_t key;
    CHECK(fm1_doom_port_init(&port, &io) == 0);
    palette[3] = 255; /* index 1: red */
    palette[6 + 1] = 255; /* index 2: green */
    palette[9 + 2] = 255; /* index 3: blue */
    screen[0] = 1;
    screen[100 * 320 + 160] = 2;
    screen[199 * 320 + 319] = 3;
    fm1_doom_palette(&port, palette);
    CHECK(fm1_doom_present(&port, screen) == 0);
    CHECK(calls == 60u && first == 0xf800u && center == 0x07e0u && last == 0x001fu);
    CHECK(!fm1_doom_next_key(&port, &pressed, &key));
    keys = (UINT64_C(1) << 14) | (UINT64_C(1) << 40);
    CHECK(fm1_doom_next_key(&port, &pressed, &key) && pressed && key == KEY_LEFTARROW);
    CHECK(fm1_doom_next_key(&port, &pressed, &key) && pressed && key == KEY_FIRE);
    CHECK(!fm1_doom_next_key(&port, &pressed, &key));
    keys = (UINT64_C(1) << 40);
    CHECK(fm1_doom_next_key(&port, &pressed, &key) && !pressed && key == KEY_LEFTARROW);
    CHECK(!fm1_doom_next_key(&port, &pressed, &key));
    CHECK(fm1_doom_port_init(NULL, &io) == -1);
    puts("FM-1 palette, 240x240 strips, simultaneous key edges and release passed");
    return 0;
}
