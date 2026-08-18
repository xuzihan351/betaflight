/*
 * This file is part of Betaflight.
 *
 * Betaflight is free software. You can redistribute this software
 * and/or modify this software under the terms of the GNU General
 * Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later
 * version.
 *
 * Betaflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include "platform.h"
#include "board.h"
#include "build/debug.h"
#include "drivers/system.h"
#include "drivers/persistent.h"
#include "hpm_ppor_drv.h"
#include "hpm_clock_drv.h"
#include "hpm_romapi.h"
#include "hpm_bgpr_drv.h"
#include "drivers/time.h"
#include "light_led.h"
#include "sound_beeper.h"
#include "hpm_mchtmr_drv.h"
#include "hpm_wdg_drv.h"
#include "scheduler/scheduler.h"
#include "watchdog_hpmicro.h"

uint32_t SystemCoreClock;
uint32_t cachedResetFlags;

static bool systemWatchdogStarted;
static bool systemWatchdogHasTaskSample;
static timeUs_t systemWatchdogLastTaskExecutionTime;

/*
 * clearResetFlagAndReset() runs with interrupts disabled until the software
 * reset fires. RESET_FLAG is W1C and its bits stay latched across warm
 * resets, so clear them immediately before ppor_sw_reset() to keep the boot
 * ROM out of its debug/ISP path on the next boot.
 */

FAST_CODE __attribute__((noinline)) static void clearResetFlagAndReset(uint32_t pporCounter)
{
    disable_global_irq(CSR_MSTATUS_MIE_MASK);
    __asm volatile ("fence.i");

    HPM_PPOR->RESET_FLAG = PPOR_RESET_FLAG_FLAG_MASK;
    ppor_reset_mask_set_source_enable(HPM_PPOR, ppor_reset_software);
    ppor_sw_reset(HPM_PPOR, pporCounter);
    while (1) {
    }
}

void systemResetToBootloader(bootloaderRequestType_e requestType)
{
    (void)requestType;
#ifdef HPM_BCFG_BASE
    (void) bgpr_write32(HPM_BGPR, 0, 0x55464455UL);
#endif
#ifdef HPM_PDGO_BASE
    if (!pdgo_is_retention_mode_enabled(HPM_PDGO)) {
        pdgo_enable_retention_mode(HPM_PDGO);
    }
    pdgo_write_gpr(HPM_PDGO, 0, 0x55464455UL);

#endif
    clearResetFlagAndReset(10);
}

bool isMPUSoftReset(void)
{
    return (cachedResetFlags & ppor_reset_software) != 0U;
}

void delay(timeMs_t ms)
{
    timeMs_t start = millis();
    while (millis() - start < ms);
}

static void indicate(uint8_t count, uint16_t duration)
{
    if (count) {
        LED1_ON;
        LED0_OFF;

        while (count--) {
            LED1_TOGGLE;
            LED0_TOGGLE;
            BEEP_ON;
            delay(duration);

            LED1_TOGGLE;
            LED0_TOGGLE;
            BEEP_OFF;
            delay(duration);
        }
    }
}

void indicateFailure(failureMode_e mode, int codeRepeatsRemaining)
{
    while (codeRepeatsRemaining--) {
        indicate(WARNING_FLASH_COUNT, WARNING_FLASH_DURATION_MS);

        delay(WARNING_PAUSE_DURATION_MS);

        indicate(mode + 1, WARNING_CODE_DURATION_LONG_MS);

        delay(1000);
    }
}

void failureMode(failureMode_e mode)
{
    // The indication sequence intentionally lasts longer than the watchdog
    // timeout and must finish before requesting the ROM bootloader.
    systemWatchdogDisable();
    indicateFailure(mode, 10);
#ifdef DEBUG
    systemReset();
#else
    systemResetToBootloader(BOOTLOADER_REQUEST_ROM);
#endif
}

