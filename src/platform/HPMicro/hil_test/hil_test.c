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

#if defined(HPM6750)
#define HIL_CHIP_TAG "HPM6750"
#elif defined(HPM6360)
#define HIL_CHIP_TAG "HPM6360"
#else
#define HIL_CHIP_TAG "HPMicro"
#endif

/* ------------------------------------------------------------------ *
 *  Per-test state
 * ------------------------------------------------------------------ */

const char *hil_current_test = "";
int         hil_fail_count   = 0;

/* ------------------------------------------------------------------ *
 *  Forward declarations of test functions — add new tests here
 * ------------------------------------------------------------------ */

/* hil_test_io.c */
void hil_test_io_output_high_low(void);
void hil_test_io_input_pull_up(void);
void hil_test_io_input_pull_down(void);
void hil_test_io_input_floating(void);
void hil_test_io_toggle(void);
void hil_test_io_exti(void);

/* hil_test_uart.c */
void hil_test_uart_loopback(void);

/* hil_test_spi.c */
void hil_test_spi1_who_am_i(void);

/* hil_test_adc.c */
void hil_test_adc_sdk_direct(void);
void hil_test_adc_tag_map_lookup(void);
void hil_test_adc_device_by_instance(void);
void hil_test_adc_bf_init(void);
void hil_test_adc_bf_read(void);
void hil_test_adc_bf_unconfigured(void);
void hil_test_adc_bf_multi_channel(void);

/* hil_test_clock.c */
void hil_test_clock_micros_monotonic(void);
void hil_test_clock_micros_isr(void);
void hil_test_clock_delay_micros(void);
void hil_test_clock_millis_rate(void);
void hil_test_clock_delay(void);
void hil_test_clock_cycle_counter(void);

/* hil_test_time.c */
void hil_test_time_make(void);
void hil_test_time_rtc_get(void);
void hil_test_time_rtc_datetime(void);
void hil_test_time_format_utc(void);
void hil_test_time_format_local(void);
void hil_test_time_rtc_persist(void);

/* hil_test_persistent.c */
void hil_test_persistent_read_write(void);
void hil_test_persistent_init_corrupt_magic(void);
void hil_test_persistent_init_preserve(void);
void hil_test_persistent_soft_reset(void);

/* hil_test_config_flash.c */
void hil_test_config_flash_unlock(void);
void hil_test_config_flash_write_word(void);
void hil_test_config_flash_lock_flags(void);

/* hil_test_i2c.c */
void hil_test_i2c_ms5611_prom(void);
void hil_test_i2c_ms5611_data(void);
void hil_test_i2c_mmc5983_id(void);
void hil_test_i2c_mmc5983_data(void);

/* ------------------------------------------------------------------ *
 *  Test registry — one entry per test case
 * ------------------------------------------------------------------ */

