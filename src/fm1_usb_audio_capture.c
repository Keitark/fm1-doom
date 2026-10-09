#include "fm1_usb_audio.h"

#ifdef FM1_USB_AUDIO_CAPTURE_TEST
typedef unsigned char spinlock_t;
extern unsigned fm1_usb_audio_test_irq_save(void);
extern void fm1_usb_audio_test_irq_restore(unsigned);
extern void fm1_usb_audio_test_lock(spinlock_t *);
extern void fm1_usb_audio_test_unlock(spinlock_t *);
#define local_irq_save(f) ((f) = fm1_usb_audio_test_irq_save())
#define local_irq_restore(f) fm1_usb_audio_test_irq_restore(f)
#define arch_spin_lock(p) fm1_usb_audio_test_lock(p)
#define arch_spin_unlock(p) fm1_usb_audio_test_unlock(p)
#elif defined(FM1_TARGET_PI32V2)
#include "system/includes.h"
#include "system/spinlock.h"
#else
typedef unsigned char spinlock_t;
#define local_irq_save(f) ((f) = 0)
#define local_irq_restore(f) ((void)(f))
#define arch_spin_lock(p) ((void)(p))
#define arch_spin_unlock(p) ((void)(p))
#endif

/* One ownership domain for the two cores. USB controller operations are
 * deliberately outside this lock and run only on CPU0. No allocation/waits.
 * The pending packet is separate from endpoint DMA and never producer-owned. */
static struct {
    int16_t fifo[FM1_USB_AUDIO_FIFO_FRAMES][2];
    uint8_t pending[FM1_USB_AUDIO_MAX_PACKET_BYTES];
    uint32_t rd, wr;
    uint32_t pushed, submitted, sent, silent, under, over;
    uint32_t busy, short_write, starts, stops;
    uint16_t pending_bytes;
    uint8_t phase, armed, active, primed;
} capture;
static volatile unsigned capture_epoch;
static spinlock_t capture_lock;

static unsigned take(void)
{
    unsigned f;
    local_irq_save(f);
    arch_spin_lock(&capture_lock);
    return f;
}

static void release(unsigned f)
{
    arch_spin_unlock(&capture_lock);
    local_irq_restore(f);
}

static void reset_stream(void)
{
    ++capture_epoch;
    capture.rd = capture.wr = 0;
    capture.pending_bytes = 0;
    capture.phase = capture.primed = 0;
}

void fm1_usb_audio_capture_init(void)
{
    unsigned f = take();
    capture.armed = 1;
    capture.active = 0;
    capture.pushed = capture.submitted = capture.sent = capture.silent = 0;
    capture.under = capture.over = capture.busy = capture.short_write = 0;
    capture.starts = capture.stops = 0;
    reset_stream();
    release(f);
}

void fm1_usb_audio_capture_stop(void)
{
    unsigned f = take();
    capture.armed = capture.active = 0;
    ++capture.stops;
    reset_stream();
    release(f);
}

int fm1_usb_audio_capture_stream(int enabled)
{
    unsigned f = take();
    int active = capture.armed && enabled;
    capture.active = (uint8_t)active;
    if (active) ++capture.starts;
    else ++capture.stops;
    reset_stream();
    release(f);
    return active;
}

static int16_t pcm16(int32_t value)
{
    /* Truncate toward zero like the existing MDX tap, using bounded shifts
     * rather than a hardware divide in the already expensive IIS callback.
     * Unsigned magnitude also handles INT32_MIN without signed overflow. */
    int32_t v = value < 0 ? -(int32_t)((0u - (uint32_t)value) >> 8)
                          : (int32_t)((uint32_t)value >> 8);
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

void fm1_usb_audio_capture_push_pcm24(const int32_t *pcm, unsigned frames)
{
    unsigned i, f;
    if (!pcm || !frames || frames > FM1_USB_AUDIO_MAX_INPUT_FRAMES) return;
    f = take();
    if (capture.armed && capture.active) {
        for (i = 0; i < frames; ++i) {
            unsigned slot;
            if (capture.wr - capture.rd == FM1_USB_AUDIO_FIFO_FRAMES) {
                ++capture.rd;
                ++capture.over;
            }
            slot = capture.wr & (FM1_USB_AUDIO_FIFO_FRAMES - 1u);
            capture.fifo[slot][0] = pcm16(pcm[2u * i]);
            capture.fifo[slot][1] = pcm16(pcm[2u * i + 1u]);
            ++capture.wr;
        }
        capture.pushed += frames;
    }
    release(f);
}

unsigned fm1_usb_audio_capture_prepare(unsigned ready, const uint8_t **packet,
    const volatile unsigned **epoch, unsigned *expected)
{
    unsigned f, frames, available, count, i;
    if (!packet || !epoch || !expected) return 0;
    f = take();
    if (!capture.armed || !capture.active) { release(f); return 0; }
    if (!ready) { ++capture.busy; release(f); return 0; }
    if (!capture.pending_bytes) {
        frames = 44;
        if (++capture.phase == 10) { capture.phase = 0; ++frames; }
        available = capture.wr - capture.rd;
        if (!capture.primed && available >= 64u) capture.primed = 1;
        /* Native-clock elastic capture: preserve samples and change only
         * packet count by one frame outside the64-frame burst occupancy band.
         * endpoint remains within its180-byte maximum. */
        if (capture.primed) {
            if (available >= 108u && frames < 45u) ++frames;
            else if (available <= 48u && frames > 43u) --frames;
        }
        count = capture.primed ? (available < frames ? available : frames) : 0;
        if (capture.primed && count < frames) {
            ++capture.under;
            capture.primed = 0;
        }
        capture.silent += frames - count;
        for (i = 0; i < frames; ++i) {
            unsigned ch, slot = capture.rd & (FM1_USB_AUDIO_FIFO_FRAMES - 1u);
            for (ch = 0; ch < 2u; ++ch) {
                uint16_t v = i < count ? (uint16_t)capture.fifo[slot][ch] : 0;
                capture.pending[4u * i + 2u * ch] = (uint8_t)v;
                capture.pending[4u * i + 2u * ch + 1u] = (uint8_t)(v >> 8);
            }
            if (i < count) ++capture.rd;
        }
        capture.pending_bytes = (uint16_t)(frames * 4u);
    }
    *packet = capture.pending;
    *epoch = &capture_epoch;
    *expected = capture_epoch;
    frames = capture.pending_bytes;
    release(f);
    return frames;
}

void fm1_usb_audio_capture_complete(unsigned expected, unsigned written)
{
    unsigned f = take();
    if (expected == capture_epoch && capture.pending_bytes) {
        if (written == capture.pending_bytes) {
            ++capture.submitted;
            capture.sent += capture.pending_bytes / 4u;
            capture.pending_bytes = 0;
        } else ++capture.short_write;
    }
    release(f);
}

void fm1_usb_audio_capture_get_diagnostics(fm1_usb_audio_capture_diagnostics *out)
{
    unsigned f;
    if (!out) return;
    f = take();
    out->active = capture.active; out->armed = capture.armed;
    out->fill = capture.wr - capture.rd; out->pending_bytes = capture.pending_bytes;
    out->pushed_frames = capture.pushed; out->submitted_packets = capture.submitted;
    out->sent_frames = capture.sent; out->silent_frames = capture.silent;
    out->underruns = capture.under; out->overruns = capture.over;
    out->busy = capture.busy; out->short_writes = capture.short_write;
    out->starts = capture.starts; out->stops = capture.stops; out->epoch = capture_epoch;
    release(f);
}
