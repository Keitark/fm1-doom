#ifndef FM1_DOOM_USB_H
#define FM1_DOOM_USB_H

#include <stddef.h>
#include <stdint.h>

struct fm1_doom_usb_status {
    uint32_t stage, frames, lcd_stage;
    int fault, lcd_error, key_error, sys_hz, lsb_hz;
    char error_message[256];
    /* Best-effort scalar snapshots: do not take the scanner lock from USB. */
    uint32_t now_ms, tick_entered, tick_completed, phase, lcd_y;
    uint32_t scan_kicks, scan_irqs, key_lo, key_hi;
    uint32_t scan_sequence, scan_completions, scan_age_ms, input_retries;
    uint32_t max_frame_interval_ms;
    uint32_t scan_failure_reason, scan_failure_row;
    uint32_t scan_failure_con, scan_failure_dma_count;
    uint32_t coarse_gameplay;
    int audio_ready, audio_error;
    uint32_t audio_irqs, audio_frames, sfx_started, sfx_voices;
    uint32_t music_playing, music_ticks, music_events, music_loops;
    uint32_t music_steals, music_errors, music_voices, usb_stack_words;
    uint32_t max_audio_irq_us;
};

void fm1_doom_usb_get_status(struct fm1_doom_usb_status *status);
void fm1_doom_usb_request_stop(void);
int fm1_doom_usb_is_stopped(void);
void fm1_doom_usb_task(void *argument);

#define FM1_DOOM_USB_LINE_BYTES 64u
#define FM1_DOOM_USB_FRAGMENT_TIMEOUT_MS 10000u

typedef struct {
    char line[FM1_DOOM_USB_LINE_BYTES];
    unsigned used, dropping;
    uint32_t started_ms;
} fm1_doom_usb_protocol;

typedef struct {
    void *context;
    void (*reply)(void *context, const char *text);
    void (*get_status)(void *context, struct fm1_doom_usb_status *status);
    void (*request_stop)(void *context);
    int (*is_stopped)(void *context);
    int (*tx_drained)(void *context);
    int (*boot_arm)(void *context);
} fm1_doom_usb_protocol_io;

void fm1_doom_usb_protocol_reset(fm1_doom_usb_protocol *protocol);
void fm1_doom_usb_protocol_feed(fm1_doom_usb_protocol *protocol,
                               const uint8_t *bytes, size_t length,
                               uint32_t now_ms,
                               const fm1_doom_usb_protocol_io *io);
void fm1_doom_usb_protocol_tick(fm1_doom_usb_protocol *protocol,
                               uint32_t now_ms,
                               const fm1_doom_usb_protocol_io *io);
void fm1_doom_usb_protocol_status(const fm1_doom_usb_protocol_io *io);
void fm1_doom_usb_protocol_trace(const fm1_doom_usb_protocol_io *io);
void fm1_doom_usb_protocol_audio(const fm1_doom_usb_protocol_io *io);

#endif
