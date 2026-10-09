#ifndef FM1_USB_AUDIO_H
#define FM1_USB_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#define FM1_USB_AUDIO_RATE 44100u
#define FM1_USB_AUDIO_FIFO_FRAMES 128u
#define FM1_USB_AUDIO_MAX_INPUT_FRAMES 64u
#define FM1_USB_AUDIO_MAX_PACKET_BYTES 180u
#define FM1_USB_AUDIO_DESCRIPTOR_BYTES 99u

typedef struct {
    uint32_t active, armed, fill, pending_bytes;
    uint32_t pushed_frames, submitted_packets, sent_frames;
    uint32_t silent_frames, underruns, overruns;
    uint32_t busy, short_writes, starts, stops, epoch;
} fm1_usb_audio_capture_diagnostics;

/* CPU1 may call push_pcm24 from the IIS callback. All functions own a short
 * IRQ-safe cross-core lock; callers must not hold that same lock. PCM input
 * remains unchanged. Conversion is saturating PCM24 -> PCM16, no resampling.
 * FIFO overflow discards the oldest queued frame and counts each discard. */
void fm1_usb_audio_capture_init(void);
void fm1_usb_audio_capture_stop(void);
int fm1_usb_audio_capture_stream(int enabled);
void fm1_usb_audio_capture_push_pcm24(const int32_t *stereo, unsigned frames);

/* Stream setup, prepare/complete and USB controller access belong to CPU0.
 * Pass endpoint_ready=0 on TxPktRdy: no packet/FIFO bytes are changed.
 * A successful prepare returns a persistent CPU staging packet, its epoch
 * address and expected value. Submit it once with the bounded packet helper,
 * then complete with the exact number accepted. Rejection retains the same
 * pending packet. The pointer stays valid until complete or a stream change;
 * the helper must recheck the epoch inside its CPU0 controller commit guard.
 * Legal packet lengths are 172,176,180 bytes. Nominal cadence is 44/45 frames;
 * a one-frame occupancy correction handles independent USB/IIS clock drift.
 */
unsigned fm1_usb_audio_capture_prepare(unsigned endpoint_ready,
    const uint8_t **packet, const volatile unsigned **epoch, unsigned *expected);
void fm1_usb_audio_capture_complete(unsigned expected, unsigned written);
void fm1_usb_audio_capture_get_diagnostics(fm1_usb_audio_capture_diagnostics *out);

/* UAC1 AudioControl interface2, capture interface3 alt0/1, EP0x81.
 * Stereo PCM16 at fixed44.1kHz; virtual Line Connector terminal0x0603.
 * There are no volume or sample-rate controls. Root adapter registers only
 * these two interfaces and uses one aligned256-byte endpoint DMA buffer. */
extern const uint8_t fm1_usb_audio_capture_descriptor[FM1_USB_AUDIO_DESCRIPTOR_BYTES];
int fm1_usb_audio_capture_request_valid(uint8_t type, uint8_t request,
    uint16_t value, uint16_t index, uint16_t length);

#endif
