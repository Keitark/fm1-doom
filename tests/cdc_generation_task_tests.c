/* Compile the production task unchanged. The generated overlay writer and
 * actual bounded packet helper participate in the cancellation transaction. */
#include "cdc_task_sdk_fake.h"
#include "fm1_doom_usb.h"
#include "fm1_usb_audio_target.h"
#include <limits.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"CDC generation line %d: %s\n",__LINE__,#x); exit(1); } } while (0)

volatile unsigned fm1_cdc_generation = 10;
struct fm1_packet_regs fm1_packet_regs;
static struct usb_device_t device = {USB_CONFIGURED};
static uint32_t dma[64];
static unsigned iteration, writes, queries, syncs, csr, reads, wrapper_calls;
static unsigned change_before_call;
static unsigned irq_enabled = 1, irq_depth, frame_ends, audio_inits;
static jmp_buf finished;
static unsigned stale_queued_bytes;
static unsigned diagnostics, burst, received_length;
static const char *diagnostic_command;
static char expected_reply[512], received_reply[512];
static size_t expected_length;
static unsigned startup_waits;

/* Include rather than clone the task, making queue state observable to the
 * fake scheduler without adding hooks or storage to firmware. */
#include "../src/fm1_doom_usb.c"

struct fake_cdc { unsigned bmTransceiver; };
static struct fake_cdc handle = {17};
static struct fake_cdc *cdc_hdl[1] = {&handle};
#define BIT(n) (1u << (n))
#define MAXP_SIZE_CDC_BULKIN 64u
#define CDC_DATA_EP_IN 3u
#define fm1_cdc_write_packet overlay_cdc_write_packet
#include "cdc_overlay_generated.inc"
#undef fm1_cdc_write_packet

u32 fm1_cdc_write_packet(usb_dev id, u8 *data, u32 length, unsigned generation)
{
    ++wrapper_calls;
    if (diagnostics) {
        CHECK(!id && length && length <= 63 && generation == 10);
        CHECK(tx_write == expected_length && tx_read <= tx_write);
        CHECK(!memcmp(data, expected_reply + tx_read, length));
        return overlay_cdc_write_packet(id, data, length, generation);
    }
    CHECK(!id && length && length <= 63 && tx_read == 0);
    if (!iteration) {
        CHECK(generation == 10 && fm1_cdc_generation == (change_before_call ? 11u : 10u));
        stale_queued_bytes = tx_write;
        CHECK(stale_queued_bytes >= length && !memcmp(data, tx, length));
        /* RTS1->3 increments the session while ready remains true, after
         * queue bytes have been copied and before the writer checks them. */
        if (!change_before_call) ++fm1_cdc_generation;
        CHECK(fm1_cdc_ready(0));
    } else CHECK(iteration == 2 && generation == 11);
    return overlay_cdc_write_packet(id, data, length, generation);
}

unsigned fm1_packet_irq_save(void)
{ unsigned old = irq_enabled; irq_enabled = 0; return old; }
void fm1_packet_irq_restore(unsigned old)
{ CHECK(!irq_depth); irq_enabled = old; }
void __local_irq_disable(void) { irq_enabled = 0; ++irq_depth; }
void __local_irq_enable(void) { CHECK(irq_depth); if (!--irq_depth) irq_enabled = 1; }
struct usb_device_t *usb_id2device(usb_dev id)
{ CHECK(!id && irq_depth && !irq_enabled); return &device; }
unsigned usb_read_txcsr(usb_dev id, unsigned ep)
{
    unsigned value;
    CHECK(!id && ep == 3 && irq_depth && !irq_enabled);
    __local_irq_disable(); value = csr; __local_irq_enable();
    CHECK(irq_depth && !irq_enabled); ++reads; return value;
}
void *usb_get_dma_taddr(usb_dev id, unsigned ep)
{ CHECK(!id && ep == 3 && irq_depth && !irq_enabled); ++queries; return dma; }
void __asm_csync(void) { CHECK(irq_depth && !irq_enabled); ++syncs; }
void usb_write_txcsr(usb_dev id, unsigned ep, unsigned value)
{
    CHECK(!id && ep == 3 && irq_depth && !irq_enabled && syncs == writes + 1);
    if (diagnostics) {
        unsigned length = fm1_packet_regs.EP3_CNT;
        CHECK(length && length <= 63 && received_length + length <= sizeof(received_reply));
        memcpy(received_reply + received_length, dma, length);
        received_length += length;
    }
    __local_irq_disable(); csr = value; __local_irq_enable();
    CHECK(irq_depth && !irq_enabled); ++writes;
}

