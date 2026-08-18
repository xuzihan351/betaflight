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
#include "common/utils.h"
#include "drivers/io.h"
#include "drivers/system.h"
#include "drivers/time.h"
#include "hpm_gpio_drv.h"
#include "hpm_uart_drv.h"
#include "hil_test.h"
#include "watchdog_hpmicro.h"

/* ------------------------------------------------------------------ *
 *  Chip name for the banner (derived from compiler -D flag)
 * ------------------------------------------------------------------ */

#if defined(HPM6750)
#define HIL_CHIP_NAME "HPM6750"
#elif defined(HPM6360)
#define HIL_CHIP_NAME "HPM6360"
#else
#define HIL_CHIP_NAME "HPMicro"
#endif

/* ------------------------------------------------------------------ *
 *  Menu timeout in milliseconds
 * ------------------------------------------------------------------ */

#define HIL_MENU_TIMEOUT_MS  60000

/* ------------------------------------------------------------------ *
 *  On-board LED control
 * ------------------------------------------------------------------ */

#if defined(BOARD_LED_GPIO_CTRL) && defined(BOARD_LED_GPIO_PIN)

static void hil_led_init(void)
{
    gpio_set_pin_output_with_initial(HPM_GPIO0,
        BOARD_LED_GPIO_INDEX, BOARD_LED_GPIO_PIN,
        BOARD_LED_OFF_LEVEL);
}

static void hil_led_on(void)
{
    gpio_write_pin(HPM_GPIO0, BOARD_LED_GPIO_INDEX,
                   BOARD_LED_GPIO_PIN, BOARD_LED_ON_LEVEL);
}

static void hil_led_off(void)
{
    gpio_write_pin(HPM_GPIO0, BOARD_LED_GPIO_INDEX,
                   BOARD_LED_GPIO_PIN, BOARD_LED_OFF_LEVEL);
}

#else

static void hil_led_init(void) { }
static void hil_led_on(void)  { }
static void hil_led_off(void) { }

#endif

/* ------------------------------------------------------------------ *
 *  Post-run blink patterns (block forever)
 * ------------------------------------------------------------------ */

static void hil_led_blink_pass(void)
{
    while (1) {
        hil_led_on();
        hil_delay_ms(500);
        hil_led_off();
        hil_delay_ms(500);
    }
}

static void hil_led_blink_fail(void)
{
    while (1) {
        hil_led_on();
        hil_delay_ms(150);
        hil_led_off();
        hil_delay_ms(100);
        hil_led_on();
        hil_delay_ms(150);
        hil_led_off();
        hil_delay_ms(600);
    }
}

/* ------------------------------------------------------------------ *
 *  UART input with per-char timeout: get one character.
 *  Returns the character, or -1 on timeout.
 * ------------------------------------------------------------------ */

