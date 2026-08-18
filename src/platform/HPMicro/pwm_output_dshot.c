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

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "platform.h"

#ifdef USE_DSHOT

#include "drivers/dma.h"
#include "dma_hpmicro.h"
#include "drivers/io.h"
#include "io_hpmicro.h"
#include "drivers/time.h"
#include "drivers/timer.h"
#include "drivers/system.h"
#include "timer_hpmicro.h"
#include "hpm_soc.h"
#ifdef HPMSOC_HAS_HPMSDK_DMAV2
#include "hpm_dmav2_drv.h"
#else
#include "hpm_dma_drv.h"
#endif
#ifdef HPMSOC_HAS_HPMSDK_DMAV2
#include "hpm_pwmv2_drv.h"
#else
#include "hpm_pwm_drv.h"
#endif
#include "hpm_clock_drv.h"
#include "drivers/dshot.h"
#include "dshot_dpwm.h"
#include "drivers/dshot_command.h"
#include "pwm_output_dshot_shared.h"
#include "hpm_trgm_drv.h"
#include "hpm_dmamux_drv.h"
#include "hpm_gptmr_drv.h"

#include "timer_hw_ext.h"
#include "trgm_dshot_resource.h"
#include "hpm_dma_reqmap.h"

#define GPTMR_CAPTURE_IN_CH_POS     2
#define GPTMR_CAPTURE_IN_CH_NEG     3
#define DSHOT_SHADOW_CMP_INDEX      23

FAST_DATA_ZERO_INIT uint32_t dshot_duty_count;
#ifdef USE_DSHOT_TELEMETRY
FAST_DATA_ZERO_INIT uint32_t dshot_telemetry_bit_width;
FAST_DATA_ZERO_INIT dma_channel_config_t dshot_dma_config[MAX_SUPPORTED_MOTORS];
FAST_DATA_ZERO_INIT dma_channel_config_t dshot_cap_pos_edge_config[MAX_SUPPORTED_MOTORS];
FAST_DATA_ZERO_INIT dma_channel_config_t dshot_cap_neg_edge_config[MAX_SUPPORTED_MOTORS];
#endif

/* TRGM resources allocated per motor. */
static trgmDshotResource_t trgmDshotRes[MAX_SUPPORTED_MOTORS];

static FAST_CODE int gptmrIndexFromHwExt(const hpmicroTimerHwExt_t *hw_ext)
{
    return trgmDshotGptmrIndexByGptmr(hw_ext ? hw_ext->gptmr : NULL);
}

#if defined(HPMSOC_HAS_HPMSDK_PWM) && defined(HPMSOC_HAS_HPMSDK_DMA)
FAST_CODE void pwmDshotSetDirectionOutput(motorDmaOutput_t * const motor)
{
    motor->isInput = false;

    const timerHardware_t *time_hw = motor->timerHardware;
    const hpmicroTimerHwExt_t *hw_ext = hpmicroTimerHwExtByTimer(time_hw);
    trgmDshotResource_t *res = &trgmDshotRes[motor->index];
#ifdef HPM_USE_PWM_OUTPUT_DSHOT
    const IO_t motorIO = IOGetByTag(time_hw->tag);
#endif

    if (hw_ext == NULL) {
        return;
    }

    int gptmr_idx = gptmrIndexFromHwExt(hw_ext);
    if (gptmr_idx < 0) {
        return;
    }
    const dmaChannelSpec_t *capNegSpec = hpmDmaGetGptmrCapChannelSpec((uint8_t)gptmr_idx, false);
    const dmaChannelSpec_t *capPosSpec = hpmDmaGetGptmrCapChannelSpec((uint8_t)gptmr_idx, true);
    const dmaChannelSpec_t *outSpec = hpmDmaGetPwmOutChannelSpec(motor->index);
    if (capNegSpec == NULL || capPosSpec == NULL || outSpec == NULL) {
        return;
    }

    //Disable DSHOT input dma channels
    dma_disable_channel(capNegSpec->ref->base, capNegSpec->ref->channel);
    dma_disable_channel(capPosSpec->ref->base, capPosSpec->ref->channel);

    //Set IO to output and enable PWM channel
#ifdef HPM_USE_PWM_OUTPUT_DSHOT
    IOConfigGPIOAF(motorIO, IOCFG_AF_PP, time_hw->alternateFunction);
#else
    trgm_enable_io_output(trgmDshotResourceTrgm(res), 1 << (res->port_index));
#endif
    pwm_enable_output((PWM_Type *)time_hw->tim, time_hw->channel);

    //Map dma channel to output dma request
    dmamux_config(HPM_DMAMUX, \
                  DMA_SOC_CHN_TO_DMAMUX_CHN(outSpec->ref->base, outSpec->ref->channel), \
                  hw_ext->pwm_dmamux_src, \
                  true);

    //Clear input data
    memset(motor->dmaBuffer_neg_edge, 0, MAX_GCR_EDGES * sizeof(uint32_t));
    memset(motor->dmaBuffer_pos_edge, 0, MAX_GCR_EDGES * sizeof(uint32_t));
}


