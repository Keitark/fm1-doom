#include "fm1_doom_usb.h"
#include "fm1_doom_music.h"
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

typedef struct {
    char output[8192];
    unsigned used, stop_requests, arm_calls, frame_ends, frame_reads, heap_queries, volume_queries;
    unsigned mute_requests;
    unsigned monitor_requests, monitor_mode;
    int stopped, drained, arm_result;
    struct fm1_doom_usb_status status;
    struct fm1_doom_usb_game game;
    struct fm1_doom_usb_volume volume;
    fm1_doom_usb_frame_control frame;
} fake_usb;

static void capture(void *context, const char *text)
{
    fake_usb *fake = context;
    size_t length = strlen(text);
    if (length < sizeof(fake->output) - fake->used) {
        memcpy(fake->output + fake->used, text, length + 1u);
        fake->used += (unsigned)length;
    }
}

static void get_status(void *context, struct fm1_doom_usb_status *status)
{
    *status = ((fake_usb *)context)->status;
}
static int get_heap_free(void *context)
{
    fake_usb *fake = context;
    ++fake->heap_queries;
    return fake->status.heap_free;
}
static void get_volume(void *context, struct fm1_doom_usb_volume *volume)
{
    fake_usb *fake = context;
    ++fake->volume_queries;
    *volume = fake->volume;
}
static void format_usb_audio(char *out, size_t length)
{ snprintf(out, length, "DOOM USB_AUDIO rate=44100 active=0\n"); }
static void set_speaker_muted(void *context, unsigned muted)
{
    fake_usb *fake = context;
    ++fake->mute_requests;
    fake->status.speaker_muted = muted;
}
static void set_music_monitor(void *context, unsigned mode)
{
    fake_usb *fake = context;
    ++fake->monitor_requests;
    fake->monitor_mode = mode;
}

static void request_stop(void *context) { ++((fake_usb *)context)->stop_requests; }
static int is_stopped(void *context) { return ((fake_usb *)context)->stopped; }
static int tx_drained(void *context) { return ((fake_usb *)context)->drained; }
static int boot_arm(void *context)
{
    fake_usb *fake = context;
    ++fake->arm_calls;
    return fake->arm_result;
}

static void get_game(void *context, struct fm1_doom_usb_game *game)
{ *game = ((fake_usb *)context)->game; }
static int frame_begin(void *context, uint32_t now)
{
    fake_usb *fake = context;
    if (fake->status.stage != 4 || fake->stopped || fake->stop_requests) return -1;
    return fm1_doom_usb_frame_control_begin(&fake->frame, now);
}
static void frame_info(void *context, fm1_doom_usb_frame_control *frame)
{ *frame = ((fake_usb *)context)->frame; }
static size_t frame_read(void *context, uint32_t offset, uint8_t *out, size_t capacity)
{
    fake_usb *fake = context;
    size_t i;
    ++fake->frame_reads;
    if (fake->frame.state != 2 || offset >= FM1_DOOM_USB_FRAME_BYTES) return 0;
    if (capacity > FM1_DOOM_USB_FRAME_BYTES - offset)
        capacity = FM1_DOOM_USB_FRAME_BYTES - offset;
    for (i = 0; i < capacity; ++i) out[i] = (uint8_t)(offset + i);
    return capacity;
}
static void frame_end(void *context)
{
    fake_usb *fake = context;
    ++fake->frame_ends; fake->frame.state = 0;
}
static void frame_tick(void *context, uint32_t now)
{ fm1_doom_usb_frame_control_tick(&((fake_usb *)context)->frame, now); }

static void feed(fm1_doom_usb_protocol *protocol, const char *text, uint32_t now,
                 const fm1_doom_usb_protocol_io *io)
{
    fm1_doom_usb_protocol_feed(protocol, (const uint8_t *)text, strlen(text), now, io);
}

static int music_monitor_commands_test(fake_usb *fake, fm1_doom_usb_protocol *protocol,
                                       const fm1_doom_usb_protocol_io *io)
{
    memset(fake, 0, sizeof(*fake));fm1_doom_usb_protocol_reset(protocol);
    feed(protocol, "DOOM MUSIC ME", 100, io);
    CHECK(!fake->used && !fake->monitor_requests);
    feed(protocol, "LODY\n", 101, io);
    CHECK(fake->monitor_requests == 1 && fake->monitor_mode == FM1_DOOM_MUSIC_MONITOR_MELODY);
    CHECK(protocol->monitor_mode == FM1_DOOM_MUSIC_MONITOR_MELODY && protocol->monitor_started_ms == 101);
    feed(protocol, "DOOM MUSIC DRUMS\n", 102, io);
    CHECK(fake->monitor_requests == 2 && fake->monitor_mode == FM1_DOOM_MUSIC_MONITOR_DRUMS);
    feed(protocol, "DOOM MUSIC PAUSE\n", 103, io);
    CHECK(fake->monitor_requests == 3 && fake->monitor_mode == FM1_DOOM_MUSIC_MONITOR_PAUSE);
    feed(protocol, "DOOM MUSIC FULL\n", 104, io);
    CHECK(fake->monitor_requests == 4 && fake->monitor_mode == FM1_DOOM_MUSIC_MONITOR_FULL);
    CHECK(!protocol->monitor_mode);
    CHECK(!strcmp(fake->output, "OK DOOM MUSIC MELODY\nOK DOOM MUSIC DRUMS\nOK DOOM MUSIC PAUSE\nOK DOOM MUSIC FULL\n"));
    CHECK(!fake->stop_requests && !fake->arm_calls && !fake->mute_requests);
    return 0;
}