hil_test_entry_t hil_test_registry[] = {

    /* ---- GPIO / IO tests ---- */
    { "GPIO output high/low",       hil_test_io_output_high_low, 0 },
    { "GPIO input pull-up",         hil_test_io_input_pull_up,   0 },
    { "GPIO input pull-down",       hil_test_io_input_pull_down, 0 },
    { "GPIO input floating",        hil_test_io_input_floating,  0 },
    { "GPIO toggle",                hil_test_io_toggle,          0 },
    { "GPIO EXTI rising/falling",   hil_test_io_exti,            0 },

    /* ---- UART tests ---- */
    { "UART loopback",              hil_test_uart_loopback,      0 },

    /* ---- SPI tests ---- */
    { "SPI1 IMU6200AL WHO_AM_I",   hil_test_spi1_who_am_i,      0 },

    /* ---- ADC tests ---- */
    { "ADC SDK direct init/read",   hil_test_adc_sdk_direct,       0 },
    { "ADC tag-map lookup",         hil_test_adc_tag_map_lookup,   0 },
    { "ADC device-by-instance",     hil_test_adc_device_by_instance, 0 },
    { "ADC BF init single chan",    hil_test_adc_bf_init,          0 },
    { "ADC BF read single chan",    hil_test_adc_bf_read,          0 },
    { "ADC BF unconfigured ret 0",  hil_test_adc_bf_unconfigured,  0 },
    { "ADC BF multi-channel",       hil_test_adc_bf_multi_channel, 0 },

    /* ---- System clock tests ---- */
    { "Clock micros monotonic",     hil_test_clock_micros_monotonic, 0 },
    { "Clock microsISR==micros",    hil_test_clock_micros_isr,      0 },
    { "Clock delayMicroseconds",    hil_test_clock_delay_micros,    0 },
    { "Clock millis rate",          hil_test_clock_millis_rate,     0 },
    { "Clock delay()",              hil_test_clock_delay,           0 },
    { "Clock cycle counter",        hil_test_clock_cycle_counter,   0 },

    /* ---- RTC time tests ---- */
    { "Time rtcTimeMake",           hil_test_time_make,             0 },
    { "Time rtcSet/rtcGet",         hil_test_time_rtc_get,          0 },
    { "Time rtcSet/GetDateTime",    hil_test_time_rtc_datetime,     0 },
    { "Time dateTimeFormatUTC",     hil_test_time_format_utc,       0 },
    { "Time dateTimeFormatLocal",   hil_test_time_format_local,     0 },
    { "Time rtcPersistWrite/Read",  hil_test_time_rtc_persist,      0 },

    /* ---- Persistent register tests ---- */
    { "Persistent read/write",      hil_test_persistent_read_write,       0 },
    { "Persistent init corrupt magic", hil_test_persistent_init_corrupt_magic, 0 },
    { "Persistent init preserve",   hil_test_persistent_init_preserve,    0 },

    /* ---- Config flash tests ---- */
    { "Config flash unlock/props",  hil_test_config_flash_unlock,       0 },
    { "Config flash writeWord",     hil_test_config_flash_write_word,   0 },
    { "Config flash lock/flags",    hil_test_config_flash_lock_flags,   0 },

    /* ---- I2C / Barometer tests ---- */
    { "I2C MS5611 PROM read + CRC", hil_test_i2c_ms5611_prom,          0 },
    { "I2C MS5611 D1/D2 ADC data",  hil_test_i2c_ms5611_data,          0 },

    /* ---- I2C / Magnetometer tests ---- */
    { "I2C MMC5983 Product ID",     hil_test_i2c_mmc5983_id,            0 },
    { "I2C MMC5983 Mag Data",       hil_test_i2c_mmc5983_data,          0 },

    /* ---- Soft reset test LAST: it reboots the board on the first pass,
     * so everything else must have already run. ---- */
    { "Persistent soft reset",      hil_test_persistent_soft_reset,       0 },

    /* ---- Add new test modules below ---- */
};

const unsigned hil_test_count =
    sizeof(hil_test_registry) / sizeof(hil_test_registry[0]);

/* ------------------------------------------------------------------ *
 *  Test runner
 * ------------------------------------------------------------------ */

static void hil_test_print_summary(int pass, int fail, int skip, unsigned total);

int hil_test_run_all(void)
{
    int pass  = 0;
    int fail  = 0;
    int skip  = 0;   /* reserved for future HIL_SKIP support */

    /* TAP header */
    printf("\n");
    printf("======================================================\n");
    printf("  Betaflight HIL (Hardware-in-the-Loop) Test Suite\n");
    printf("  Platform: HPMicro  |  Chip: " HIL_CHIP_TAG "\n");
    printf("======================================================\n");
    printf("1..%u\n", hil_test_count);

    for (unsigned i = 0; i < hil_test_count; i++) {
        hil_test_entry_t *t = &hil_test_registry[i];

        /* reset per-test state */
        hil_current_test = t->name;
        hil_fail_count   = 0;

        /* run the test */
        t->func();

        /* report TAP result */
        if (hil_fail_count == 0) {
            printf("ok %u - %s\n", i + 1, t->name);
            t->result = 1;
            pass++;
        } else {
            printf("\n");
            printf("  ********************************************\n");
            printf("  *** FAILED [%u] %s\n", i + 1, t->name);
            printf("  ***       %d failure(s)\n", hil_fail_count);
            printf("  ********************************************\n");
            printf("not ok %u - %s  (%d failure(s))\n",
                   i + 1, t->name, hil_fail_count);
            t->result = -1;
            fail++;
        }
    }

    /* summary */
    hil_test_print_summary(pass, fail, skip, hil_test_count);

    return fail ? 1 : 0;
}

