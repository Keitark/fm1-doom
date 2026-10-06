#include "fm1_doom_usb.h"
#include "fm1_doom_music.h"
#include <stdio.h>
#include <string.h>

/* Keep mutually exclusive reply buffers in separate emitted stack frames.
 * The target uses LTO; the builder gates emitted frames against its stack. */
#if defined(_MSC_VER)
#define USB_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define USB_NOINLINE __attribute__((noinline))
#else
#error USB protocol requires a compiler noinline attribute
#endif

void fm1_doom_usb_protocol_reset(fm1_doom_usb_protocol *protocol)
{
    memset(protocol, 0, sizeof(*protocol));
}

int fm1_doom_usb_frame_control_begin(fm1_doom_usb_frame_control *frame, uint32_t now_ms)
{
    if (frame->state) return -2;
    if (!++frame->id) ++frame->id;
    frame->started_ms = now_ms;
    frame->state = 1;
    return 0;
}

void fm1_doom_usb_frame_control_tick(fm1_doom_usb_frame_control *frame, uint32_t now_ms)
{
    if (frame->state && (uint32_t)(now_ms - frame->started_ms)
                        >= FM1_DOOM_USB_FRAME_TIMEOUT_MS)
        frame->state = 0;
}

static USB_NOINLINE void game_status(const fm1_doom_usb_protocol_io *io)
{
    struct fm1_doom_usb_game game;
    char output[448];
    if (!io->get_game) { io->reply(io->context, "ERR GAME_UNAVAILABLE\n"); return; }
    memset(&game, 0, sizeof(game));
    io->get_game(io->context, &game);
    snprintf(output, sizeof(output),
             "DOOM GAME stage=%lu tic=%lu state=%d skill=%d x=%ld y=%ld angle=%lu health=%d bullets=%d shells=%d weapon=%d total=%d killed=%d sector=%d things=%d vertexes=%d lines=%d sides=%d sectors=%d segs=%d subsectors=%d nodes=%d menu=%lu coarse=%lu\n",
             (unsigned long)game.stage, (unsigned long)game.tic, game.state, game.skill,
             (long)game.x, (long)game.y, (unsigned long)game.angle, game.health,
             game.bullets, game.shells, game.weapon, game.total_kills, game.kills,
             game.sector, game.things, game.vertexes, game.lines, game.sides,
             game.sectors, game.segs, game.subsectors, game.nodes,
             (unsigned long)game.menu, (unsigned long)game.coarse);
    io->reply(io->context, output);
}

static USB_NOINLINE void frame_info(const fm1_doom_usb_protocol_io *io)
{
    fm1_doom_usb_frame_control frame;
    char output[160];
    if (!io->frame_info) { io->reply(io->context, "ERR FRAME_UNAVAILABLE\n"); return; }
    io->frame_info(io->context, &frame);
    snprintf(output, sizeof(output),
             "DOOM FRAMEINFO state=%lu ready=%d id=%lu width=160 height=100 bytes=16512 coarse=%lu menu=%lu\n",
             (unsigned long)frame.state, frame.state == 2, (unsigned long)frame.id,
             (unsigned long)frame.coarse, (unsigned long)frame.menu);
    io->reply(io->context, output);
}

static USB_NOINLINE void edit_status(const fm1_doom_usb_protocol_io *io)
{
    struct fm1_doom_usb_status status;
    uint32_t value;
    char output[128];
    memset(&status, 0, sizeof(status));
    io->get_status(io->context, &status);
    value = status.edit_controls;
    snprintf(output, sizeof(output),
             "DOOM EDIT preset=%u algorithm=%u vco=%u vcf=%u vca=%u reverb=%u synth_mode=%lu\n",
             (unsigned)(value >> 28 & 3u), (unsigned)(value >> 30),
             (unsigned)(value & 127u), (unsigned)(value >> 7 & 127u),
             (unsigned)(value >> 14 & 127u), (unsigned)(value >> 21 & 127u),
             (unsigned long)status.synth_mode);
    io->reply(io->context, output);
}

static USB_NOINLINE void frame_read(const char *text, const fm1_doom_usb_protocol_io *io)
{
    static const char hex[] = "0123456789ABCDEF";
    uint8_t data[FM1_DOOM_USB_FRAME_CHUNK];
    fm1_doom_usb_frame_control frame;
    char output[280];
    uint32_t offset = 0;
    size_t count, i, length;
    if (!*text) { io->reply(io->context, "ERR FRAME_OFFSET\n"); return; }
    while (*text) {
        if (*text < '0' || *text > '9' || offset > (UINT32_MAX - 9u) / 10u) {
            io->reply(io->context, "ERR FRAME_OFFSET\n"); return;
        }
        offset = offset * 10u + (unsigned)(*text++ - '0');
    }
    if (offset >= FM1_DOOM_USB_FRAME_BYTES) {
        io->reply(io->context, "ERR FRAME_OFFSET\n"); return;
    }
    if (!io->frame_info || !io->frame_read) {
        io->reply(io->context, "ERR FRAME_UNAVAILABLE\n"); return;
    }
    io->frame_info(io->context, &frame);
    if (frame.state != 2) { io->reply(io->context, "ERR FRAME_NOT_READY\n"); return; }
    count = io->frame_read(io->context, offset, data, sizeof(data));
    if (!count || count > sizeof(data) || count > FM1_DOOM_USB_FRAME_BYTES - offset) {
        io->reply(io->context, "ERR FRAME_NOT_READY\n"); return;
    }
    length = (size_t)snprintf(output, sizeof(output),
             "DOOM FRAME id=%lu offset=%lu bytes=%u data=",
             (unsigned long)frame.id, (unsigned long)offset, (unsigned)count);
    for (i = 0; i < count; ++i) {
        output[length++] = hex[data[i] >> 4];
        output[length++] = hex[data[i] & 15u];
    }
    output[length++] = '\n'; output[length] = 0;
    io->reply(io->context, output);
}