static int music_monitor_invalid_commands_test(fake_usb *fake, fm1_doom_usb_protocol *protocol,
                                               const fm1_doom_usb_protocol_io *io)
{
    memset(fake, 0, sizeof(*fake));fm1_doom_usb_protocol_reset(protocol);
    feed(protocol, "DOOM MUSIC\nDOOM MUSIC 0\nDOOM MUSIC drums\nDOOM MUSIC DRUM\nDOOM MUSIC FULL \nDOOM MUSIC PAUSE X\nMUSIC DRUMS\n", 200, io);
    CHECK(!strcmp(fake->output, "ERR COMMAND\nERR COMMAND\nERR COMMAND\nERR COMMAND\nERR COMMAND\nERR COMMAND\nERR COMMAND\n"));
    feed(protocol, "DOOM MUSIC DRUMS\r\n", 201, io);
    CHECK(strstr(fake->output, "ERR LINE ABORTED\n"));
    CHECK(!fake->monitor_requests && !protocol->monitor_mode && !fake->stop_requests && !fake->arm_calls);
    return 0;
}

static int music_monitor_unavailable_test(fake_usb *fake, fm1_doom_usb_protocol *protocol,
                                          const fm1_doom_usb_protocol_io *io)
{
    fm1_doom_usb_protocol_io unavailable = *io;
    unavailable.set_music_monitor = 0;
    memset(fake, 0, sizeof(*fake));fm1_doom_usb_protocol_reset(protocol);
    feed(protocol, "DOOM MUSIC DRUMS\nDOOM MUSIC FULL\n", 300, &unavailable);
    CHECK(!strcmp(fake->output, "ERR MUSIC_MONITOR_UNAVAILABLE\nERR MUSIC_MONITOR_UNAVAILABLE\n"));
    CHECK(!fake->monitor_requests && !protocol->monitor_mode);
    fm1_doom_usb_protocol_tick(protocol, 15300, &unavailable);
    CHECK(!fake->monitor_requests && !protocol->monitor_mode);
    return 0;
}

static int music_monitor_deadline_test(fake_usb *fake, fm1_doom_usb_protocol *protocol,
                                       const fm1_doom_usb_protocol_io *io)
{
    unsigned mode;
    static const char *commands[]={"DOOM MUSIC MELODY\n", "DOOM MUSIC DRUMS\n", "DOOM MUSIC PAUSE\n"};
    for(mode=FM1_DOOM_MUSIC_MONITOR_MELODY;mode<=FM1_DOOM_MUSIC_MONITOR_PAUSE;mode++){
        memset(fake, 0, sizeof(*fake));fm1_doom_usb_protocol_reset(protocol);
        feed(protocol, commands[mode-1u], UINT32_MAX-10000u, io);
        fm1_doom_usb_protocol_tick(protocol, 4998u, io);
        CHECK(fake->monitor_requests == 1 && fake->monitor_mode == mode && protocol->monitor_mode == mode);
        fm1_doom_usb_protocol_tick(protocol, 4999u, io);
        CHECK(fake->monitor_requests == 2 && fake->monitor_mode == FM1_DOOM_MUSIC_MONITOR_FULL && !protocol->monitor_mode);
        CHECK(strstr(fake->output, "OK DOOM MUSIC FULL TIMEOUT\n"));
        fm1_doom_usb_protocol_tick(protocol, 19999u, io);
        CHECK(fake->monitor_requests == 2);
    }
    return 0;
}

static int music_monitor_full_cancels_deadline_test(fake_usb *fake, fm1_doom_usb_protocol *protocol,
                                                   const fm1_doom_usb_protocol_io *io)
{
    memset(fake, 0, sizeof(*fake));fm1_doom_usb_protocol_reset(protocol);
    feed(protocol, "DOOM MUSIC PAUSE\n", 400, io);
    feed(protocol, "DOOM MUSIC FULL\n", 500, io);
    fm1_doom_usb_protocol_tick(protocol, 15500, io);
    CHECK(fake->monitor_requests == 2 && fake->monitor_mode == FM1_DOOM_MUSIC_MONITOR_FULL);
    CHECK(!protocol->monitor_mode && !strstr(fake->output, "TIMEOUT"));
    return 0;
}

