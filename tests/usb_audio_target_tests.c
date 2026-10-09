#include "usb_audio_target_fake.h"
#include "fm1_usb_audio.h"
#include "fm1_usb_audio_target.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "USB audio target line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

typedef u32 (*setup_handler)(struct usb_device_t *, struct usb_ctrlrequest *);
typedef void (*reset_handler)(struct usb_device_t *, u32);
typedef void (*tx_handler)(struct usb_device_t *, u32);
static struct usb_device_t device;
static setup_handler setups[4];
static reset_handler resets[4];
static tx_handler transmitter;
static unsigned irq_enabled = 1, lock_held, irq_saves, irq_restores;
static unsigned csr, csr_reads, flushes, tx_irq, config_calls, ep_enables;
static unsigned interface_calls, reset_calls, phase, payload_calls, payload_size;
static unsigned packet_calls, packets_written, packet_size, packet_expected;
static unsigned fail_interface = 99, fail_reset = 99;
static uint8_t *endpoint_dma;
static uint8_t packet_copy[180], payload_copy[64], cdc_dma[2][256];
static const uint8_t *packet_address;
static const volatile unsigned *packet_epoch;
enum { COMMIT_OK, COMMIT_BUSY, COMMIT_RESET, COMMIT_SHORT };
static unsigned commit_fault;

static u32 cdc_setup(struct usb_device_t *d, struct usb_ctrlrequest *r)
{ (void)d; (void)r; CHECK(0); return 0; }
static void cdc_reset(struct usb_device_t *d, u32 i)
{ (void)d; (void)i; CHECK(0); }

unsigned fm1_usb_audio_test_irq_save(void)
{ unsigned previous = irq_enabled; irq_enabled = 0; ++irq_saves; return previous; }
void fm1_usb_audio_test_irq_restore(unsigned previous)
{ CHECK(!lock_held); irq_enabled = previous; ++irq_restores; }
void fm1_usb_audio_test_lock(unsigned char *lock)
{ CHECK(!irq_enabled && !lock_held && !*lock); *lock = 1; lock_held = 1; }
void fm1_usb_audio_test_unlock(unsigned char *lock)
{ CHECK(!irq_enabled && lock_held && *lock); *lock = 0; lock_held = 0; }

usb_dev usb_device2id(const struct usb_device_t *d)
{ CHECK(d == &device && !lock_held); return 0; }
struct usb_device_t *usb_id2device(usb_dev id)
{ CHECK(!id && !lock_held); return &device; }
void usb_clr_intr_txe(usb_dev id, u32 ep)
{ CHECK(!id && ep == 1 && !lock_held); tx_irq = 0; }
void usb_set_intr_txe(usb_dev id, u32 ep)
{ CHECK(!id && ep == 1 && transmitter && !lock_held); tx_irq = 1; }
void usb_enable_ep(usb_dev id, u32 ep)
{ CHECK(!id && ep == 1 && !lock_held); ++ep_enables; }
u32 usb_read_txcsr(usb_dev id, u32 ep)
{ CHECK(!id && ep == 1 && !lock_held); ++csr_reads; return csr; }
void usb_write_txcsr(usb_dev id, u32 ep, u32 value)
{
    CHECK(!id && ep == 1 && !lock_held);
    CHECK(value == (TXCSRP_FlushFIFO | TXCSRP_ClrDataTog | TXCSRP_ISOCHRONOUS));
    csr = TXCSRP_ISOCHRONOUS;
    ++flushes;
}
u32 usb_g_ep_config(usb_dev id, u32 ep, u32 type, u32 irq, u8 *dma, u32 maximum)
{
    CHECK(!id && ep == 0x81 && type == USB_ENDPOINT_XFER_ISOC && !irq);
    CHECK(!lock_held && dma && !((uintptr_t)dma & 63u) && maximum == 180);
    if (endpoint_dma) CHECK(endpoint_dma == dma);
    endpoint_dma = dma;
    memset(endpoint_dma, 0xa5, 256);
    ++config_calls;
    return 0;
}
u32 usb_g_set_intr_hander(usb_dev id, u32 ep, tx_handler handler)
{ CHECK(!id && ep == 0x81 && !lock_held); transmitter = handler; return ep; }
u32 usb_set_interface_hander(usb_dev id, u32 i, setup_handler handler)
{
    CHECK(!id && i >= 2 && i <= 3 && handler && !lock_held);
    ++interface_calls;
    if (i == fail_interface) return 99;
    setups[i] = handler;
    return i;
}
u32 usb_set_reset_hander(usb_dev id, u32 i, reset_handler handler)
{
    CHECK(!id && i >= 2 && i <= 3 && handler && !lock_held);
    ++reset_calls;
    if (i == fail_reset) return 99;
    resets[i] = handler;
    return i;
}
void usb_set_setup_phase(struct usb_device_t *d, u8 value)
{ CHECK(d == &device && !lock_held); phase = value; }
void *usb_get_setup_buffer(const struct usb_device_t *d)
{ CHECK(d == &device && !lock_held); return device.setup; }
u8 *usb_set_data_payload(struct usb_device_t *d, struct usb_ctrlrequest *request,
                         const void *data, u32 size)
{
    CHECK(d == &device && !lock_held && size == request->wLength && size <= 2);
    memcpy(payload_copy, data, size);
    payload_size = size;
    ++payload_calls;
    return device.setup;
}