USB_NOINLINE void fm1_doom_usb_protocol_status(const fm1_doom_usb_protocol_io *io)
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

USB_NOINLINE void fm1_doom_usb_protocol_trace(const fm1_doom_usb_protocol_io *io)
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

USB_NOINLINE void fm1_doom_usb_protocol_audio(const fm1_doom_usb_protocol_io *io)
{
    struct fm1_doom_usb_status status;
    char output[512];
    memset(&status, 0, sizeof(status));
    io->get_status(io->context, &status);
    if (io->get_heap_free) status.heap_free = io->get_heap_free(io->context);
    snprintf(output, sizeof(output),
             "DOOM AUDIO ready=%d error=%d irqs=%lu frames=%lu sfx_started=%lu sfx_voices=%lu music_playing=%lu music_ticks=%lu music_events=%lu music_loops=%lu music_steals=%lu music_errors=%lu music_voices=%lu usb_stack_words=%lu max_irq_us=%lu volume_raw=%lu volume_gain=%lu volume_valid=%lu volume_errors=%lu volume_samples=%lu volume_target=%lu synth_mode=%lu speaker_muted=%lu heap_free=%d\n",
             status.audio_ready, status.audio_error,
             (unsigned long)status.audio_irqs, (unsigned long)status.audio_frames,
             (unsigned long)status.sfx_started, (unsigned long)status.sfx_voices,
             (unsigned long)status.music_playing, (unsigned long)status.music_ticks,
             (unsigned long)status.music_events, (unsigned long)status.music_loops,
             (unsigned long)status.music_steals, (unsigned long)status.music_errors,
             (unsigned long)status.music_voices, (unsigned long)status.usb_stack_words,
             (unsigned long)status.max_audio_irq_us,
             (unsigned long)status.volume_raw, (unsigned long)status.volume_gain,
             (unsigned long)status.volume_valid, (unsigned long)status.volume_errors,
             (unsigned long)status.volume_samples, (unsigned long)status.volume_target,
             (unsigned long)status.synth_mode,
             (unsigned long)status.speaker_muted, status.heap_free);
    io->reply(io->context, output);
}

USB_NOINLINE void fm1_doom_usb_protocol_volume(const fm1_doom_usb_protocol_io *io)
{
    struct fm1_doom_usb_volume volume;
    char output[512];
    const fm1_doom_volume_hardware *hardware = &volume.hardware;
    if (!io->get_volume) { io->reply(io->context, "ERR VOLUME_UNAVAILABLE\n"); return; }
    memset(&volume, 0, sizeof(volume));
    io->get_volume(io->context, &volume);
    snprintf(output, sizeof(output),
             "DOOM VOLUME ms=%lu adc_con=%08lx adc_res=%lu pb_dir=%08lx pb_die=%08lx pb_pu=%08lx pb_pd=%08lx pb_hd0=%08lx pb_hd1=%08lx pb_dieh=%08lx wla_con0=%08lx pll_con1=%08lx raw=%lu accepted=%lu target=%lu gain=%lu run=%lu valid=%lu waiting=%lu samples=%lu errors=%lu sys_hz=%d lsb_hz=%d\n",
             (unsigned long)volume.now_ms,
             (unsigned long)hardware->adc_con, (unsigned long)hardware->adc_res,
             (unsigned long)hardware->pb_dir, (unsigned long)hardware->pb_die,
             (unsigned long)hardware->pb_pu, (unsigned long)hardware->pb_pd,
             (unsigned long)hardware->pb_hd0, (unsigned long)hardware->pb_hd1,
             (unsigned long)hardware->pb_dieh, (unsigned long)hardware->wla_con0,
             (unsigned long)hardware->pll_con1,
             (unsigned long)hardware->raw, (unsigned long)hardware->accepted,
             (unsigned long)hardware->target, (unsigned long)hardware->gain,
             (unsigned long)hardware->running, (unsigned long)hardware->valid,
             (unsigned long)hardware->waiting, (unsigned long)hardware->samples,
             (unsigned long)hardware->errors, volume.sys_hz, volume.lsb_hz);
    io->reply(io->context, output);
}