#ifdef USE_DSHOT_TELEMETRY
FAST_CODE static bool pwmDshotSetDirectionInput(motorDmaOutput_t * const motor)
{
    const timerHardware_t *timer_hw = motor->timerHardware;
    const hpmicroTimerHwExt_t *hw_ext = hpmicroTimerHwExtByTimer(timer_hw);
    trgmDshotResource_t *res = &trgmDshotRes[motor->index];
#ifdef HPM_USE_PWM_OUTPUT_DSHOT
    const IO_t motorIO = IOGetByTag(timer_hw->tag);
#endif
    GPTMR_Type *gptmr_base = hw_ext ? hw_ext->gptmr : NULL;

    if (hw_ext == NULL || gptmr_base == NULL) {
        return false;
    }

    int gptmr_idx = gptmrIndexFromHwExt(hw_ext);
    if (gptmr_idx < 0) {
        return false;
    }
    const dmaChannelSpec_t *capNegSpec = hpmDmaGetGptmrCapChannelSpec((uint8_t)gptmr_idx, false);
    const dmaChannelSpec_t *capPosSpec = hpmDmaGetGptmrCapChannelSpec((uint8_t)gptmr_idx, true);
    if (capNegSpec == NULL || capPosSpec == NULL) {
        return false;
    }
    DMA_Type *dma_neg = capNegSpec->ref->base;
    DMA_Type *dma_pos = capPosSpec->ref->base;

    dmamux_config(HPM_DMAMUX, \
                  DMA_SOC_CHN_TO_DMAMUX_CHN(dma_neg, capNegSpec->ref->channel), \
                  capNegSpec->dmaMuxId, \
                  true);

    if (status_success != dma_setup_channel(dma_neg, \
                                            capNegSpec->ref->channel, \
                                            &dshot_cap_neg_edge_config[motor->index], \
                                            false)) {
        return false;
    }
    if (status_success != dma_setup_channel(dma_pos, \
                                            capPosSpec->ref->channel, \
                                            &dshot_cap_pos_edge_config[motor->index], \
                                            false)) {
        dma_disable_channel(dma_neg, capNegSpec->ref->channel);
        return false;
    }

    /* Switch the pin only after both capture channels are ready.  If setup
     * fails, leave the motor in output mode and skip telemetry for this frame. */
#ifdef HPM_USE_PWM_OUTPUT_DSHOT
    IOConfigGPIOAF(motorIO, IOCFG_IN_FLOATING, 0);
#else
    trgm_disable_io_output(trgmDshotResourceTrgm(res), 1 << (res->port_index));
#endif
    motor->isInput = true;

    if (!inputStampUs) {
        inputStampUs = micros();
    }

    gptmr_channel_reset_count(gptmr_base, GPTMR_CAPTURE_IN_CH_NEG);
    gptmr_channel_reset_count(gptmr_base, GPTMR_CAPTURE_IN_CH_POS);
    gptmr_start_counter(gptmr_base, GPTMR_CAPTURE_IN_CH_NEG);
    gptmr_start_counter(gptmr_base, GPTMR_CAPTURE_IN_CH_POS);


    gptmr_trigger_channel_software_sync(gptmr_base, 0xF);
    dma_enable_channel(dma_pos, capPosSpec->ref->channel);
    dma_enable_channel(dma_neg, capNegSpec->ref->channel);

    return true;
}
#endif