static int hil_getchar(uint32_t timeout_ms)
{
    timeMs_t start = millis();

    while (millis() - start < timeout_ms) {
        uint8_t c;
        if (uart_receive_byte((UART_Type *)BOARD_CONSOLE_UART_BASE, &c) == status_success) {
            if (c == '\r' || c == '\n') {
                printf("\r\n");
                /*
                 * Consume the paired CR/LF so the caller doesn't see
                 * \r\n as two separate Enter presses.
                 */
                uint8_t next;
                if (c == '\r') {
                    if (uart_receive_byte((UART_Type *)BOARD_CONSOLE_UART_BASE,
                                          &next) == status_success && next != '\n') {
                        /* not LF — push it back by returning the extra char next time?
                         * unlikely on real terminals; just discard */
                    }
                } else {
                    if (uart_receive_byte((UART_Type *)BOARD_CONSOLE_UART_BASE,
                                          &next) == status_success && next != '\r') {
                    }
                }
                return '\r';  /* always return CR for Enter */
            }
            printf("%c", c);   /* echo */
            return (int)c;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ *
 *  Display the test menu and wait for user selection.
 *
 *  Accepts 0 to run all tests, or a 1-based test number (e.g. "34" +
 *  Enter) to select a specific test.
 *  Returns the 0-based test index to run, or -1 to run all.
 * ------------------------------------------------------------------ */

static int hil_test_menu(void)
{
    unsigned page_start = 0;
    const unsigned page_size = 10;

    printf("\r\n");
    printf("==========================================\r\n");
    printf("  HIL Test Menu\r\n");
    printf("==========================================\r\n");
    printf("   0              - Run ALL tests\r\n");
    printf("  a / Enter       - Run ALL tests (default)\r\n");
    printf("  <number> Enter  - Run a specific test\r\n");
    printf("  n / p          - Next / Previous page\r\n");
    printf("------------------------------------------\r\n");

    /* Print first page of tests */
    unsigned display = hil_test_count;
    if (display > page_size) display = page_size;
    for (unsigned i = page_start; i < page_start + display; i++) {
        printf("  %2u - %s\r\n", i + 1, hil_test_registry[i].name);
    }
    printf("------------------------------------------\r\n");
    if (hil_test_count > page_size) {
        printf("  (page %u/%u, %u tests total)\r\n",
               page_start / page_size + 1,
               (hil_test_count + page_size - 1) / page_size,
               hil_test_count);
    }
    printf("  Auto-run ALL in %lu seconds if no input...\r\n",
           (unsigned long)(HIL_MENU_TIMEOUT_MS / 1000));
    printf("==========================================\r\n");
    printf("\r\nSelect: ");

    /*
     * millis() calibration note:
     * On some HPMicro clock configurations, millis() may run faster than real
     * wall-clock time (observed ~10x on HPM6360).  The timeout is set large
     * enough to compensate.  If this drifts, adjust HIL_MENU_TIMEOUT_MS or
     * check clock_get_frequency(clock_mchtmr0) in micros().
     */
    HIL_INFO("millis() tick: start=%lu", (unsigned long)millis());

    /* Read input: accumulate digits until Enter, with overall timeout */
    int choice = -1;
    int digits[4];   /* up to 4 digits (max test count < 1000) */
    int ndigits = 0;
    bool got_input = false;
    timeMs_t overall_start = millis();

    while (true) {
        const timeMs_t elapsed = millis() - overall_start;
        if (elapsed >= HIL_MENU_TIMEOUT_MS) {
            break;
        }
        int c = hil_getchar(HIL_MENU_TIMEOUT_MS - elapsed);

        if (c == -1) {
            /* timeout */
            if (!got_input) {
                printf("\r\n[Timeout] Running ALL tests...\r\n");
                return -1;
            }
            /* got partial digits but no Enter — run the accumulated number */
            break;
        }

        got_input = true;

        if (c == 'a' || c == 'A') {
            printf("\r\nRunning ALL tests...\r\n");
            return -1;
        }

        if (c == 'n' || c == 'N') {
            /* next page */
            page_start += page_size;
            if (page_start >= hil_test_count) page_start = 0;
            ndigits = 0;  /* reset input on page change */
            /* reprint menu */
            printf("\r\n");
            printf("==========================================\r\n");
            printf("  HIL Test Menu\r\n");
            printf("==========================================\r\n");
            printf("   0              - Run ALL tests\r\n");
            printf("  a / Enter       - Run ALL tests (default)\r\n");
            printf("  <number> Enter  - Run a specific test\r\n");
            printf("  n / p          - Next / Previous page\r\n");
            printf("------------------------------------------\r\n");
            unsigned end = page_start + page_size;
            if (end > hil_test_count) end = hil_test_count;
            for (unsigned i = page_start; i < end; i++) {
                printf("  %2u - %s\r\n", i + 1, hil_test_registry[i].name);
            }
            printf("------------------------------------------\r\n");
            if (hil_test_count > page_size) {
                printf("  (page %u/%u, %u tests total)\r\n",
                       page_start / page_size + 1,
                       (hil_test_count + page_size - 1) / page_size,
                       hil_test_count);
            }
            printf("  Auto-run ALL in %lu seconds if no input...\r\n",
                   (unsigned long)(HIL_MENU_TIMEOUT_MS / 1000));
            printf("==========================================\r\n");
            printf("\r\nSelect: ");
            continue;
        }

        if (c == 'p' || c == 'P') {
            /* previous page */
            if (page_start >= page_size) {
                page_start -= page_size;
            } else {
                page_start = ((hil_test_count - 1) / page_size) * page_size;
            }
            ndigits = 0;
            /* reprint menu */
            printf("\r\n");
            printf("==========================================\r\n");
            printf("  HIL Test Menu\r\n");
            printf("==========================================\r\n");
            printf("   0              - Run ALL tests\r\n");
            printf("  a / Enter       - Run ALL tests (default)\r\n");
            printf("  <number> Enter  - Run a specific test\r\n");
            printf("  n / p          - Next / Previous page\r\n");
            printf("------------------------------------------\r\n");
            unsigned end = page_start + page_size;
            if (end > hil_test_count) end = hil_test_count;
            for (unsigned i = page_start; i < end; i++) {
                printf("  %2u - %s\r\n", i + 1, hil_test_registry[i].name);
            }
            printf("------------------------------------------\r\n");
            if (hil_test_count > page_size) {
                printf("  (page %u/%u, %u tests total)\r\n",
                       page_start / page_size + 1,
                       (hil_test_count + page_size - 1) / page_size,
                       hil_test_count);
            }
            printf("  Auto-run ALL in %lu seconds...\r\n",
                   (unsigned long)(HIL_MENU_TIMEOUT_MS / 1000));
            printf("==========================================\r\n");
            printf("\r\nSelect: ");
            continue;
        }

        if (c == '\r') {
            /* Enter — confirm selection */
            if (ndigits == 0) {
                printf("Running ALL tests...\r\n");
                return -1;
            }
            break;
        }

        if (c >= '0' && c <= '9' && ndigits < 4) {
            digits[ndigits++] = c - '0';
            continue;
        }

        /* Backspace / delete  —  remove last digit */
        if ((c == 0x7F || c == 0x08) && ndigits > 0) {
            ndigits--;
            printf("\b \b");   /* erase echoed digit */
        }

        /* any other char ignored */
    }

    /* Build number from accumulated digits */
    if (ndigits > 0) {
        choice = 0;
        for (int i = 0; i < ndigits; i++) {
            choice = choice * 10 + digits[i];
        }
    } else {
        choice = -1;
    }

    if (choice == 0) {
        printf("\r\nRunning ALL tests...\r\n");
        return -1;
    }

    if (choice < 1 || choice > (int)hil_test_count) {
        printf("\r\n# %d: out of range, running ALL tests...\r\n", choice);
        return -1;
    }

    const unsigned index = (unsigned)(choice - 1);
    printf("\r\nRunning test [%u]: %s\r\n", (unsigned)choice,
           hil_test_registry[index].name);
    return (int)index;
}

/* ------------------------------------------------------------------ *
 *  Entry point
 * ------------------------------------------------------------------ */

int main(void)
{
    /* Standard Betaflight platform init -- clocks, console, pinmux */
    systemInit();
    /* HIL has no production scheduler and may wait indefinitely in its menu. */
    systemWatchdogDisable();

    /* Initialise IO subsystem so IOGetByTag() returns valid handles */
    IOInitGlobal();

    /* Configure on-board LED */
    hil_led_init();

    /* Print banner */
    printf("\n\n");
    printf("==========================================\n");
    printf("  Betaflight HIL Test Firmware\n");
    printf("  Chip  : " HIL_CHIP_NAME "\n");
    printf("  Board : " STR(BOARD_NAME) "\n");
    printf("==========================================\n");

    /* ---- Interactive menu loop ---- */
    int result = 0;

    for (;;) {
        int choice = hil_test_menu();

        if (choice < 0) {
            /* Run all tests */
            result = hil_test_run_all();
            break;   /* all tests done -> LED feedback */
        }

        /* Run a single test */
        int r = hil_test_run_one((unsigned)choice);
        if (r != 0) result = 1;   /* remember any failure */

        /* Print one-line summary and loop back to menu */
        if (r == 0) {
            printf("\r\n-----[ Result ]-----\r\n  PASS\r\n---------------------\r\n");
        } else {
            printf("\r\n-----[ Result ]-----\r\n  FAIL\r\n---------------------\r\n");
        }
        printf("\r\nReturning to menu...\r\n");
    }

    /* Indefinite LED feedback */
    if (result == 0) {
        printf("\r\nLED: steady blink = ALL TESTS PASSED\r\n");
        hil_led_blink_pass();
    } else {
        printf("\r\nLED: fast double-blink = FAILURES DETECTED\r\n");
        hil_led_blink_fail();
    }

    /* unreachable */
    return 0;
}