void USB_NOINLINE fm1_doom_usb_protocol_usb_audio(const fm1_doom_usb_protocol_io *io)
{
    char output[448];
    if (!io->format_usb_audio) { io->reply(io->context, "ERR USB_AUDIO_UNAVAILABLE\n"); return; }
    io->format_usb_audio(output, sizeof(output));
    io->reply(io->context, output);
}

static USB_NOINLINE void command(fm1_doom_usb_protocol *protocol, int isolated, uint32_t now_ms,
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
    } else if (!strcmp(protocol->line, "DOOM EDIT")) {
        edit_status(io);
    } else if (!strcmp(protocol->line, "DOOM USB_AUDIO")) {
        fm1_doom_usb_protocol_usb_audio(io);
    } else if (!strcmp(protocol->line, "DOOM MUSIC FULL") ||
               !strcmp(protocol->line, "DOOM MUSIC MELODY") ||
               !strcmp(protocol->line, "DOOM MUSIC DRUMS") ||
               !strcmp(protocol->line, "DOOM MUSIC PAUSE")) {
        unsigned mode = !strcmp(protocol->line + 11, "MELODY") ? FM1_DOOM_MUSIC_MONITOR_MELODY
                      : !strcmp(protocol->line + 11, "DRUMS") ? FM1_DOOM_MUSIC_MONITOR_DRUMS
                      : !strcmp(protocol->line + 11, "PAUSE") ? FM1_DOOM_MUSIC_MONITOR_PAUSE
                      : FM1_DOOM_MUSIC_MONITOR_FULL;
        if (io->set_music_monitor) {
            io->set_music_monitor(io->context, mode);
            protocol->monitor_mode = mode;
            protocol->monitor_started_ms = now_ms;
            io->reply(io->context, mode == FM1_DOOM_MUSIC_MONITOR_MELODY ? "OK DOOM MUSIC MELODY\n"
                      : mode == FM1_DOOM_MUSIC_MONITOR_DRUMS ? "OK DOOM MUSIC DRUMS\n"
                      : mode == FM1_DOOM_MUSIC_MONITOR_PAUSE ? "OK DOOM MUSIC PAUSE\n"
                      : "OK DOOM MUSIC FULL\n");
        } else io->reply(io->context, "ERR MUSIC_MONITOR_UNAVAILABLE\n");
    } else if (!strcmp(protocol->line, "DOOM MUTE 1") ||
               !strcmp(protocol->line, "DOOM MUTE 0")) {
        unsigned muted = protocol->line[10] == '1';
        if (io->set_speaker_muted) {
            io->set_speaker_muted(io->context, muted);
            io->reply(io->context, muted ? "OK DOOM MUTE 1\n" : "OK DOOM MUTE 0\n");
        } else io->reply(io->context, "ERR MUTE_UNAVAILABLE\n");
    } else if (!strcmp(protocol->line, "DOOM VOLUME") ||
               !strcmp(protocol->line, "VOLUME")) {
        fm1_doom_usb_protocol_volume(io);
    } else if (!strcmp(protocol->line, "DOOM GAME")) {
        game_status(io);
    } else if (!strcmp(protocol->line, "DOOM FRAME BEGIN")) {
        int result = io->frame_begin ? io->frame_begin(io->context, now_ms) : -1;
        io->reply(io->context, result == 0 ? "OK DOOM FRAME REQUESTED\n"
                  : result == -2 ? "ERR FRAME_BUSY\n" : "ERR FRAME_UNAVAILABLE\n");
    } else if (!strcmp(protocol->line, "DOOM FRAMEINFO")) {
        frame_info(io);
    } else if (!strncmp(protocol->line, "DOOM FRAME READ ", 16)) {
        frame_read(protocol->line + 16, io);
    } else if (!strcmp(protocol->line, "DOOM FRAME END")) {
        if (io->frame_end) io->frame_end(io->context);
        io->reply(io->context, "OK DOOM FRAME END\n");
    } else if (!strcmp(protocol->line, "DOOM STOP")) {
        if (io->frame_end) io->frame_end(io->context);
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
    if (io->frame_tick) io->frame_tick(io->context, now_ms);
    if (protocol->monitor_mode &&
        (uint32_t)(now_ms - protocol->monitor_started_ms) >= FM1_DOOM_USB_MUSIC_MONITOR_MS) {
        if (io->set_music_monitor) io->set_music_monitor(io->context, FM1_DOOM_MUSIC_MONITOR_FULL);
        protocol->monitor_mode = 0;
        io->reply(io->context, "OK DOOM MUSIC FULL TIMEOUT\n");
    }
    if ((protocol->used || protocol->dropping) &&
        (uint32_t)(now_ms - protocol->started_ms) >= FM1_DOOM_USB_FRAGMENT_TIMEOUT_MS) {
        if (protocol->monitor_mode && io->set_music_monitor)
            io->set_music_monitor(io->context, FM1_DOOM_MUSIC_MONITOR_FULL);
        fm1_doom_usb_protocol_reset(protocol);
        if (io->frame_end) io->frame_end(io->context);
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
                command(protocol, !prior_line && i + 1u == length, now_ms, io);
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
