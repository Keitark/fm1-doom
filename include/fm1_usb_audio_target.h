#ifndef FM1_USB_AUDIO_TARGET_H
#define FM1_USB_AUDIO_TARGET_H
#include <stddef.h>
#include <stdint.h>
void fm1_usb_audio_target_init(void);
void fm1_usb_audio_target_stop(void);
void fm1_usb_audio_target_status(char *out, size_t length);
unsigned fm1_usb_packet_write(unsigned id, unsigned ep, const uint8_t *data,
    unsigned size, const volatile unsigned *epoch, unsigned expected);
#endif