/* The real packet helper is tested separately with MMIO and counted IRQ
 * nesting. This mock injects controller cancellation after capture_prepare,
 * before commit, and checks the adapter does not hold the capture lock there. */
unsigned fm1_usb_packet_write(unsigned id, unsigned ep, const uint8_t *data,
    unsigned size, const volatile unsigned *epoch, unsigned expected)
{
    unsigned fault = commit_fault, i;
    uint8_t dma_before[256];
    CHECK(!id && ep == 1 && !lock_held && endpoint_dma && data && epoch);
    CHECK(size == 172 || size == 176 || size == 180);
    ++packet_calls;
    packet_address = data; packet_epoch = epoch;
    packet_size = size; packet_expected = expected;
    memcpy(packet_copy, data, size);
    memcpy(dma_before, endpoint_dma, sizeof(dma_before));
    commit_fault = COMMIT_OK;
    if (fault == COMMIT_BUSY) csr |= TXCSRP_TxPktRdy;
    if (fault == COMMIT_RESET) {
        CHECK(resets[3]);
        resets[3](&device, 3);
        CHECK(*epoch != expected && !memcmp(data, packet_copy, size));
    }
    if (fault == COMMIT_SHORT || (csr & TXCSRP_TxPktRdy) || *epoch != expected) {
        CHECK(!memcmp(endpoint_dma, dma_before, sizeof(dma_before)));
        return fault == COMMIT_SHORT ? size - 4u : 0;
    }
    memcpy(endpoint_dma, data, size);
    for (i = size; i < 256; ++i) CHECK(endpoint_dma[i] == dma_before[i]);
    csr |= TXCSRP_TxPktRdy;
    ++packets_written;
    return size;
}

