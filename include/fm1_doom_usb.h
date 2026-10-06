#ifndef FM1_DOOM_USB_H
#define FM1_DOOM_USB_H

#include <stddef.h>
#include <stdint.h>
#include "fm1_doom_volume.h"

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
    uint32_t volume_raw, volume_gain, volume_valid, volume_errors;
    uint32_t volume_samples, volume_target;
    uint32_t synth_mode;
    uint32_t edit_controls; /* Four7-bit knobs, Preset2-bit, Algorithm2-bit. */
    uint32_t speaker_muted;
    int heap_free;
};

void fm1_doom_usb_get_status(struct fm1_doom_usb_status *status);
void fm1_doom_usb_request_stop(void);
int fm1_doom_usb_is_stopped(void);
void fm1_doom_usb_task(void *argument);
int fm1_doom_usb_board_ready(void);

struct fm1_doom_usb_volume {
    fm1_doom_volume_hardware hardware;
    uint32_t now_ms;
    int sys_hz, lsb_hz;
};
void fm1_doom_usb_get_volume(struct fm1_doom_usb_volume *volume);

struct fm1_doom_usb_game {
    uint32_t stage, tic, angle;
    int state, skill, health, bullets, shells, weapon, total_kills, kills;
    int32_t x, y;
    int sector, things, vertexes, lines, sides, sectors, segs, subsectors, nodes;
    uint32_t menu, coarse;
};

#define FM1_DOOM_USB_FRAME_WIDTH 160u
#define FM1_DOOM_USB_FRAME_HEIGHT 100u
#define FM1_DOOM_USB_FRAME_PIXELS 16000u
#define FM1_DOOM_USB_FRAME_BYTES 16512u
#define FM1_DOOM_USB_FRAME_CHUNK 96u
#define FM1_DOOM_USB_FRAME_TIMEOUT_MS 15000u
/* State: 0=idle, 1=requested, 2=held between completed engine ticks. */
typedef struct {
    uint32_t state, id, started_ms, coarse, menu;
} fm1_doom_usb_frame_control;

int fm1_doom_usb_frame_control_begin(fm1_doom_usb_frame_control *frame, uint32_t now_ms);
void fm1_doom_usb_frame_control_tick(fm1_doom_usb_frame_control *frame, uint32_t now_ms);
void fm1_doom_usb_get_game(struct fm1_doom_usb_game *game);
int fm1_doom_usb_frame_begin(uint32_t now_ms);
void fm1_doom_usb_frame_info(fm1_doom_usb_frame_control *frame);
size_t fm1_doom_usb_frame_read(uint32_t offset, uint8_t *out, size_t capacity);
void fm1_doom_usb_frame_end(void);
void fm1_doom_usb_frame_tick(uint32_t now_ms);

#define FM1_DOOM_USB_LINE_BYTES 64u
#define FM1_DOOM_USB_FRAGMENT_TIMEOUT_MS 10000u
#define FM1_DOOM_USB_MUSIC_MONITOR_MS 15000u

typedef struct {
    char line[FM1_DOOM_USB_LINE_BYTES];
    unsigned used, dropping;
    uint32_t started_ms;
    uint32_t monitor_mode, monitor_started_ms;
} fm1_doom_usb_protocol;

typedef struct {
    void *context;
    void (*reply)(void *context, const char *text);
    void (*get_status)(void *context, struct fm1_doom_usb_status *status);
    void (*request_stop)(void *context);
    int (*is_stopped)(void *context);
    int (*tx_drained)(void *context);
    int (*boot_arm)(void *context);
    void (*get_game)(void *context, struct fm1_doom_usb_game *game);
    int (*frame_begin)(void *context, uint32_t now_ms);
    void (*frame_info)(void *context, fm1_doom_usb_frame_control *frame);
    size_t (*frame_read)(void *context, uint32_t offset, uint8_t *out, size_t capacity);
    void (*frame_end)(void *context);
    void (*frame_tick)(void *context, uint32_t now_ms);
    int (*get_heap_free)(void *context);
    void (*get_volume)(void *context, struct fm1_doom_usb_volume *volume);
    void (*format_usb_audio)(char *out, size_t length);
    void (*set_speaker_muted)(void *context, unsigned muted);
    void (*set_music_monitor)(void *context, unsigned mode);
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
void fm1_doom_usb_protocol_volume(const fm1_doom_usb_protocol_io *io);
void fm1_doom_usb_protocol_usb_audio(const fm1_doom_usb_protocol_io *io);

#endif
