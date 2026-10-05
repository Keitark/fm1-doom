#include "fm1_doom_target_io.h"
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

static struct {
    int data;
    size_t length;
    uint8_t first[4];
} transactions[8];
static unsigned count;
static unsigned fail_on;

static int capture(int data, const uint8_t *bytes, size_t length)
{
    if (count == fail_on) return -1;
    if (count >= 8 || !bytes || !length) return -1;
    transactions[count].data = data;
    transactions[count].length = length;
    memcpy(transactions[count].first, bytes, length < 4 ? length : 4);
    ++count;
    return 0;
}

int main(void)
{
    uint8_t pixels[8u * 240u * 2u] = {0x12, 0x34, 0x56, 0x78};
    fm1_doom_key_filter keys = {0};
    CHECK(fm1_doom_debounce_keys(&keys, 1, 100) == 0);
    CHECK(fm1_doom_debounce_keys(&keys, 0, 105) == 0);
    CHECK(fm1_doom_debounce_keys(&keys, 1, 108) == 0);
    CHECK(fm1_doom_debounce_keys(&keys, 1, 117) == 0);
    CHECK(fm1_doom_debounce_keys(&keys, 1, 118) == 1);
    CHECK(fm1_doom_debounce_keys(&keys, 0, 119) == 1);
    CHECK(fm1_doom_debounce_keys(&keys, 0, 129) == 0);
    count = 0; fail_on = 99;
    CHECK(fm1_doom_lcd_rows(capture, 0, 8, pixels) == 0 && count == 6);
    CHECK(transactions[0].data == 0 && transactions[0].first[0] == 0x2a);
    CHECK(transactions[1].data == 1 && transactions[1].length == 4
          && transactions[1].first[3] == 239);
    CHECK(transactions[2].data == 0 && transactions[2].first[0] == 0x2b);
    CHECK(transactions[3].data == 1 && transactions[3].first[1] == 40
          && transactions[3].first[3] == 47);
    CHECK(transactions[4].data == 0 && transactions[4].first[0] == 0x2c);
    CHECK(transactions[5].data == 1 && transactions[5].length == sizeof(pixels)
          && transactions[5].first[0] == 0x12 && transactions[5].first[1] == 0x34);
    count = 0;
    CHECK(fm1_doom_lcd_rows(capture, 232, 8, pixels) == 0 && count == 4);
    CHECK(transactions[1].first[0] == 1 && transactions[1].first[1] == 16
          && transactions[1].first[2] == 1 && transactions[1].first[3] == 23);
    count = 0;
    CHECK(fm1_doom_lcd_rows(capture, 233, 8, pixels) == -1 && count == 0);
    CHECK(fm1_doom_lcd_rows(capture, 232, 9, pixels) == -1 && count == 0);
    CHECK(fm1_doom_lcd_rows(capture, 0, 8, NULL) == -1 && count == 0);
    fail_on = 3;
    CHECK(fm1_doom_lcd_rows(capture, 0, 8, pixels) == -1 && count == 3);
    puts("FM-1 Doom LCD strip command and error contract passed");
    return 0;
}
