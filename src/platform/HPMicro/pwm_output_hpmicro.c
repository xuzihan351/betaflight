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

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "pwm_output_impl.h"
#include "platform.h"
#include "dma.h"
#ifdef HPMSOC_HAS_HPMSDK_DMAV2
#include "hpm_dmav2_drv.h"
#else
#include "hpm_dma_drv.h"
#endif
#include "timer_hpmicro.h"
#ifdef USE_PWM_OUTPUT

#include "drivers/io.h"
#include "io_hpmicro.h"
#include "drivers/motor.h"
#include "drivers/pwm_output.h"
#include "drivers/system.h"
#include "drivers/timer.h"
#include "hpm_clock_drv.h"
#include "hpm_pwm_drv.h"
#include "pg/motor.h"
#include <stdio.h>
#include "timer_hw_ext.h"
#include "trgm_dshot_resource.h"
#include "motor_impl.h"

static bool useContinuousUpdate = true;

#if defined(HPMSOC_HAS_HPMSDK_PWM)
void pwmOutputConfig(timerChannel_t *channel, const timerHardware_t *timerHardware,
                  uint32_t hz, uint16_t period, uint16_t value,
                  uint8_t inversion)
{
    (void)value;
    trgm_output_t trgm_io_config = {0};
    const hpmicroTimerHwExt_t *hw_ext = hpmicroTimerHwExtByTimer(timerHardware);
    if (hw_ext == NULL) {
        // No TRGM routing info for this timer; channel->ccr stays NULL and
        // pwmWriteStandard()/pwmShutdownPulsesForAllMotors() will skip it
        return;
    }

    trgmDshotResource_t res;
    if (!trgmDshotResourceAlloc(timerHardware->tag, &res)) {
        return;
    }
    if (hw_ext->pwm_ref_src != 0) {
        if (!trgmDshotResourceAllocPwmRef(&res, hw_ext->pwm_ref_src)) {
            trgmDshotResourceFree(&res);
            return;
        }
    }

    pwm_stop_counter((PWM_Type *)timerHardware->tim);
    uint32_t reload = 0;
    uint32_t freq;
    pwm_config_t pwm_config = {0};
    pwm_cmp_config_t cmp_config = {0};
    pwm_get_default_pwm_config((PWM_Type *)timerHardware->tim, &pwm_config);

    pwm_config.enable_output = true;
    pwm_config.dead_zone_in_half_cycle = 0;
    pwm_config.invert_output = inversion ? true : false;
    freq = clock_get_frequency(timerRCC((PWM_Type *)timerHardware->tim));
    reload = freq / hz * period - 1;
    /*
     * reload and start counter
     */
    pwm_set_reload((PWM_Type *)timerHardware->tim, 0, reload);
    pwm_set_start_count((PWM_Type *)timerHardware->tim, 0, 0);

    /*
     * config cmp = RELOAD + 1
     */
    cmp_config.mode = pwm_cmp_mode_output_compare;
    cmp_config.cmp = reload + 1;
    cmp_config.update_trigger = pwm_shadow_register_update_on_modify;
    /*
     * config pwm as output driven by cmp
     */
    if (status_success !=
        pwm_setup_waveform((PWM_Type *)timerHardware->tim, timerHardware->channel,
                           &pwm_config, hw_ext->cmp_index, &cmp_config,
                           1))
    {
        printf("failed to setup waveform\n");
        failureMode(FAILURE_DEVELOPER);
    }
    pwm_start_counter((PWM_Type *)timerHardware->tim);
    pwm_issue_shadow_register_lock_event((PWM_Type *)timerHardware->tim);

    channel->ccr = timerChCCR(timerHardware);

    channel->tim = timerHardware->tim;

    // CMP beyond reload keeps the output low (0% high time) until the first
    // motor value is written
    *channel->ccr = PWM_CMP_CMP_SET(reload + 1);
    memset(&trgm_io_config, 0, sizeof(trgm_io_config));
    trgm_io_config.invert = 0;
    trgm_io_config.type = trgm_output_same_as_input;
    trgm_io_config.input = res.pwm_ref_src;
    trgm_output_config(trgmDshotResourceTrgm(&res), res.trgm_p_dst, &trgm_io_config);
    trgm_enable_io_output(trgmDshotResourceTrgm(&res), 1 << (res.port_index));
}

