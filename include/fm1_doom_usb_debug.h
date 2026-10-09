#ifndef FM1_DOOM_USB_DEBUG_H
#define FM1_DOOM_USB_DEBUG_H

#include <stddef.h>
#include <stdint.h>

#define FM1_DOOM_USB_DEBUG_REGISTER_COUNT 7u
#define FM1_DOOM_USB_DEBUG_WIRE_BYTES 3840u

/* Phase 0 precedes usb_device_mode; phase 1 follows it. Each phase is captured
 * once by the USB task. The corresponding valid bit is published last. */
extern volatile uint32_t fm1_doom_usb_debug_registers[2][FM1_DOOM_USB_DEBUG_REGISTER_COUNT];
extern volatile uint32_t fm1_doom_usb_debug_valid;
void fm1_doom_usb_debug_snapshot(unsigned phase);

/* Opt-in startup display, after LCD initialization. The single LCD producer
 * supplies its owned, 64-byte-aligned RGB565 DMA strip. No pointer is retained.
 * Returns 0 on success or -1 for an invalid strip or an LCD write failure. */
int fm1_doom_usb_debug_draw(uint8_t *wire, size_t bytes, uint32_t elapsed_ms);

#endif