void systemWatchdogInit(void)
{
    // WDG0 runs from the independent 32 kHz clock. It raises an interrupt
    // after 128K periods (4 seconds), then resets after another 128 periods
    // (approximately 4 ms) unless the ISR observes scheduler progress.
    const uint32_t control =
        WDG_CTRL_RSTTIME_SET(reset_interval_clock_period_mult_128) |
        WDG_CTRL_INTTIME_SET(interrupt_interval_clock_period_multi_128k) |
        WDG_CTRL_RSTEN_MASK |
        WDG_CTRL_INTEN_MASK |
        WDG_CTRL_EN_MASK;

    systemWatchdogHasTaskSample = false;
    systemWatchdogLastTaskExecutionTime = 0;
    clock_add_to_group(clock_watchdog0, BOARD_RUNNING_CORE);
    intc_m_disable_irq(IRQn_WDG0);
    wdg_write_enable(HPM_WDG0);
    HPM_WDG0->CTRL = control;
    wdg_clear_status(HPM_WDG0, WDG_ST_INTEXPIRED_MASK);
    wdg_restart(HPM_WDG0);
    systemWatchdogStarted = true;
    intc_m_enable_irq_with_priority(IRQn_WDG0, 1);
}

void systemWatchdogDisable(void)
{
    if (systemWatchdogStarted) {
        intc_m_disable_irq(IRQn_WDG0);
        wdg_disable(HPM_WDG0);
        wdg_clear_status(HPM_WDG0, WDG_ST_INTEXPIRED_MASK);
        systemWatchdogStarted = false;
    }
}

SDK_DECLARE_EXT_ISR_M(IRQn_WDG0, systemWatchdogIsr)
void systemWatchdogIsr(void)
{
    taskInfo_t systemTaskInfo;
    getTaskInfo(TASK_SYSTEM, &systemTaskInfo);

    const bool schedulerProgressed = !systemWatchdogHasTaskSample ||
        systemTaskInfo.totalExecutionTimeUs != systemWatchdogLastTaskExecutionTime;
    systemWatchdogHasTaskSample = true;
    systemWatchdogLastTaskExecutionTime = systemTaskInfo.totalExecutionTimeUs;

    if (schedulerProgressed) {
        wdg_clear_status(HPM_WDG0, WDG_ST_INTEXPIRED_MASK);
        wdg_restart(HPM_WDG0);
    } else {
        // Avoid repeatedly entering the ISR while the short reset interval
        // elapses. Leaving the watchdog pending lets hardware reset the SoC.
        intc_m_disable_irq(IRQn_WDG0);
    }
}


void systemInit(void)
{
    SystemCoreClock = BOARD_CPU_FREQ;
    // RESET_FLAG is W1C, so preserve the startup snapshot before any board or
    // application code can clear it.
    cachedResetFlags = ppor_reset_get_flags(HPM_PPOR);
    board_init();
    cycleCounterInit();
    systemWatchdogInit();
}

void systemReset(void)
{
    clearResetFlagAndReset(1000);
}

static uint32_t us_per_tick;
static uint32_t cycles_per_us;
static uint64_t microsLastCount;
static uint32_t microsResidualTicks;
static timeUs_t microsElapsedUs;

FAST_CODE timeUs_t micros(void)
{
    uint32_t irq = disable_global_irq(CSR_MSTATUS_MIE_MASK);
    uint64_t count = mchtmr_get_count(HPM_MCHTMR);
    uint64_t deltaTicks = count - microsLastCount;
    microsLastCount = count;

    if ((deltaTicks >> 32) == 0 && (uint32_t)deltaTicks <= UINT32_MAX - microsResidualTicks) {
        const uint32_t pendingTicks = (uint32_t)deltaTicks + microsResidualTicks;
        microsElapsedUs += pendingTicks / us_per_tick;
        microsResidualTicks = pendingTicks % us_per_tick;
    } else {
        // This is only reachable after more than 2^32 timer ticks without a
        // micros() call (about 179 seconds at 24 MHz). Keep the full-range
        // fallback for startup/debug pauses without burdening the hot path.
        deltaTicks += microsResidualTicks;
        microsElapsedUs += (timeUs_t)(deltaTicks / us_per_tick);
        microsResidualTicks = (uint32_t)(deltaTicks % us_per_tick);
    }

    const timeUs_t elapsedUs = microsElapsedUs;
    enable_global_irq(irq);

    return elapsedUs;
}

