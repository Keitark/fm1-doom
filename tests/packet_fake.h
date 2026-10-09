#ifndef FM1_USB_PACKET_FAKE_H
#define FM1_USB_PACKET_FAKE_H
#include <stdint.h>
#ifdef _MSC_VER
#define __attribute__(attributes)
#endif
typedef unsigned usb_dev;
#define USB_CONFIGURED 4u
#define TXCSRP_TxPktRdy 1u
struct usb_device_t { unsigned bDeviceStates; };
struct fm1_packet_regs { volatile unsigned EP1_CNT, EP3_CNT; };
extern struct fm1_packet_regs fm1_packet_regs;
#define JL_USB (&fm1_packet_regs)
unsigned fm1_packet_irq_save(void);
void fm1_packet_irq_restore(unsigned);
void __local_irq_disable(void);
void __local_irq_enable(void);
void __asm_csync(void);
#define local_irq_save(flags) ((flags) = fm1_packet_irq_save())
#define local_irq_restore(flags) fm1_packet_irq_restore(flags)
struct usb_device_t *usb_id2device(usb_dev);
unsigned usb_read_txcsr(usb_dev, unsigned);
void usb_write_txcsr(usb_dev, unsigned, unsigned);
void *usb_get_dma_taddr(usb_dev, unsigned);
#endif
