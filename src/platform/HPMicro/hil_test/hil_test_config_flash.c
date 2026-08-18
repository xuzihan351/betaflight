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

#include <string.h>

#include "platform.h"

#include "config/config_streamer.h"
#include "config/config_streamer_impl.h"
#include "hpm_interrupt.h"
#include "hpm_romapi.h"
#include "hil_test.h"

/* Globals/functions defined in src/platform/HPMicro/config_flash.c */
extern uint32_t flash_size;
extern uint32_t page_size;
extern uint32_t sector_size;
extern const xpi_nor_config_t *configFlashGetXpiConfig(void);

/* Linker symbol: last flash address used by the firmware image */
extern char __last_addr__[];

#define HIL_XIP_BASE 0x80000000u

/* ------------------------------------------------------------------ *
 *  Test: configUnlock() initializes the XPI NOR and reads sane properties
 *
 *  configUnlock() must succeed (auto-config, property reads, and the
 *  sector-alignment check -- it would hang in while(1) on any error).
 *  The reported flash/page/sector sizes must be non-zero and mutually
 *  consistent, and the config region must start on a sector boundary.
 * ------------------------------------------------------------------ */

void hil_test_config_flash_unlock(void)
{
    const uint32_t irqBefore = read_csr(CSR_MSTATUS) & CSR_MSTATUS_MIE_MASK;
    configUnlock();
    const uint32_t irqAfterUnlock = read_csr(CSR_MSTATUS) & CSR_MSTATUS_MIE_MASK;
    configLock();
    const uint32_t irqAfterLock = read_csr(CSR_MSTATUS) & CSR_MSTATUS_MIE_MASK;

    HIL_INFO("flash_size=0x%lx page_size=0x%lx sector_size=0x%lx config_start=0x%lx",
             (unsigned long)flash_size, (unsigned long)page_size,
             (unsigned long)sector_size,
             (unsigned long)(uintptr_t)&__config_start);

    HIL_ASSERT(flash_size != 0);
    HIL_ASSERT(page_size != 0);
    HIL_ASSERT(sector_size != 0);
    HIL_ASSERT(page_size <= sector_size);
    HIL_ASSERT(sector_size <= flash_size);
    HIL_ASSERT(sector_size % page_size == 0);
    HIL_ASSERT(flash_size % sector_size == 0);
    HIL_ASSERT_EQ(irqAfterUnlock, irqBefore);
    HIL_ASSERT_EQ(irqAfterLock, irqBefore);

    /* configWriteWord() only erases when a write address falls on a
     * sector boundary, so the config region must be sector-aligned. */
    HIL_ASSERT((uintptr_t)&__config_start % sector_size == 0);
    HIL_ASSERT((uintptr_t)&__config_start > HIL_XIP_BASE);
    HIL_ASSERT((uintptr_t)&__config_start < HIL_XIP_BASE + flash_size);
}

/* ------------------------------------------------------------------ *
 *  Test: configWriteWord() sector erase + full-buffer program round trip
 *
 *  Writes two full CONFIG_STREAMER_BUFFER_SIZE (32-byte) buffers into a
 *  scratch sector in free flash and reads them back via the ROM API.  The
 *  first write lands on a sector boundary and must trigger an erase; the
 *  second lands in the same sector (offset by 32 bytes) and must program
 *  without an erase.  The scratch sector is erased again at the end so
 *  the flash is left clean.
 *
 *  configWriteWord() preserves the caller's IRQ state around each blocking
 *  erase/program operation, so no ISR can fetch from XIP mid-operation while
 *  interrupts remain available between buffer writes.
 * ------------------------------------------------------------------ */