int fm1_cdc_ready(usb_dev id)
{ CHECK(!id); return (handle.bmTransceiver & 17u) == 17u; }
int usb_device_mode(usb_dev id, unsigned classes)
{ CHECK(!id && classes == CDC_CLASS && startup_waits == 3); return 0; }
int fm1_doom_usb_board_ready(void) { return startup_waits == 3; }
void fm1_doom_usb_debug_snapshot(unsigned phase)
{ CHECK(startup_waits == 3 && phase <= 1); }
void fm1_doom_music_set_monitor(unsigned mode) { (void)mode; CHECK(0); }
unsigned fm1_doom_sound_lock(void) { CHECK(0); return 0; }
void fm1_doom_sound_unlock(unsigned flags) { (void)flags; CHECK(0); }
uint32_t timer_get_ms(void) { return iteration * 10u; }
u32 cdc_read_data(usb_dev id, u8 *out, u32 capacity)
{
    static const char hello[] = "HELLO\n";
    CHECK(!id && capacity == 64 && fm1_cdc_ready(0));
    if (diagnostics) {
        if (!iteration) {
            size_t length = strlen(diagnostic_command);
            CHECK(length * (burst ? 2u : 1u) <= capacity);
            memcpy(out, diagnostic_command, length);
            if (burst) memcpy(out + length, diagnostic_command, length);
            return (u32)(length * (burst ? 2u : 1u));
        }
        return 0;
    }
    if (iteration == 1) {
        CHECK(frame_ends == 2 && !tx_read && !tx_write && !protocol.used && !protocol.dropping);
        CHECK(!writes && !queries && !syncs && wrapper_calls == 1);
    }
    if (iteration == 2 || (!iteration && change_before_call)) {
        memcpy(out, hello, sizeof(hello)-1); return sizeof(hello)-1;
    }
    return 0;
}
unsigned fm1_usb_rx_generation(void) { return fm1_cdc_generation; }
int fm1_usb_rx_fault(void) { return 0; }
int fm1_usb_boot_pending(void)
{
    if (diagnostics) return 1;
    if (!iteration && change_before_call) {
        /* This companion schedule changes generation after the task has
         * checked RX ownership and queued HELLO, before evaluating writer
         * arguments. It catches passing the live scalar instead of the
         * captured local generation even with the corrected overlay. */
        CHECK(tx_write > tx_read && fm1_cdc_generation == 10);
        ++fm1_cdc_generation;
        return 1;
    }
    return iteration != 0;
}
int fm1_usb_boot_arm(void) { CHECK(0); return 0; }
void fm1_usb_audio_target_init(void) { ++audio_inits; }
void fm1_usb_audio_target_status(char *out, size_t size)
{
    size_t i;
    if (!size) return;
    if (!diagnostics) { *out = 0; return; }
    /* Exercise the adapter callback's entire 447-byte output contract. */
    CHECK(size == 448);
    for (i = 0; i + 1 < size; ++i) out[i] = (char)('A' + i % 26);
    out[size - 2] = '\n'; out[size - 1] = 0;
}
void fm1_doom_sound_set_speaker_muted(unsigned muted) { (void)muted; CHECK(0); }
void fm1_doom_usb_get_status(struct fm1_doom_usb_status *status)
{
    memset(status,diagnostics ? 0xff : 0,sizeof(*status)); status->stage = 4;
    if (diagnostics) status->audio_ready = status->audio_error = INT_MIN;
}
void fm1_doom_usb_request_stop(void) { CHECK(0); }
int fm1_doom_usb_is_stopped(void) { return 0; }
void fm1_doom_usb_get_game(struct fm1_doom_usb_game *game) { memset(game,0,sizeof(*game)); }
int fm1_doom_usb_frame_begin(uint32_t now) { (void)now; CHECK(0); return -1; }
void fm1_doom_usb_frame_info(fm1_doom_usb_frame_control *frame) { memset(frame,0,sizeof(*frame)); }
size_t fm1_doom_usb_frame_read(uint32_t offset, uint8_t *out, size_t length)
{ (void)offset; (void)out; (void)length; CHECK(0); return 0; }
void fm1_doom_usb_frame_end(void) { ++frame_ends; }
void fm1_doom_usb_frame_tick(uint32_t now) { (void)now; }
int get_malloc_remain_heap_size(void) { CHECK(diagnostics); return INT_MIN; }
void fm1_doom_usb_get_volume(struct fm1_doom_usb_volume *volume)
{
    CHECK(diagnostics); memset(volume,0xff,sizeof(*volume));
    volume->sys_hz = volume->lsb_hz = INT_MIN;
}
void os_time_dly(unsigned ticks)
{
    unsigned i;
    if (startup_waits < 3) {
        CHECK(ticks == 1 && fm1_doom_usb_stage == 1 && !iteration);
        ++startup_waits;
        return;
    }
    CHECK(ticks == 1 && irq_enabled && !irq_depth && fm1_cdc_ready(0));
    if (diagnostics) {
        CHECK(tx_write == expected_length && tx_read <= tx_write);
        CHECK(fm1_doom_usb_tx_dropped == (burst ? 1u : 0u));
        if (iteration < 4) {
            CHECK(!tx_read && !received_length && !writes && !queries && !syncs);
            for (i=0;i<sizeof(dma)/sizeof(*dma);++i) CHECK(dma[i] == 0xa5a5a5a5u);
            if (iteration == 3) csr = 0;
        } else {
            CHECK(tx_read == received_length);
            CHECK(!memcmp(received_reply, expected_reply, received_length));
            csr = 0; /* Acknowledge this packet before the next task iteration. */
            if (tx_read == tx_write) {
                CHECK(received_length == expected_length && audio_inits == 1);
                CHECK(writes == (expected_length + 62u) / 63u);
                longjmp(finished,1);
            }
        }
        CHECK(iteration < 20);
        ++iteration;
        return;
    }
    if (!iteration) {
        CHECK(wrapper_calls == 1 && stale_queued_bytes && tx_write == stale_queued_bytes && !tx_read);
        CHECK(reads == 1 && !writes && !queries && !syncs && fm1_packet_regs.EP3_CNT == 0x11223344u);
        for (i=0;i<sizeof(dma)/sizeof(*dma);++i) CHECK(dma[i] == 0xa5a5a5a5u);
    } else if (iteration == 1) {
        CHECK(!tx_read && !tx_write && wrapper_calls == 1 && !writes && !queries);
    } else {
        static const char response[] = "FM1DIAG/1 DOOM-FM1/1 UBOOT=SERIAL COMMIT=BLOCKED\n";
        CHECK(iteration == 2 && wrapper_calls == 2 && writes == 1 && queries == 1 && syncs == 1);
        CHECK(tx_read == tx_write && tx_read == sizeof(response)-1 && fm1_packet_regs.EP3_CNT == sizeof(response)-1);
        CHECK(!memcmp(dma,response,sizeof(response)-1));
        for (i=sizeof(response)-1;i<sizeof(dma);++i) CHECK(((u8 *)dma)[i] == 0xa5);
        longjmp(finished,1);
    }
    ++iteration;
}
static void capture_expected(void *context, const char *text)
{
    (void)context; expected_length = strlen(text);
    CHECK(expected_length && expected_length < sizeof(expected_reply));
    memcpy(expected_reply,text,expected_length + 1);
}