FAST_CODE void pwmCompleteDshotMotorUpdate(void)
{
    // HPM starts each motor DMA from pwmWriteDshotInt().  Command timing is
    // therefore gated there, before the first motor transfer is started.
}

FAST_CODE static void motor_dshot_transfer_done_handler(dmaChannelDescriptor_t *descriptor)
{
    motorDmaOutput_t * const motor = &dmaMotors[descriptor->userParam];

    if (hpmDmaGetChannelStatus(descriptor) & (DMA_CHANNEL_STATUS_ERROR | DMA_CHANNEL_STATUS_ABORT)) {
        const hpmicroTimerHwExt_t *hw_ext = hpmicroTimerHwExtByTimer(motor->timerHardware);
        if (hw_ext) {
            pwm_disable_dma_request((PWM_Type *)motor->timerHardware->tim,
                                    PWM_DMAEN_CMPENX_SET(1 << hw_ext->dma_req_cmp_index));
        }
        return;
    }

#ifdef USE_DSHOT_TELEMETRY
    if (!motor->isInput) {
        dshotDMAHandlerCycleCounters.irqAt = getCycleCounter();
        if (useDshotTelemetry && pwmDshotSetDirectionInput(motor)) {
            dshotDMAHandlerCycleCounters.changeDirectionCompletedAt = getCycleCounter();
        }
    }
#endif
}

#ifdef USE_DSHOT_TELEMETRY

/**
 * @brief Setup DSHOT TELEMETRY related dma channel.
 *
 */
