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
#include "drivers/system.h"
#include "hil_test.h"

/* ------------------------------------------------------------------ *
 *  Test: micros() is monotonic and advances
 *
 *  Samples micros() 1000 times in a tight loop.  The counter must never
 *  go backwards, and (with ~1us resolution over a few hundred us of
 *  loop time) it must advance at least once.
 * ------------------------------------------------------------------ */

void hil_test_clock_micros_monotonic(void)
{
    timeUs_t first = micros();
    timeUs_t prev = first;
    unsigned advances = 0;

    for (int i = 0; i < 1000; i++) {
        timeUs_t now = micros();
        if (now < prev) {
            HIL_FAIL("micros() went backwards");
            return;
        }
        if (now > prev) {
            advances++;
        }
        prev = now;
    }

    HIL_INFO("micros(): 1000 samples, %u advances (first=%lu)",
             advances, (unsigned long)first);
    HIL_ASSERT(advances > 0);
}

/* ------------------------------------------------------------------ *
 *  Test: microsISR() tracks micros()
 * ------------------------------------------------------------------ */

void hil_test_clock_micros_isr(void)
{
    timeUs_t a = micros();
    timeUs_t b = microsISR();
    timeDelta_t diff = (timeDelta_t)(b - a);
    /* Same source; allow a few us for an interrupt between the reads */
    HIL_ASSERT(diff >= 0 && diff <= 10);
}

/* ------------------------------------------------------------------ *
 *  Test: delayMicroseconds() actually waits
 * ------------------------------------------------------------------ */

void hil_test_clock_delay_micros(void)
{
    timeUs_t start = micros();
    delayMicroseconds(1000);
    timeUs_t elapsed = micros() - start;

    HIL_INFO("delayMicroseconds(1000) elapsed = %lu us",
             (unsigned long)elapsed);
    HIL_ASSERT(elapsed >= 1000);
    HIL_ASSERT(elapsed < 50000);   /* gross upper bound */
}

/* ------------------------------------------------------------------ *
 *  Test: millis() tracks wall clock
 * ------------------------------------------------------------------ */

void hil_test_clock_millis_rate(void)
{
    timeMs_t start = millis();
    hil_delay_ms(100);
    timeMs_t elapsed = millis() - start;

    HIL_INFO("hil_delay_ms(100) elapsed = %lu ms",
             (unsigned long)elapsed);
    HIL_ASSERT(elapsed >= 100);
    HIL_ASSERT(elapsed < 1000);    /* generous upper bound */
}

/* ------------------------------------------------------------------ *
 *  Test: delay() (ms) actually waits
 * ------------------------------------------------------------------ */

void hil_test_clock_delay(void)
{
    timeMs_t start = millis();
    delay(50);
    timeMs_t elapsed = millis() - start;

    HIL_INFO("delay(50) elapsed = %lu ms", (unsigned long)elapsed);
    HIL_ASSERT(elapsed >= 50);
    HIL_ASSERT(elapsed < 500);
}

/* ------------------------------------------------------------------ *
 *  Test: cycle counter helpers
 *
 *  getCycleCounter() is defined as (uint32_t)micros() on HPMicro, and
 *  the clockCyclesTo* helpers are identity/scaling conversions.  This
 *  verifies they are wired to the real time base rather than stubbed.
 * ------------------------------------------------------------------ */

void hil_test_clock_cycle_counter(void)
{
    uint32_t a = getCycleCounter();
    uint32_t b = (uint32_t)micros();
    uint32_t diff = (a > b) ? (a - b) : (b - a);
    HIL_INFO("getCycleCounter()=%lu micros()=%lu",
             (unsigned long)a, (unsigned long)b);
    HIL_ASSERT(diff <= 20);

    HIL_ASSERT_EQ(clockCyclesToMicros(5), 5);
    HIL_ASSERT_EQ(clockCyclesTo10thMicros(5), 50);
    HIL_ASSERT_EQ(clockCyclesTo100thMicros(5), 500);
    HIL_ASSERT_EQ(clockMicrosToCycles(5), 5);
}
