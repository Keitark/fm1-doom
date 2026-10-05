#include "app_config.h"
#include "system/includes.h"
#include "system/sys_time.h"
#include "os/os_api.h"
#include "usb/device/cdc.h"
#include "usb/usb_config.h"
#include "boot_entry.h"
#include "fm1_doom_usb.h"

extern int fm1_cdc_ready(usb_dev id);
extern volatile unsigned fm1_cdc_generation;

volatile uint32_t fm1_doom_usb_stage, fm1_doom_usb_heartbeat, fm1_doom_usb_tx_dropped;
volatile int fm1_doom_usb_error;
static fm1_doom_usb_protocol protocol;
static char tx[1024];
static unsigned tx_read, tx_write;

static void reply(void *context, const char *text)
{
    size_t length = strlen(text), i;
    (void)context;
    if (length > sizeof(tx) - (tx_write - tx_read)) {
        ++fm1_doom_usb_tx_dropped;
        return;
    }
    for (i = 0; i < length; ++i) tx[(tx_write++) % sizeof(tx)] = text[i];
}

static void get_status(void *context, struct fm1_doom_usb_status *status)
{
    (void)context;
    fm1_doom_usb_get_status(status);
}

static void request_stop(void *context)
{
    (void)context;
    fm1_doom_usb_request_stop();
}

static int is_stopped(void *context)
{
    (void)context;
    return fm1_doom_usb_is_stopped();
}

static int tx_drained(void *context)
{
    (void)context;
    return tx_write == tx_read;
}

static int boot_arm(void *context)
{
    (void)context;
    return fm1_usb_boot_arm();
}

void __attribute__((noinline, used)) fm1_doom_usb_task(void *argument)
{
    uint8_t rx[64], output[63];
    unsigned generation = 0, i, length;
    uint32_t last = 0;
    const fm1_doom_usb_protocol_io io = {
        0, reply, get_status, request_stop, is_stopped, tx_drained, boot_arm
    };
    (void)argument;
    fm1_doom_usb_stage = 1;
    fm1_doom_usb_error = usb_device_mode(FM1_USB_CONTROLLER, CDC_CLASS);
    if (fm1_doom_usb_error) {
        fm1_doom_usb_stage = 0xff;
        for (;;) os_time_dly(100);
    }
    fm1_doom_usb_stage = 2;
    for (;;) {
        uint32_t now = timer_get_ms();
        ++fm1_doom_usb_heartbeat;
        if (generation != fm1_cdc_generation || !fm1_cdc_ready(FM1_USB_CONTROLLER)) {
            generation = fm1_cdc_generation;
            fm1_doom_usb_protocol_reset(&protocol);
            tx_read = tx_write = 0;
            last = now - 1000u;
        }
        if (fm1_cdc_ready(FM1_USB_CONTROLLER)) {
            if (fm1_usb_rx_fault()) {
                fm1_doom_usb_protocol_reset(&protocol);
                tx_read = tx_write = 0;
                reply(0, "ERR RX_OR_UBOOT_ABORTED\n");
            }
            length = cdc_read_data(FM1_USB_CONTROLLER, rx, sizeof(rx));
            if (generation != fm1_usb_rx_generation()) {
                generation = fm1_usb_rx_generation();
                fm1_doom_usb_protocol_reset(&protocol);
                tx_read = tx_write = 0;
            }
            if (length) fm1_doom_usb_protocol_feed(&protocol, rx, length, now, &io);
            fm1_doom_usb_protocol_tick(&protocol, now, &io);
            if (!fm1_usb_boot_pending() && !protocol.used && !protocol.dropping &&
                (uint32_t)(now - last) >= 1000u && tx_read == tx_write) {
                last = now;
                fm1_doom_usb_protocol_status(&io);
            }
            /* Short packets avoid the SDK's blocking zero-length packet path. */
            length = tx_write - tx_read;
            if (length > sizeof(output)) length = sizeof(output);
            for (i = 0; i < length; ++i) output[i] = tx[(tx_read + i) % sizeof(tx)];
            if (length) tx_read += cdc_write_data(FM1_USB_CONTROLLER, output, length);
        }
        os_time_dly(1);
    }
}