static fm1_usb_audio_capture_diagnostics diagnostics(void)
{
    fm1_usb_audio_capture_diagnostics d;
    unsigned previous = irq_enabled;
    fm1_usb_audio_capture_get_diagnostics(&d);
    CHECK(!lock_held && irq_enabled == previous && irq_saves == irq_restores);
    return d;
}
static void cdc_unchanged(void)
{
    unsigned i, j;
    CHECK(setups[0] == cdc_setup && setups[1] == cdc_setup);
    CHECK(resets[0] == cdc_reset && resets[1] == cdc_reset);
    for (i = 0; i < 2; ++i) for (j = 0; j < 256; ++j)
        CHECK(cdc_dma[i][j] == (uint8_t)(j + 37u * i));
}
static void fixture(void)
{
    unsigned i, j;
    CHECK(!lock_held);
    memset(&device, 0, sizeof(device)); device.bDeviceStates = USB_CONFIGURED;
    memset(setups, 0, sizeof(setups)); memset(resets, 0, sizeof(resets));
    setups[0] = setups[1] = cdc_setup; resets[0] = resets[1] = cdc_reset;
    for (i = 0; i < 2; ++i) for (j = 0; j < 256; ++j)
        cdc_dma[i][j] = (uint8_t)(j + 37u * i);
    transmitter = NULL; endpoint_dma = NULL;
    csr = csr_reads = flushes = tx_irq = config_calls = ep_enables = 0;
    interface_calls = reset_calls = payload_calls = payload_size = 0;
    packet_calls = packets_written = packet_size = 0;
    packet_address = NULL; packet_epoch = NULL;
    commit_fault = COMMIT_OK; fail_interface = fail_reset = 99;
    irq_enabled = 1; irq_saves = irq_restores = 0;
    fm1_usb_audio_target_init();
    CHECK(diagnostics().armed && !diagnostics().active);
}
static void configure(void)
{
    uint8_t guarded[FM1_USB_AUDIO_DESCRIPTOR_BYTES + 2];
    u32 interface = 2;
    memset(guarded, 0xa5, sizeof(guarded));
    CHECK(fm1_uac_desc_config(0, guarded + 1, &interface) == 99 && interface == 4);
    CHECK(guarded[0] == 0xa5 && guarded[100] == 0xa5);
    CHECK(!memcmp(guarded + 1, fm1_usb_audio_capture_descriptor, 99));
    CHECK(interface_calls == 2 && reset_calls == 2 && setups[2] && setups[3]);
    CHECK(resets[2] && resets[3]); cdc_unchanged();
}
static void request(unsigned type, unsigned code, unsigned value, unsigned index, unsigned length)
{
    struct usb_ctrlrequest r;
    r.bRequestType = (u8)type; r.bRequest = (u8)code;
    r.wValue = (uint16_t)value; r.wIndex = (uint16_t)index; r.wLength = (uint16_t)length;
    phase = 99;
    CHECK(setups[3]);
    CHECK(!setups[3](&device, &r));
    CHECK(!lock_held && irq_saves == irq_restores);
    cdc_unchanged();
}
static void start(void)
{
    request(1, 11, 1, 3, 0);
    CHECK(phase == USB_EP0_STAGE_SETUP && diagnostics().active);
    CHECK(tx_irq && transmitter && config_calls == 1 && ep_enables == 1);
    CHECK(flushes == 1 && packet_calls == 1 && packets_written == 1 && packet_size == 176);
    CHECK(diagnostics().sent_frames == 44 && diagnostics().silent_frames == 44);
    CHECK(!diagnostics().pending_bytes);
}
static void transmit(void)
{ CHECK(transmitter && tx_irq); csr &= ~TXCSRP_TxPktRdy; transmitter(&device, 1); }
static void produce(unsigned seed)
{
    int32_t pcm[128], original[128];
    unsigned i, previous = irq_enabled;
    for (i = 0; i < 64; ++i) {
        pcm[2 * i] = (int32_t)(0x1234u + seed) * 256;
        pcm[2 * i + 1] = -(int32_t)(0x2345u + seed) * 256;
    }
    memcpy(original, pcm, sizeof(pcm));
    fm1_usb_audio_capture_push_pcm24(pcm, 64);
    CHECK(!memcmp(original, pcm, sizeof(pcm)) && irq_enabled == previous && !lock_held);
}