FAST_CODE static void setup_dshot_telemetry_dma(motorDmaOutput_t * const motor, motorProtocolTypes_e pwmProtocolType)
{
    const timerHardware_t *timerHardware = motor->timerHardware;
    const hpmicroTimerHwExt_t *hw_ext = hpmicroTimerHwExtByTimer(timerHardware);
    trgmDshotResource_t *res = &trgmDshotRes[motor->index];
    if (hw_ext == NULL) {
        return;
    }

    //Source Info From fullTimerHardware in timer_hpm6750.c
    int gptmr_idx = gptmrIndexFromHwExt(hw_ext);
    if (gptmr_idx < 0) {
        return;
    }
    const dmaChannelSpec_t *capNegSpec = hpmDmaGetGptmrCapChannelSpec((uint8_t)gptmr_idx, false);
    const dmaChannelSpec_t *capPosSpec = hpmDmaGetGptmrCapChannelSpec((uint8_t)gptmr_idx, true);
    if (capNegSpec == NULL || capPosSpec == NULL || res->gptmr == NULL) {
        return;
    }

    DMA_Type *dma_neg = capNegSpec->ref->base;
    DMA_Type *dma_pos = capPosSpec->ref->base;
    GPTMR_Type *gptmr = res->gptmr;
    uint8_t motor_idx = motor->index;
    dma_channel_config_t *dma_config;

    gptmr_channel_config_t config;

    gptmr_channel_get_default_config(gptmr, &config);
    uint32_t gptmr_freq = clock_get_frequency(res->gptmr_clock);

    //dshot_reload_counter is used to decode dshot telemetry message
    dshot_telemetry_bit_width = gptmr_freq / getDshotHz(pwmProtocolType) * MOTOR_BITLENGTH * 4 / 5;

    config.reload = gptmr_freq / 10 - 1;
    config.enable_software_sync = true;
    config.dma_request_event = gptmr_dma_request_on_input_signal_toggle;

    config.mode = gptmr_work_mode_capture_at_falling_edge;
    gptmr_channel_config(gptmr, GPTMR_CAPTURE_IN_CH_NEG, &config, false);
    config.mode = gptmr_work_mode_capture_at_rising_edge;
    gptmr_channel_config(gptmr, GPTMR_CAPTURE_IN_CH_POS, &config, false);

    // Setup dshot_cap_neg_edge_config and dshot_cap_pos_edge_config to decrease change dshot direction time cost
    dma_config = &dshot_cap_neg_edge_config[motor_idx];
    dma_default_channel_config(dma_neg, dma_config);
    dma_config->src_mode = DMA_HANDSHAKE_MODE_HANDSHAKE;
    dma_config->src_width = DMA_TRANSFER_WIDTH_WORD;
    dma_config->src_addr_ctrl = DMA_ADDRESS_CONTROL_FIXED;
    dma_config->src_burst_size = DMA_NUM_TRANSFER_PER_BURST_1T;
    dma_config->dst_width = DMA_TRANSFER_WIDTH_WORD;
    dma_config->dst_addr_ctrl = DMA_ADDRESS_CONTROL_INCREMENT;
    dma_config->dst_mode = DMA_HANDSHAKE_MODE_NORMAL;
    dma_config->size_in_byte = MAX_GCR_EDGES * sizeof(uint32_t);
    dma_config->linked_ptr = 0;
    // Capture completion is polled by the decoder; only surface failures.
    dma_config->interrupt_mask = DMA_INTERRUPT_MASK_TERMINAL_COUNT;
    dma_config->dst_addr = core_local_mem_to_sys_address(HPM_CORE0, (uint32_t)motor->dmaBuffer_neg_edge);
    dma_config->src_addr = (uint32_t)&gptmr->CHANNEL[GPTMR_CAPTURE_IN_CH_NEG].CAPNEG;

    dma_config = &dshot_cap_pos_edge_config[motor_idx];
    dma_default_channel_config(dma_pos, dma_config);
    dma_config->src_mode = DMA_HANDSHAKE_MODE_HANDSHAKE;
    dma_config->src_width = DMA_TRANSFER_WIDTH_WORD;
    dma_config->src_addr_ctrl = DMA_ADDRESS_CONTROL_FIXED;
    dma_config->src_burst_size = DMA_NUM_TRANSFER_PER_BURST_1T;
    dma_config->dst_width = DMA_TRANSFER_WIDTH_WORD;
    dma_config->dst_addr_ctrl = DMA_ADDRESS_CONTROL_INCREMENT;
    dma_config->dst_mode = DMA_HANDSHAKE_MODE_NORMAL;
    dma_config->size_in_byte = MAX_GCR_EDGES * sizeof(uint32_t);
    dma_config->linked_ptr = 0;
    // Capture completion is polled by the decoder; only surface failures.
    dma_config->interrupt_mask = DMA_INTERRUPT_MASK_TERMINAL_COUNT;
    dma_config->dst_addr = core_local_mem_to_sys_address(HPM_CORE0, (uint32_t)motor->dmaBuffer_pos_edge);
    dma_config->src_addr = (uint32_t)&gptmr->CHANNEL[GPTMR_CAPTURE_IN_CH_POS].CAPPOS;

}
#endif