FAST_CODE static void pwmWriteStandard(uint8_t index, float value)
{
    /* TODO: move value to be a number between 0-1 (i.e. percent throttle from mixer) */
    if (pwmMotors[index].channel.ccr) {
        *pwmMotors[index].channel.ccr = lrintf((value * pwmMotors[index].pulseScale) + pwmMotors[index].pulseOffset);
    }
}

FAST_CODE static void pwmShutdownPulsesForAllMotors(void)
{
    for (int index = 0; index < pwmMotorCount; index++)
    {
        // Set the compare value beyond reload, which stops the output pulsing
        if (pwmMotors[index].channel.ccr)
        {
            *pwmMotors[index].channel.ccr = PWM_CMP_CMP_SET(0xFFFFFFFF);
        }
    }
}

FAST_CODE void pwmDisableMotors(void) { pwmShutdownPulsesForAllMotors(); }

static void pwmCompleteMotorUpdate(void)
{
    if (useContinuousUpdate) {
        return;
    }

    for (int index = 0; index < pwmMotorCount; index++) {
        // Set the compare value beyond reload, which stops the output pulsing
        // until the next main loop writes a new pulse width
        if (pwmMotors[index].channel.ccr) {
            *pwmMotors[index].channel.ccr = PWM_CMP_CMP_SET(0xFFFFFFFF);
        }
    }
}

static float pwmConvertFromExternal(uint16_t externalValue)
{
    return (float)externalValue;
}

static uint16_t pwmConvertToExternal(float motorValue)
{
    return (uint16_t)motorValue;
}

static motorVTable_t motorPwmVTable = {
    .postInit = motorPostInitNull,
    .enable = pwmEnableMotors,
    .disable = pwmDisableMotors,
    .isMotorEnabled = pwmIsMotorEnabled,
    .shutdown = pwmShutdownPulsesForAllMotors,
    .convertExternalToMotor = pwmConvertFromExternal,
    .convertMotorToExternal = pwmConvertToExternal,
    .write = pwmWriteStandard,
    .updateComplete = pwmCompleteMotorUpdate,
    .getMotorIO = pwmGetMotorIO,
};

