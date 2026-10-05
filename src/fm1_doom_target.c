/* FM-1 SDK application root for the 160x100 E1M1 profile. The archive is
 * generated locally from a user-supplied IWAD and linked in read-only flash.
 * This is an offline candidate, not a hardware-qualified update image. */
#include "app_config.h"
#include "system/includes.h"
#include "system/task.h"
#include "system/sys_time.h"
#include "system/timer.h"
#include "system/spinlock.h"
#include "generic/jiffies.h"
#include "os/os_api.h"
#include "os/FreeRTOS/task.h"
#include "asm/clock.h"
#include "asm/wdt.h"

#include "display_test.h"
#include "boot_trace.h"
#include "fm1_wl82_keyscan.h"
#include "fm1_nes.h" /* Shared scanner result codes, including BUSY. */
#include "fm1_doom_archive.h"
#include "fm1_doom_memory.h"
#include "fm1_doom_runtime.h"
#include "fm1_doom_target_io.h"
#include "fm1_doom_wad_file.h"
#include "fm1_doom_usb.h"
#include "fm1_doom_sound.h"
#include "fm1_doom_music.h"
#include "doomgeneric.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

extern const uint8_t fm1_doom_embedded_archive[];
extern const uint32_t fm1_doom_embedded_archive_len;
int fm1_fmd_zliblite_inflate(void *, const uint8_t *, size_t, uint8_t *, size_t);

const struct irq_info irq_info_table[] = {{-1, -1, -1}};
const struct task_info task_info_table[] = {
    {"app_core", 15, 4096, 1024},
    {"sys_event", 29, 512, 0},
    {"systimer", 14, 256, 0},
    {"sys_timer", 9, 512, 128},
    {"doom_usb", 11, 1024, 0}, /* 4 KiB; bounded status output plus stack telemetry. */
    {"fm1_doom", 10, 2048, 0}, /* SDK stack size is in 32-bit words: 8 KiB. */
    {0, 0, 0, 0, 0},
};

/* Stages are RAM diagnostics, not proof of physical acceptance. */
volatile uint32_t fm1_doom_stage, fm1_doom_frames, fm1_doom_last_frame_ms;
volatile uint32_t fm1_doom_max_frame_interval_ms, fm1_doom_slow_frames;
volatile int fm1_doom_fault;
volatile int fm1_doom_key_error;
char fm1_doom_error_message[256];
static volatile unsigned stop_requested, stopped;
static fm1_wl82_keyscan scanner;
static spinlock_t input_lock;
static unsigned input_irq_enabled, input_irq_registered, input_retries;
static int scan_timer;
static uint32_t input_retry_at, input_good_at;
static uint32_t input_healthy_at;
static unsigned input_healthy;
static uint64_t input_last_keys;
/* Phase: 0=startup, 1=engine, 2=keys, 3=LCD, 4=after Tick, 5=stopped. */
static volatile uint32_t trace_tick_entered, trace_tick_completed, trace_phase;
static volatile uint32_t trace_lcd_y, trace_scan_kicks, trace_scan_irqs;
static fm1_fmd_t archive;
static fm1_doom_key_filter keys;
volatile uint32_t fm1_doom_lcd_snapshot[4][FM1_LCD_REGISTER_COUNT];

static void scan_stop(void);

void fm1_doom_usb_get_status(struct fm1_doom_usb_status *status)
{
    fm1_doom_sound_diagnostics sound;
    fm1_doom_music_diagnostics music;
    unsigned flags;
    status->stage = fm1_doom_stage;
    status->frames = fm1_doom_frames;
    status->fault = fm1_doom_fault;
    status->lcd_stage = fm1_display_stage;
    status->lcd_error = fm1_display_error;
    status->key_error = fm1_doom_key_error;
    status->sys_hz = clk_get("sys");
    status->lsb_hz = clk_get("lsb");
    memcpy(status->error_message, fm1_doom_error_message, sizeof(status->error_message));
    status->error_message[sizeof(status->error_message) - 1u] = 0;
    status->now_ms = timer_get_ms();
    status->tick_entered = trace_tick_entered;
    status->tick_completed = trace_tick_completed;
    status->phase = trace_phase;
    status->lcd_y = trace_lcd_y;
    status->scan_kicks = trace_scan_kicks;
    status->scan_irqs = trace_scan_irqs;
    status->key_lo = (uint32_t)input_last_keys;
    status->key_hi = (uint32_t)(input_last_keys >> 32);
    status->scan_sequence = scanner.sequence;
    status->scan_completions = scanner.completions;
    status->scan_age_ms = status->now_ms - input_good_at;
    status->input_retries = input_retries;
    status->max_frame_interval_ms = fm1_doom_max_frame_interval_ms;
    status->scan_failure_reason = scanner.failure.reason;
    status->scan_failure_row = scanner.failure.row;
    status->scan_failure_con = scanner.failure.con;
    status->scan_failure_dma_count = scanner.failure.dma_count;
    status->coarse_gameplay = fm1_doom_active_port()
        ? fm1_doom_active_port()->coarse_gameplay : 0;
    /* Copy bounded scalar audio state while holding its shared IRQ lock.
       No formatting, allocation or device access occurs in this section. */
    flags = fm1_doom_sound_lock();
    fm1_doom_sound_get_diagnostics(&sound);
    fm1_doom_music_get_diagnostics(&music);
    status->music_playing = fm1_doom_music_is_playing();
    fm1_doom_sound_unlock(flags);
    status->audio_ready = sound.ready;
    status->audio_error = sound.error;
    status->audio_irqs = sound.irq_count;
    status->audio_frames = sound.output_frames;
    status->sfx_started = sound.sfx_started;
    status->sfx_voices = sound.active_voices;
    status->max_audio_irq_us = sound.max_irq_us;
    status->music_ticks = music.ticks;
    status->music_events = music.events;
    status->music_loops = music.loops;
    status->music_steals = music.voice_steals;
    status->music_errors = music.errors;
    status->music_voices = music.active_voices;
    /* This getter runs on doom_usb; FreeRTOS reports unused stack words. */
    status->usb_stack_words = uxTaskGetStackHighWaterMark(NULL);
}

