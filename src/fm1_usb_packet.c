/* Bounded WL82 USB0 commit, adapted from fm1-mdx firmware/usb-diag/packet.c.
 * Caller and USB controller callbacks are pinned to CPU0. */
#ifdef FM1_USB_PACKET_HOST
#include "packet_fake.h"
#else
#include "app_config.h"
#include "system/includes.h"
#include "usb/device/usb_stack.h"
#endif
#include "fm1_usb_audio_target.h"
#include <string.h>

__attribute__((noinline, used))
unsigned fm1_usb_packet_write(unsigned id, unsigned ep, const uint8_t *data,
    unsigned size, const volatile unsigned *epoch, unsigned expected)
{
    unsigned flags, csr, result = 0;
    void *dma;
    if (id != 0 || !data || !epoch || !size ||
        !((ep == 1 && (size == 172 || size == 176 || size == 180)) ||
          (ep == 3 && size < 64))) return 0;
    local_irq_save(flags);
    /* SDK CSR helpers nest this counted mask; they must not unmask the
     * caller halfway through the DMA copy/count/doorbell transaction. */
    __local_irq_disable();
    if (usb_id2device(0)->bDeviceStates != USB_CONFIGURED) goto done;
    csr = usb_read_txcsr(0, ep);
    if ((csr & TXCSRP_TxPktRdy) || *epoch != expected) goto done;
    dma = usb_get_dma_taddr(0, ep);
    if (!dma || ((uintptr_t)dma & 3u)) goto done;
    memcpy(dma, data, size);
    if (ep == 1) JL_USB->EP1_CNT = size;
    else JL_USB->EP3_CNT = size;
    __asm_csync();
    usb_write_txcsr(0, ep, csr | TXCSRP_TxPktRdy);
    result = size;
done:
    __local_irq_enable();
    local_irq_restore(flags);
    return result;
}