FAST_CODE timeUs_t microsISR(void)
{
    return micros();
}

timeMs_t millis(void)
{
    return micros() / 1000;
}

void cycleCounterInit(void)
{
    clock_add_to_group(clock_mchtmr0, 0);
    mchtmr_init_counter(HPM_MCHTMR, 0);

    us_per_tick = clock_get_frequency(clock_mchtmr0) / 1000000;
    if (!us_per_tick) {
        us_per_tick = 1;
    }

    cycles_per_us = SystemCoreClock / 1000000;
    if (!cycles_per_us) {
        cycles_per_us = 1;
    }
}

void delayMicroseconds(uint32_t us)
{
    uint32_t now = micros();
    while (micros() - now < us);
}

// Cycle counter functions
int32_t clockCyclesToMicros(int32_t clockCycles)
{
    return clockCycles / (int32_t)cycles_per_us;
}

float clockCyclesToMicrosf(int32_t clockCycles)
{
    return (float)clockCycles / (float)cycles_per_us;
}

int32_t clockCyclesTo10thMicros(int32_t clockCycles)
{
    return 10 * clockCycles / (int32_t)cycles_per_us;
}

int32_t clockCyclesTo100thMicros(int32_t clockCycles)
{
    return 100 * clockCycles / (int32_t)cycles_per_us;
}

uint32_t clockMicrosToCycles(uint32_t micros)
{
    return micros * cycles_per_us;
}

uint32_t getCycleCounter(void)
{
    return read_csr(CSR_MCYCLE);
}

void debugInit(void)
{

}

uint32_t board_sd_configure_clock(SDXC_Type *ptr, uint32_t freq, bool need_inverse)
{
#ifdef HPM6360

    uint32_t actual_freq = 0;
    do {
        clock_name_t sdxc_clk = clock_sdxc0;
        clock_add_to_group(sdxc_clk, 0);
        sdxc_enable_inverse_clock(ptr, false);
        sdxc_enable_sd_clock(ptr, false);
        /* Configure the SDXC Frequency to 200MHz */
        clock_set_source_divider(sdxc_clk, clk_src_pll0_clk0, 2);
        sdxc_enable_freq_selection(ptr);

        hpm_stat_t status = clock_wait_source_stable(sdxc_clk);
        if (status != status_success) {
            break;
        }

        /* Configure the clock below 400KHz for the identification state */
        if (freq <= 400000UL) {
            sdxc_set_clock_divider(ptr, 600);
        }
            /* configure the clock to 24MHz for the SDR12/Default speed */
        else if (freq <= 26000000UL) {
            sdxc_set_clock_divider(ptr, 8);
        }
            /* Configure the clock to 50MHz for the SDR25/High speed/50MHz DDR/50MHz SDR */
        else if (freq <= 52000000UL) {
            sdxc_set_clock_divider(ptr, 4);
        }
            /* Configure the clock to 100MHz for the SDR50 */
        else if (freq <= 100000000UL) {
            sdxc_set_clock_divider(ptr, 2);
        }
            /* Configure the clock to 166MHz for SDR104/HS200/HS400  */
        else if (freq <= 208000000UL) {
            sdxc_set_clock_divider(ptr, 1);
        }
            /* For other unsupported clock ranges, configure the clock to 24MHz */
        else {
            sdxc_set_clock_divider(ptr, 8);
        }
        if (need_inverse) {
            sdxc_enable_inverse_clock(ptr, true);
        }
        sdxc_enable_sd_clock(ptr, true);
        actual_freq = clock_get_frequency(sdxc_clk) / sdxc_get_clock_divider(ptr);
    } while (false);
#elif defined(HPM6750)
    uint32_t actual_freq = 0;
    do {
        clock_name_t sdxc_clk = (ptr == HPM_SDXC0) ? clock_sdxc0 : clock_sdxc1;
        clock_add_to_group(sdxc_clk, 0);
        sdxc_enable_inverse_clock(ptr, false);
        sdxc_enable_sd_clock(ptr, false);
        /* Configure the clock below 400KHz for the identification state */
        if (freq <= 400000UL) {
            clock_set_source_divider(sdxc_clk, clk_src_osc24m, 63);
        }
            /* configure the clock to 24MHz for the SDR12/Default speed */
        else if (freq <= 26000000UL) {
            clock_set_source_divider(sdxc_clk, clk_src_osc24m, 1);
        }
            /* Configure the clock to 50MHz for the SDR25/High speed/50MHz DDR/50MHz SDR */
        else if (freq <= 52000000UL) {
            clock_set_source_divider(sdxc_clk, clk_src_pll1_clk1, 8);
        }
            /* Configure the clock to 100MHz for the SDR50 */
        else if (freq <= 100000000UL) {
            clock_set_source_divider(sdxc_clk, clk_src_pll1_clk1, 4);
        }
            /* Configure the clock to 166MHz for SDR104/HS200/HS400  */
        else if (freq <= 208000000UL) {
            clock_set_source_divider(sdxc_clk, clk_src_pll2_clk0, 2);
        }
            /* For other unsupported clock ranges, configure the clock to 24MHz */
        else {
            clock_set_source_divider(sdxc_clk, clk_src_osc24m, 1);
        }
        if (need_inverse) {
            sdxc_enable_inverse_clock(ptr, true);
        }
        sdxc_enable_sd_clock(ptr, true);
        actual_freq = clock_get_frequency(sdxc_clk);
    } while (false);
#endif
    return actual_freq;
}