static void test_diagnostic_ring(void)
{
    const fm1_doom_usb_protocol_io io = {
        0, capture_expected, get_status, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        get_heap_free, get_volume, fm1_usb_audio_target_status, 0
    };
    const char *commands[] = {"DOOM AUDIO\n", "DOOM VOLUME\n", "DOOM USB_AUDIO\n"};
    unsigned command_index;
    diagnostics = 1;
    CHECK(sizeof(tx) == 512);
    for (command_index = 0; command_index < 3; ++command_index) {
        diagnostic_command = commands[command_index];
        if (!command_index) fm1_doom_usb_protocol_audio(&io);
        else if (command_index == 1) fm1_doom_usb_protocol_volume(&io);
        else fm1_doom_usb_protocol_usb_audio(&io);
        CHECK(expected_length > sizeof(tx) / 2);
        if (!command_index) CHECK(expected_length == 511);
        if (command_index == 2) CHECK(expected_length == 447);
        for (burst = 0; burst < 2; ++burst) {
            iteration = writes = queries = syncs = reads = wrapper_calls = 0;
            frame_ends = audio_inits = received_length = fm1_doom_usb_tx_dropped = 0;
            fm1_cdc_generation = 10; fm1_doom_usb_heartbeat = 0;
            memset(dma,0xa5,sizeof(dma)); memset(received_reply,0,sizeof(received_reply));
            csr = BIT(0); fm1_packet_regs.EP3_CNT = 0x11223344u;
            if (!setjmp(finished)) fm1_doom_usb_task(NULL);
            CHECK(wrapper_calls == writes + 4 && fm1_doom_usb_stage == 2);
        }
        printf("PASS CDC diagnostic 512-byte ring: %sreply=%u, four busy retries intact, complete transmit, duplicate burst drops one whole reply\n",
               command_index == 0 ? "AUDIO " : command_index == 1 ? "VOLUME " : "USB_AUDIO ",
               (unsigned)expected_length);
    }
    diagnostics = 0;
}

