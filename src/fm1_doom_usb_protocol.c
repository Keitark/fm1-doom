#include "fm1_doom_usb.h"
#include <stdio.h>
#include <string.h>

void fm1_doom_usb_protocol_reset(fm1_doom_usb_protocol *protocol)
{
    memset(protocol, 0, sizeof(*protocol));
}

void fm1_doom_usb_protocol_status(const fm1_doom_usb_protocol_io *io)
{
    struct fm1_doom_usb_status status;
    char output[448];
    unsigned i;
    memset(&status, 0, sizeof(status));
    io->get_status(io->context, &status);
    status.error_message[sizeof(status.error_message) - 1u] = 0;
    for (i = 0; status.error_message[i]; ++i) {
        unsigned char value = (unsigned char)status.error_message[i];
        if (value < 32u || value > 126u) status.error_message[i] = ' ';
    }
    snprintf(output, sizeof(output),
             "DOOM STATUS stage=%lu fault=%d frames=%lu stopped=%d lcd_stage=%lu lcd_error=%d key_error=%d sys_hz=%d lsb_hz=%d error=%s\n",
             (unsigned long)status.stage, status.fault,
             (unsigned long)status.frames, io->is_stopped(io->context),
             (unsigned long)status.lcd_stage, status.lcd_error, status.key_error,
             status.sys_hz, status.lsb_hz, status.error_message);
    io->reply(io->context, output);
}

void fm1_doom_usb_protocol_trace(const fm1_doom_usb_protocol_io *io)
{
    struct fm1_doom_usb_status status;
    char output[352];
    memset(&status, 0, sizeof(status));
    io->get_status(io->context, &status);
    snprintf(output, sizeof(output),
             "DOOM TRACE ms=%lu entered=%lu completed=%lu phase=%lu y=%lu kicks=%lu irqs=%lu key_lo=%lu key_hi=%lu sequence=%lu scans=%lu age_ms=%lu retries=%lu max_frame_ms=%lu fail=%lu row=%lu con=%lu dma=%lu coarse=%lu\n",
             (unsigned long)status.now_ms, (unsigned long)status.tick_entered,
             (unsigned long)status.tick_completed, (unsigned long)status.phase,
             (unsigned long)status.lcd_y, (unsigned long)status.scan_kicks,
             (unsigned long)status.scan_irqs, (unsigned long)status.key_lo,
             (unsigned long)status.key_hi, (unsigned long)status.scan_sequence,
             (unsigned long)status.scan_completions, (unsigned long)status.scan_age_ms,
             (unsigned long)status.input_retries,
             (unsigned long)status.max_frame_interval_ms,
             (unsigned long)status.scan_failure_reason,
             (unsigned long)status.scan_failure_row,
             (unsigned long)status.scan_failure_con,
             (unsigned long)status.scan_failure_dma_count,
             (unsigned long)status.coarse_gameplay);
    io->reply(io->context, output);
}

void fm1_doom_usb_protocol_audio(const fm1_doom_usb_protocol_io *io)
{
    struct fm1_doom_usb_status status;
    char output[352];
    memset(&status, 0, sizeof(status));
    io->get_status(io->context, &status);
    snprintf(output, sizeof(output),
             "DOOM AUDIO ready=%d error=%d irqs=%lu frames=%lu sfx_started=%lu sfx_voices=%lu music_playing=%lu music_ticks=%lu music_events=%lu music_loops=%lu music_steals=%lu music_errors=%lu music_voices=%lu usb_stack_words=%lu max_irq_us=%lu\n",
             status.audio_ready, status.audio_error,
             (unsigned long)status.audio_irqs, (unsigned long)status.audio_frames,
             (unsigned long)status.sfx_started, (unsigned long)status.sfx_voices,
             (unsigned long)status.music_playing, (unsigned long)status.music_ticks,
             (unsigned long)status.music_events, (unsigned long)status.music_loops,
             (unsigned long)status.music_steals, (unsigned long)status.music_errors,
             (unsigned long)status.music_voices, (unsigned long)status.usb_stack_words,
             (unsigned long)status.max_audio_irq_us);
    io->reply(io->context, output);
}

static void command(fm1_doom_usb_protocol *protocol, int isolated,
                    const fm1_doom_usb_protocol_io *io)
{
    if (!strcmp(protocol->line, "HELLO")) {
        io->reply(io->context, "FM1DIAG/1 DOOM-FM1/1 UBOOT=SERIAL COMMIT=BLOCKED\n");
    } else if (!strcmp(protocol->line, "STATUS") ||
               !strcmp(protocol->line, "DOOM STATUS")) {
        fm1_doom_usb_protocol_status(io);
    } else if (!strcmp(protocol->line, "DOOM TRACE")) {
        fm1_doom_usb_protocol_trace(io);
    } else if (!strcmp(protocol->line, "DOOM AUDIO")) {
        fm1_doom_usb_protocol_audio(io);
    } else if (!strcmp(protocol->line, "DOOM STOP")) {
        io->request_stop(io->context);
        io->reply(io->context, "OK DOOM STOP REQUESTED\n");
    } else if (!strcmp(protocol->line, "UBOOT")) {
        if (!isolated) {
            io->reply(io->context, "ERR UBOOT_BUSY_OR_TRAILING\n");
        } else if (!io->is_stopped(io->context)) {
            io->reply(io->context, "ERR UBOOT_RETRY_AFTER_DOOM_STOP\n");
        } else if (!io->tx_drained(io->context) || !io->boot_arm(io->context)) {
            io->reply(io->context, "ERR UBOOT_RETRY\n");
        } else {
            io->reply(io->context, "OK UBOOT ARMED CONFIRM-WITHIN-5000MS\n");
        }
    } else if (!strcmp(protocol->line, "COMMIT")) {
        io->reply(io->context, "ERR COMMIT_BLOCKED\n");
    } else {
        io->reply(io->context, "ERR COMMAND\n");
    }
}

void fm1_doom_usb_protocol_tick(fm1_doom_usb_protocol *protocol,
                               uint32_t now_ms,
                               const fm1_doom_usb_protocol_io *io)
{
    if ((protocol->used || protocol->dropping) &&
        (uint32_t)(now_ms - protocol->started_ms) >= FM1_DOOM_USB_FRAGMENT_TIMEOUT_MS) {
        fm1_doom_usb_protocol_reset(protocol);
        io->reply(io->context, "ERR TIMEOUT ABORTED\n");
    }
}

void fm1_doom_usb_protocol_feed(fm1_doom_usb_protocol *protocol,
                               const uint8_t *bytes, size_t length,
                               uint32_t now_ms,
                               const fm1_doom_usb_protocol_io *io)
{
    size_t i;
    int prior_line = 0;
    fm1_doom_usb_protocol_tick(protocol, now_ms, io);
    for (i = 0; i < length; ++i) {
        uint8_t value = bytes[i];
        if (value == '\n') {
            if (protocol->dropping) {
                io->reply(io->context, "ERR LINE ABORTED\n");
            } else {
                protocol->line[protocol->used] = 0;
                command(protocol, !prior_line && i + 1u == length, io);
            }
            protocol->used = protocol->dropping = 0;
            prior_line = 1;
        } else {
            if (!protocol->used && !protocol->dropping)
                protocol->started_ms = now_ms;
            if (value < 32u || value > 126u ||
                protocol->used >= sizeof(protocol->line) - 1u) {
                protocol->dropping = 1;
            } else if (!protocol->dropping) {
                protocol->line[protocol->used++] = (char)value;
            }
        }
    }
}
