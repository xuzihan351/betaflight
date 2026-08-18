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


#ifdef USE_ADC

#include "drivers/io.h"
#include "io_hpmicro.h"
#include "drivers/adc.h"
#include "platform/adc_impl.h"
#include "hpm_clock_drv.h"
#include "pg/adc.h"
#include "hil_test.h"

#ifdef HPM6750
#include "hpm_adc12_drv.h"
#elif defined(HPM6360)
#include "hpm_adc16_drv.h"
#endif

/* ------------------------------------------------------------------ *
 *  Forward declarations for helpers in adc_hpmicro.c that aren't
 *  exposed in the platform header but are useful for direct testing.
 * ------------------------------------------------------------------ */

extern int adcFindTagMapEntry(ioTag_t tag);

/* adcDevice[] is defined in adc_hpmicro.c but not declared in adc_impl.h */
extern adcDevice_t adcDevice[ADCDEV_COUNT];

#if PLATFORM_TRAIT_ADC_DEVICE
extern adcDevice_e adcDeviceByInstance(const ADC_TypeDef *instance);
#endif

/* ------------------------------------------------------------------ *
 *  Per-platform test parameters
 *
 *  We pick an IO tag that:
 *    a. Exists in adcTagMap[]     (required for Betaflight-level tests)
 *    b. Is brought out on the board (set to analog by pinmux init)
 *
 *  For HPM6360 on SK Flycontrol: PC16 = ADC0 channel 12
 *  For HPM6750:                  PE14 = ADC0 channel 0
 * ------------------------------------------------------------------ */

#ifdef HPM6360
#define HIL_ADC_TAG           DEFIO_TAG_E__PC16
#define HIL_ADC_EXPECT_CH     8
#define HIL_ADC_SECOND_TAG    DEFIO_TAG_E__PC15
#define HIL_ADC_SECOND_CH     7
#else
#define HIL_ADC_TAG           DEFIO_TAG_E__PE14
#define HIL_ADC_EXPECT_CH     0
#define HIL_ADC_SECOND_TAG    DEFIO_TAG_E__PE15
#define HIL_ADC_SECOND_CH     1
#endif

/* ================================================================== *
 *  Test 1 -- Raw SDK ADC init / periodic read / deinit
 *
 *  Bypasses the Betaflight abstraction and exercises the HPM SDK
 *  driver directly.  Validates that clocks, the ADC IP, and the
 *  low-level driver all function correctly.
 * ================================================================== */