void fm1_doom_usb_request_stop(void) { stop_requested = 1; }
int fm1_doom_usb_is_stopped(void) { return stopped != 0; }

void __attribute__((noreturn)) fm1_doom_target_fatal(const char *format, va_list args)
{
    vsnprintf(fm1_doom_error_message, sizeof(fm1_doom_error_message), format, args);
    if (!fm1_doom_fault) fm1_doom_fault = -30;
    fm1_doom_stage = 0xff;
    fm1_doom_sound_shutdown();
    trace_phase = 5;
    scan_stop();
    fm1_display_test_stop();
    stopped = 1;
    for (;;) { wdt_clear(); os_time_dly(100); }
}

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
    /* timer_get_ms advances in 10 ms steps: one tick boundary would equal
       the scanner's entire 10 ms no-progress timeout. The SDK interpolates
       this clock from the hardware counter in half-millisecond units. */
    return (uint32_t)jiffies_half_msec() * 500u;
}

/* Same DMA2, paced-IRQ ownership as the working NES input path. */
static unsigned input_take(void)
{
    unsigned flags;
    local_irq_save(flags);
    arch_spin_lock(&input_lock);
    return flags;
}

static void input_release(unsigned flags)
{
    arch_spin_unlock(&input_lock);
    local_irq_restore(flags);
}

static void scan_tick(void *unused)
{
    unsigned flags;
    ++trace_scan_kicks;
    flags = input_take();
    (void)unused;
    if (input_irq_enabled) fm1_wl82_keyscan_async_kick(&scanner);
    input_release(flags);
}

___interrupt
static void scan_isr(void)
{
    unsigned flags;
    ++trace_scan_irqs;
    flags = input_take();
    if (input_irq_enabled) fm1_wl82_keyscan_async_step(&scanner);
    input_release(flags);
}

static void scan_stop(void)
{
    unsigned flags;
    if (input_irq_registered) bit_clr_ie(IRQ_SPI2_IDX, 0);
    flags = input_take();
    input_irq_enabled = 0;
    if (scanner.running) fm1_wl82_keyscan_stop(&scanner);
    input_release(flags);
    if (scan_timer > 0) sys_usec_timer_del(scan_timer);
    scan_timer = 0;
    if (input_irq_registered) {
        unrequest_irq(IRQ_SPI2_IDX, 0);
        input_irq_registered = 0;
    }
}

static int scan_start(void)
{
    unsigned flags;
    int rc;
    bit_clr_ie(IRQ_SPI2_IDX, 0);
    flags = input_take();
    rc = fm1_wl82_keyscan_async_start(&scanner, 0, now_us);
    input_irq_enabled = !rc;
    input_release(flags);
    if (!rc) {
        input_irq_registered = 1;
        request_irq(IRQ_SPI2_IDX, 5, scan_isr, 0);
        scan_timer = sys_usec_timer_add(0, scan_tick, 1000u, 1, 0);
        if (scan_timer <= 0) rc = -31;
    }
    if (rc) scan_stop();
    return rc;
}

static void input_failed(int rc, uint32_t now)
{
    fm1_doom_key_error = rc;
    scan_stop();
    input_retry_at = now;
    input_last_keys = 0;
    input_healthy = 0;
    memset(&keys, 0, sizeof(keys));
}

