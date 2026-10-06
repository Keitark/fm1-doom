#include "packet_fake.h"
#include "fm1_usb_audio_target.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "USB packet line %d: %s\n", __LINE__, #condition); exit(1); \
} } while (0)

struct fm1_packet_regs fm1_packet_regs;
static struct usb_device_t device;
/* Prefix/suffix guards surround each 256-byte endpoint DMA allocation. */
static uint32_t storage[2][72];
static unsigned enabled, depth, saved_depth, reads, writes, syncs, dma_queries;
static unsigned saves, restores, csr[2], missing, unaligned, change_epoch;
static volatile unsigned epoch;

static unsigned slot(unsigned ep) { CHECK(ep == 1 || ep == 3); return ep == 3; }
unsigned fm1_packet_irq_save(void)
{
    unsigned old = enabled;
    saved_depth = depth;
    enabled = 0;
    ++saves;
    return old;
}
void fm1_packet_irq_restore(unsigned old)
{ CHECK(depth == saved_depth); enabled = old; ++restores; }
void __local_irq_disable(void) { enabled = 0; ++depth; }
void __local_irq_enable(void) { CHECK(depth); if (!--depth) enabled = 1; }
struct usb_device_t *usb_id2device(unsigned id)
{ CHECK(!id && !enabled && depth); return &device; }
unsigned usb_read_txcsr(unsigned id, unsigned ep)
{
    unsigned value;
    CHECK(!id && !enabled && depth);
    /* SDK CSR helpers nest the counted IRQ mask around indexed registers. */
    __local_irq_disable();
    value = csr[slot(ep)];
    __local_irq_enable();
    CHECK(!enabled && depth);
    ++reads;
    if (change_epoch) { ++epoch; change_epoch = 0; }
    return value;
}
void *usb_get_dma_taddr(unsigned id, unsigned ep)
{
    uint8_t *dma = (uint8_t *)(storage[slot(ep)] + 4);
    CHECK(!id && !enabled && depth);
    ++dma_queries;
    return missing ? NULL : unaligned ? dma + 1 : dma;
}
void __asm_csync(void) { CHECK(!enabled && depth); ++syncs; }
void usb_write_txcsr(unsigned id, unsigned ep, unsigned value)
{
    CHECK(!id && !enabled && depth && syncs == writes + 1);
    __local_irq_disable();
    csr[slot(ep)] = value;
    __local_irq_enable();
    CHECK(!enabled && depth);
    ++writes;
}