void hil_test_adc_sdk_direct(void)
{
#if defined(HPM6360)
    adc16_config_t          cfg;
    adc16_channel_config_t  ch_cfg;
    adc16_prd_config_t      prd_cfg;
    hpm_stat_t              stat;

    ADC16_Type *adc = HPM_ADC0;

    /* ---- 1. Default config + periodic mode ---- */
    adc16_get_default_config(&cfg);
    cfg.res          = adc16_res_16_bits;
    cfg.conv_mode    = adc16_conv_mode_period;
    cfg.adc_clk_div  = adc16_clock_divider_4;
    cfg.sel_sync_ahb = true;

    clock_add_to_group(clock_adc0, 0);
    clock_set_adc_source(clock_adc0, clk_adc_src_ahb0);

    stat = adc16_deinit(adc);
    HIL_ASSERT_TRUE(stat == status_success);

    /* ---- 2. Init ADC instance ---- */
    stat = adc16_init(adc, &cfg);
    HIL_ASSERT_TRUE(stat == status_success);
    HIL_INFO("ADC16 (HPM_ADC0) initialised OK");

    /* ---- 3. Configure a single channel ---- */
    adc16_get_channel_default_config(&ch_cfg);
    ch_cfg.ch           = (uint8_t)HIL_ADC_EXPECT_CH;
    ch_cfg.sample_cycle = 20;

    stat = adc16_init_channel(adc, &ch_cfg);
    HIL_ASSERT_TRUE(stat == status_success);
    HIL_INFO("ADC16 channel %u configured", (unsigned)ch_cfg.ch);

    /* ---- 4. Start periodic conversion (~105 ms period) ---- */
    prd_cfg.ch           = (uint8_t)HIL_ADC_EXPECT_CH;
    prd_cfg.prescale     = 22;
    prd_cfg.period_count = 5;

    stat = adc16_set_prd_config(adc, &prd_cfg);
    HIL_ASSERT_TRUE(stat == status_success);
    HIL_INFO("ADC16 periodic mode started");

    /* ---- 5. Wait for conversions, then read result ---- */
    hil_delay_ms(300);

    uint16_t result = 0;
    stat = adc16_get_prd_result(adc, (uint8_t)HIL_ADC_EXPECT_CH, &result);
    HIL_ASSERT_TRUE(stat == status_success);

    HIL_INFO("ADC16 channel %u raw result = %5u (0x%04X)",
             (unsigned)HIL_ADC_EXPECT_CH, (unsigned)result, (unsigned)result);

    /* 16-bit raw value from hardware -- always fits in uint16_t range.
     * (No inequality assertion needed; the type itself guarantees [0, 65535].) */

    /* ---- 6. De-init to leave a clean state ---- */
    stat = adc16_deinit(adc);
    HIL_ASSERT_TRUE(stat == status_success);
    HIL_INFO("ADC16 deinit OK");

#elif defined(HPM6750)
    adc12_config_t          cfg;
    adc12_channel_config_t  ch_cfg;
    adc12_prd_config_t      prd_cfg;
    hpm_stat_t              stat;

    ADC12_Type *adc = HPM_ADC0;

    adc12_get_default_config(&cfg);
    cfg.res          = adc12_res_12_bits;
    cfg.conv_mode    = adc12_conv_mode_period;
    cfg.adc_clk_div  = adc12_clock_divider_4;
    cfg.sel_sync_ahb = true;

    clock_add_to_group(clock_adc0, 0);
    clock_set_adc_source(clock_adc0, clk_adc_src_ahb0);

    stat = adc12_deinit(adc);
    HIL_ASSERT_TRUE(stat == status_success);

    stat = adc12_init(adc, &cfg);
    HIL_ASSERT_TRUE(stat == status_success);
    HIL_INFO("ADC12 (HPM_ADC0) initialised OK");

    adc12_get_channel_default_config(&ch_cfg);
    ch_cfg.ch           = (uint8_t)HIL_ADC_EXPECT_CH;
    ch_cfg.sample_cycle = 20;

    stat = adc12_init_channel(adc, &ch_cfg);
    HIL_ASSERT_TRUE(stat == status_success);
    HIL_INFO("ADC12 channel %u configured", (unsigned)ch_cfg.ch);

    prd_cfg.ch           = (uint8_t)HIL_ADC_EXPECT_CH;
    prd_cfg.prescale     = 22;
    prd_cfg.period_count = 5;

    stat = adc12_set_prd_config(adc, &prd_cfg);
    HIL_ASSERT_TRUE(stat == status_success);
    HIL_INFO("ADC12 periodic mode started");

    hil_delay_ms(300);

    uint16_t result = 0;
    stat = adc12_get_prd_result(adc, (uint8_t)HIL_ADC_EXPECT_CH, &result);
    HIL_ASSERT_TRUE(stat == status_success);

    HIL_INFO("ADC12 channel %u raw result = %5u (0x%04X)",
             (unsigned)HIL_ADC_EXPECT_CH, (unsigned)result, (unsigned)result);

    /* 12-bit raw value must fit in 12 bits */
    HIL_ASSERT_TRUE(result <= 4095);

    stat = adc12_deinit(adc);
    HIL_ASSERT_TRUE(stat == status_success);
    HIL_INFO("ADC12 deinit OK");
#endif
}

/* ================================================================== *
 *  Test 2 -- adcFindTagMapEntry lookup
 *
 *  Validates the tag->channel mapping used by adcInit() to associate
 *  an IO tag with an ADC device + channel number.
 * ================================================================== */

