#ifdef FM1_USB_AUDIO_TARGET_HOST
#include "usb_audio_target_fake.h"
#else
#include "app_config.h"
#include "system/includes.h"
#include "usb/device/usb_stack.h"
#endif
#include "fm1_usb_audio.h"
#include "fm1_usb_audio_target.h"
#include <stdio.h>
#include <string.h>

#ifdef _MSC_VER
static __declspec(align(64)) u8 audio_dma[256];
#else
static u8 audio_dma[256] __attribute__((aligned(64)));
#endif

static void tx(struct usb_device_t *device, u32 ep)
{
    const uint8_t *packet;
    const volatile unsigned *epoch;
    unsigned expected, count, written;
    usb_dev id = usb_device2id(device);
    (void)ep;
    count = fm1_usb_audio_capture_prepare(
        !(usb_read_txcsr(id, 1) & TXCSRP_TxPktRdy),
        &packet, &epoch, &expected);
    if (count) {
        written = fm1_usb_packet_write(id, 1, packet, count, epoch, expected);
        fm1_usb_audio_capture_complete(expected, written);
    }
}

static void stream(struct usb_device_t *device, int on)
{
    usb_dev id = usb_device2id(device);
    usb_clr_intr_txe(id, 1);
    usb_g_set_intr_hander(id, 0x81, NULL);
    usb_write_txcsr(id, 1,
        TXCSRP_FlushFIFO | TXCSRP_ClrDataTog | TXCSRP_ISOCHRONOUS);
    on = fm1_usb_audio_capture_stream(on);
    if (on) {
        usb_enable_ep(id, 1);
        usb_g_set_intr_hander(id, 0x81, tx);
        usb_g_ep_config(id, 0x81, USB_ENDPOINT_XFER_ISOC, 0,
            audio_dma, FM1_USB_AUDIO_MAX_PACKET_BYTES);
        tx(device, 1);
        usb_set_intr_txe(id, 1);
    }
}

static void reset(struct usb_device_t *device, u32 interface)
{ (void)interface; stream(device, 0); }

static u32 setup(struct usb_device_t *device, struct usb_ctrlrequest *request)
{
    u8 *reply = usb_get_setup_buffer(device);
    fm1_usb_audio_capture_diagnostics diagnostics;
    reply[0] = reply[1] = 0;
    if (device->bDeviceStates != USB_CONFIGURED ||
        !fm1_usb_audio_capture_request_valid(request->bRequestType,
            request->bRequest, request->wValue, request->wIndex, request->wLength)) {
        usb_set_setup_phase(device, USB_EP0_SET_STALL);
        return 0;
    }
    if (request->bRequest == 11) {
        if (request->wIndex == 3) stream(device, request->wValue);
        usb_set_setup_phase(device, USB_EP0_STAGE_SETUP);
    } else {
        if (request->bRequest == 10 && request->wIndex == 3) {
            fm1_usb_audio_capture_get_diagnostics(&diagnostics);
            reply[0] = (u8)diagnostics.active;
        }
        usb_set_data_payload(device, request, reply, request->wLength);
    }
    return 0;
}

u32 fm1_uac_desc_config(usb_dev id, u8 *out, u32 *interface)
{
    unsigned i;
    if (id != FM1_USB_CONTROLLER || *interface != 2) return 0;
    for (i = 2; i < 4; ++i) {
        if (usb_set_interface_hander(id, i, setup) != i ||
            usb_set_reset_hander(id, i, reset) != i) return 0;
    }
    memcpy(out, fm1_usb_audio_capture_descriptor, FM1_USB_AUDIO_DESCRIPTOR_BYTES);
    *interface = 4;
    return FM1_USB_AUDIO_DESCRIPTOR_BYTES;
}

void fm1_usb_audio_target_init(void) { fm1_usb_audio_capture_init(); }
void fm1_usb_audio_target_stop(void)
{
    fm1_usb_audio_capture_stop();
    stream(usb_id2device(FM1_USB_CONTROLLER), 0);
}

void __attribute__((noinline)) fm1_usb_audio_target_status(char *out, size_t length)
{
    fm1_usb_audio_capture_diagnostics d;
    fm1_usb_audio_capture_get_diagnostics(&d);
    snprintf(out, length,
        "DOOM USB_AUDIO rate=44100 bits=16 channels=2 active=%lu armed=%lu fill=%lu pending=%lu pushed=%lu packets=%lu sent=%lu silent=%lu under=%lu over=%lu busy=%lu short=%lu starts=%lu stops=%lu epoch=%lu\n",
        (unsigned long)d.active, (unsigned long)d.armed, (unsigned long)d.fill,
        (unsigned long)d.pending_bytes, (unsigned long)d.pushed_frames,
        (unsigned long)d.submitted_packets, (unsigned long)d.sent_frames,
        (unsigned long)d.silent_frames, (unsigned long)d.underruns,
        (unsigned long)d.overruns, (unsigned long)d.busy,
        (unsigned long)d.short_writes, (unsigned long)d.starts,
        (unsigned long)d.stops, (unsigned long)d.epoch);
}
