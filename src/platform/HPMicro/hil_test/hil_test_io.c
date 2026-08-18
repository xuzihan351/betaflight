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
#include "drivers/io.h"
#include "drivers/exti.h"
#include "drivers/nvic.h"
#include "hil_test.h"

/* ------------------------------------------------------------------ *
 *  Pin-pair table -- one output + one input per pair
 * ------------------------------------------------------------------ */

typedef struct {
    ioTag_t  out_tag;
    ioTag_t  in_tag;
    const char *out_name;
    const char *in_name;
} hil_io_pair_t;

static const hil_io_pair_t hil_io_pairs[] = {
    { DEFIO_TAG_E__PA19, DEFIO_TAG_E__PA22, "PA19", "PA22" },
    { DEFIO_TAG_E__PA18, DEFIO_TAG_E__PA27, "PA18", "PA27" },
    { DEFIO_TAG_E__PA17, DEFIO_TAG_E__PB20, "PA17", "PB20" },
    { DEFIO_TAG_E__PA16, DEFIO_TAG_E__PB21, "PA16", "PB21" },
};

#define HIL_IO_PAIR_COUNT  (sizeof(hil_io_pairs) / sizeof(hil_io_pairs[0]))

/* ------------------------------------------------------------------ *
 *  Helper: get an IO handle, log a warning if missing
 * ------------------------------------------------------------------ */

static IO_t hil_io_require(ioTag_t tag, const char *name)
{
    IO_t io = IOGetByTag(tag);
    if (!io) {
        printf("# WARNING: pin %s unavailable -- skipping pair\n", name);
    }
    return io;
}

/* ------------------------------------------------------------------ *
 *  Test: GPIO output high / low on all four pairs
 * ------------------------------------------------------------------ */

void hil_test_io_output_high_low(void)
{
    for (unsigned i = 0; i < HIL_IO_PAIR_COUNT; i++) {
        IO_t out = hil_io_require(hil_io_pairs[i].out_tag,
                                  hil_io_pairs[i].out_name);
        IO_t in  = hil_io_require(hil_io_pairs[i].in_tag,
                                  hil_io_pairs[i].in_name);
        if (!out || !in) continue;

        /* OUT = push-pull, IN = floating input */
        IOConfigGPIO(out, IOCFG_OUT_PP);
        IOConfigGPIO(in,  IOCFG_IN_FLOATING);

        /* output low -> input reads low */
        IOLo(out);
        hil_delay_ms(1);
        HIL_ASSERT_FALSE(IORead(in));

        /* output high -> input reads high */
        IOHi(out);
        hil_delay_ms(1);
        HIL_ASSERT_TRUE(IORead(in));

        /* back to low */
        IOLo(out);
        hil_delay_ms(1);
        HIL_ASSERT_FALSE(IORead(in));

        /* IOWrite interface */
        IOWrite(out, true);
        hil_delay_ms(1);
        HIL_ASSERT_TRUE(IORead(in));

        IOWrite(out, false);
        hil_delay_ms(1);
        HIL_ASSERT_FALSE(IORead(in));
    }
}

/* ------------------------------------------------------------------ *
 *  Test: GPIO input pull-up
 *
 *  Configures the output side as high-impedance (floating input) so it
 *  does NOT drive the line.  The input side is pull-up only.  Since
 *  the pins are connected, both sides see the pull-up, and IORead
 *  should return true.
 * ------------------------------------------------------------------ */

void hil_test_io_input_pull_up(void)
{
    for (unsigned i = 0; i < HIL_IO_PAIR_COUNT; i++) {
        IO_t out = hil_io_require(hil_io_pairs[i].out_tag,
                                  hil_io_pairs[i].out_name);
        IO_t in  = hil_io_require(hil_io_pairs[i].in_tag,
                                  hil_io_pairs[i].in_name);
        if (!out || !in) continue;

        IOConfigGPIO(out, IOCFG_IN_FLOATING);   /* high-Z */
        IOConfigGPIO(in,  IOCFG_IPU);           /* pull-up */

        hil_delay_ms(1);
        HIL_ASSERT_TRUE(IORead(in));
    }
}

/* ------------------------------------------------------------------ *
 *  Test: GPIO input pull-down
 *
 *  Same pattern -- output floating, input pull-down -> reads low.
 * ------------------------------------------------------------------ */

void hil_test_io_input_pull_down(void)
{
    for (unsigned i = 0; i < HIL_IO_PAIR_COUNT; i++) {
        IO_t out = hil_io_require(hil_io_pairs[i].out_tag,
                                  hil_io_pairs[i].out_name);
        IO_t in  = hil_io_require(hil_io_pairs[i].in_tag,
                                  hil_io_pairs[i].in_name);
        if (!out || !in) continue;

        IOConfigGPIO(out, IOCFG_IN_FLOATING);   /* high-Z */
        IOConfigGPIO(in,  IOCFG_IPD);           /* pull-down */

        hil_delay_ms(1);
        HIL_ASSERT_FALSE(IORead(in));
    }
}