FAST_CODE void pwmDshotStartTransfer(motorDmaOutput_t *motor, uint32_t size)
{
    const hpmicroTimerHwExt_t *hw_ext = hpmicroTimerHwExtByTimer(motor->timerHardware);
    if (hw_ext == NULL) {
        return;
    }

    const dmaChannelSpec_t *outSpec = hpmDmaGetPwmOutChannelSpec(motor->index);
    if (outSpec == NULL) {
        return;
    }

    DMA_Type *dma_base = outSpec->ref->base;
    dma_channel_config_t *ch_config = &dshot_dma_config[motor->index];
    ch_config->size_in_byte = size;

    // Disable the PWM DMA request before reconfiguring the channel so that
    // any pending request from the free-running timer does not trigger a
    // spurious transfer mid-setup.  The request is re-enabled after the
    // channel has been fully configured.
    pwm_disable_dma_request((PWM_Type *)motor->timerHardware->tim,
                            PWM_DMAEN_CMPENX_SET(1 << (hw_ext->dma_req_cmp_index)));

    // RISC-V weak memory ordering: ensure CPU writes to the DMA buffer are
    // visible to the DMA engine before the channel is started.
    __asm__ volatile("fence w, w" ::: "memory");

    dma_disable_channel(dma_base, outSpec->ref->channel);
    if (status_success != dma_setup_channel(dma_base, \
                                            outSpec->ref->channel, \
                                            ch_config, \
                                            true)) {
        printf(" dma setup channel failed\n");
        failureMode(FAILURE_DEVELOPER);
    }

    // Re-enable the PWM DMA request.  The next free-running timer compare
    // match starts this motor's transfer.
    pwm_enable_dma_request((PWM_Type *)motor->timerHardware->tim,
                           PWM_DMAEN_CMPENX_SET(1 << hw_ext->dma_req_cmp_index));
}