static void descriptor_test(void)
{
    unsigned offset = 0, interfaces = 0, endpoints = 0, format = 0;
    uint8_t out[99], before[99]; u32 interface;
    fixture(); memset(out, 0x5a, sizeof(out)); memcpy(before, out, sizeof(out));
    interface = 2;
    CHECK(!fm1_uac_desc_config(1, out, &interface));
    CHECK(interface == 2 && !memcmp(out, before, 99) && !interface_calls);
    interface = 0;
    CHECK(!fm1_uac_desc_config(0, out, &interface));
    CHECK(!interface && !memcmp(out, before, 99) && !interface_calls);
    configure();
    while (offset < 99) {
        const uint8_t *d = fm1_usb_audio_capture_descriptor + offset;
        CHECK(d[0] >= 2 && offset + d[0] <= 99);
        if (d[1] == 4) {
            CHECK(d[0] == 9 && d[5] == 1);
            if (!interfaces) CHECK(d[2] == 2 && !d[3] && !d[4] && d[6] == 1);
            else CHECK(d[2] == 3 && d[3] == interfaces - 1 && d[4] == interfaces - 1 && d[6] == 2);
            ++interfaces;
        } else if (d[1] == 5) {
            CHECK(d[0] == 9 && d[2] == 0x81 && (d[3] & 3u) == 1);
            CHECK((d[4] | ((unsigned)d[5] << 8)) == 180 && d[6] == 1);
            ++endpoints;
        } else if (d[1] == 0x24 && d[2] == 2 && d[0] == 11) {
            CHECK(d[3] == 1 && d[4] == 2 && d[5] == 2 && d[6] == 16 && d[7] == 1);
            CHECK((d[8] | ((unsigned)d[9] << 8) | ((unsigned)d[10] << 16)) == 44100);
            ++format;
        }
        offset += d[0];
    }
    CHECK(interfaces == 3 && endpoints == 1 && format == 1);
    fixture(); fail_interface = 2; interface = 2;
    CHECK(!fm1_uac_desc_config(0, out, &interface) && interface == 2 && !memcmp(out, before, 99));
    cdc_unchanged();
    fixture(); fail_reset = 3; interface = 2;
    CHECK(!fm1_uac_desc_config(0, out, &interface) && interface == 2 && !memcmp(out, before, 99));
    cdc_unchanged();
}
static void control_test(void)
{
    static const unsigned invalid[][5] = {
        {0,11,1,3,0}, {1,11,2,3,0}, {1,11,1,2,0}, {1,11,1,4,0},
        {1,11,1,3,1}, {0x81,10,0,3,2}, {0x81,10,1,3,1},
        {0x81,0,0,3,1}, {0xa1,0x81,0,3,2}, {0x21,1,0,3,0}
    };
    unsigned i, old_calls, old_flush;
    fixture(); configure();
    request(1,11,0,2,0);
    CHECK(phase == USB_EP0_STAGE_SETUP && !flushes && !diagnostics().active);
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        old_calls = payload_calls; old_flush = flushes;
        request(invalid[i][0], invalid[i][1], invalid[i][2], invalid[i][3], invalid[i][4]);
        CHECK(phase == USB_EP0_SET_STALL && payload_calls == old_calls && flushes == old_flush);
    }
    device.bDeviceStates = 0; request(1,11,1,3,0);
    CHECK(phase == USB_EP0_SET_STALL && !config_calls && !diagnostics().active);
    device.bDeviceStates = USB_CONFIGURED; start();
    request(0x81,10,0,3,1); CHECK(payload_size == 1 && payload_copy[0] == 1);
    request(0x81,10,0,2,1); CHECK(payload_size == 1 && !payload_copy[0]);
    for (i = 2; i <= 3; ++i) {
        request(0x81,0,0,i,2); CHECK(payload_size == 2 && !payload_copy[0] && !payload_copy[1]);
    }
    request(1,11,0,3,0);
    CHECK(!diagnostics().active && diagnostics().armed && !tx_irq && !transmitter);
    request(0x81,10,0,3,1); CHECK(!payload_copy[0]);
}
static void producer_and_retry_test(void)
{
    fm1_usb_audio_capture_diagnostics before, after;
    uint8_t held[180], dma_before[256];
    const uint8_t *held_address; unsigned calls, reads, held_size;
    fixture(); configure(); start();
    produce(0); before = diagnostics(); calls = packet_calls; reads = csr_reads;
    transmitter(&device, 1); after = diagnostics();
    CHECK(csr_reads == reads + 1 && packet_calls == calls && after.busy == before.busy + 1);
    CHECK(after.fill == before.fill && after.pending_bytes == before.pending_bytes);
    CHECK(after.sent_frames == before.sent_frames && after.silent_frames == before.silent_frames);
    transmit();
    CHECK(packet_size == 176 && diagnostics().pushed_frames == 64 && diagnostics().fill == 20);
    CHECK(diagnostics().sent_frames == 88 && diagnostics().silent_frames == 44);
    for (calls = 0; calls < 44; ++calls) {
        CHECK(endpoint_dma[4 * calls] == 0x34 && endpoint_dma[4 * calls + 1] == 0x12);
        CHECK(endpoint_dma[4 * calls + 2] == 0xbb && endpoint_dma[4 * calls + 3] == 0xdc);
    }
    fixture(); configure(); start(); produce(0);
    memcpy(dma_before, endpoint_dma, 256); commit_fault = COMMIT_BUSY; transmit();
    CHECK(!memcmp(endpoint_dma, dma_before, 256));
    before = diagnostics(); held_size = packet_size; held_address = packet_address;
    memcpy(held, packet_copy, held_size);
    CHECK(before.pending_bytes == 176 && before.short_writes == 1 && before.fill == 20);
    irq_enabled = 0; produce(777); CHECK(!irq_enabled); irq_enabled = 1;
    CHECK(!memcmp(held_address, held, held_size) && diagnostics().fill == 84);
    calls = packet_calls; transmitter(&device, 1);
    CHECK(packet_calls == calls && !memcmp(held_address, held, held_size));
    commit_fault = COMMIT_SHORT; transmit();
    CHECK(diagnostics().short_writes == 2 && diagnostics().pending_bytes == 176);
    CHECK(!memcmp(endpoint_dma, dma_before, 256) && !memcmp(held_address, held, held_size));
    transmit();
    CHECK(packet_address == held_address && packet_size == held_size);
    CHECK(!memcmp(endpoint_dma, held, held_size) && !diagnostics().pending_bytes);
    CHECK(diagnostics().fill == 84 && diagnostics().submitted_packets == 2);
    cdc_unchanged();
}
static void teardown_test(void)
{
    unsigned i, old_expected;
    uint8_t dma_before[256], old_packet[180];
    fm1_usb_audio_capture_diagnostics before, after;
    for (i = 2; i <= 3; ++i) {
        fixture(); configure(); start(); produce(0);
        before = diagnostics(); resets[i](&device, i); after = diagnostics();
        CHECK(!after.active && after.armed && !after.fill && !after.pending_bytes);
        CHECK(after.epoch != before.epoch && !tx_irq && !transmitter && flushes == 2);
        request(1,11,1,3,0);
        CHECK(diagnostics().active && tx_irq && transmitter && config_calls == 2);
        CHECK(diagnostics().sent_frames == 88 && diagnostics().silent_frames == 88);
        cdc_unchanged();
    }
    fixture(); configure(); start(); produce(0);
    memcpy(dma_before, endpoint_dma, 256); before = diagnostics();
    commit_fault = COMMIT_RESET; transmit(); after = diagnostics();
    CHECK(!after.active && !after.pending_bytes && after.epoch != before.epoch);
    CHECK(after.submitted_packets == before.submitted_packets && after.short_writes == before.short_writes);
    CHECK(!memcmp(endpoint_dma, dma_before, 256) && !tx_irq && !transmitter);
    memcpy(old_packet, packet_copy, packet_size); old_expected = packet_expected;
    CHECK(*packet_epoch != old_expected && !memcmp(packet_address, old_packet, packet_size));
    fm1_usb_audio_capture_complete(old_expected, packet_size);
    CHECK(diagnostics().submitted_packets == before.submitted_packets);
    request(1,11,1,3,0); before = diagnostics();
    fm1_usb_audio_target_stop(); after = diagnostics();
    CHECK(!after.armed && !after.active && !after.pending_bytes && !after.fill);
    CHECK(after.epoch != before.epoch && !tx_irq && !transmitter);
    i = config_calls; request(1,11,1,3,0);
    CHECK(!diagnostics().active && !tx_irq && !transmitter && config_calls == i);
    produce(0); CHECK(!diagnostics().fill);
    fm1_usb_audio_target_init(); request(1,11,1,3,0);
    CHECK(diagnostics().active && diagnostics().armed && tx_irq && transmitter);
    cdc_unchanged();
}
static void status_test(void)
{
    char out[512], tiny[10];
    fixture(); configure(); start(); produce(0); transmit();
    fm1_usb_audio_target_status(out, sizeof(out));
    CHECK(strstr(out, "DOOM USB_AUDIO rate=44100 bits=16 channels=2 active=1 armed=1"));
    CHECK(strstr(out, "pushed=64 packets=2 sent=88 silent=44"));
    CHECK(out[strlen(out) - 1] == '\n');
    memset(tiny, 0xa5, sizeof(tiny)); fm1_usb_audio_target_status(tiny + 1, 8);
    CHECK((unsigned char)tiny[0] == 0xa5 && tiny[8] == 0 && (unsigned char)tiny[9] == 0xa5);
    CHECK(irq_enabled && !lock_held && irq_saves == irq_restores);
}
int main(void)
{
    descriptor_test(); control_test(); producer_and_retry_test(); teardown_test(); status_test();
    puts("PASS USB audio adapter: capture-only descriptors, EP0 controls, aligned persistent DMA, real64-frame producer, busy/stale retry, reset/stop teardown and CDC preservation");
    return 0;
}