bool motorPwmDevInit(motorDevice_t *device, const motorDevConfig_t *motorConfig, uint16_t idlePulse)
{
    memset(pwmMotors, 0, sizeof(pwmMotors));

    pwmMotorCount = device->count;
    device->vTable = &motorPwmVTable;
    useContinuousUpdate = motorConfig->useContinuousUpdate;

    float sMin = 0;
    float sLen = 0;
    switch (motorConfig->motorProtocol)
    {
    default:
    case MOTOR_PROTOCOL_ONESHOT125:
        sMin = 125e-6f;
        sLen = 125e-6f;
        break;
    case MOTOR_PROTOCOL_ONESHOT42:
        sMin = 42e-6f;
        sLen = 42e-6f;
        break;
    case MOTOR_PROTOCOL_MULTISHOT:
        sMin = 5e-6f;
        sLen = 20e-6f;
        break;
    case MOTOR_PROTOCOL_BRUSHED:
        sMin = 0;
        useContinuousUpdate = true;
        idlePulse = 0;
        break;
    case MOTOR_PROTOCOL_PWM:
        sMin = 1e-3f;
        sLen = 1e-3f;
        useContinuousUpdate = true;
        idlePulse = 0;
        break;
    }

    for (int motorIndex = 0; motorIndex < MAX_SUPPORTED_MOTORS && motorIndex < pwmMotorCount; motorIndex++)
    {
        const unsigned reorderedMotorIndex =
            motorConfig->motorOutputReordering[motorIndex];
        const ioTag_t tag = motorConfig->ioTags[reorderedMotorIndex];
        const timerHardware_t *timerHardware = timerAllocate(
            tag, OWNER_MOTOR, RESOURCE_INDEX(reorderedMotorIndex));
        if (timerHardware == NULL)
        {
            /* not enough motors initialised for the mixer or a
             * break in the motors */
            device->vTable = NULL;
            pwmMotorCount = 0;
            /* TODO: block arming and add reason system cannot arm */
            return false;
        }
        pwmMotors[motorIndex].io = IOGetByTag(tag);
        IOInit(pwmMotors[motorIndex].io, OWNER_MOTOR,
               RESOURCE_INDEX(reorderedMotorIndex));

        IOConfigGPIOAF(pwmMotors[motorIndex].io, IOCFG_AF_PP,
                       timerHardware->alternateFunction);
        /* standard PWM outputs */
        // margin of safety is 4 periods when unsynced
        const unsigned pwmRateHz = useContinuousUpdate
                                       ? motorConfig->motorPwmRate
                                       : ceilf(1 / ((sMin + sLen) * 4));

        const uint32_t clock = getPWMFre((TIM_TypeDef *)timerHardware->tim);
        /* used to find the desired timer frequency for max resolution
         */
        const unsigned prescaler =
            ((clock / pwmRateHz) + 0xffff) / 0x10000; /* rounding up */
        const uint32_t hz = clock / prescaler;
        const unsigned period = useContinuousUpdate ? hz / pwmRateHz : 0xffff;

        /*
            if brushed then it is the entire length of the period.
            TODO: this can be moved back to periodMin and periodLen
            once mixer outputs a 0..1 float value.
        */
        /*
         * HPM PWM output compare mode (non-inverted):
         *   HIGH time = reload - CMP
         * The PWM counter runs at the source clock frequency (clock), so all
         * CMP values must be in source-clock ticks, NOT prescaled hz ticks.
         * We want HIGH time = desired_pulse_seconds, so:
         *   CMP = reload - desired_pulse_ticks
         * desired_pulse_ticks = clock * (sMin + sLen * (value - 1000) / 1000)
         *   = clock*sMin + (clock*sLen/1000)*value - clock*sLen
         * CMP = reload + clock*(sLen - sMin) - (clock*sLen/1000)*value
         */
        const uint32_t reload = (clock / hz) * period - 1;
        if (motorConfig->motorProtocol == MOTOR_PROTOCOL_BRUSHED) {
            // Brushed: throttle 0..1000 maps to CMP = reload (0% high) .. 0 (~100% high)
            pwmMotors[motorIndex].pulseScale = -((float)reload / 1000.0f);
            pwmMotors[motorIndex].pulseOffset = (float)reload;
        } else {
            pwmMotors[motorIndex].pulseScale = -(sLen * clock) / 1000.0f;
            pwmMotors[motorIndex].pulseOffset = (float)reload + clock * (sLen - sMin);
        }

        pwmOutputConfig(&pwmMotors[motorIndex].channel, timerHardware, hz, period,
                     idlePulse, motorConfig->motorInversion);

        bool timerAlreadyUsed = false;
        for (int i = 0; i < motorIndex; i++)
        {
            if (pwmMotors[i].channel.tim == pwmMotors[motorIndex].channel.tim)
            {
                timerAlreadyUsed = true;
                break;
            }
        }
        pwmMotors[motorIndex].forceOverflow = !timerAlreadyUsed;
        pwmMotors[motorIndex].enabled = true;
    }
    return true;
}

FAST_CODE pwmOutputPort_t *pwmGetMotors(void) { return pwmMotors; }

#endif
#endif // USE_PWM_OUTPUT
