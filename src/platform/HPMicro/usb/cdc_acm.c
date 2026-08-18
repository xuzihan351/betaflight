/*
 * Copyright (c) 2022-2023 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include "platform.h"

#include "build/version.h"

#include "usbd_core.h"
#include "usbd_cdc_acm.h"

#include "usb_descriptor_hpm.h"

/*!< endpoint address */
#define CDC_IN_EP  0x81
#define CDC_OUT_EP 0x01
#define CDC_INT_EP 0x83
#define CDC_ACM_TX_BUFFER_SIZE 2048
#define CDC_ACM_RX_BUFFER_SIZE 2048
/*!< config descriptor size */
#define USB_CONFIG_SIZE (9 + CDC_ACM_DESCRIPTOR_LEN)

static volatile bool flag_con = false, flag_configured = false;

static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, USBD_VID, USBD_CDC_PID, 0x0100, 0x01)
};

static const uint8_t config_descriptor_hs[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_HS, 0x02),
};

static const uint8_t config_descriptor_fs[] = {
    USB_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_FS, 0x02),
};

static const uint8_t device_quality_descriptor[] = {
    USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0xEF, 0x02, 0x01, 0x01),
};

static const uint8_t other_speed_config_descriptor_hs[] = {
    USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_FS, 0x02),
};

static const uint8_t other_speed_config_descriptor_fs[] = {
    USB_OTHER_SPEED_CONFIG_DESCRIPTOR_INIT(USB_CONFIG_SIZE, 0x02, 0x01, USB_CONFIG_BUS_POWERED, USBD_MAX_POWER),
    CDC_ACM_DESCRIPTOR_INIT(0x00, CDC_INT_EP, CDC_OUT_EP, CDC_IN_EP, USB_BULK_EP_MPS_HS, 0x02),
};

static const char *const string_descriptors[] = {
    (const char[]){ 0x09, 0x04 }, /* Langid */
    FC_FIRMWARE_NAME,              /* Manufacturer */
    USBD_PRODUCT_STRING,           /* Product */
    NULL,                          /* Serial Number: generated from OTP UUID */
};

static const uint8_t *device_descriptor_callback(uint8_t speed)
{
    (void)speed;

    return device_descriptor;
}

static const uint8_t *config_descriptor_callback(uint8_t speed)
{
    if (speed == USB_SPEED_HIGH) {
        return config_descriptor_hs;
    } else if (speed == USB_SPEED_FULL) {
        return config_descriptor_fs;
    } else {
        return NULL;
    }
}

static const uint8_t *device_quality_descriptor_callback(uint8_t speed)
{
    (void)speed;

    return device_quality_descriptor;
}

static const uint8_t *other_speed_config_descriptor_callback(uint8_t speed)
{
    if (speed == USB_SPEED_HIGH) {
        return other_speed_config_descriptor_hs;
    } else if (speed == USB_SPEED_FULL) {
        return other_speed_config_descriptor_fs;
    } else {
        return NULL;
    }
}

static const char *string_descriptor_callback(uint8_t speed, uint8_t index)
{
    (void)speed;

    if (index == USB_STRING_SERIAL_INDEX) {
        return hpmUsbGetSerialNumber();
    }
    if (index >= (sizeof(string_descriptors) / sizeof(char *))) {
        return NULL;
    }
    return string_descriptors[index];
}

static const struct usb_descriptor cdc_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .other_speed_descriptor_callback = other_speed_config_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback,
};

static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t read_buffer[CDC_ACM_RX_BUFFER_SIZE];
static uint8_t write_buffer[CDC_ACM_TX_BUFFER_SIZE];
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t tx_buffer[CDC_ACM_TX_BUFFER_SIZE];
static uint8_t *read_buffer_ptr;
static volatile uint32_t read_buffer_available;
static volatile uint32_t write_buffer_rd = 0;
static volatile uint32_t write_buffer_wr = 0;
static volatile bool ep_tx_busy_flag;
static uint8_t cdc_busid;

static void cdc_start_read(uint8_t busid)
{
    read_buffer_ptr = &read_buffer[0];
    usbd_ep_start_read(busid, CDC_OUT_EP, read_buffer_ptr, usbd_get_ep_mps(busid, CDC_OUT_EP));
}