int main(void)
{
    uint32_t before[64];
    u8 old_bytes[] = {11,22,33,44};
    for (change_before_call = 0; change_before_call < 2; ++change_before_call) {
        iteration = writes = queries = syncs = csr = reads = wrapper_calls = 0;
        frame_ends = audio_inits = stale_queued_bytes = 0;
        fm1_cdc_generation = 10; fm1_doom_usb_heartbeat = 0;
        memset(dma,0xa5,sizeof(dma)); fm1_packet_regs.EP3_CNT = 0x11223344u;
        if (!setjmp(finished)) fm1_doom_usb_task(NULL);
        CHECK(audio_inits == 1 && fm1_doom_usb_stage == 2 && fm1_doom_usb_heartbeat == 3);
    }
    /* Demonstrate the old fresh-snapshot hazard and retain compatibility:
     * old queued epoch rejects, whereas the compatibility API submits the
     * same bytes as current-session data because it has no queued epoch. */
    csr = 0; fm1_cdc_generation = 21; memcpy(before,dma,sizeof(before));
    CHECK(!overlay_cdc_write_packet(0,old_bytes,sizeof(old_bytes),20));
    CHECK(!memcmp(before,dma,sizeof(before)) && writes == 1);
    CHECK(cdc_write_data(0,old_bytes,sizeof(old_bytes)) == sizeof(old_bytes));
    CHECK(!memcmp(dma,old_bytes,sizeof(old_bytes)) && writes == 2 && irq_enabled && !irq_depth);
    puts("PASS CDC captured generation: stale queued reply cancelled, queue reset next iteration, fresh HELLO succeeds, compatibility API retained");
    test_diagnostic_ring();
    return 0;
}