/* ------------------------------------------------------------------ *
 *  Test: GPIO input floating
 *
 *  Both sides floating.  Verify IORead returns a valid boolean
 *  (the actual level is undefined -- capacitance may hold the last
 *  driven state).
 * ------------------------------------------------------------------ */

void hil_test_io_input_floating(void)
{
    for (unsigned i = 0; i < HIL_IO_PAIR_COUNT; i++) {
        IO_t out = hil_io_require(hil_io_pairs[i].out_tag,
                                  hil_io_pairs[i].out_name);
        IO_t in  = hil_io_require(hil_io_pairs[i].in_tag,
                                  hil_io_pairs[i].in_name);
        if (!out || !in) continue;

        IOConfigGPIO(out, IOCFG_IN_FLOATING);
        IOConfigGPIO(in,  IOCFG_IN_FLOATING);

        hil_delay_ms(1);

        bool val = IORead(in);
        (void)val;
        HIL_INFO("%s->%s floating reads %s (OK either way)",
                 hil_io_pairs[i].out_name, hil_io_pairs[i].in_name,
                 val ? "HIGH" : "LOW");
    }
}

/* ------------------------------------------------------------------ *
 *  Test: GPIO toggle on all four pairs
 * ------------------------------------------------------------------ */

void hil_test_io_toggle(void)
{
    for (unsigned i = 0; i < HIL_IO_PAIR_COUNT; i++) {
        IO_t out = hil_io_require(hil_io_pairs[i].out_tag,
                                  hil_io_pairs[i].out_name);
        IO_t in  = hil_io_require(hil_io_pairs[i].in_tag,
                                  hil_io_pairs[i].in_name);
        if (!out || !in) continue;

        IOConfigGPIO(out, IOCFG_OUT_PP);
        IOConfigGPIO(in,  IOCFG_IN_FLOATING);

        /* Start low */
        IOLo(out);
        hil_delay_ms(1);
        HIL_ASSERT_FALSE(IORead(in));

        /* Toggle -> high */
        IOToggle(out);
        hil_delay_ms(1);
        HIL_ASSERT_TRUE(IORead(in));

        /* Toggle -> low */
        IOToggle(out);
        hil_delay_ms(1);
        HIL_ASSERT_FALSE(IORead(in));

        /* Two toggles = net unchanged (low) */
        IOToggle(out);
        IOToggle(out);
        hil_delay_ms(1);
        HIL_ASSERT_FALSE(IORead(in));
    }
}

/* ------------------------------------------------------------------ *
 *  Test: GPIO external interrupt (EXTI) on all four pairs
 *
 *  Configures the input side as a floating-input EXTI and drives the
 *  output side to generate rising and falling edges.  Verifies the
 *  callback fires exactly once for each edge.
 *
 *  HPM6300/HPM6700 emulate BETAFLIGHT_EXTI_TRIGGER_BOTH by switching
 *  the armed polarity after each interrupt, so test both edges using
 *  one EXTI configuration.
 * ------------------------------------------------------------------ */

static volatile unsigned hil_exti_count;

static void hil_exti_callback(extiCallbackRec_t *arg)
{
    (void)arg;
    hil_exti_count++;
}

static void hil_test_io_exti_one_pair(IO_t out, IO_t in,
                                      const char *out_name,
                                      const char *in_name)
{
    (void)out_name;
    (void)in_name;

    extiCallbackRec_t cb;

    IOConfigGPIO(out, IOCFG_OUT_PP);
    IOLo(out);
    hil_delay_ms(1);

    hil_exti_count = 0;
    EXTIHandlerInit(&cb, hil_exti_callback);
    EXTIConfig(in, &cb, 1, IOCFG_IN_FLOATING,
               BETAFLIGHT_EXTI_TRIGGER_BOTH);
    EXTIEnable(in);

    IOHi(out);
    hil_delay_ms(1);

    HIL_ASSERT_EQ(hil_exti_count, 1);

    IOLo(out);
    hil_delay_ms(1);

    HIL_ASSERT_EQ(hil_exti_count, 2);

    /* Clean up */
    EXTIRelease(in);
    IOLo(out);
}

void hil_test_io_exti(void)
{
    for (unsigned i = 0; i < HIL_IO_PAIR_COUNT; i++) {
        IO_t out = hil_io_require(hil_io_pairs[i].out_tag,
                                  hil_io_pairs[i].out_name);
        IO_t in  = hil_io_require(hil_io_pairs[i].in_tag,
                                  hil_io_pairs[i].in_name);
        if (!out || !in) continue;

        hil_test_io_exti_one_pair(out, in,
                                  hil_io_pairs[i].out_name,
                                  hil_io_pairs[i].in_name);
    }
}