static void cdc_reset_transfer_state(void)
{
    read_buffer_available = 0;
    write_buffer_rd = 0;
    write_buffer_wr = 0;
    ep_tx_busy_flag = false;
}

static void usbd_event_handler(uint8_t busid, uint8_t event)
{
    switch (event) {
    case USBD_EVENT_RESET:
        flag_configured = false;
        cdc_reset_transfer_state();
        break;
    case USBD_EVENT_CONNECTED:
        flag_con = true;
        break;
    case USBD_EVENT_DISCONNECTED:
        flag_con  = false;
        flag_configured = false;
        cdc_reset_transfer_state();
        break;
    case USBD_EVENT_RESUME:
        break;
    case USBD_EVENT_SUSPEND:
        break;
    case USBD_EVENT_CONFIGURED:
        flag_configured = true;
        read_buffer_available = 0;
        /* setup first out ep read transfer */
        cdc_start_read(busid);
        break;
    case USBD_EVENT_SET_REMOTE_WAKEUP:
        break;
    case USBD_EVENT_CLR_REMOTE_WAKEUP:
        break;

    default:
        break;
    }
}
void usbd_cdc_acm_bulk_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)ep;

    read_buffer_ptr = &read_buffer[0];
    read_buffer_available = nbytes;

    /* Keep a non-empty OUT packet in read_buffer until the VCP consumer has
     * drained it.  Leaving the endpoint unarmed makes USB apply backpressure
     * instead of allowing a later packet to overwrite unread data. */
    if (!nbytes) {
        cdc_start_read(busid);
    }
}

void usbd_cdc_acm_bulk_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)ep;
    (void)nbytes;

    /* If the last IN transfer was an exact multiple of the MPS, the host is
     * waiting for a zero-length packet. Otherwise we can drain any further
     * bytes that arrived in the TX ring buffer while the transfer was in
     * flight. */
    if ((nbytes % usbd_get_ep_mps(busid, ep)) == 0 && nbytes) {
        usbd_ep_start_write(busid, ep, NULL, 0);
        return;
    }

    uint32_t tx_len = 0;
    while (write_buffer_rd != write_buffer_wr && tx_len < CDC_ACM_TX_BUFFER_SIZE) {
        tx_buffer[tx_len++] = write_buffer[write_buffer_rd];
        write_buffer_rd = (write_buffer_rd + 1) % CDC_ACM_TX_BUFFER_SIZE;
    }
    if (tx_len) {
        usbd_ep_start_write(busid, CDC_IN_EP, tx_buffer, tx_len);
    } else {
        ep_tx_busy_flag = false;
    }
}

/*!< endpoint call back */
static struct usbd_endpoint cdc_out_ep = {
    .ep_addr = CDC_OUT_EP,
    .ep_cb = usbd_cdc_acm_bulk_out
};

static struct usbd_endpoint cdc_in_ep = {
    .ep_addr = CDC_IN_EP,
    .ep_cb = usbd_cdc_acm_bulk_in
};

static struct usbd_interface intf0;
static struct usbd_interface intf1;

static struct cdc_line_coding lineCoding = {
    .dwDTERate = 115200,
    .bCharFormat = 0,
    .bParityType = 0,
    .bDataBits = 8,
};
static uint16_t ctrlLineState;
static void (*ctrlLineStateCb)(void *context, uint16_t ctrlLineState);
static void *ctrlLineStateCbContext;
static void (*baudRateCb)(void *context, uint32_t baud);
static void *baudRateCbContext;

/* function ------------------------------------------------------------------*/

void cdc_acm_init(uint8_t busid, uint32_t reg_base)
{
    cdc_busid = busid;

    usbd_desc_register(busid, &cdc_descriptor);
    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf0));
    usbd_add_interface(busid, usbd_cdc_acm_init_intf(busid, &intf1));
    usbd_add_endpoint(busid, &cdc_out_ep);
    usbd_add_endpoint(busid, &cdc_in_ep);
    usbd_initialize(busid, reg_base, usbd_event_handler);
}