static int music_monitor_new_command_restarts_deadline_test(fake_usb *fake, fm1_doom_usb_protocol *protocol,
                                                            const fm1_doom_usb_protocol_io *io)
{
    memset(fake, 0, sizeof(*fake));fm1_doom_usb_protocol_reset(protocol);
    feed(protocol, "DOOM MUSIC MELODY\n", 1000, io);
    feed(protocol, "DOOM MUSIC DRUMS\n", 10000, io);
    fm1_doom_usb_protocol_tick(protocol, 16000, io);
    CHECK(fake->monitor_requests == 2 && fake->monitor_mode == FM1_DOOM_MUSIC_MONITOR_DRUMS);
    fm1_doom_usb_protocol_tick(protocol, 24999, io);
    CHECK(fake->monitor_requests == 2);
    fm1_doom_usb_protocol_tick(protocol, 25000, io);
    CHECK(fake->monitor_requests == 3 && fake->monitor_mode == FM1_DOOM_MUSIC_MONITOR_FULL);
    return 0;
}

static int music_monitor_fragment_timeout_restores_full_test(fake_usb *fake, fm1_doom_usb_protocol *protocol,
                                                             const fm1_doom_usb_protocol_io *io)
{
    memset(fake, 0, sizeof(*fake));fm1_doom_usb_protocol_reset(protocol);
    feed(protocol, "DOOM MUSIC PAUSE\n", 1000, io);
    feed(protocol, "DOOM MUSIC DR", 1001, io);
    fm1_doom_usb_protocol_tick(protocol, 11000, io);
    CHECK(fake->monitor_requests == 1 && fake->monitor_mode == FM1_DOOM_MUSIC_MONITOR_PAUSE && protocol->used);
    fm1_doom_usb_protocol_tick(protocol, 11001, io);
    CHECK(fake->monitor_requests == 2 && fake->monitor_mode == FM1_DOOM_MUSIC_MONITOR_FULL);
    CHECK(!protocol->monitor_mode && !protocol->used && !protocol->dropping);
    CHECK(strstr(fake->output, "ERR TIMEOUT ABORTED\n"));
    fm1_doom_usb_protocol_tick(protocol, 16000, io);
    CHECK(fake->monitor_requests == 2);
    feed(protocol, "UMS\n", 16001, io);
    CHECK(fake->monitor_requests == 2 && strstr(fake->output, "ERR COMMAND\n"));
    feed(protocol, "DOOM MUSIC DRUMS\n", 16002, io);
    CHECK(fake->monitor_requests == 3 && fake->monitor_mode == FM1_DOOM_MUSIC_MONITOR_DRUMS);
    return 0;
}

static int synth_edit_status_preserves_named_fields_test(fake_usb *fake,
                                                        fm1_doom_usb_protocol *protocol,
                                                        const fm1_doom_usb_protocol_io *io)
{
    memset(fake, 0, sizeof(*fake));fm1_doom_usb_protocol_reset(protocol);
    fake->status.edit_controls = 16u | (72u << 7) | (32u << 14);
    feed(protocol, "DOOM ED", 222, io);
    CHECK(!fake->used);
    feed(protocol, "IT\n", 222, io);
    CHECK(!strcmp(fake->output,
          "DOOM EDIT bank=0 preset=0 algorithm=0 vco=16 vcf=72 vca=32 reverb=0 synth_mode=0\n"));
    CHECK(!fake->stop_requests && !fake->arm_calls && !fake->monitor_requests && !fake->mute_requests);
    return 0;
}

static int synth_edit_status_masks_packed_field_widths_test(fake_usb *fake,
                                                          fm1_doom_usb_protocol *protocol,
                                                          const fm1_doom_usb_protocol_io *io)
{
    memset(fake, 0, sizeof(*fake));fm1_doom_usb_protocol_reset(protocol);
    fake->status.edit_controls = UINT32_MAX;
    fake->status.synth_mode = 1;
    feed(protocol, "DOOM EDIT\n", 222, io);
    CHECK(!strcmp(fake->output,
          "DOOM EDIT bank=0 preset=3 algorithm=3 vco=127 vcf=127 vca=127 reverb=127 synth_mode=1\n"));
    CHECK(!fake->stop_requests && !fake->arm_calls && !fake->monitor_requests && !fake->mute_requests);
    return 0;
}

static int nes_edit_status_names_the_filter_and_lfo_controls_test(fake_usb *fake,
                                                                fm1_doom_usb_protocol *protocol,
                                                                const fm1_doom_usb_protocol_io *io)
{
    memset(fake, 0, sizeof(*fake));fm1_doom_usb_protocol_reset(protocol);
    fake->status.edit_bank = FM1_DOOM_EDIT_NES_FX;
    fake->status.edit_controls = 84u | (100u << 7) | (29u << 14) | (96u << 21)
                               | (3u << 28) | (2u << 30);
    fake->status.synth_mode = 1;
    feed(protocol, "DOOM EDIT\n", 222, io);
    CHECK(!strcmp(fake->output,
          "DOOM EDIT bank=1 preset=3 algorithm=2 cutoff=84 resonance=100 rate=29 depth=96 synth_mode=1\n"));
    CHECK(!strstr(fake->output, "vco=") && !strstr(fake->output, "reverb="));
    CHECK(!fake->stop_requests && !fake->arm_calls && !fake->monitor_requests && !fake->mute_requests);
    return 0;
}