static void reset_fixture(void)
{
    device.bDeviceStates = USB_CONFIGURED;
    memset(storage, 0xa5, sizeof(storage));
    fm1_packet_regs.EP1_CNT = 0x11223344u;
    fm1_packet_regs.EP3_CNT = 0x55667788u;
    enabled = 1; depth = saved_depth = 0;
    reads = writes = syncs = dma_queries = saves = restores = 0;
    csr[0] = csr[1] = 0x4000u;
    missing = unaligned = change_epoch = 0;
    epoch = 17;
}
static void rejected(unsigned id, unsigned ep, const uint8_t *data, unsigned size,
                     const volatile unsigned *generation, unsigned expected,
                     unsigned expected_reads, unsigned expected_dma_queries,
                     unsigned expected_saves)
{
    uint32_t before[2][72];
    unsigned old_enabled = enabled, old_depth = depth, old_reads = reads;
    unsigned old_queries = dma_queries, old_saves = saves, old_restores = restores;
    unsigned old_writes = writes, old_syncs = syncs;
    unsigned old_ep1 = fm1_packet_regs.EP1_CNT, old_ep3 = fm1_packet_regs.EP3_CNT;
    memcpy(before, storage, sizeof(before));
    CHECK(!fm1_usb_packet_write(id, ep, data, size, generation, expected));
    CHECK(reads - old_reads == expected_reads && dma_queries - old_queries == expected_dma_queries);
    CHECK(saves - old_saves == expected_saves && restores - old_restores == expected_saves);
    CHECK(enabled == old_enabled && depth == old_depth);
    CHECK(writes == old_writes && syncs == old_syncs &&
          fm1_packet_regs.EP1_CNT == old_ep1 && fm1_packet_regs.EP3_CNT == old_ep3);
    CHECK(!memcmp(before, storage, sizeof(before)));
}
static void accepted(unsigned ep, const uint8_t *data, unsigned size)
{
    uint32_t before[2][72];
    unsigned which = slot(ep), old_enabled = enabled, old_depth = depth;
    unsigned old_reads = reads, old_writes = writes, old_syncs = syncs;
    unsigned old_queries = dma_queries, old_saves = saves, old_restores = restores;
    unsigned old_csr = csr[which], other_count = ep == 1 ? fm1_packet_regs.EP3_CNT : fm1_packet_regs.EP1_CNT;
    uint8_t *dma = (uint8_t *)(storage[which] + 4);
    memcpy(before, storage, sizeof(before));
    CHECK(fm1_usb_packet_write(0, ep, data, size, &epoch, epoch) == size);
    CHECK(reads == old_reads + 1 && writes == old_writes + 1 && syncs == old_syncs + 1);
    CHECK(dma_queries == old_queries + 1 && saves == old_saves + 1 && restores == old_restores + 1);
    CHECK(enabled == old_enabled && depth == old_depth && csr[which] == (old_csr | 1u));
    CHECK(!memcmp(dma, data, size));
    CHECK(!memcmp(storage[which], before[which], 16));
    CHECK(!memcmp(dma + size, (uint8_t *)before[which] + 16 + size, 272 - size));
    CHECK(!memcmp(storage[!which], before[!which], sizeof(storage[!which])));
    CHECK((ep == 1 ? fm1_packet_regs.EP1_CNT : fm1_packet_regs.EP3_CNT) == size);
    CHECK((ep == 1 ? fm1_packet_regs.EP3_CNT : fm1_packet_regs.EP1_CNT) == other_count);
}
int main(void)
{
    uint8_t data[256];
    unsigned i, j;
    for (i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)(i * 17u);
    reset_fixture();
    rejected(1, 1, data, 176, &epoch, epoch, 0, 0, 0);
    rejected(0, 2, data, 176, &epoch, epoch, 0, 0, 0);
    rejected(0, 1, NULL, 176, &epoch, epoch, 0, 0, 0);
    rejected(0, 1, data, 176, NULL, epoch, 0, 0, 0);
    for (i = 0; i <= 256; ++i) {
        if (i != 172 && i != 176 && i != 180) rejected(0, 1, data, i, &epoch, epoch, 0, 0, 0);
        if (!i || i >= 64) rejected(0, 3, data, i, &epoch, epoch, 0, 0, 0);
    }
    device.bDeviceStates = 0;
    rejected(0, 1, data, 176, &epoch, epoch, 0, 0, 1);
    device.bDeviceStates = USB_CONFIGURED;
    csr[0] |= 1u;
    rejected(0, 1, data, 176, &epoch, epoch, 1, 0, 1);
    csr[0] &= ~1u;
    rejected(0, 1, data, 176, &epoch, epoch - 1u, 1, 0, 1);
    change_epoch = 1;
    rejected(0, 1, data, 176, &epoch, epoch, 1, 0, 1);
    missing = 1;
    rejected(0, 1, data, 176, &epoch, epoch, 1, 1, 1);
    missing = 0; unaligned = 1;
    rejected(0, 1, data, 176, &epoch, epoch, 1, 1, 1);
    unaligned = 0;
    for (i = 172; i <= 180; i += 4) { csr[0] &= ~1u; accepted(1, data, i); }
    for (i = 1; i < 64; ++i) { csr[1] &= ~1u; accepted(3, data, i); }
    enabled = 0; csr[0] &= ~1u; accepted(1, data, 176);
    enabled = 0; depth = 2; csr[1] &= ~1u; accepted(3, data, 31);
    csr[1] |= 1u; rejected(0, 3, data, 31, &epoch, epoch, 1, 0, 1);
    /* Mixed DMA transactions cover cancellation and counted-mask restoration. */
    for (i = 0; i < 50000; ++i) {
        unsigned ep = i & 1u ? 1 : 3, n = ep == 1 ? 172 + 4 * (i % 3) : 1 + i % 63;
        unsigned which = slot(ep);
        reset_fixture();
        for (j = 0; j < n; ++j) data[j] = (uint8_t)(i + j * 23);
        enabled = i % 2; depth = i % 8 == 7 ? 2 : 0;
        if (depth) enabled = 0;
        switch (i % 6) {
        case 0: csr[which] |= 1u; rejected(0, ep, data, n, &epoch, epoch, 1, 0, 1); break;
        case 1: change_epoch = 1; rejected(0, ep, data, n, &epoch, epoch, 1, 0, 1); break;
        case 2: missing = 1; rejected(0, ep, data, n, &epoch, epoch, 1, 1, 1); break;
        case 3: unaligned = 1; rejected(0, ep, data, n, &epoch, epoch, 1, 1, 1); break;
        default: accepted(ep, data, n); break;
        }
    }
    puts("PASS USB packet: legal audio/CDC lengths, bounded CSR, busy/stale rejection, DMA guards, original IRQ state and 50000 mixed transactions");
    return 0;
}