void usbd_cdc_acm_set_line_coding(uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding)
{
    (void)busid;
    (void)intf;

    lineCoding = *line_coding;
    if (baudRateCb) {
        baudRateCb(baudRateCbContext, lineCoding.dwDTERate);
    }
}

void usbd_cdc_acm_get_line_coding(uint8_t busid, uint8_t intf, struct cdc_line_coding *line_coding)
{
    (void)busid;
    (void)intf;

    *line_coding = lineCoding;
}

static void cdcSetCtrlLineState(uint16_t mask, bool enabled)
{
    if (enabled) {
        ctrlLineState |= mask;
    } else {
        ctrlLineState &= ~mask;
    }
}

void usbd_cdc_acm_set_dtr(uint8_t busid, uint8_t intf, bool dtr)
{
    (void)busid;
    (void)intf;

    cdcSetCtrlLineState(1U << 0, dtr);
}

void usbd_cdc_acm_set_rts(uint8_t busid, uint8_t intf, bool rts)
{
    (void)busid;
    (void)intf;

    cdcSetCtrlLineState(1U << 1, rts);

    /* CherryUSB calls the DTR hook followed by the RTS hook for each
     * SET_CONTROL_LINE_STATE request. Notify Betaflight here so it observes
     * the complete state once per request. */
    if (ctrlLineStateCb) {
        ctrlLineStateCb(ctrlLineStateCbContext, ctrlLineState);
    }
}

uint32_t bf_usbd_cdc_baud_rate(void)
{
    return lineCoding.dwDTERate;
}

void bf_usbd_cdc_set_baud_rate_cb(void (*cb)(void *context, uint32_t baud), void *context)
{
    baudRateCbContext = context;
    baudRateCb = cb;
}

void bf_usbd_cdc_set_ctrl_line_state_cb(void (*cb)(void *context, uint16_t state), void *context)
{
    ctrlLineStateCbContext = context;
    ctrlLineStateCb = cb;
}

uint32_t bf_usbd_ep_write_buffer(const void *data, int count)
{
    const uint8_t *src = (const uint8_t *)data;
    int written = 0;
    for (; written < count; written++) {
        uint32_t next_wr = (write_buffer_wr + 1) % CDC_ACM_TX_BUFFER_SIZE;

        if (next_wr == write_buffer_rd) {
            // TX ring buffer full: let the caller flush and retry the rest.
            break;
        }
        write_buffer[write_buffer_wr] = src[written];
        write_buffer_wr = next_wr;
    }

    return written;
}

uint32_t bf_usbd_ep_tx_free(void)
{
    return (write_buffer_rd - write_buffer_wr - 1 + CDC_ACM_TX_BUFFER_SIZE) % CDC_ACM_TX_BUFFER_SIZE;
}

void bf_usbd_ep_start_write(void)
{
    if (write_buffer_rd == write_buffer_wr) {
        return;
    }

    ep_tx_busy_flag = true;
    uint32_t len = 0;
    while (write_buffer_rd != write_buffer_wr && len < CDC_ACM_TX_BUFFER_SIZE) {
        tx_buffer[len++] = write_buffer[write_buffer_rd];
        write_buffer_rd = (write_buffer_rd + 1) % CDC_ACM_TX_BUFFER_SIZE;
    }
    usbd_ep_start_write(cdc_busid, CDC_IN_EP, tx_buffer, len);
}

bool bf_get_tx_flag(void)
{
    return (ep_tx_busy_flag);
}

uint32_t bf_usbd_ep_rx_available(void)
{
    return read_buffer_available;
}

uint8_t bf_usbd_ep_read(void)
{
    uint8_t data = 0;
    const uint32_t irqState = disable_global_irq(CSR_MSTATUS_MIE_MASK);

    if (read_buffer_available) {
        data = *read_buffer_ptr++;
        read_buffer_available--;
        if (!read_buffer_available && flag_configured) {
            cdc_start_read(cdc_busid);
        }
    }
    enable_global_irq(irqState);

    return data;
}

uint8_t usbIsConnected(void)
{
    return flag_con ? 1 : 0;
}

uint8_t usbIsConfigured(void)
{
    return flag_configured ? 1 : 0;
}