static int nes_edit_status_masks_packed_fields_independently_of_source_test(fake_usb *fake,
                                                                          fm1_doom_usb_protocol *protocol,
                                                                          const fm1_doom_usb_protocol_io *io)
{
    memset(fake, 0, sizeof(*fake));fm1_doom_usb_protocol_reset(protocol);
    fake->status.edit_bank = FM1_DOOM_EDIT_NES_FX;
    fake->status.edit_controls = UINT32_MAX;
    feed(protocol, "DOOM EDIT\n", 222, io);
    CHECK(!strcmp(fake->output,
          "DOOM EDIT bank=1 preset=3 algorithm=3 cutoff=127 resonance=127 rate=127 depth=127 synth_mode=0\n"));
    CHECK(!fake->stop_requests && !fake->arm_calls && !fake->monitor_requests && !fake->mute_requests);
    return 0;
}

int main(void)
{
    fake_usb fake = {0};
    fm1_doom_usb_protocol protocol;
    const fm1_doom_usb_protocol_io io = {
        &fake, capture, get_status, request_stop, is_stopped, tx_drained, boot_arm,
        get_game, frame_begin, frame_info, frame_read, frame_end, frame_tick,
        get_heap_free, get_volume, format_usb_audio, set_speaker_muted, set_music_monitor
    };
    char overflow[FM1_DOOM_USB_LINE_BYTES + 16u];
    unsigned i;
    CHECK(!music_monitor_commands_test(&fake, &protocol, &io));
    CHECK(!music_monitor_invalid_commands_test(&fake, &protocol, &io));
    CHECK(!music_monitor_unavailable_test(&fake, &protocol, &io));
    CHECK(!music_monitor_deadline_test(&fake, &protocol, &io));
    CHECK(!music_monitor_full_cancels_deadline_test(&fake, &protocol, &io));
    CHECK(!music_monitor_new_command_restarts_deadline_test(&fake, &protocol, &io));
    CHECK(!music_monitor_fragment_timeout_restores_full_test(&fake, &protocol, &io));
    CHECK(!synth_edit_status_preserves_named_fields_test(&fake, &protocol, &io));
    CHECK(!synth_edit_status_masks_packed_field_widths_test(&fake, &protocol, &io));
    CHECK(!nes_edit_status_names_the_filter_and_lfo_controls_test(&fake, &protocol, &io));
    CHECK(!nes_edit_status_masks_packed_fields_independently_of_source_test(&fake, &protocol, &io));
    memset(&fake, 0, sizeof(fake));
    fm1_doom_usb_protocol_reset(&protocol);
    feed(&protocol, "HE", 100, &io);
    CHECK(!fake.used);
    feed(&protocol, "LLO\n", 101, &io);
    CHECK(!strcmp(fake.output, "FM1DIAG/1 DOOM-FM1/1 UBOOT=SERIAL COMMIT=BLOCKED\n"));
    memset(&fake, 0, sizeof(fake));
    feed(&protocol, "DOOM USB_AUDIO\n", 102, &io);
    CHECK(!strcmp(fake.output, "DOOM USB_AUDIO rate=44100 active=0\n"));
    { fm1_doom_usb_protocol_io unavailable = io;
      unavailable.format_usb_audio = 0;
      memset(&fake, 0, sizeof(fake));
      feed(&protocol, "DOOM USB_AUDIO\n", 103, &unavailable);
      CHECK(!strcmp(fake.output, "ERR USB_AUDIO_UNAVAILABLE\n")); }

    memset(&fake, 0, sizeof(fake));
    feed(&protocol, "DOOM MU", 104, &io);
    CHECK(!fake.mute_requests && !fake.used);
    feed(&protocol, "TE 1\n", 105, &io);
    CHECK(fake.mute_requests == 1 && fake.status.speaker_muted == 1);
    CHECK(!strcmp(fake.output, "OK DOOM MUTE 1\n"));
    feed(&protocol, "DOOM MUTE 0\n", 106, &io);
    CHECK(fake.mute_requests == 2 && !fake.status.speaker_muted);
    CHECK(!strcmp(fake.output, "OK DOOM MUTE 1\nOK DOOM MUTE 0\n"));
    feed(&protocol, "DOOM MUTE 2\nDOOM MUTE -1\nDOOM MUTE 01\nDOOM MUTE 1 X\nMUTE 1\n", 107, &io);
    CHECK(fake.mute_requests == 2 && !fake.status.speaker_muted);
    feed(&protocol, "DOOM MUTE 1\r\n", 108, &io);
    CHECK(fake.mute_requests == 2 && strstr(fake.output, "ERR LINE ABORTED\n"));
    { fm1_doom_usb_protocol_io unavailable = io;
      unavailable.set_speaker_muted = 0;
      feed(&protocol, "DOOM MUTE 1\n", 109, &unavailable);
      CHECK(fake.mute_requests == 2 && !fake.status.speaker_muted);
      CHECK(strstr(fake.output, "ERR MUTE_UNAVAILABLE\n")); }
    CHECK(!fake.stop_requests && !fake.arm_calls && !fake.volume_queries && !fake.heap_queries);

    memset(&fake, 0, sizeof(fake));
    fake.status.stage = 255;
    fake.status.fault = -30;
    fake.status.frames = 37;
    fake.status.lcd_stage = 4;
    fake.status.lcd_error = -2;
    fake.status.key_error = -3;
    fake.status.sys_hz = 240000000;
    fake.status.lsb_hz = 60000000;
    memcpy(fake.status.error_message, "bad\nSDK\rpanic", sizeof("bad\nSDK\rpanic"));
    feed(&protocol, "DOOM STATUS\nSTATUS\n", 200, &io);
    CHECK(strstr(fake.output, "stage=255 fault=-30 frames=37 stopped=0 lcd_stage=4 lcd_error=-2 key_error=-3 sys_hz=240000000 lsb_hz=60000000 error=bad SDK panic\n"));
    CHECK(!strchr(fake.output, '\r'));
    CHECK(strstr(fake.output + 1, "DOOM STATUS"));
    CHECK(!fake.heap_queries);
    CHECK(!fake.volume_queries);

    memset(&fake, 0, sizeof(fake));
    fake.status.now_ms = 12000;
    fake.status.tick_entered = 81;
    fake.status.tick_completed = 80;
    fake.status.phase = 2;
    fake.status.lcd_y = 232;
    fake.status.scan_kicks = 12001;
    fake.status.scan_irqs = 132011;
    fake.status.key_lo = 131072;
    fake.status.key_hi = 256;
    fake.status.scan_sequence = 11000;
    fake.status.scan_completions = 121000;
    fake.status.scan_age_ms = 105;
    fake.status.input_retries = 5;
    fake.status.max_frame_interval_ms = 201;
    fake.status.scan_failure_reason = 4;
    fake.status.scan_failure_row = 7;
    fake.status.scan_failure_con = 4096;
    fake.status.scan_failure_dma_count = 2;
    feed(&protocol, "DOOM TR", 210, &io);
    CHECK(!fake.used);
    feed(&protocol, "ACE\n", 211, &io);
    CHECK(!strcmp(fake.output,
          "DOOM TRACE ms=12000 entered=81 completed=80 phase=2 y=232 kicks=12001 irqs=132011 key_lo=131072 key_hi=256 sequence=11000 scans=121000 age_ms=105 retries=5 max_frame_ms=201 fail=4 row=7 con=4096 dma=2 coarse=0\n"));
    CHECK(!fake.stop_requests && !fake.arm_calls);
    feed(&protocol, "DOOM TRACE\r\n", 212, &io);
    CHECK(strstr(fake.output, "ERR LINE ABORTED\n"));
    feed(&protocol, "TRACE\nDOOM TRACE X\n", 213, &io);
    CHECK(strstr(fake.output, "ERR COMMAND\nERR COMMAND\n"));

    memset(&fake, 0, sizeof(fake));
    fake.status.now_ms = fake.status.tick_entered = fake.status.tick_completed = UINT32_MAX;
    fake.status.phase = fake.status.lcd_y = fake.status.scan_kicks = UINT32_MAX;
    fake.status.scan_irqs = fake.status.key_lo = fake.status.key_hi = UINT32_MAX;
    fake.status.scan_sequence = fake.status.scan_completions = UINT32_MAX;
    fake.status.scan_age_ms = fake.status.input_retries = UINT32_MAX;
    fake.status.max_frame_interval_ms = UINT32_MAX;
    fake.status.scan_failure_reason = fake.status.scan_failure_row = UINT32_MAX;
    fake.status.scan_failure_con = fake.status.scan_failure_dma_count = UINT32_MAX;
    fake.status.coarse_gameplay = UINT32_MAX;
    feed(&protocol, "DOOM TRACE\nSTATUS\n", 214, &io);
    CHECK(strstr(fake.output,
          "DOOM TRACE ms=4294967295 entered=4294967295 completed=4294967295 phase=4294967295 y=4294967295 kicks=4294967295 irqs=4294967295 key_lo=4294967295 key_hi=4294967295 sequence=4294967295 scans=4294967295 age_ms=4294967295 retries=4294967295 max_frame_ms=4294967295 fail=4294967295 row=4294967295 con=4294967295 dma=4294967295 coarse=4294967295\n"));
    CHECK(strchr(fake.output, '\n') - fake.output < 350);
    CHECK(strstr(fake.output,
          "\nDOOM STATUS stage=0 fault=0 frames=0 stopped=0 lcd_stage=0 lcd_error=0 key_error=0 sys_hz=0 lsb_hz=0 error=\n"));
    CHECK(!fake.stop_requests && !fake.arm_calls);

    memset(&fake, 0, sizeof(fake));
    fake.status.audio_ready = 1;
    fake.status.audio_error = -42;
    fake.status.audio_irqs = fake.status.audio_frames = UINT32_MAX;
    fake.status.sfx_started = UINT32_MAX;
    fake.status.sfx_voices = 2;
    fake.status.music_playing = 1;
    fake.status.music_ticks = fake.status.music_events = UINT32_MAX;
    fake.status.music_loops = fake.status.music_steals = UINT32_MAX;
    fake.status.music_errors = UINT32_MAX;
    fake.status.music_voices = 8;
    fake.status.usb_stack_words = 500;
    fake.status.max_audio_irq_us = 1000;
    fake.status.volume_raw = 1023;
    fake.status.volume_gain = 127;
    fake.status.volume_valid = 1;
    fake.status.volume_errors = UINT32_MAX;
    fake.status.volume_samples = UINT32_MAX;
    fake.status.volume_target = 127;
    fake.status.synth_mode = 1;
    fake.status.speaker_muted = 1;
    fake.status.heap_free = INT32_MAX;
    feed(&protocol, "DOOM AU", 220, &io);
    CHECK(!fake.used);
    feed(&protocol, "DIO\n", 221, &io);
    CHECK(!strcmp(fake.output,
          "DOOM AUDIO ready=1 error=-42 irqs=4294967295 frames=4294967295 sfx_started=4294967295 sfx_voices=2 music_playing=1 music_ticks=4294967295 music_events=4294967295 music_loops=4294967295 music_steals=4294967295 music_errors=4294967295 music_voices=8 usb_stack_words=500 max_irq_us=1000 volume_raw=1023 volume_gain=127 volume_valid=1 volume_errors=4294967295 volume_samples=4294967295 volume_target=127 synth_mode=1 speaker_muted=1 heap_free=2147483647\n"));
    CHECK(fake.used < 512 && fake.output[fake.used - 1] == '\n' && !fake.stop_requests && !fake.arm_calls);
    CHECK(fake.heap_queries == 1);
    CHECK(!fake.volume_queries);
    feed(&protocol, "DOOM AUDIO\r\n", 222, &io);
    CHECK(strstr(fake.output, "ERR LINE ABORTED\n"));

    memset(&fake, 0, sizeof(fake));
    fake.volume.now_ms = UINT32_MAX;
    fake.volume.sys_hz = INT32_MIN; fake.volume.lsb_hz = INT32_MAX;
    fake.volume.hardware.adc_con = 0xf4deu; fake.volume.hardware.adc_res = 260;
    fake.volume.hardware.pb_dir = UINT32_MAX; fake.volume.hardware.pb_die = 0xffffffbfu;
    fake.volume.hardware.pb_pu = fake.volume.hardware.pb_pd = fake.volume.hardware.pb_hd0 = 0xffffffbfu;
    fake.volume.hardware.pb_hd1 = 0x12345678u; fake.volume.hardware.pb_dieh = 0xffffffbfu;
    fake.volume.hardware.wla_con0 = 0x20005u; fake.volume.hardware.pll_con1 = 0xfffeffffu;
    fake.volume.hardware.raw = fake.volume.hardware.accepted = 260;
    fake.volume.hardware.target = fake.volume.hardware.gain = 32;
    fake.volume.hardware.running = fake.volume.hardware.valid = 1;
    fake.volume.hardware.samples = fake.volume.hardware.errors = UINT32_MAX;
    feed(&protocol, "DOOM VOL", 223, &io);
    CHECK(!fake.used && !fake.volume_queries);
    feed(&protocol, "UME\n", 224, &io);
    CHECK(!strcmp(fake.output,
          "DOOM VOLUME ms=4294967295 adc_con=0000f4de adc_res=260 pb_dir=ffffffff pb_die=ffffffbf pb_pu=ffffffbf pb_pd=ffffffbf pb_hd0=ffffffbf pb_hd1=12345678 pb_dieh=ffffffbf wla_con0=00020005 pll_con1=fffeffff raw=260 accepted=260 target=32 gain=32 run=1 valid=1 waiting=0 samples=4294967295 errors=4294967295 sys_hz=-2147483648 lsb_hz=2147483647\n"));
    CHECK(fake.volume_queries == 1 && !fake.heap_queries && !fake.stop_requests && !fake.arm_calls);
    CHECK(fake.used < 512 && fake.output[fake.used - 1] == '\n');
    feed(&protocol, "VOLUME\n", 225, &io);
    CHECK(fake.volume_queries == 2);
    {
        fm1_doom_usb_protocol_io without_volume = io;
        without_volume.get_volume = 0;
        feed(&protocol, "DOOM VOLUME\n", 226, &without_volume);
        CHECK(strstr(fake.output, "ERR VOLUME_UNAVAILABLE\n") && fake.volume_queries == 2);
    }
    feed(&protocol, "DOOM VOLUME X\n", 227, &io);
    CHECK(strstr(fake.output, "ERR COMMAND\n") && fake.volume_queries == 2);
    fake.used = 0; fake.output[0] = 0;
    memset(&fake.volume.hardware, 0xff, sizeof(fake.volume.hardware));
    feed(&protocol, "DOOM VOLUME\n", 228, &io);
    CHECK(fake.used < 512 && fake.output[fake.used - 1] == '\n' &&
          strstr(fake.output, "errors=4294967295 sys_hz=-2147483648 lsb_hz=2147483647\n"));
    CHECK(fake.volume_queries == 3 && !fake.heap_queries && !fake.stop_requests && !fake.arm_calls);

    memset(&fake, 0, sizeof(fake));
    fake.game.stage = 4; fake.game.tic = 123; fake.game.state = 0;
    fake.game.skill = 2; fake.game.x = -65536; fake.game.y = 131072;
    fake.game.angle = UINT32_MAX; fake.game.health = 97;
    fake.game.bullets = 43; fake.game.shells = 4; fake.game.weapon = 1;
    fake.game.total_kills = 6; fake.game.kills = 1; fake.game.sector = 3;
    fake.game.things = 138; fake.game.vertexes = 467; fake.game.lines = 475;
    fake.game.sides = 648; fake.game.sectors = 85; fake.game.segs = 732;
    fake.game.subsectors = 237; fake.game.nodes = 236;
    fake.game.menu = 0; fake.game.coarse = 1;
    feed(&protocol, "DOOM GAME\n", 230, &io);
    CHECK(!strcmp(fake.output,
          "DOOM GAME stage=4 tic=123 state=0 skill=2 x=-65536 y=131072 angle=4294967295 health=97 bullets=43 shells=4 weapon=1 total=6 killed=1 sector=3 things=138 vertexes=467 lines=475 sides=648 sectors=85 segs=732 subsectors=237 nodes=236 menu=0 coarse=1\n"));
    CHECK(!fake.stop_requests && !fake.arm_calls && !fake.frame_reads);

    memset(&fake, 0, sizeof(fake));
    feed(&protocol, "DOOM FRAME BEGIN\n", 231, &io);
    CHECK(!strcmp(fake.output, "ERR FRAME_UNAVAILABLE\n"));
    CHECK(!fake.frame.state);
    fake.status.stage = 4;
    fake.used = 0; fake.output[0] = 0;
    feed(&protocol, "DOOM FRAME BEGIN\nDOOM FRAMEINFO\n", 232, &io);
    CHECK(!strcmp(fake.output,
          "OK DOOM FRAME REQUESTED\nDOOM FRAMEINFO state=1 ready=0 id=1 width=160 height=100 bytes=16512 coarse=0 menu=0\n"));
    CHECK(fake.frame.started_ms == 232);
    feed(&protocol, "DOOM FRAME BEGIN\nDOOM FRAME READ 0\n", 233, &io);
    CHECK(strstr(fake.output, "ERR FRAME_BUSY\nERR FRAME_NOT_READY\n"));
    CHECK(!fake.frame_reads && fake.frame.started_ms == 232);
    fake.frame.state = 2; fake.frame.coarse = 1;
    fake.used = 0; fake.output[0] = 0;
    feed(&protocol, "DOOM FRAME READ 0\n", 234, &io);
    CHECK(!strcmp(fake.output,
          "DOOM FRAME id=1 offset=0 bytes=96 data=000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F202122232425262728292A2B2C2D2E2F303132333435363738393A3B3C3D3E3F404142434445464748494A4B4C4D4E4F505152535455565758595A5B5C5D5E5F\n"));
    CHECK(fake.frame_reads == 1 && !fake.stop_requests && !fake.arm_calls);
    fake.used = 0; fake.output[0] = 0;
    feed(&protocol, "DOOM FRAME READ 16511\n", 235, &io);
    CHECK(!strcmp(fake.output, "DOOM FRAME id=1 offset=16511 bytes=1 data=7F\n"));
    feed(&protocol, "DOOM FRAME READ 16512\nDOOM FRAME READ -1\nDOOM FRAME READ 0x10\nDOOM FRAME READ 0 \nDOOM FRAME READ 42949672960\nDOOM FRAME READ \n", 236, &io);
    CHECK(fake.frame_reads == 2);
    CHECK(strstr(fake.output, "ERR FRAME_OFFSET\nERR FRAME_OFFSET\nERR FRAME_OFFSET\nERR FRAME_OFFSET\nERR FRAME_OFFSET\nERR FRAME_OFFSET\n"));
    feed(&protocol, "DOOM FRAME END\nDOOM FRAME READ 0\n", 237, &io);
    CHECK(!fake.frame.state && fake.frame_ends == 1);
    CHECK(strstr(fake.output, "OK DOOM FRAME END\nERR FRAME_NOT_READY\n"));
    CHECK(fake.frame_reads == 2);

    memset(&fake, 0, sizeof(fake));
    fake.status.stage = 4;
    feed(&protocol, "DOOM FRAME BEGIN\n", UINT32_MAX - 10000u, &io);
    fake.frame.state = 2;
    fm1_doom_usb_protocol_tick(&protocol, 4998u, &io);
    CHECK(fake.frame.state == 2);
    fm1_doom_usb_protocol_tick(&protocol, 4999u, &io);
    CHECK(fake.frame.state == 0); /* hard deadline survives clock wrap */
    feed(&protocol, "DOOM FRAME READ 0\nDOOM FRAME BEGIN\n", 5000u, &io);
    CHECK(strstr(fake.output, "ERR FRAME_NOT_READY\nOK DOOM FRAME REQUESTED\n"));
    CHECK(fake.frame.id == 2);
    fake.frame.state = 2;
    feed(&protocol, "DOOM FRAME READ ", 5001u, &io);
    fm1_doom_usb_protocol_tick(&protocol, 15001u, &io);
    CHECK(!protocol.used && !fake.frame.state && fake.frame_ends == 1);
    CHECK(strstr(fake.output, "ERR TIMEOUT ABORTED\n"));
    feed(&protocol, "DOOM FRAME BEGIN\nDOOM STOP\n", 15002u, &io);
    CHECK(fake.stop_requests == 1 && !fake.frame.state && fake.frame_ends == 2);
    feed(&protocol, "DOOM FRAME BEGIN\n", 15003u, &io);
    CHECK(strstr(fake.output, "OK DOOM STOP REQUESTED\nERR FRAME_UNAVAILABLE\n"));

    memset(&fake, 0, sizeof(fake));
    feed(&protocol, "DOOM STOP\n", 300, &io);
    CHECK(fake.stop_requests == 1);
    CHECK(!strcmp(fake.output, "OK DOOM STOP REQUESTED\n"));
    fake.drained = fake.arm_result = 1;
    feed(&protocol, "UBOOT\n", 301, &io);
    CHECK(!fake.arm_calls);
    CHECK(strstr(fake.output, "ERR UBOOT_RETRY_AFTER_DOOM_STOP\n"));
    fake.stopped = 1;
    fake.drained = 0;
    feed(&protocol, "UBOOT\n", 302, &io);
    CHECK(!fake.arm_calls && strstr(fake.output, "ERR UBOOT_RETRY\n"));
    fake.drained = 1;
    fake.arm_result = 0;
    feed(&protocol, "UBOOT\n", 303, &io);
    CHECK(fake.arm_calls == 1);
    fake.arm_result = 1;
    feed(&protocol, "UB", 304, &io);
    CHECK(fake.arm_calls == 1);
    feed(&protocol, "OOT\n", 305, &io);
    CHECK(fake.arm_calls == 2);
    CHECK(strstr(fake.output, "OK UBOOT ARMED CONFIRM-WITHIN-5000MS\n"));

    memset(&fake, 0, sizeof(fake));
    fake.stopped = fake.drained = fake.arm_result = 1;
    feed(&protocol, "UBOOT\nHELLO\n", 400, &io);
    feed(&protocol, "HELLO\nUBOOT\n", 401, &io);
    feed(&protocol, "DOOM STOP\nUBOOT\n", 402, &io);
    feed(&protocol, "UBOOT\nX", 403, &io);
    CHECK(!fake.arm_calls);
    fm1_doom_usb_protocol_reset(&protocol);
    feed(&protocol, "UBOOT\r\n", 404, &io);
    feed(&protocol, "UBOOT \n", 405, &io);
    feed(&protocol, "UBOOT CONFIRM\n", 406, &io);
    CHECK(!fake.arm_calls);
    CHECK(strstr(fake.output, "ERR LINE ABORTED\n"));
    {
        const uint8_t invalid[] = {'U', 'B', 0, 'O', 'O', 'T', '\n'};
        fm1_doom_usb_protocol_feed(&protocol, invalid, sizeof(invalid), 407, &io);
        CHECK(!fake.arm_calls && !protocol.dropping);
    }

    memset(overflow, 'A', sizeof(overflow));
    memcpy(overflow + sizeof(overflow) - 7u, "UBOOT\n", 7u);
    feed(&protocol, overflow, 500, &io);
    CHECK(!fake.arm_calls && !protocol.used && !protocol.dropping);
    feed(&protocol, "UBOOT\n", 501, &io);
    CHECK(fake.arm_calls == 1);

    memset(&fake, 0, sizeof(fake));
    fake.stopped = fake.drained = fake.arm_result = 1;
    feed(&protocol, "UB", UINT32_MAX - 5000u, &io);
    fm1_doom_usb_protocol_tick(&protocol, 4998u, &io);
    CHECK(protocol.used == 2 && !fake.used);
    fm1_doom_usb_protocol_tick(&protocol, 4999u, &io);
    CHECK(!protocol.used && strstr(fake.output, "ERR TIMEOUT ABORTED\n"));
    feed(&protocol, "OOT\n", 5000u, &io);
    CHECK(!fake.arm_calls);
    feed(&protocol, "UBOOT\n", 5001u, &io);
    CHECK(fake.arm_calls == 1);

    memset(&fake, 0, sizeof(fake));
    feed(&protocol, "\r", 6000, &io);
    CHECK(protocol.dropping);
    fm1_doom_usb_protocol_tick(&protocol, 16000u, &io);
    CHECK(!protocol.dropping);
    feed(&protocol, "COMMIT\nHELLO\n", 16001u, &io);
    CHECK(strstr(fake.output, "ERR COMMIT_BLOCKED\n"));
    CHECK(strstr(fake.output, "DOOM-FM1/1"));

    memset(&fake, 0, sizeof(fake));
    fake.status.fault = -11;
    fake.status.stage = 255;
    fake.stopped = fake.drained = fake.arm_result = 1;
    for (i = 0; i < sizeof(fake.status.error_message); ++i)
        fake.status.error_message[i] = 'x';
    fm1_doom_usb_protocol_status(&io);
    CHECK(strstr(fake.output, "stage=255 fault=-11"));
    CHECK(fake.used && fake.output[fake.used - 1u] == '\n');
    feed(&protocol, "UBOOT\n", 17000u, &io);
    CHECK(fake.arm_calls == 1);
    puts("FM-1 Doom USB framing, GAME/FRAME bounds, music monitor recovery, timeout, STOP and serial UBOOT contract passed");
    return 0;
}
