/* FM-1 SDK application root for the 160x100 E1M1 profile. The archive is
 * generated locally from a user-supplied IWAD and linked in read-only flash.
 * This is an offline candidate, not a hardware-qualified update image. */
#include "app_config.h"
#include "system/includes.h"
#include "system/task.h"
#include "system/sys_time.h"
#include "os/os_api.h"
#include "asm/wdt.h"

#include "display_test.h"
#include "fm1_wl82_keyscan.h"
#include "fm1_doom_archive.h"
#include "fm1_doom_memory.h"
#include "fm1_doom_runtime.h"
#include "fm1_doom_target_io.h"
#include "fm1_doom_wad_file.h"
#include "doomgeneric.h"
#include <stdint.h>

extern const uint8_t fm1_doom_embedded_archive[];
extern const uint32_t fm1_doom_embedded_archive_len;
int fm1_fmd_zliblite_inflate(void *, const uint8_t *, size_t, uint8_t *, size_t);

const struct irq_info irq_info_table[] = {{-1, -1, -1}};
const struct task_info task_info_table[] = {
    {"app_core", 15, 4096, 1024},
    {"sys_event", 29, 512, 0},
    {"systimer", 14, 256, 0},
    {"sys_timer", 9, 512, 128},
    {"fm1_doom", 10, 8192, 0},
    {0, 0, 0, 0, 0},
};

/* Stages are RAM diagnostics, not proof of physical acceptance. */
volatile uint32_t fm1_doom_stage, fm1_doom_frames, fm1_doom_last_frame_ms;
volatile uint32_t fm1_doom_max_frame_interval_ms, fm1_doom_slow_frames;
volatile int fm1_doom_fault;
static fm1_wl82_keyscan scanner;
static fm1_fmd_t archive;
static fm1_doom_key_filter keys;
volatile uint32_t fm1_doom_lcd_snapshot[4][FM1_LCD_REGISTER_COUNT];

void fm1_display_snapshot(unsigned phase, const uint32_t registers[FM1_LCD_REGISTER_COUNT])
{
    unsigned i;
    if (phase >= 4u) return;
    for (i = 0; i < FM1_LCD_REGISTER_COUNT; ++i)
        fm1_doom_lcd_snapshot[phase][i] = registers[i];
}

static uint32_t now_us(void *unused)
{
    (void)unused;
    return timer_get_ms() * 1000u;
}

static uint64_t read_keys(void *unused)
{
    uint64_t bits = 0;
    int rc;
    (void)unused;
    if (fm1_doom_fault) return 0;
    rc = fm1_wl82_keyscan_poll(&scanner, &bits);
    if (rc) { fm1_doom_fault = rc; return 0; }
    wdt_clear();
    return fm1_doom_debounce_keys(&keys, bits, timer_get_ms());
}

static int write_rows(void *unused, unsigned y, unsigned rows, const uint8_t *pixels)
{
    uint32_t now;
    (void)unused;
    if (fm1_doom_lcd_rows(fm1_display_write, y, rows, pixels)) {
        fm1_doom_fault = -20;
        return -1;
    }
    if (y + rows == 240u) {
        now = timer_get_ms();
        if (fm1_doom_frames) {
            uint32_t interval = now - fm1_doom_last_frame_ms;
            if (interval > fm1_doom_max_frame_interval_ms)
                fm1_doom_max_frame_interval_ms = interval;
            if (interval > 33u) ++fm1_doom_slow_frames;
        }
        ++fm1_doom_frames;
        fm1_doom_last_frame_ms = now;
    }
    wdt_clear();
    return 0;
}

static uint32_t ticks_ms(void *unused)
{
    (void)unused;
    return timer_get_ms();
}

static void sleep_ms(void *unused, uint32_t milliseconds)
{
    (void)unused;
    wdt_clear();
    /* The FM-1 OS tick is 10 ms. A zero delay yields without imposing a
       fixed 10 ms pause on Doom's 35 Hz tic scheduler. */
    os_time_dly(milliseconds >= 10u ? (int)((milliseconds + 9u) / 10u) : 0);
}

static void fm1_doom_task(void *unused)
{
    static char *argv[] = {"fm1doom", "-iwad", "doom1.wad", "-warp", "1",
                           "-skill", "1", "-nomusic", "-nosfx", "-nogui", 0};
    fm1_doom_io io = {0, read_keys, write_rows, ticks_ms, sleep_ms};
    uint8_t *cache;
    size_t cache_len;
    int rc;
    (void)unused;
    fm1_doom_stage = 2;
    if (fm1_doom_embedded_archive_len < 16u ||
        fm1_doom_embedded_archive_len > 131072u) { fm1_doom_fault = -10; goto failed; }
    cache = fm1_doom_archive_cache(&cache_len);
    rc = fm1_fmd_open(&archive, fm1_doom_embedded_archive,
                      fm1_doom_embedded_archive_len, cache, cache_len,
                      fm1_fmd_zliblite_inflate, 0);
    if (rc) { fm1_doom_fault = -11; goto failed; }
    rc = fm1_display_test_init();
    if (rc) { fm1_doom_fault = rc; goto failed_display; }
    rc = fm1_wl82_keyscan_start(&scanner, 0, now_us);
    if (rc) { fm1_doom_fault = rc; goto failed_keys; }
    fm1_doom_set_wad_archive("doom1.wad", &archive);
    if (fm1_doom_bind_io(&io)) { fm1_doom_fault = -12; goto failed_keys; }
    fm1_doom_stage = 3;
    doomgeneric_Create(10, argv);
    fm1_doom_stage = 4;
    while (!fm1_doom_fault) {
        doomgeneric_Tick();
        wdt_clear();
        os_time_dly(0);
    }
failed_keys:
    fm1_wl82_keyscan_stop(&scanner);
failed_display:
    fm1_display_test_stop();
failed:
    fm1_doom_stage = 0xff;
    for (;;) { wdt_clear(); os_time_dly(100); }
}

void app_main(void)
{
    fm1_doom_stage = 1;
    fm1_doom_fault = task_create(fm1_doom_task, 0, "fm1_doom");
    if (fm1_doom_fault) fm1_doom_stage = 0xff;
}
