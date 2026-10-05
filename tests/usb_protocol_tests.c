#include "fm1_doom_usb.h"
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

typedef struct {
    char output[8192];
    unsigned used, stop_requests, arm_calls, frame_ends, frame_reads;
    int stopped, drained, arm_result;
    struct fm1_doom_usb_status status;
    struct fm1_doom_usb_game game;
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

int main(void)
{
    fake_usb fake = {0};
    fm1_doom_usb_protocol protocol;
    const fm1_doom_usb_protocol_io io = {
        &fake, capture, get_status, request_stop, is_stopped, tx_drained, boot_arm,
        get_game, frame_begin, frame_info, frame_read, frame_end, frame_tick
    };
    char overflow[FM1_DOOM_USB_LINE_BYTES + 16u];
    unsigned i;
    fm1_doom_usb_protocol_reset(&protocol);
    feed(&protocol, "HE", 100, &io);
    CHECK(!fake.used);
    feed(&protocol, "LLO\n", 101, &io);
    CHECK(!strcmp(fake.output, "FM1DIAG/1 DOOM-FM1/1 UBOOT=SERIAL COMMIT=BLOCKED\n"));

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
    fake.status.synth_mode = 1;
    feed(&protocol, "DOOM AU", 220, &io);
    CHECK(!fake.used);
    feed(&protocol, "DIO\n", 221, &io);
    CHECK(!strcmp(fake.output,
          "DOOM AUDIO ready=1 error=-42 irqs=4294967295 frames=4294967295 sfx_started=4294967295 sfx_voices=2 music_playing=1 music_ticks=4294967295 music_events=4294967295 music_loops=4294967295 music_steals=4294967295 music_errors=4294967295 music_voices=8 usb_stack_words=500 max_irq_us=1000 volume_raw=1023 volume_gain=127 volume_valid=1 volume_errors=4294967295 synth_mode=1\n"));
    CHECK(fake.used < 510 && !fake.stop_requests && !fake.arm_calls);
    feed(&protocol, "DOOM AUDIO\r\n", 222, &io);
    CHECK(strstr(fake.output, "ERR LINE ABORTED\n"));

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
    puts("FM-1 Doom USB framing, GAME/FRAME bounds, timeout, STOP and serial UBOOT contract passed");
    return 0;
}
