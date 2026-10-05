#include "fm1_doom_port.h"
#include "doomkeys.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static uint64_t keys;
static unsigned calls;
static unsigned music_toggles;
static void toggle_music(void *u) { ++*(unsigned *)u; }
static uint16_t first, last, center, block_edge, next_block;
static uint64_t read_keys(void *u) { (void)u; return keys; }
static uint32_t ticks(void *u) { (void)u; return 123u; }
static void sleep_ms(void *u, uint32_t ms) { (void)u; (void)ms; }
static int rows(void *u, unsigned y, unsigned count, const uint8_t *p)
{
    (void)u;
    if (count != FM1_DOOM_STRIP_ROWS || y != calls * FM1_DOOM_STRIP_ROWS) return -1;
    if (!calls) {
        first = (uint16_t)((p[0] << 8u) | p[1]);
        block_edge = (uint16_t)((p[14] << 8u) | p[15]);
        next_block = (uint16_t)((p[16] << 8u) | p[17]);
    }
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

typedef struct {
    const uint8_t *screen, *palette;
    unsigned calls;
    int menu_visible;
    int coarse_gameplay;
} reference_frame;

static int reference_rows(void *u, unsigned y, unsigned count, const uint8_t *p)
{
    reference_frame *frame = u;
    unsigned row, x;
    if (count != FM1_DOOM_STRIP_ROWS || y != frame->calls * count) return -1;
    for (row = 0; row < count; ++row) {
        unsigned display_y = y + row;
        unsigned sy = display_y * FM1_DOOM_SOURCE_HEIGHT / FM1_DOOM_HEIGHT;
#if FM1_DOOM_SOURCE_WIDTH == 160 && FM1_DOOM_SOURCE_HEIGHT == 100
        if (!frame->menu_visible)
            sy = display_y < 224u
               ? (frame->coarse_gameplay ? (display_y / 8u) * 93u / 28u
                                         : display_y * 93u / 224u)
               : 93u + (display_y - 224u) * 7u / 16u;
#endif
        for (x = 0; x < FM1_DOOM_WIDTH; ++x) {
            unsigned sx = x * (FM1_DOOM_SOURCE_WIDTH - 1u) / (FM1_DOOM_WIDTH - 1u);
#if FM1_DOOM_SOURCE_WIDTH == 160 && FM1_DOOM_SOURCE_HEIGHT == 100
            if (frame->coarse_gameplay && !frame->menu_visible && display_y < 224u)
                sx = (x / 8u) * FM1_DOOM_SOURCE_WIDTH / 30u;
#endif
            unsigned offset = (row * FM1_DOOM_WIDTH + x) * 2u;
            unsigned index, r, g, b;
            uint16_t pixel;
            index = frame->screen[sy * FM1_DOOM_SOURCE_WIDTH + sx];
            r = frame->palette[index * 3u];
            g = frame->palette[index * 3u + 1u];
            b = frame->palette[index * 3u + 2u];
            pixel = (uint16_t)(((r & 0xf8u) << 8u) | ((g & 0xfcu) << 3u) | (b >> 3u));
            if (p[offset] != (uint8_t)(pixel >> 8u) || p[offset + 1u] != (uint8_t)pixel) {
                fprintf(stderr, "Pixel mismatch at %u,%u (menu=%d coarse=%d)\n",
                        x, display_y, frame->menu_visible, frame->coarse_gameplay);
                return -1;
            }
        }
    }
    ++frame->calls;
    return 0;
}

static int test_present_reference(void)
{
    static uint8_t screen[FM1_DOOM_SOURCE_WIDTH * FM1_DOOM_SOURCE_HEIGHT];
    static uint8_t palette[768];
    fm1_doom_port port;
    reference_frame frame = {screen, palette, 0, 0, 0};
    fm1_doom_io io = {&frame, read_keys, reference_rows, ticks, sleep_ms};
    unsigned seed, x, y, i, menu, coarse;
    CHECK(fm1_doom_port_init(&port, &io) == 0);
    for (seed = 0; seed < 4u; ++seed) {
        for (i = 0; i < sizeof(palette); ++i)
            palette[i] = (uint8_t)(i * 73u + seed * 19u + (i >> (seed + 1u)));
        for (y = 0; y < FM1_DOOM_SOURCE_HEIGHT; ++y)
            for (x = 0; x < FM1_DOOM_SOURCE_WIDTH; ++x)
                screen[y * FM1_DOOM_SOURCE_WIDTH + x] =
                    (uint8_t)(x * 17u + y * 31u + seed * 43u + x * y * (seed + 1u));
        fm1_doom_palette(&port, palette);
        for (coarse = 0; coarse < 2u; ++coarse)
            for (menu = 0; menu < 2u; ++menu) {
                frame.calls = 0;
                frame.coarse_gameplay = port.coarse_gameplay = (uint8_t)coarse;
                frame.menu_visible = port.menu_visible = (int)menu;
                CHECK(fm1_doom_present(&port, screen) == 0);
                CHECK(frame.calls == FM1_DOOM_HEIGHT / FM1_DOOM_STRIP_ROWS);
            }
    }
    return 0;
}

static int test_presentation_toggle(void)
{
    fm1_doom_port port;
    fm1_doom_io io = {0, read_keys, rows, ticks, sleep_ms};
    const uint64_t toggle = UINT64_C(1) << 24;
    int pressed = 7;
    uint8_t key = 41, initial;
    unsigned i;
    keys = 0;
    CHECK(fm1_doom_port_init(&port, &io) == 0);
#if FM1_DOOM_SOURCE_WIDTH == 160 && FM1_DOOM_SOURCE_HEIGHT == 100
    CHECK(port.coarse_gameplay == 1);
#else
    CHECK(port.coarse_gameplay == 0);
#endif
    initial = port.coarse_gameplay;
    keys = toggle;
    CHECK(!fm1_doom_next_key(&port, &pressed, &key));
    CHECK(pressed == 7 && key == 41 && port.coarse_gameplay == (initial ^ 1u));
    CHECK((port.reported_keys & toggle) && !port.pending_keys);
    for (i = 0; i < 5u; ++i) {
        CHECK(!fm1_doom_next_key(&port, &pressed, &key));
        CHECK(port.coarse_gameplay == (initial ^ 1u));
    }
    keys = 0;
    CHECK(!fm1_doom_next_key(&port, &pressed, &key));
    CHECK(port.coarse_gameplay == (initial ^ 1u) && !(port.reported_keys & toggle));
    keys = toggle;
    CHECK(!fm1_doom_next_key(&port, &pressed, &key));
    CHECK(port.coarse_gameplay == initial);
    keys = 0;
    CHECK(!fm1_doom_next_key(&port, &pressed, &key));

    keys = toggle | (UINT64_C(1) << 14) | (UINT64_C(1) << 40);
    CHECK(fm1_doom_next_key(&port, &pressed, &key) && pressed && key == KEY_LEFTARROW);
    CHECK(port.coarse_gameplay == (initial ^ 1u));
    /* A changed physical sample must not discard the queued fire edge. */
    keys = 0;
    CHECK(fm1_doom_next_key(&port, &pressed, &key) && pressed && key == KEY_FIRE);
    CHECK(fm1_doom_next_key(&port, &pressed, &key) && !pressed && key == KEY_LEFTARROW);
    CHECK(fm1_doom_next_key(&port, &pressed, &key) && !pressed && key == KEY_FIRE);
    CHECK(!fm1_doom_next_key(&port, &pressed, &key));
    CHECK(port.coarse_gameplay == (initial ^ 1u));

    keys = UINT64_C(1) << 40;
    CHECK(fm1_doom_next_key(&port, &pressed, &key) && pressed && key == KEY_FIRE);
    CHECK(!fm1_doom_next_key(&port, &pressed, &key));
    keys = toggle;
    CHECK(fm1_doom_next_key(&port, &pressed, &key) && !pressed && key == KEY_FIRE);
    CHECK(port.coarse_gameplay == initial);
    CHECK(!fm1_doom_next_key(&port, &pressed, &key));
    keys = toggle | (UINT64_C(1) << 22) | (UINT64_C(1) << 23);
    CHECK(fm1_doom_next_key(&port, &pressed, &key) && pressed && key == KEY_ENTER);
    CHECK(fm1_doom_next_key(&port, &pressed, &key) && pressed && key == '2');
    CHECK(!fm1_doom_next_key(&port, &pressed, &key));
    CHECK(port.coarse_gameplay == initial);
    keys = 0;
    CHECK(fm1_doom_next_key(&port, &pressed, &key) && !pressed && key == KEY_ENTER);
    CHECK(fm1_doom_next_key(&port, &pressed, &key) && !pressed && key == '2');
    CHECK(!fm1_doom_next_key(&port, &pressed, &key));
    CHECK(port.coarse_gameplay == initial);
    return 0;
}

static int test_music_toggle(void)
{
    fm1_doom_port port;
    fm1_doom_io io = {&music_toggles, read_keys, rows, ticks, sleep_ms};
    const uint64_t toggle = UINT64_C(1) << 25;
    int pressed = 7;
    uint8_t key = 41;
    unsigned i;
    io.toggle_music_mode = toggle_music;
    keys = 0; music_toggles = 0;
    CHECK(fm1_doom_port_init(&port, &io) == 0);
    keys = toggle | (UINT64_C(1) << 17) | (UINT64_C(1) << 40);
    CHECK(fm1_doom_next_key(&port, &pressed, &key) && pressed && key == KEY_UPARROW);
    CHECK(music_toggles == 1);
    CHECK(fm1_doom_next_key(&port, &pressed, &key) && pressed && key == KEY_FIRE);
    for (i = 0; i < 5; ++i) CHECK(!fm1_doom_next_key(&port, &pressed, &key));
    CHECK(music_toggles == 1);
    keys = 0;
    CHECK(fm1_doom_next_key(&port, &pressed, &key) && !pressed && key == KEY_UPARROW);
    CHECK(fm1_doom_next_key(&port, &pressed, &key) && !pressed && key == KEY_FIRE);
    CHECK(!fm1_doom_next_key(&port, &pressed, &key) && music_toggles == 1);
    keys = toggle;
    CHECK(!fm1_doom_next_key(&port, &pressed, &key) && music_toggles == 2);
    io.toggle_music_mode = NULL;
    CHECK(fm1_doom_port_init(&port, &io) == 0);
    CHECK(!fm1_doom_next_key(&port, &pressed, &key));
    CHECK(music_toggles == 2);
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
    port.coarse_gameplay = 0;
    palette[3] = 255; /* index 1: red */
    palette[6 + 1] = 255; /* index 2: green */
    palette[9 + 2] = 255; /* index 3: blue */
    screen[0] = 1;
#if FM1_DOOM_SOURCE_WIDTH == 160 && FM1_DOOM_SOURCE_HEIGHT == 100
    screen[(120u * 93u / 224u) * FM1_DOOM_SOURCE_WIDTH
           + (120u * (FM1_DOOM_SOURCE_WIDTH - 1u)) / (FM1_DOOM_WIDTH - 1u)] = 2;
    screen[((120u * FM1_DOOM_SOURCE_HEIGHT) / FM1_DOOM_HEIGHT) * FM1_DOOM_SOURCE_WIDTH
           + (120u * (FM1_DOOM_SOURCE_WIDTH - 1u)) / (FM1_DOOM_WIDTH - 1u)] = 3;
#else
    screen[((120u * FM1_DOOM_SOURCE_HEIGHT) / FM1_DOOM_HEIGHT) * FM1_DOOM_SOURCE_WIDTH
           + (120u * (FM1_DOOM_SOURCE_WIDTH - 1u)) / (FM1_DOOM_WIDTH - 1u)] = 2;
#endif
    screen[(FM1_DOOM_SOURCE_HEIGHT - 1) * FM1_DOOM_SOURCE_WIDTH
           + FM1_DOOM_SOURCE_WIDTH - 1] = 3;
    fm1_doom_palette(&port, palette);
    CHECK(fm1_doom_present(&port, screen) == 0);
    CHECK(calls == FM1_DOOM_HEIGHT / FM1_DOOM_STRIP_ROWS && first == 0xf800u && center == 0x07e0u && last == 0x001fu);
#if FM1_DOOM_SOURCE_WIDTH == 160 && FM1_DOOM_SOURCE_HEIGHT == 100
    CHECK(block_edge == 0 && next_block == 0);
    port.menu_visible = 1;
    calls = 0;
    CHECK(fm1_doom_present(&port, screen) == 0);
    CHECK(center == 0x001fu);
#endif
    CHECK(!fm1_doom_next_key(&port, &pressed, &key));
    keys = (UINT64_C(1) << 14) | (UINT64_C(1) << 40);
    CHECK(fm1_doom_next_key(&port, &pressed, &key) && pressed && key == KEY_LEFTARROW);
    CHECK(fm1_doom_next_key(&port, &pressed, &key) && pressed && key == KEY_FIRE);
    CHECK(!fm1_doom_next_key(&port, &pressed, &key));
    keys = (UINT64_C(1) << 40);
    CHECK(fm1_doom_next_key(&port, &pressed, &key) && !pressed && key == KEY_LEFTARROW);
    CHECK(!fm1_doom_next_key(&port, &pressed, &key));
    keys = (UINT64_C(1) << 19) | (UINT64_C(1) << 40);
    CHECK(fm1_doom_next_key(&port, &pressed, &key) && pressed && key == KEY_ESCAPE);
    CHECK(!fm1_doom_next_key(&port, &pressed, &key));
    CHECK(fm1_doom_port_init(NULL, &io) == -1);
    {
        uint8_t wire[FM1_DOOM_WIDTH * FM1_DOOM_STRIP_ROWS * 2u];
        io.strip_buffer = wire;
        io.strip_buffer_bytes = sizeof(wire) - 1u;
        CHECK(fm1_doom_port_init(&port, &io) == -1);
        io.strip_buffer_bytes = sizeof(wire);
        CHECK(fm1_doom_port_init(&port, &io) == 0 && port.strip == wire);
        fm1_doom_palette(&port, palette);
        calls = 0;
        CHECK(fm1_doom_present(&port, screen) == 0 && first == 0xf800u);
        CHECK(calls == FM1_DOOM_HEIGHT / FM1_DOOM_STRIP_ROWS);
    }
    CHECK(test_present_reference() == 0);
    CHECK(test_presentation_toggle() == 0);
    CHECK(test_music_toggle() == 0);
    puts("FM-1 palette, both gameplay modes, image menu and simultaneous key/toggle edges passed");
    return 0;
}