void hil_test_adc_tag_map_lookup(void)
{
    /* Known tag -- must be found */
    int idx = adcFindTagMapEntry(HIL_ADC_TAG);
    HIL_ASSERT_TRUE(idx >= 0);
    HIL_INFO("adcFindTagMapEntry(tag=0x%08lX) -> index %d",
             (unsigned long)HIL_ADC_TAG, idx);

    /* The returned index must point to a valid entry whose channel
     * matches the expected hardware channel. */
    if (idx >= 0 && idx < (int)ADC_TAG_MAP_COUNT) {
        HIL_ASSERT_EQ(adcTagMap[idx].channel, HIL_ADC_EXPECT_CH);
        HIL_INFO("  tagMap[%d]: tag=0x%08lX  devices=0x%02X  channel=%lu",
                 idx,
                 (unsigned long)adcTagMap[idx].tag,
                 (unsigned)adcTagMap[idx].devices,
                 (unsigned long)adcTagMap[idx].channel);
    }

    /* Invalid tag (0) -- must return -1 */
    int bad = adcFindTagMapEntry(0);
    HIL_ASSERT_EQ(bad, -1);
    HIL_INFO("adcFindTagMapEntry(0) -> %d  (expected -1)", bad);

    /* Another definitely-invalid tag (PA0 is a GPIO, never an ADC input) */
    bad = adcFindTagMapEntry(DEFIO_TAG_E__PA0);
    HIL_ASSERT_EQ(bad, -1);
}

/* ================================================================== *
 *  Test 3 -- adcDeviceByInstance mapping
 *
 *  Confirms the ADCx instance pointer -> ADCDEV_ enumeration mapping
 *  that the parameter-group system relies on.
 * ================================================================== */

#if PLATFORM_TRAIT_ADC_DEVICE
void hil_test_adc_device_by_instance(void)
{
    HIL_ASSERT_EQ((int)adcDeviceByInstance(HPM_ADC0), (int)ADCDEV_1);
    HIL_INFO("HPM_ADC0 -> ADCDEV_%d  OK", (int)ADCDEV_1 + 1);

#if defined(ADC2)
    HIL_ASSERT_EQ((int)adcDeviceByInstance(HPM_ADC1), (int)ADCDEV_2);
    HIL_INFO("HPM_ADC1 -> ADCDEV_%d  OK", (int)ADCDEV_2 + 1);
#endif

#if defined(ADC3)
    HIL_ASSERT_EQ((int)adcDeviceByInstance(HPM_ADC2), (int)ADCDEV_3);
    HIL_INFO("HPM_ADC2 -> ADCDEV_%d  OK", (int)ADCDEV_3 + 1);
#endif
}
#endif /* PLATFORM_TRAIT_ADC_DEVICE */

/* ================================================================== *
 *  Test 4 -- Betaflight adcInit() with a single-channel configuration
 *
 *  Exercises the full Betaflight ADC initialisation path:
 *    - tag -> channel lookup     (adcFindTagMapEntry)
 *    - device assignment        (adcDevice[dev].channelBits)
 *    - pinmux (IOConfigGPIOAF)
 *    - ADC instance init + periodic config
 * ================================================================== */