static uint64_t read_keys_sample(void *unused)
{
    uint64_t bits = 0;
    uint8_t rows[11];
    uint32_t now = timer_get_ms();
    unsigned flags;
    int rc;
    (void)unused;
    if (fm1_doom_fault || stop_requested) return 0;
    if (fm1_doom_key_error) {
        uint32_t retry_delay = 50u << input_retries;
        if (retry_delay > 1000u) retry_delay = 1000u;
        if ((uint32_t)(now - input_retry_at) < retry_delay) return 0;
        /* Keep retrying after transient scanner faults, with a capped backoff.
           One second of fresh sweeps, rather than starting SPI2, resets it. */
        if (input_retries < 5u) ++input_retries;
        rc = scan_start();
        if (rc) { input_failed(rc, now); return 0; }
        fm1_doom_key_error = 0;
        input_good_at = now;
    }
    flags = input_take();
    rc = fm1_wl82_keyscan_async_raw(&scanner, rows);
    input_release(flags);
    /* BUSY is the negative no-new-sweep code, not an input fault. */
    if (rc < 0 && rc != FM1_NES_BUSY) { input_failed(rc, now); return 0; }
    if (!rc) {
        input_last_keys = fm1_stock_decode_keys(rows);
        input_good_at = now;
        if (!input_healthy) {
            input_healthy_at = now;
            input_healthy = 1;
        }
        if ((uint32_t)(now - input_healthy_at) >= 1000u) input_retries = 0;
    }
    if ((uint32_t)(now - input_good_at) < 100u) bits = input_last_keys;
    else input_healthy = 0;
    wdt_clear();
    return fm1_doom_debounce_keys(&keys, bits, now);
}

static uint64_t read_keys(void *unused)
{
    uint64_t bits;
    trace_phase = 2;
    bits = read_keys_sample(unused);
    trace_phase = 1;
    return bits;
}

static int write_rows(void *unused, unsigned y, unsigned rows, const uint8_t *pixels)
{
    uint32_t now;
    (void)unused;
    trace_lcd_y = y;
    trace_phase = 3;
    if (fm1_doom_lcd_rows(fm1_display_write, y, rows, pixels)) {
        fm1_doom_fault = -20;
        trace_phase = 1;
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
    trace_phase = 1;
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
                           "-skill", "1", "-nogui", 0};
    fm1_doom_io io = {0, read_keys, write_rows, ticks_ms, sleep_ms};
    uint8_t *cache;
    size_t cache_len;
    int rc;
    (void)unused;
    fm1_boot_trace_mark(FM1_TRACE_WORKER);
    fm1_doom_stage = 2;
    if (fm1_doom_embedded_archive_len < 16u ||
        fm1_doom_embedded_archive_len > 196608u) { fm1_doom_fault = -10; goto failed; }
    cache = fm1_doom_archive_cache(&cache_len);
    rc = fm1_fmd_open(&archive, fm1_doom_embedded_archive,
                      fm1_doom_embedded_archive_len, cache, cache_len,
                      fm1_fmd_zliblite_inflate, 0);
    if (rc) { fm1_doom_fault = -11; goto failed; }
    rc = fm1_display_test_init();
    if (rc) { fm1_doom_fault = rc; goto failed_display; }
    /* Input failure must not hide the first rendered frame. */
    rc = scan_start();
    input_good_at = timer_get_ms();
    if (rc) input_failed(rc, input_good_at);
    io.strip_buffer = fm1_display_strip_buffer(&io.strip_buffer_bytes);
    fm1_doom_set_wad_archive("doom1.wad", &archive);
    if (fm1_doom_bind_io(&io)) { fm1_doom_fault = -12; goto failed_keys; }
    if (stop_requested) goto failed_keys;
    fm1_doom_stage = 3;
    doomgeneric_Create(8, argv);
    fm1_doom_stage = 4;
    while (!fm1_doom_fault && !stop_requested) {
        ++trace_tick_entered;
        trace_phase = 1;
        doomgeneric_Tick();
        ++trace_tick_completed;
        trace_phase = 4;
        wdt_clear();
        os_time_dly(0);
    }
failed_keys:
    fm1_doom_sound_shutdown();
    scan_stop();
failed_display:
    fm1_display_test_stop();
failed:
    fm1_doom_stage = fm1_doom_fault ? 0xff : 5;
    trace_phase = 5;
    stopped = 1;
    for (;;) { wdt_clear(); os_time_dly(100); }
}

void app_main(void)
{
    fm1_boot_trace_mark(FM1_TRACE_APP);
    fm1_doom_stage = 1;
    fm1_doom_fault = task_create(fm1_doom_usb_task, 0, "doom_usb");
    if (fm1_doom_fault) { fm1_doom_stage = 0xff; stopped = 1; return; }
    fm1_doom_fault = task_create(fm1_doom_task, 0, "fm1_doom");
    if (fm1_doom_fault) { fm1_doom_stage = 0xff; stopped = 1; }
}
