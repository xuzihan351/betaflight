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
#include "common/time.h"
#include "drivers/persistent.h"
#include "hil_test.h"

#include <string.h>

/* ------------------------------------------------------------------ *
 *  Test: rtcTimeMake / rtcTimeGetSeconds / rtcTimeGetMillis
 * ------------------------------------------------------------------ */

void hil_test_time_make(void)
{
    rtcTime_t t = rtcTimeMake(1000, 500);
    HIL_ASSERT_EQ(rtcTimeGetSeconds(&t), 1000);
    HIL_ASSERT_EQ(rtcTimeGetMillis(&t), 500);

    rtcTime_t t999 = rtcTimeMake(0, 999);
    HIL_ASSERT_EQ(rtcTimeGetSeconds(&t999), 0);
    HIL_ASSERT_EQ(rtcTimeGetMillis(&t999), 999);

    /* Negative seconds truncate toward zero in the second/millis split */
    rtcTime_t neg = rtcTimeMake(-1, 500);
    HIL_ASSERT_EQ(rtcTimeGetSeconds(&neg), 0);
}

/* ------------------------------------------------------------------ *
 *  Test: rtcSet() / rtcGet() / rtcHasTime()
 *
 *  rtcSet() anchors 'started' against the free-running millis() clock;
 *  rtcGet() returns started + millis(), so it tracks wall time.
 * ------------------------------------------------------------------ */

void hil_test_time_rtc_get(void)
{
    rtcTime_t t0 = rtcTimeMake(1700000000, 0);
    HIL_ASSERT(rtcSet(&t0));
    HIL_ASSERT(rtcHasTime());

    rtcTime_t now;
    HIL_ASSERT(rtcGet(&now));
    HIL_ASSERT(now >= t0);

    rtcTime_t before = now;
    hil_delay_ms(100);
    HIL_ASSERT(rtcGet(&now));

    rtcTime_t elapsed = now - before;
    HIL_INFO("rtcGet() advanced %lld ms after 100 ms wait",
             (long long)elapsed);
    HIL_ASSERT(elapsed >= 100);
    HIL_ASSERT(elapsed < 10000);
}

/* ------------------------------------------------------------------ *
 *  Test: rtcSetDateTime() / rtcGetDateTime() round trip
 *
 *  The seconds field may tick between set and read, so allow a small
 *  tolerance.  Sub-second millis is not compared.
 * ------------------------------------------------------------------ */

void hil_test_time_rtc_datetime(void)
{
    dateTime_t dt = { 0 };
    dt.year = 2026; dt.month = 7; dt.day = 31;
    dt.hours = 10; dt.minutes = 20; dt.seconds = 30; dt.millis = 123;

    HIL_ASSERT(rtcSetDateTime(&dt));

    dateTime_t out = { 0 };
    HIL_ASSERT(rtcGetDateTime(&out));

    HIL_INFO("set 2026-07-31 10:20:30.123, read %04u-%02u-%02u %02u:%02u:%02u.%03u",
             out.year, out.month, out.day, out.hours, out.minutes,
             out.seconds, out.millis);

    HIL_ASSERT_EQ(out.year, dt.year);
    HIL_ASSERT_EQ(out.month, dt.month);
    HIL_ASSERT_EQ(out.day, dt.day);
    HIL_ASSERT_EQ(out.hours, dt.hours);
    HIL_ASSERT_EQ(out.minutes, dt.minutes);
    HIL_ASSERT(out.seconds >= dt.seconds);
    HIL_ASSERT(out.seconds <= dt.seconds + 2);
}

/* ------------------------------------------------------------------ *
 *  Test: dateTimeFormatUTC() — ISO 8601 output
 * ------------------------------------------------------------------ */

void hil_test_time_format_utc(void)
{
    dateTime_t dt = { 0 };
    dt.year = 2026; dt.month = 7; dt.day = 31;
    dt.hours = 10; dt.minutes = 20; dt.seconds = 30; dt.millis = 123;

    char buf[FORMATTED_DATE_TIME_BUFSIZE];
    HIL_ASSERT(dateTimeFormatUTC(buf, &dt));
    HIL_INFO("UTC format: %s", buf);
    HIL_ASSERT(strcmp(buf, "2026-07-31T10:20:30.123+00:00") == 0);
}

/* ------------------------------------------------------------------ *
 *  Test: dateTimeFormatLocal() / dateTimeFormatLocalShort()
 *
 *  Exercises the timezone offset handling in dateTimeFormat().
 * ------------------------------------------------------------------ */

void hil_test_time_format_local(void)
{
    dateTime_t dt = { 0 };
    dt.year = 2026; dt.month = 7; dt.day = 31;
    dt.hours = 10; dt.minutes = 20; dt.seconds = 30; dt.millis = 123;

    char buf[FORMATTED_DATE_TIME_BUFSIZE];

    /* UTC+1:30 -> 11:50 local */
    timeConfigMutable()->tz_offsetMinutes = 90;
    HIL_ASSERT(dateTimeFormatLocal(buf, &dt));
    HIL_INFO("local format (+90 min): %s", buf);
    HIL_ASSERT(strcmp(buf, "2026-07-31T11:50:30.123+01:30") == 0);

    /* UTC-1:00 -> 09:20 local */
    timeConfigMutable()->tz_offsetMinutes = -60;
    HIL_ASSERT(dateTimeFormatLocal(buf, &dt));
    HIL_INFO("local format (-60 min): %s", buf);
    HIL_ASSERT(strcmp(buf, "2026-07-31T09:20:30.123-01:00") == 0);

    /* Short format with no offset */
    timeConfigMutable()->tz_offsetMinutes = 0;
    HIL_ASSERT(dateTimeFormatLocalShort(buf, &dt));
    HIL_ASSERT(strcmp(buf, "2026-07-31 10:20:30") == 0);

    timeConfigMutable()->tz_offsetMinutes = 0;
}

/* ------------------------------------------------------------------ *
 *  Test: rtcPersistWrite() / rtcPersistRead()
 *
 *  Stores the current RTC value into the persistent backup registers
 *  and reads it back.  The previous register contents are restored so
 *  the real config data is left untouched.
 * ------------------------------------------------------------------ */

void hil_test_time_rtc_persist(void)
{
    uint32_t savedHigh = persistentObjectRead(PERSISTENT_OBJECT_RTC_HIGH);
    uint32_t savedLow  = persistentObjectRead(PERSISTENT_OBJECT_RTC_LOW);

    rtcTime_t t0 = rtcTimeMake(1700000000, 0);
    HIL_ASSERT(rtcSet(&t0));

    rtcPersistWrite(0);

    rtcTime_t restored = 0;
    HIL_ASSERT(rtcPersistRead(&restored));
    HIL_INFO("rtcPersistRead() returned %lld ms", (long long)restored);

    /* Written moments ago, so it must be very close to t0 */
    rtcTime_t drift = restored - t0;
    HIL_ASSERT(drift >= 0);
    HIL_ASSERT(drift < 10000);

    persistentObjectWrite(PERSISTENT_OBJECT_RTC_HIGH, savedHigh);
    persistentObjectWrite(PERSISTENT_OBJECT_RTC_LOW, savedLow);
}