void init_sdxc_cd_pin(SDXC_Type  *ptr, bool as_gpio)
{
    if (ptr == HPM_SDXC0) {
        while (!as_gpio);
        // Configure SDXC card detect pin as GPIO input with pull-up
        IOConfigGPIOAF(IOGetByTag(SDXC_CD_PIN), IOCFG_IPU, 0);
    }
}

void init_sdxc_cmd_pin(SDXC_Type *ptr, bool open_drain, bool is_1v8)
{
    (void) is_1v8;
    if (ptr == HPM_SDXC0) {
        uint32_t cmd_func_ctl = SDXC_CMD_PIN_AF | IOC_PAD_FUNC_CTL_LOOP_BACK_SET(1);
        ioConfig_t cmd_config = open_drain ? IOCFG_AF_OD : IOCFG_AF_PP;
        // Configure SDXC command pin
        IOConfigGPIOAF(IOGetByTag(SDXC_CMD_PIN), cmd_config, cmd_func_ctl);
    }
}

void init_sdxc_clk_data_pins(SDXC_Type *ptr, uint32_t width, bool is_1v8)
{
    (void) is_1v8;
    if (ptr == HPM_SDXC0) {
        uint32_t func_ctl = IOC_PAD_FUNC_CTL_ALT_SELECT_SET(17) | IOC_PAD_FUNC_CTL_LOOP_BACK_SET(1);

        /* SDXC0.CLK */
        IOConfigGPIOAF(IOGetByTag(SDXC_CLK_PIN), IOCFG_AF_PP, func_ctl);

        /* SDXC0.DATA0 */
        IOConfigGPIOAF(IOGetByTag(SDXC_DATA0_PIN), IOCFG_AF_PP, func_ctl);

        if (width == 4) {
            /* SDXC0.DATA1 */
            IOConfigGPIOAF(IOGetByTag(SDXC_DATA1_PIN), IOCFG_AF_PP, func_ctl);
            /* SDXC0.DATA2 */
            IOConfigGPIOAF(IOGetByTag(SDXC_DATA2_PIN), IOCFG_AF_PP, func_ctl);
            /* SDXC0.DATA3 */
            IOConfigGPIOAF(IOGetByTag(SDXC_DATA3_PIN), IOCFG_AF_PP, func_ctl);
        }
    }
}