void hil_test_adc_bf_init(void)
{
    adcConfig_t config;
    memset(&config, 0, sizeof(config));

    config.vbat.enabled = true;
    config.vbat.ioTag   = HIL_ADC_TAG;

    adcInit(&config);

    /* ---- Battery channel must be fully configured ---- */
    HIL_ASSERT_TRUE(adcOperatingConfig[ADC_BATTERY].enabled);
    HIL_ASSERT_EQ(adcOperatingConfig[ADC_BATTERY].tag, HIL_ADC_TAG);
    HIL_ASSERT_EQ(adcOperatingConfig[ADC_BATTERY].adcChannel,
                  HIL_ADC_EXPECT_CH);
    HIL_ASSERT_NE((int)adcOperatingConfig[ADC_BATTERY].adcDevice,
                  (int)ADCINVALID);

    int dev = (int)adcOperatingConfig[ADC_BATTERY].adcDevice;

    HIL_INFO("Battery ADC: dev=%d  channel=%lu  dmaIdx=%u  sampleTime=%u",
             dev,
             (unsigned long)adcOperatingConfig[ADC_BATTERY].adcChannel,
             (unsigned)adcOperatingConfig[ADC_BATTERY].dmaIndex,
             (unsigned)adcOperatingConfig[ADC_BATTERY].sampleTime);

    /* ---- Other channels must remain disabled ---- */
    HIL_ASSERT_FALSE(adcOperatingConfig[ADC_CURRENT].enabled);
    HIL_ASSERT_FALSE(adcOperatingConfig[ADC_RSSI].enabled);
    HIL_ASSERT_FALSE(adcOperatingConfig[ADC_EXTERNAL1].enabled);

    /* ---- Check device channelBits includes our channel ---- */
    if (dev >= 0 && dev < ADCDEV_COUNT) {
        uint32_t bit = 1UL << HIL_ADC_EXPECT_CH;
        HIL_ASSERT_TRUE((adcDevice[dev].channelBits & bit) != 0);
        HIL_INFO("adcDevice[%d].channelBits = 0x%08lX  (bit %lu set)",
                 dev, (unsigned long)adcDevice[dev].channelBits,
                 (unsigned long)HIL_ADC_EXPECT_CH);
    }
}

/* ================================================================== *
 *  Test 5 -- Betaflight ADC read (single channel)
 *
 *  After adcInit(), waits for periodic mode to produce samples and
 *  reads back the result through both the internal adcValues[] array
 *  and the public adcGetValue() API.
 * ================================================================== */

void hil_test_adc_bf_read(void)
{
    adcConfig_t config;
    memset(&config, 0, sizeof(config));

    config.vbat.enabled = true;
    config.vbat.ioTag   = HIL_ADC_TAG;

    adcInit(&config);

    /* Allow several periodic cycles to complete (~105 ms each) */
    hil_delay_ms(400);

    /* Fetch the latest values into adcValues[] */
    adcGetChannelValues();

    /* ---- Read from adcValues[] directly ---- */
    uint16_t val = adcValues[ADC_BATTERY];
    HIL_INFO("adcValues[ADC_BATTERY] = %5u (0x%04X)",
             (unsigned)val, (unsigned)val);

    /* HPM6360: 16-bit result is shifted right by 4 -> 12-bit effective range.
     * HPM6750: native 12-bit result.  Both produce [0, 4095]. */
    HIL_ASSERT_TRUE(val <= 4095);

    /* ---- Read through the public Betaflight API ---- */
    uint16_t apiVal = adcGetValue(ADC_BATTERY);
    HIL_INFO("adcGetValue(ADC_BATTERY)  = %5u (0x%04X)",
             (unsigned)apiVal, (unsigned)apiVal);

    HIL_ASSERT_TRUE(apiVal <= 4095);
}

/* ================================================================== *
 *  Test 6 -- Betaflight ADC: unconfigured sources return 0
 *
 *  adcGetValue() must return 0 for any source whose channel was not
 *  enabled during adcInit().
 * ================================================================== */

void hil_test_adc_bf_unconfigured(void)
{
    adcConfig_t config;
    memset(&config, 0, sizeof(config));

    config.vbat.enabled = true;
    config.vbat.ioTag   = HIL_ADC_TAG;

    adcInit(&config);
    hil_delay_ms(200);
    adcGetChannelValues();

    uint16_t val;

    val = adcGetValue(ADC_CURRENT);
    HIL_ASSERT_EQ(val, 0);
    HIL_INFO("adcGetValue(ADC_CURRENT)   = %u (expected 0)", (unsigned)val);

    val = adcGetValue(ADC_RSSI);
    HIL_ASSERT_EQ(val, 0);
    HIL_INFO("adcGetValue(ADC_RSSI)      = %u (expected 0)", (unsigned)val);

    val = adcGetValue(ADC_EXTERNAL1);
    HIL_ASSERT_EQ(val, 0);
    HIL_INFO("adcGetValue(ADC_EXTERNAL1) = %u (expected 0)", (unsigned)val);
}