bool pwmDshotMotorHardwareConfig(const timerHardware_t *timerHardware, uint8_t motorIndex, uint8_t reorderedMotorIndex, motorProtocolTypes_e pwmProtocolType, uint8_t output)
{
    // Stagger reference compare events by motor while keeping the value
    // deterministic across motor-device reinitialization.
    const uint8_t cmpOffset = 3U * (motorIndex + 1U);

    const hpmicroTimerHwExt_t *hw_ext = hpmicroTimerHwExtByTimer(timerHardware);
    if (hw_ext == NULL) {
        return false;
    }

    trgmDshotResource_t *res = &trgmDshotRes[motorIndex];

    int gptmr_idx = gptmrIndexFromHwExt(hw_ext);
    if (gptmr_idx < 0) {
        return false;
    }
    const dmaChannelSpec_t *outSpec = hpmDmaGetPwmOutChannelSpec(motorIndex);
    const dmaChannelSpec_t *capNegSpec = hpmDmaGetGptmrCapChannelSpec((uint8_t)gptmr_idx, false);
    const dmaChannelSpec_t *capPosSpec = hpmDmaGetGptmrCapChannelSpec((uint8_t)gptmr_idx, true);
    if (outSpec == NULL || capNegSpec == NULL || capPosSpec == NULL) {
        return false;
    }

    dmaResource_t *dmaRef = outSpec->ref;
    dmaIdentifier_e dmaIdentifier = dmaGetIdentifier(dmaRef);
    if (!dmaAllocate(dmaIdentifier, OWNER_MOTOR, RESOURCE_INDEX(reorderedMotorIndex))) {
        return false;
    }

    const dmaIdentifier_e dmaIdentifierCapNeg = dmaGetIdentifier(capNegSpec->ref);
    if (!dmaAllocate(dmaIdentifierCapNeg, OWNER_MOTOR, RESOURCE_INDEX(reorderedMotorIndex))) {
        hpmDmaRelease(dmaIdentifier);
        return false;
    }
    const dmaIdentifier_e dmaIdentifierCapPos = dmaGetIdentifier(capPosSpec->ref);
    if (!dmaAllocate(dmaIdentifierCapPos, OWNER_MOTOR, RESOURCE_INDEX(reorderedMotorIndex))) {
        hpmDmaRelease(dmaIdentifierCapNeg);
        hpmDmaRelease(dmaIdentifier);
        return false;
    }

#ifdef USE_DSHOT_TELEMETRY
    if (useDshotTelemetry) {
        output ^= TIMER_OUTPUT_INVERTED;
    }
#endif
    motorDmaOutput_t * const motor = &dmaMotors[motorIndex];
    TIM_TypeDef *timer = (PWM_Type *)timerHardware->tim;

    /* Allocate TRGM pin and PWM reference source for this motor. */
    if (!trgmDshotResourceAlloc(timerHardware->tag, res)) {
        hpmDmaRelease(dmaIdentifierCapPos);
        hpmDmaRelease(dmaIdentifierCapNeg);
        hpmDmaRelease(dmaIdentifier);
        return false;
    }
    if (hw_ext->pwm_ref_src != 0) {
        if (!trgmDshotResourceAllocPwmRef(res, hw_ext->pwm_ref_src)) {
            trgmDshotResourceFree(res);
            hpmDmaRelease(dmaIdentifierCapPos);
            hpmDmaRelease(dmaIdentifierCapNeg);
            hpmDmaRelease(dmaIdentifier);
            return false;
        }
    }
#ifdef USE_DSHOT_TELEMETRY
    if (useDshotTelemetry && !trgmDshotResourceAllocInput(res, hw_ext->gptmr)) {
        trgmDshotResourceFree(res);
        hpmDmaRelease(dmaIdentifierCapPos);
        hpmDmaRelease(dmaIdentifierCapNeg);
        hpmDmaRelease(dmaIdentifier);
        return false;
    }
#endif

    uint8_t timerIndex = getTimerIndex(timer);
    if (timerIndex >= MAX_DMA_TIMERS) {
        trgmDshotResourceFree(res);
        hpmDmaRelease(dmaIdentifierCapPos);
        hpmDmaRelease(dmaIdentifierCapNeg);
        hpmDmaRelease(dmaIdentifier);
        return false;
    }

    // Capture TC is masked in its channel config, but error/abort events still
    // need the shared controller IRQ enabled and dispatched.  Install handlers
    // only after every fallible resource allocation has succeeded.
    dmaSetHandler(dmaIdentifierCapNeg, NULL, 1, motorIndex);
    dmaSetHandler(dmaIdentifierCapPos, NULL, 1, motorIndex);

    dma_channel_config_t *ch_config = &dshot_dma_config[motorIndex];
    uint32_t reload = 0;

    reload = (float) getPWMFre(timer) / getDshotHz(pwmProtocolType) * MOTOR_BITLENGTH - 1;

    // dshot_duty_count is a single global shared by all motors via the
    // MOTOR_BIT_0 / MOTOR_BIT_1 macros.  The macros compute correct bit
    // timing only when every PWM timer is clocked at the same frequency.
    // Verify this invariant once at init time.
    {
        static uint32_t firstPwmFreq;
        const uint32_t freq = getPWMFre(timer);
        if (firstPwmFreq == 0) {
            firstPwmFreq = freq;
        } else if (freq != firstPwmFreq) {
            failureMode(FAILURE_DEVELOPER);
        }
    }
    dshot_duty_count = reload;

    pwm_cmp_config_t cmp_config[2] = {0};
    pwm_config_t pwm_config = {0};

    // Boolean configureTimer is always true when different channels of the same timer are processed in sequence,
    // causing the timer and the associated DMA initialized more than once.
    // To fix this, getTimerIndex must be expanded to return if a new timer has been requested.
    // However, since the initialization is idempotent, it is left as is in a favor of flash space (for now).
    motor->timer = &dmaMotorTimers[timerIndex];
    motor->timer->trgmIndex = hw_ext->pwm_trgm_index;
    motor->index = motorIndex;
    motor->timerHardware = timerHardware;
    DMA_Type* base = outSpec->ref->base;

    pwm_pair_config_t cmp_pair_config = { 0 };
    if (dmaMotorTimers[timerIndex].inited == false) {
        clock_add_to_group(timerRCC(timer), 0);
        pwm_stop_counter(timer);
        reload = (float) getPWMFre(timer) / getDshotHz(pwmProtocolType) * MOTOR_BITLENGTH - 1;
        /*
         * reload and start counter
         */
        pwm_set_reload(timer, 0, reload);
        pwm_set_start_count(timer, 0, 0);
    }
    if (output & TIMER_OUTPUT_N_CHANNEL) {
        pwm_get_default_pwm_pair_config(timer, &cmp_pair_config);
        cmp_pair_config.pwm[0].invert_output = (output & TIMER_OUTPUT_INVERTED) ? false : true;
        cmp_pair_config.pwm[0].force_cmd_shadow_update_trigger = pwm_shadow_register_update_on_modify;
        cmp_pair_config.pwm[0].enable_output = true;
    } else {
        pwm_get_default_pwm_config(timer, &pwm_config);
        pwm_config.force_cmd_shadow_update_trigger = pwm_shadow_register_update_on_hw_event;
        pwm_config.enable_output = true;
        pwm_config.dead_zone_in_half_cycle = 0;
        pwm_config.invert_output = (output & TIMER_OUTPUT_INVERTED) ? false : true;
    }
    cmp_config[0].mode = pwm_cmp_mode_output_compare;
    cmp_config[0].cmp = reload;
    cmp_config[0].update_trigger = pwm_shadow_register_update_on_hw_event;

    if (output & TIMER_OUTPUT_N_CHANNEL) {
        if (status_success != pwm_setup_waveform_in_pair(timer, \
                                                         timerHardware->channel, \
                                                         &cmp_pair_config, \
                                                         hw_ext->cmp_index, \
                                                         &cmp_config[0], \
                                                         1)) {
            printf("failed to setup waveform\n");
            failureMode(FAILURE_DEVELOPER);
        }
    } else {
        if (status_success != pwm_setup_waveform(timer, \
                                                 timerHardware->channel, \
                                                 &pwm_config, \
                                                 hw_ext->cmp_index, \
                                                 &cmp_config[0], \
                                                 1)) {
            printf("failed to setup waveform\n");
            failureMode(FAILURE_DEVELOPER);
        }
        cmp_config[0].mode = pwm_cmp_mode_output_compare;
        cmp_config[0].cmp = 1U + cmpOffset;
        cmp_config[0].update_trigger = pwm_shadow_register_update_on_modify;
        pwm_config.enable_output = false;
        /*
         * config pwm as output driven by cmp
         */
        if (status_success != pwm_setup_waveform(timer, \
                                                 hw_ext->channel_ref, \
                                                 &pwm_config, \
                                                 hw_ext->dma_req_cmp_index, \
                                                 &cmp_config[0], \
                                                 1)) {
            printf("failed to setup waveform\n");
            failureMode(FAILURE_DEVELOPER);
        }
    }
    if (dmaMotorTimers[timerIndex].inited == false) {
        cmp_config[0].mode = pwm_cmp_mode_output_compare;
        cmp_config[0].cmp = reload - 2;
        cmp_config[0].update_trigger = pwm_shadow_register_update_on_modify;
        pwm_load_cmp_shadow_on_match(timer, DSHOT_SHADOW_CMP_INDEX, &cmp_config[0]);
        pwm_start_counter(timer);
        dmaMotorTimers[timerIndex].inited = true;
    }

    pwm_issue_shadow_register_lock_event(timer);

    /* PWM half reload generate dma request */
    trgm_dma_request_config(trgmDshotTrgmByIndex(hw_ext->pwm_trgm_index), \
                            hw_ext->trgm_dma_group, \
                            hw_ext->pwm_trgm_dma_src);
    /* dma request trigger dma channel x to work */
    pwm_enable_dma_request(timer, PWM_DMAEN_CMPENX_SET(1 << hw_ext->dma_req_cmp_index));

    if (hw_ext->pwm_ref_src != 0) {
        trgm_output_t trgm_io_config = {0};
        trgm_io_config.invert = 0;
        trgm_io_config.type = trgm_output_same_as_input;
        trgm_io_config.input = res->pwm_ref_src;
        trgm_output_config(trgmDshotResourceTrgm(res), res->trgm_p_dst, &trgm_io_config);
    }

    motor->timerDmaSource = PWM_DMAEN_CMPENX_SET(1 << hw_ext->dma_req_cmp_index);
    motor->timer->timerDmaSources &= ~motor->timerDmaSource;

    motor->dmaBuffer = &dshotDmaBuffer[motorIndex][0];
    motor->dmaBuffer_pos_edge = &dshot_telemetry_pos_buf[motorIndex][0];
    motor->dmaBuffer_neg_edge = &dshot_telemetry_neg_buf[motorIndex][0];

    motor->dmaRef = dmaRef;

#ifdef USE_DSHOT_TELEMETRY
    motor->dshotTelemetryDeadtimeUs = DSHOT_TELEMETRY_DEADTIME_US + 1000000 *
        (16 * MOTOR_BITLENGTH) / getDshotHz(pwmProtocolType);
    motor->timer->outputPeriod = (pwmProtocolType == PWM_TYPE_PROSHOT1000 ? (MOTOR_NIBBLE_LENGTH_PROSHOT) : MOTOR_BITLENGTH) - 1;
#endif
    pwmDshotSetDirectionOutput(motor);

    // dmaSetHandler() also enables the channel TC interrupt and the IRQ at this priority
    dmaSetHandler(dmaIdentifier, motor_dshot_transfer_done_handler, 1, motor->index);

    const IO_t cap_pin = IOGetByTag(res->pin);
    IOConfigGPIOAF(cap_pin, IOCFG_OUT_PP_UP, res->ioc_function);
#ifdef USE_DSHOT_TELEMETRY
    if (useDshotTelemetry) {
        // avoid high line during startup to prevent bootloader activation

        clock_add_to_group(res->gptmr_clock, 0);
        setup_dshot_telemetry_dma(motor, pwmProtocolType);

        /* Route TRGMx_Py to GPTMRx_IN2/3 for telemetry input capture. */
        trgm_output_t trgm_io_config = {0};
        trgm_io_config.invert = 0;
        trgm_io_config.type = trgm_output_same_as_input;
        trgm_io_config.input = res->trgm_p_src;
        trgm_output_config(trgmDshotResourceTrgm(res), res->gptmr_in2_dst, &trgm_io_config);
        memset(&trgm_io_config, 0, sizeof(trgm_io_config));
        trgm_io_config.invert = 0;
        trgm_io_config.type = trgm_output_same_as_input;
        trgm_io_config.input = res->trgm_p_src;
        trgm_output_config(trgmDshotResourceTrgm(res), res->gptmr_in3_dst, &trgm_io_config);

        dmamux_config(HPM_DMAMUX, \
                      DMA_SOC_CHN_TO_DMAMUX_CHN(capPosSpec->ref->base, capPosSpec->ref->channel), \
                      capPosSpec->dmaMuxId, \
                      true);
    }
#endif

    dma_default_channel_config(base, ch_config);
    ch_config->src_addr = core_local_mem_to_sys_address(HPM_CORE0, (uint32_t)motor->dmaBuffer);
    ch_config->dst_addr = (uint32_t)timerChCCR(motor->timerHardware);
    ch_config->src_mode = DMA_HANDSHAKE_MODE_HANDSHAKE;
    ch_config->src_width = DMA_TRANSFER_WIDTH_WORD;
    ch_config->src_addr_ctrl = DMA_ADDRESS_CONTROL_INCREMENT;
    ch_config->src_burst_size = DMA_NUM_TRANSFER_PER_BURST_1T;
    ch_config->dst_width = DMA_TRANSFER_WIDTH_WORD;
    ch_config->dst_addr_ctrl = DMA_ADDRESS_CONTROL_FIXED;
    ch_config->dst_mode = DMA_HANDSHAKE_MODE_NORMAL;
    // ch_config->interrupt_mask = DMA_INTERRUPT_MASK_TERMINAL_COUNT;

    // Pad drive config re-applied by dshotPwmEnableMotors() on arming
    motor->iocfg = IOCFG_OUT_PP_UP;
    motor->configured = true;
    return true;
}
#endif
#endif