/* ------------------------------------------------------------------ *
 *  Summary printer — shared by the end-of-run report and the
 *  pre-soft-reset snapshot (hil_test_print_summary_so_far).
 * ------------------------------------------------------------------ */

static void hil_test_print_summary(int pass, int fail, int skip, unsigned total)
{
    printf("\n");
    if (fail > 0) {
        printf("##############################################\n");
        printf("###          HIL Test Summary              ###\n");
        printf("###                                        ###\n");
        printf("###  Total: %-4u  PASS: %-4d  FAIL: %-4d  ###\n",
               total, pass, fail);
        if (skip) printf("###  SKIP: %-4d                            ###\n", skip);
        printf("###  RESULT: %d TEST(S) FAILED           ###\n", fail);
        printf("##############################################\n");
    } else {
        printf("-----[ HIL Test Summary ]-----\n");
        printf("  Total: %u\n", total);
        printf("  PASS:  %d\n", pass);
        printf("  FAIL:  %d\n", fail);
        if (skip) printf("  SKIP:  %d\n", skip);
        printf("-------------------------------\n");
        printf("  RESULT: ALL TESTS PASSED\n");
        printf("-------------------------------\n");
    }
    printf("\n");
}

/* ------------------------------------------------------------------ *
 *  Print a snapshot summary of the tests completed so far.
 *
 *  Called by the soft-reset test before it reboots the board, so the
 *  results are visible even though this run is cut short and the normal
 *  end-of-run summary is never reached.
 * ------------------------------------------------------------------ */

void hil_test_print_summary_so_far(void)
{
    int pass = 0;
    int fail = 0;
    int skip = 0;

    for (unsigned i = 0; i < hil_test_count; i++) {
        switch (hil_test_registry[i].result) {
        case 1:  pass++; break;
        case -1: fail++; break;
        }
    }

    printf("\n=== Pre-soft-reset summary (%d of %u tests completed) ===\n",
           pass + fail, hil_test_count);
    hil_test_print_summary(pass, fail, skip, pass + fail);
}

/* ------------------------------------------------------------------ *
 *  Run a single test by registry index
 * ------------------------------------------------------------------ */

int hil_test_run_one(unsigned index)
{
    if (index >= hil_test_count) {
        printf("Invalid test index %u (max %u)\n", index, hil_test_count - 1);
        return -1;
    }

    hil_test_entry_t *t = &hil_test_registry[index];

    /* reset per-test state */
    hil_current_test = t->name;
    hil_fail_count   = 0;

    printf("\n--- Running [%u] %s ---\n", index + 1, t->name);

    /* run the test */
    t->func();

    /* report result */
    if (hil_fail_count == 0) {
        printf("ok - %s\n", t->name);
        t->result = 1;
    } else {
        printf("\n");
        printf("  ********************************************\n");
        printf("  *** FAILED [%u] %s\n", index + 1, t->name);
        printf("  ***       %d failure(s)\n", hil_fail_count);
        printf("  ********************************************\n");
        printf("not ok - %s  (%d failure(s))\n", t->name, hil_fail_count);
        t->result = -1;
    }

    return (hil_fail_count == 0) ? 0 : 1;
}

/* ------------------------------------------------------------------ *
 *  Delay helper — systemInit() must have been called first so that
 *  micros() / millis() work.
 * ------------------------------------------------------------------ */

void hil_delay_ms(unsigned ms)
{
    timeMs_t start = millis();
    while (millis() - start < ms) {
        /* spin */
    }
}
