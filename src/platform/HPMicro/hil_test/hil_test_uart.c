/*
 * This file is part of Cleanflight and Betaflight.
 *
 * Cleanflight and Betaflight are free software. You can redistribute
 * this software and/or modify this software under the terms of the
 * GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Cleanflight and Betaflight are distributed in the hope that they
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include "platform.h"

#include "drivers/time.h"
#include "hil_test.h"
#include "hpm_uart_drv.h"
#include "hpm_ioc_regs.h"

/* ------------------------------------------------------------------ *
 *  Test configuration -- adjust for your board
 *
 *  Default: UART1 (HPM_UART1) on PB24 (TX) / PB25 (RX).
 *  Ensure this UART is NOT shared with the debug console or any
 *  other active peripheral on your board.
 * ------------------------------------------------------------------ */

#define HIL_UART_BASE       HPM_UART1
#define HIL_UART_CLK        clock_uart1
#define HIL_UART_BAUD       115200

/* IOC pad and function macros for the TX / RX pins */
#define HIL_UART_TX_PAD     IOC_PAD_PB24
#define HIL_UART_TX_FUNC    IOC_PB24_FUNC_CTL_UART1_TXD
#define HIL_UART_RX_PAD     IOC_PAD_PB25
#define HIL_UART_RX_FUNC    IOC_PB25_FUNC_CTL_UART1_RXD

/* Number of bytes to send in the data-integrity loopback test */
#define HIL_UART_TEST_LEN   256

/* ------------------------------------------------------------------ *
 *  Standalone UART init (raw HPM SDK -- does not use Betaflight serial)
 * ------------------------------------------------------------------ */

static void hil_uart_init(void)
{
    uint32_t freq;

    /* Configure pin functions for UART TX / RX */
    HPM_IOC->PAD[HIL_UART_TX_PAD].FUNC_CTL = HIL_UART_TX_FUNC;
    HPM_IOC->PAD[HIL_UART_RX_PAD].FUNC_CTL = HIL_UART_RX_FUNC;

    /* Enable UART clock */
    clock_add_to_group(HIL_UART_CLK, 0);
    freq = clock_get_frequency(HIL_UART_CLK);

    /* Default UART config: 8N1, no flow control */
    uart_config_t cfg = {0};
    uart_default_config(HIL_UART_BASE, &cfg);
    cfg.baudrate   = HIL_UART_BAUD;
    cfg.src_freq_in_hz = freq;
    cfg.fifo_enable = true;
    cfg.rx_fifo_level = uart_rx_fifo_trg_not_empty;
    cfg.tx_fifo_level = uart_tx_fifo_trg_not_full;

    if (uart_init(HIL_UART_BASE, &cfg) != status_success) {
        printf("# WARNING: UART init failed\n");
    }
}

/* ------------------------------------------------------------------ *
 *  Send one byte (blocking)
 * ------------------------------------------------------------------ */

static void hil_uart_putc(uint8_t byte)
{
    uart_send_byte(HIL_UART_BASE, byte);
}

/* ------------------------------------------------------------------ *
 *  Receive one byte with timeout (returns true on success)
 * ------------------------------------------------------------------ */

static bool hil_uart_getc(uint8_t *byte, unsigned timeout_ms)
{
    timeMs_t deadline = millis() + timeout_ms;

    while (millis() < deadline) {
        if (uart_check_status(HIL_UART_BASE, uart_stat_data_ready)) {
            uart_receive_byte(HIL_UART_BASE, byte);
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ *
 *  Test: UART loopback at standard baud rates
 * ------------------------------------------------------------------ */

void hil_test_uart_loopback(void)
{
    hil_uart_init();

    /* ---- single-byte echo ---- */
    hil_uart_putc(0xA5);
    uint8_t rx;
    if (hil_uart_getc(&rx, 100)) {
        HIL_ASSERT_EQ(rx, 0xA5);
    } else {
        HIL_FAIL("no byte received -- check TX->RX jumper on "
                 "PB24->PB25");
        return;  /* can't continue without loopback */
    }

    /* ---- walking-ones pattern ---- */
    for (unsigned i = 0; i < 8; i++) {
        uint8_t pat = (uint8_t)(1U << i);
        hil_uart_putc(pat);
        if (hil_uart_getc(&rx, 50)) {
            HIL_ASSERT_EQ(rx, pat);
        } else {
            HIL_FAIL("timeout on walking-ones pattern");
        }
    }

    /* ---- full 256-byte sequential data (integrity) ---- */
    unsigned mismatches = 0;
    for (unsigned i = 0; i < HIL_UART_TEST_LEN; i++) {
        uint8_t sent = (uint8_t)i;
        hil_uart_putc(sent);
        if (hil_uart_getc(&rx, 50)) {
            if (rx != sent) {
                mismatches++;
            }
        } else {
            HIL_FAIL("timeout during sequential data test");
            return;
        }
    }
    HIL_ASSERT_EQ(mismatches, 0U);

    if (mismatches == 0 && hil_fail_count == 0) {
        HIL_INFO("UART loopback OK : 256 bytes echo match");
    }
}
