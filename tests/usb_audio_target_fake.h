#ifndef FM1_USB_AUDIO_TARGET_FAKE_H
#define FM1_USB_AUDIO_TARGET_FAKE_H
#include <stdint.h>
#ifdef _MSC_VER
#define __attribute__(attributes)
#endif
typedef uint8_t u8;
typedef uint32_t u32;
typedef unsigned usb_dev;
struct usb_device_t { unsigned bDeviceStates; uint8_t setup[64]; };
struct usb_ctrlrequest { uint8_t bRequestType, bRequest; uint16_t wValue, wIndex, wLength; };
#define FM1_USB_CONTROLLER 0u
#define USB_CONFIGURED 4u
#define USB_EP0_SET_STALL 7u
#define USB_EP0_STAGE_SETUP 0u
#define USB_ENDPOINT_XFER_ISOC 1u
#define TXCSRP_TxPktRdy 0x01u
#define TXCSRP_FlushFIFO 0x08u
#define TXCSRP_ClrDataTog 0x40u
#define TXCSRP_ISOCHRONOUS 0x4000u
usb_dev usb_device2id(const struct usb_device_t *);
struct usb_device_t *usb_id2device(usb_dev);
void usb_clr_intr_txe(usb_dev, u32);
void usb_enable_ep(usb_dev, u32);
u32 usb_read_txcsr(usb_dev, u32);
void usb_write_txcsr(usb_dev, u32, u32);
void usb_set_intr_txe(usb_dev, u32);
u32 usb_g_ep_config(usb_dev, u32, u32, u32, u8 *, u32);
u32 usb_g_set_intr_hander(usb_dev, u32, void (*)(struct usb_device_t *, u32));
u32 usb_set_interface_hander(usb_dev, u32, u32 (*)(struct usb_device_t *, struct usb_ctrlrequest *));
u32 usb_set_reset_hander(usb_dev, u32, void (*)(struct usb_device_t *, u32));
void usb_set_setup_phase(struct usb_device_t *, u8);
void *usb_get_setup_buffer(const struct usb_device_t *);
u8 *usb_set_data_payload(struct usb_device_t *, struct usb_ctrlrequest *, const void *, u32);
u32 fm1_uac_desc_config(usb_dev, u8 *, u32 *);
#endif