/* ================================================================== *
 *  Test 7 -- Betaflight ADC: multi-channel configuration
 *
 *  Configures two channels (battery + current), verifies both are
 *  correctly assigned to the same ADC device, and reads back valid
 *  values from each.
 * ================================================================== */

void hil_test_adc_bf_multi_channel(void)
{
    adcConfig_t config;
    memset(&config, 0, sizeof(config));

    config.vbat.enabled    = true;
    config.vbat.ioTag      = HIL_ADC_TAG;
    config.current.enabled = true;
    config.current.ioTag   = HIL_ADC_SECOND_TAG;

    adcInit(&config);

    /* ---- Both channels enabled ---- */
    HIL_ASSERT_TRUE(adcOperatingConfig[ADC_BATTERY].enabled);
    HIL_ASSERT_TRUE(adcOperatingConfig[ADC_CURRENT].enabled);

    HIL_INFO("Battery: dev=%d  ch=%lu  dmaIdx=%u",
             (int)adcOperatingConfig[ADC_BATTERY].adcDevice,
             (unsigned long)adcOperatingConfig[ADC_BATTERY].adcChannel,
             (unsigned)adcOperatingConfig[ADC_BATTERY].dmaIndex);
    HIL_INFO("Current: dev=%d  ch=%lu  dmaIdx=%u",
             (int)adcOperatingConfig[ADC_CURRENT].adcDevice,
             (unsigned long)adcOperatingConfig[ADC_CURRENT].adcChannel,
             (unsigned)adcOperatingConfig[ADC_CURRENT].dmaIndex);

    /* ---- Both should share the same ADC device ---- */
    HIL_ASSERT_EQ((int)adcOperatingConfig[ADC_BATTERY].adcDevice,
                  (int)adcOperatingConfig[ADC_CURRENT].adcDevice);

    /* ---- DMA indices must differ ---- */
    HIL_ASSERT_NE((int)adcOperatingConfig[ADC_BATTERY].dmaIndex,
                  (int)adcOperatingConfig[ADC_CURRENT].dmaIndex);

    /* ---- Channels must differ ---- */
    HIL_ASSERT_NE(adcOperatingConfig[ADC_BATTERY].adcChannel,
                  adcOperatingConfig[ADC_CURRENT].adcChannel);

    /* ---- Wait for samples, read both channels ---- */
    hil_delay_ms(400);
    adcGetChannelValues();

    uint16_t vbat = adcGetValue(ADC_BATTERY);
    uint16_t curr = adcGetValue(ADC_CURRENT);

    HIL_INFO("adcGetValue(ADC_BATTERY) = %5u", (unsigned)vbat);
    HIL_INFO("adcGetValue(ADC_CURRENT) = %5u", (unsigned)curr);

    HIL_ASSERT_TRUE(vbat <= 4095);
    HIL_ASSERT_TRUE(curr <= 4095);
}

#else  /* !USE_ADC -- stub all tests when ADC is not compiled in */

void hil_test_adc_sdk_direct(void)
{
    HIL_SKIP("USE_ADC not defined");
}

void hil_test_adc_tag_map_lookup(void)
{
    HIL_SKIP("USE_ADC not defined");
}

#if PLATFORM_TRAIT_ADC_DEVICE
void hil_test_adc_device_by_instance(void)
{
    HIL_SKIP("USE_ADC not defined");
}
#endif

void hil_test_adc_bf_init(void)
{
    HIL_SKIP("USE_ADC not defined");
}

void hil_test_adc_bf_read(void)
{
    HIL_SKIP("USE_ADC not defined");
}

void hil_test_adc_bf_unconfigured(void)
{
    HIL_SKIP("USE_ADC not defined");
}

void hil_test_adc_bf_multi_channel(void)
{
    HIL_SKIP("USE_ADC not defined");
}

#endif /* USE_ADC */