void hil_test_config_flash_write_word(void)
{
    configUnlock();   /* init flash and read sector_size */

    const uint32_t fwEnd = (uint32_t)(uintptr_t)&__last_addr__;
    uint32_t scratch = 0;
    if (sector_size != 0) {
        scratch = (fwEnd + sector_size - 1) / sector_size * sector_size;
    }

    HIL_INFO("fw_end=0x%lx scratch=0x%lx config_start=0x%lx sector_size=0x%lx",
             (unsigned long)fwEnd, (unsigned long)scratch,
             (unsigned long)(uintptr_t)&__config_start,
             (unsigned long)sector_size);

    if (sector_size == 0 ||
        scratch < fwEnd ||
        scratch + sector_size > (uintptr_t)&__config_start) {
        configLock();
        HIL_SKIP("no safe free flash sector for the write test");
        return;
    }

    /* Reuse the XPI NOR config that configUnlock() already built. */
    const xpi_nor_config_t *norConfig = configFlashGetXpiConfig();

    const uint32_t flashOffset = scratch - HIL_XIP_BASE;
    hpm_stat_t status;

    /* First write: erases the sector (address is sector-aligned), then
     * programs CONFIG_STREAMER_BUFFER_SIZE bytes with word1 in buf[0]. */
    const uint32_t word1 = 0xA5A5A5A5u;
    config_streamer_buffer_type_t buf[CONFIG_STREAMER_BUFFER_SIZE / sizeof(config_streamer_buffer_type_t)] = {0};
    buf[0] = word1;
    HIL_ASSERT_EQ(configWriteWord(scratch, buf), CONFIG_RESULT_SUCCESS);

    /* Second write in the same sector, offset by one full streamer-buffer
     * width: no erase, just program word2 in buf2[0]. */
    const uint32_t word2 = 0x5A5A1234u;
    config_streamer_buffer_type_t buf2[CONFIG_STREAMER_BUFFER_SIZE / sizeof(config_streamer_buffer_type_t)] = {0};
    buf2[0] = word2;
    HIL_ASSERT_EQ(configWriteWord(scratch + CONFIG_STREAMER_BUFFER_SIZE, buf2),
                  CONFIG_RESULT_SUCCESS);

    /* Read back the first word of each write — both should be intact.  The
     * ROM read spans both buffer-sized blocks to catch any wraparound or
     * overwrite from the second program. */
    uint32_t rd[2 * CONFIG_STREAMER_BUFFER_SIZE / sizeof(uint32_t)];
    memset(rd, 0, sizeof(rd));
    uint32_t irq = disable_global_irq(CSR_MSTATUS_MIE_MASK);
    status = rom_xpi_nor_read(BOARD_APP_XPI_NOR_XPI_BASE, xpi_xfer_channel_auto,
                              norConfig, rd, flashOffset, sizeof(rd));
    if (irq & CSR_MSTATUS_MIE_MASK) {
        enable_global_irq(CSR_MSTATUS_MIE_MASK);
    }
    HIL_ASSERT_EQ(status, status_success);
    HIL_ASSERT_EQ(rd[0], word1);
    HIL_ASSERT_EQ(rd[CONFIG_STREAMER_BUFFER_SIZE / sizeof(uint32_t)], word2);

    /* Clean up: erase the scratch sector back to erased (0xFF) state */
    irq = disable_global_irq(CSR_MSTATUS_MIE_MASK);
    status = rom_xpi_nor_erase(BOARD_APP_XPI_NOR_XPI_BASE, xpi_xfer_channel_auto,
                               norConfig, flashOffset, sector_size);
    if (irq & CSR_MSTATUS_MIE_MASK) {
        enable_global_irq(CSR_MSTATUS_MIE_MASK);
    }
    HIL_ASSERT_EQ(status, status_success);

    configLock();
}

/* ------------------------------------------------------------------ *
 *  Test: configLock() / configClearFlags() are callable
 * ------------------------------------------------------------------ */

void hil_test_config_flash_lock_flags(void)
{
    configClearFlags();
    configLock();
    HIL_INFO("configClearFlags()/configLock() returned");
}
