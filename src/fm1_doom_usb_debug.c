#include "fm1_doom_usb_debug.h"
#include "fm1_doom_target_io.h"
#include "display_test.h"

volatile uint32_t fm1_doom_usb_debug_registers[2][FM1_DOOM_USB_DEBUG_REGISTER_COUNT];
volatile uint32_t fm1_doom_usb_debug_valid;

extern volatile uint32_t fm1_doom_usb_stage, fm1_doom_usb_heartbeat;
extern volatile int fm1_doom_usb_error;

/* Fixed startup control/status snapshots, not a proven nonintrusive probe.
 * USB0_CON0 contains SIE/SOF pending bits; primary WL82 headers do not state
 * their read side effects. Never add indirect MUSB interrupt-status reads:
 * INTRUSB/INTRTX/INTRRX belong exclusively to the SDK interrupt handler. */
static const uintptr_t register_addresses[FM1_DOOM_USB_DEBUG_REGISTER_COUNT] = {
    0x10010u, /* CLK_CON1 */
    0x16a00u, /* USBPLL_CON0 */
    0x16a04u, /* USBPLL_CON1 */
    0x16a08u, /* USBPLL_CON2 */
    0x11800u, /* USB0_CON0 */
    0x11804u, /* USB0_CON1 */
    0x51000u  /* IO_CON0 */
};

static const char *const register_labels[FM1_DOOM_USB_DEBUG_REGISTER_COUNT] = {
    "CLK_CON1", "USBPLL_CON0", "USBPLL_CON1", "USBPLL_CON2",
    "USB0_CON0", "USB0_CON1", "IO_CON0"
};

void fm1_doom_usb_debug_snapshot(unsigned phase)
{
    unsigned i;
    if (phase > 1u) return;
    for (i = 0; i < FM1_DOOM_USB_DEBUG_REGISTER_COUNT; ++i)
        fm1_doom_usb_debug_registers[phase][i] =
            *(volatile const uint32_t *)register_addresses[i];
    fm1_doom_usb_debug_valid |= 1u << phase;
}

/* Original 5x7 row bitmaps, bits 4..0 left to right. Only uppercase text and
 * decimal/hex digits are needed; an unsupported character is left blank. */
static const uint8_t digit_glyphs[10][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14}
};
static const uint8_t letter_glyphs[26][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14}, {7,2,2,2,2,18,12},
    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31}
};
static const uint8_t dash_glyph[7] = {0,0,0,31,0,0,0};
static const uint8_t underscore_glyph[7] = {0,0,0,0,0,0,31};

static const uint8_t *glyph(unsigned char c)
{
    if (c >= '0' && c <= '9') return digit_glyphs[c - '0'];
    if (c >= 'A' && c <= 'Z') return letter_glyphs[c - 'A'];
    if (c == '-') return dash_glyph;
    if (c == '_') return underscore_glyph;
    return 0;
}

static void text(uint8_t *wire, unsigned strip_y, unsigned x, unsigned y,
                 const char *line, unsigned scale, uint16_t color)
{
    unsigned row, col, dx, dy;
    if (y >= strip_y + 8u || y + 7u * scale <= strip_y) return;
    while (*line && x < 240u) {
        const uint8_t *bitmap = glyph((unsigned char)*line++);
        if (bitmap) for (row = 0; row < 7u; ++row) {
            for (dy = 0; dy < scale; ++dy) {
                unsigned py = y + row * scale + dy;
                if (py < strip_y || py >= strip_y + 8u) continue;
                for (col = 0; col < 5u; ++col) {
                    if (!(bitmap[row] & (16u >> col))) continue;
                    for (dx = 0; dx < scale; ++dx) {
                        unsigned px = x + col * scale + dx;
                        size_t offset;
                        if (px >= 240u) continue;
                        offset = ((py - strip_y) * 240u + px) * 2u;
                        wire[offset] = (uint8_t)(color >> 8);
                        wire[offset + 1u] = (uint8_t)color;
                    }
                }
            }
        }
        x += 6u * scale;
    }
}

/* Each formatter appends into the same bounded 40-byte stack line. */
static unsigned append(char line[40], unsigned used, const char *value)
{
    while (*value && used < 39u) line[used++] = *value++;
    line[used] = 0;
    return used;
}

static unsigned hex(char line[40], unsigned used, uint32_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    unsigned i;
    for (i = 0; i < 8u && used < 39u; ++i)
        line[used++] = digits[(value >> (28u - 4u * i)) & 15u];
    line[used] = 0;
    return used;
}

static unsigned decimal(char line[40], unsigned used, uint32_t value)
{
    char reverse[10];
    unsigned count = 0;
    do {
        reverse[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value && count < sizeof(reverse));
    while (count && used < 39u) line[used++] = reverse[--count];
    line[used] = 0;
    return used;
}

int fm1_doom_usb_debug_draw(uint8_t *wire, size_t bytes, uint32_t elapsed_ms)
{
    char line[40];
    unsigned strip_y, i, used;
    uint32_t valid = fm1_doom_usb_debug_valid;
    uint32_t stage = fm1_doom_usb_stage;
    uint32_t heartbeat = fm1_doom_usb_heartbeat;
    int error = fm1_doom_usb_error;
    if (!wire || bytes < FM1_DOOM_USB_DEBUG_WIRE_BYTES
        || ((uintptr_t)wire & 63u)) return -1;
    for (strip_y = 0; strip_y < 240u; strip_y += 8u) {
        for (i = 0; i < FM1_DOOM_USB_DEBUG_WIRE_BYTES; ++i) wire[i] = 0;
        text(wire, strip_y, 4u, 4u, "USB DEBUG", 2u, 0xffffu);
        used = append(line, 0u, "STAGE ");
        used = hex(line, used, stage);
        used = append(line, used, " ERROR ");
        if (error < 0) used = append(line, used, "-");
        decimal(line, used, error < 0 ? 0u - (uint32_t)error : (uint32_t)error);
        text(wire, strip_y, 4u, 24u, line, 1u, 0xffe0u);
        used = append(line, 0u, "HEART ");
        used = hex(line, used, heartbeat);
        used = append(line, used, " SEC ");
        decimal(line, used, elapsed_ms / 1000u);
        text(wire, strip_y, 4u, 34u, line, 1u, 0xffe0u);
        used = append(line, 0u, "VALID ");
        decimal(line, used, valid);
        text(wire, strip_y, 4u, 44u, line, 1u, 0xffe0u);
        text(wire, strip_y, 4u, 55u, "BEFORE", 1u, 0x07e0u);
        text(wire, strip_y, 126u, 55u, "AFTER", 1u, 0x07e0u);
        for (i = 0; i < FM1_DOOM_USB_DEBUG_REGISTER_COUNT; ++i) {
            unsigned y = 64u + 24u * i;
            text(wire, strip_y, 4u, y, register_labels[i], 1u, 0xffffu);
            hex(line, 0u, valid & 1u ? fm1_doom_usb_debug_registers[0][i] : 0u);
            text(wire, strip_y, 4u, y + 8u, line, 2u,
                 valid & 1u ? 0xffffu : 0x7befu);
            hex(line, 0u, valid & 2u ? fm1_doom_usb_debug_registers[1][i] : 0u);
            text(wire, strip_y, 126u, y + 8u, line, 2u,
                 valid & 2u ? 0xffffu : 0x7befu);
        }
        if (fm1_doom_lcd_rows(fm1_display_write, strip_y, 8u, wire)) return -1;
    }
    return 0;
}
