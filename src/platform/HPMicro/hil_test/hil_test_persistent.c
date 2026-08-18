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
#include "drivers/persistent.h"
#include "drivers/system.h"
#include "hpm_wdg_drv.h"
#include "hil_test.h"
#include "watchdog_hpmicro.h"

/* Magic value written by persistentObjectInit(); must match
 * PERSISTENT_OBJECT_MAGIC_VALUE in src/platform/HPMicro/persistent.c:
 * ('B' << 24) | ('e' << 16) | ('f' << 8) | ('1' << 0) */
#define HIL_PERSISTENT_MAGIC_VALUE 0x42656631u

/* ------------------------------------------------------------------ *
 *  Helpers: save / restore the whole object table
 * ------------------------------------------------------------------ */

static void hil_persistent_save_all(uint32_t *saved)
{
    for (int i = 0; i < PERSISTENT_OBJECT_COUNT; i++) {
        saved[i] = persistentObjectRead((persistentObjectId_e)i);
    }
}

static void hil_persistent_restore_all(const uint32_t *saved)
{
    for (int i = 0; i < PERSISTENT_OBJECT_COUNT; i++) {
        persistentObjectWrite((persistentObjectId_e)i, saved[i]);
    }
}

/* ------------------------------------------------------------------ *
 *  Test: persistentObjectRead() / persistentObjectWrite() round trip
 * ------------------------------------------------------------------ */

void hil_test_persistent_read_write(void)
{
    uint32_t saved[PERSISTENT_OBJECT_COUNT];
    hil_persistent_save_all(saved);

    persistentObjectWrite(PERSISTENT_OBJECT_HSE_VALUE, 0x12345678);
    HIL_ASSERT_EQ(persistentObjectRead(PERSISTENT_OBJECT_HSE_VALUE),
                  0x12345678);

    persistentObjectWrite(PERSISTENT_OBJECT_SERIALRX_BAUD, 115200);
    HIL_ASSERT_EQ(persistentObjectRead(PERSISTENT_OBJECT_SERIALRX_BAUD),
                  115200);

    hil_persistent_restore_all(saved);
}

/* ------------------------------------------------------------------ *
 *  Test: persistentObjectInit() re-initializes on a corrupt magic
 *
 *  A corrupt magic forces re-initialization regardless of the reset
 *  source: every object is cleared and the magic is rewritten.
 * ------------------------------------------------------------------ */

void hil_test_persistent_init_corrupt_magic(void)
{
    uint32_t saved[PERSISTENT_OBJECT_COUNT];
    hil_persistent_save_all(saved);

    persistentObjectWrite(PERSISTENT_OBJECT_MAGIC, 0);
    persistentObjectWrite(PERSISTENT_OBJECT_HSE_VALUE, 0xDEADBEEF);

    persistentObjectInit();

    HIL_ASSERT_EQ(persistentObjectRead(PERSISTENT_OBJECT_MAGIC),
                  HIL_PERSISTENT_MAGIC_VALUE);
    HIL_ASSERT_EQ(persistentObjectRead(PERSISTENT_OBJECT_HSE_VALUE), 0);

    hil_persistent_restore_all(saved);
}

/* ------------------------------------------------------------------ *
 *  Test: persistentObjectInit() preserves data across a soft reset
 *
 *  persistentObjectInit() only clears the objects when the reset was
 *  not a software reset or the magic is invalid.
 *  The test adapts its expectation to the actual reset source so it is
 *  deterministic regardless of how the board was booted:
 *    - software reset with valid magic  -> objects are preserved
 *    - cold boot                        -> objects are cleared
 * ------------------------------------------------------------------ */

void hil_test_persistent_init_preserve(void)
{
    uint32_t saved[PERSISTENT_OBJECT_COUNT];
    hil_persistent_save_all(saved);

    const bool wasSoftReset = isMPUSoftReset();
    HIL_INFO("PPOR reset flags at startup = 0x%lx (%s)",
             (unsigned long)cachedResetFlags,
             wasSoftReset ? "software reset" : "cold boot");

    /* Plant a valid magic plus a sentinel object */
    persistentObjectWrite(PERSISTENT_OBJECT_MAGIC, HIL_PERSISTENT_MAGIC_VALUE);
    persistentObjectWrite(PERSISTENT_OBJECT_HSE_VALUE, 0xCAFEBABE);

    persistentObjectInit();

    if (wasSoftReset) {
        HIL_ASSERT_EQ(persistentObjectRead(PERSISTENT_OBJECT_HSE_VALUE),
                      0xCAFEBABE);
    } else {
        HIL_ASSERT_EQ(persistentObjectRead(PERSISTENT_OBJECT_HSE_VALUE), 0);
    }
    HIL_ASSERT_EQ(persistentObjectRead(PERSISTENT_OBJECT_MAGIC),
                  HIL_PERSISTENT_MAGIC_VALUE);

    hil_persistent_restore_all(saved);
}

/* ------------------------------------------------------------------ *
 *  Test: persistent data survives a software reset
 *
 *  This is a two-pass test spanning two boots, because a software reset
 *  reboots the board (and the whole suite re-runs):
 *
 *    pass 1 (fresh boot): write a marker object, then call systemReset().
 *          The board reboots; this test re-runs on the next boot.
 *    pass 2 (post-reset): the marker survived -> the backup registers are
 *          retained across a software reset. Clear it and report PASS.
 *
 *  The marker is stored in PERSISTENT_OBJECT_HSE_VALUE, which no HPMicro
 *  production code reads, so it is a safe scratch slot.
 *
 *  To avoid an endless reset loop if persistence is broken, a flag in the
 *  .noncacheable.non_init RAM section (retained across a soft reset but
 *  cleared by a power cycle) records that a marker was planted.  If we
 *  come back from a reset without the marker, the test FAILs instead of
 *  resetting again.
 * ------------------------------------------------------------------ */

/* Lives in AXI SRAM .noncacheable.non_init: not zeroed by the startup
 * code, so it survives a software reset but is lost on power-down. */
__attribute__((section(".noncacheable.non_init"), used))
static volatile uint32_t hil_soft_reset_flag;

#define HIL_SOFT_RESET_FLAG_VALUE 0x5A5A1234u
#define HIL_SR_MARKER             0xA5A5A5A5u

void hil_test_persistent_soft_reset(void)
{
    if (hil_soft_reset_flag != HIL_SOFT_RESET_FLAG_VALUE) {
        /* Fresh (cold) boot: plant the marker and a soft-reset flag, then
         * reboot. systemReset() does not return. */
        HIL_INFO("planting marker 0x%lx and triggering a software reset",
                 (unsigned long)HIL_SR_MARKER);
        persistentObjectWrite(PERSISTENT_OBJECT_HSE_VALUE, HIL_SR_MARKER);
        hil_soft_reset_flag = HIL_SOFT_RESET_FLAG_VALUE;

        /* Drive PA20/PA21 (boot-mode pins on this board) low across the
         * reset so the boot ROM boots from flash. */
        IO_t pa20 = IOGetByTag(DEFIO_TAG_E__PA20);
        IO_t pa21 = IOGetByTag(DEFIO_TAG_E__PA21);
        if (pa20) { IOConfigGPIO(pa20, IOCFG_OUT_PP); IOLo(pa20); }
        if (pa21) { IOConfigGPIO(pa21, IOCFG_OUT_PP); IOLo(pa21); }

        /* Print the accumulated results before rebooting -- the normal
         * end-of-run summary is never reached on this boot. */
        hil_test_print_summary_so_far();

        systemReset();
        return;  /* defensive -- not reached when the reset works */
    }

    /* Post-software-reset boot: the marker must have survived. */
    const uint32_t marker = persistentObjectRead(PERSISTENT_OBJECT_HSE_VALUE);
    if (marker == HIL_SR_MARKER) {
        HIL_INFO("persistent object survived the software reset");
        HIL_ASSERT_EQ(persistentObjectRead(PERSISTENT_OBJECT_HSE_VALUE),
                      HIL_SR_MARKER);
        /* HSE_VALUE is unused by the HPMicro port; restore to zero */
        persistentObjectWrite(PERSISTENT_OBJECT_HSE_VALUE, 0);
    } else {
        HIL_FAIL("marker lost across software reset (backup registers not retained)");
    }
    hil_soft_reset_flag = 0;  /* allow a fresh run on the next power cycle */
}
