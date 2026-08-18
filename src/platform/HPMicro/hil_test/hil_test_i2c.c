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

#include "drivers/bus_i2c.h"
#include "drivers/bus_i2c_impl.h"
#include "drivers/io.h"
#include "drivers/io_impl.h"
#include "../io_hpmicro.h"
#include "hpm_ioc_regs.h"
#include "hil_test.h"

/* ================================================================== *
 *  Shared helpers
 * ================================================================== */

extern i2cDevice_t i2cDevice[];
extern const i2cHardware_t i2cHardware[];

static void hil_i2c_print_ioc_pin(const char *label, ioTag_t tag)
{
    IO_t io = IOGetByTag(tag);
    if (!io) {
        printf("#   %-4s: IOGetByTag returned NULL\n", label);
        return;
    }
    uint32_t ioc_idx = IOCIndex(io);
    uint32_t func_ctl = HPM_IOC->PAD[ioc_idx].FUNC_CTL;
    uint32_t pad_ctl  = HPM_IOC->PAD[ioc_idx].PAD_CTL;

    printf("#   %-4s  IOC_PAD=%-3lu  FUNC_CTL=0x%08lX  PAD_CTL=0x%08lX",
           label, (unsigned long)ioc_idx, (unsigned long)func_ctl, (unsigned long)pad_ctl);

    printf("  [");
    if (pad_ctl & IOC_PAD_PAD_CTL_PE_MASK)    printf(" PE");
    if (pad_ctl & IOC_PAD_PAD_CTL_PS_MASK)    printf(" PS(up)");
    else                                      printf(" PS(down)");
    if (pad_ctl & IOC_PAD_PAD_CTL_OD_MASK)    printf(" OD");
    printf(" DS=%lu ]\n", IOC_PAD_PAD_CTL_DS_GET(pad_ctl));

    printf("#        FUNC_CTL decode: ALT_SELECT=%lu",
           IOC_PAD_FUNC_CTL_ALT_SELECT_GET(func_ctl));
    if (func_ctl & IOC_PAD_FUNC_CTL_LOOP_BACK_MASK) printf(" LOOP_BACK");
    if (func_ctl & IOC_PAD_FUNC_CTL_ANALOG_MASK)    printf(" ANALOG");
    printf("\n");
}

static void hil_i2c_print_ioc(const char *bus_name, ioTag_t scl_tag, ioTag_t sda_tag)
{
    printf("# --- %s IOC registers ---\n", bus_name);
    hil_i2c_print_ioc_pin("SCL", scl_tag);
    hil_i2c_print_ioc_pin("SDA", sda_tag);
    printf("#\n");
}

/* Generic I2C bus setup */
static bool hil_i2c_setup(i2cDevice_e dev, ioTag_t scl_tag, ioTag_t sda_tag,
                          uint8_t scl_af, uint8_t sda_af,
                          const char *bus_name, bool *initialized)
{
    if (*initialized) {
        return true;
    }

    i2cDevice_t *pDev = &i2cDevice[dev];

    memset(pDev, 0, sizeof(*pDev));

    pDev->scl       = IOGetByTag(scl_tag);
    pDev->sda       = IOGetByTag(sda_tag);
    pDev->sclAF     = scl_af;
    pDev->sdaAF     = sda_af;
    pDev->hardware  = &i2cHardware[dev];
    pDev->reg       = i2cHardware[dev].reg;
    pDev->pullUp    = true;

    HIL_INFO("%s: SCL tag=0x%02X io=%p  SDA tag=0x%02X io=%p",
             bus_name, scl_tag, (void *)pDev->scl, sda_tag, (void *)pDev->sda);

    if (!pDev->scl || !pDev->sda) {
        HIL_FAIL("pin lookup failed");
        return false;
    }
    if (!pDev->hardware || !pDev->reg) {
        HIL_FAIL("hardware/reg lookup failed");
        return false;
    }

    hil_i2c_print_ioc(bus_name, scl_tag, sda_tag);

    i2cInit(dev);

    hil_i2c_print_ioc(bus_name, scl_tag, sda_tag);

    bool error = false;
    bool busy = i2cBusy(dev, &error);
    HIL_INFO("%s: i2cBusy=%d error=%d errCount=%d",
             bus_name, busy, error, i2cGetErrorCounter());
    if (busy) {
        HIL_FAIL("bus reports busy after init");
        return false;
    }

    *initialized = true;
    return true;
}

/* ------------------------------------------------------------------ *
 *  Diagnostic helpers using Betaflight I2C API only
 * ------------------------------------------------------------------ */

static bool i2c_write_check(i2cDevice_e dev, uint8_t addr, uint8_t reg, uint8_t data)
{
    uint16_t err_before = i2cGetErrorCounter();

    if (i2cWrite(dev, addr, reg, data)) {
        return true;
    }

    uint16_t err_after = i2cGetErrorCounter();
    bool error = false;
    bool busy = i2cBusy(dev, &error);

    HIL_INFO("i2cWrite(addr=0x%02X reg=0x%02X) FAILED: busy=%d error=%d errB=%u errA=%u",
             addr, reg, busy, error, err_before, err_after);
    if (err_after > err_before) {
        HIL_INFO("I2C error counter incremented — hardware-level failure");
    }
    return false;
}

static bool i2c_read_check(i2cDevice_e dev, uint8_t addr, uint8_t reg,
                           uint8_t *buf, uint8_t len)
{
    uint16_t err_before = i2cGetErrorCounter();

    if (i2cRead(dev, addr, reg, len, buf)) {
        return true;
    }

    uint16_t err_after = i2cGetErrorCounter();
    bool error = false;
    bool busy = i2cBusy(dev, &error);

    HIL_INFO("i2cRead(addr=0x%02X reg=0x%02X len=%u) FAILED: busy=%d error=%d errB=%u errA=%u",
             addr, reg, len, busy, error, err_before, err_after);
    if (err_after > err_before) {
        HIL_INFO("I2C error counter incremented — hardware-level failure");
    }
    return false;
}

/* ================================================================== *
 *  MS5611 barometer  (I2CDEV_1 / HPM_I2C0 / PA23+PA24 / addr 0x77)
 * ================================================================== */

#define BARO_DEV             I2CDEV_1
#define BARO_I2C_ADDR        0x77
#define BARO_SCL_TAG         DEFIO_TAG_E__PA23
#define BARO_SDA_TAG         DEFIO_TAG_E__PA24
#define BARO_SCL_AF          IOC_PA23_FUNC_CTL_I2C0_SCL
#define BARO_SDA_AF          IOC_PA24_FUNC_CTL_I2C0_SDA

#define MS5611_CMD_RESET     0x1E
#define MS5611_CMD_PROM_RD   0xA0
#define MS5611_CMD_ADC_READ  0x00
#define MS5611_CMD_ADC_CONV  0x40
#define MS5611_CMD_ADC_D1    0x00
#define MS5611_CMD_ADC_D2    0x10
#define MS5611_CMD_ADC_4096  0x08
#define MS5611_PROM_NB       8

static bool baro_initialized;

static bool baro_setup(void)
{
    return hil_i2c_setup(BARO_DEV, BARO_SCL_TAG, BARO_SDA_TAG,
                         BARO_SCL_AF, BARO_SDA_AF,
                         "I2C1/BARO", &baro_initialized);
}

static uint16_t ms5611_read_prom(uint8_t coef_num)
{
    uint8_t buf[2] = {0};
    uint8_t cmd = MS5611_CMD_PROM_RD + coef_num * 2;
    if (!i2c_read_check(BARO_DEV, BARO_I2C_ADDR, cmd, buf, 2)) {
        HIL_FAIL("MS5611 PROM read failed");
        return 0xFFFF;
    }
    return ((uint16_t)buf[0] << 8) | buf[1];
}

static int8_t ms5611_crc4(uint16_t *prom)
{
    int32_t i, j;
    uint32_t res = 0;
    uint8_t crc = prom[7] & 0xF;
    prom[7] &= 0xFF00;

    bool blankEeprom = true;
    for (i = 0; i < 16; i++) {
        if (prom[i >> 1]) blankEeprom = false;
        if (i & 1)
            res ^= ((prom[i >> 1]) & 0x00FF);
        else
            res ^= (prom[i >> 1] >> 8);
        for (j = 8; j > 0; j--) {
            if (res & 0x8000) res ^= 0x1800;
            res <<= 1;
        }
    }
    prom[7] |= crc;
    if (!blankEeprom && crc == ((res >> 12) & 0xF)) return 0;
    return -1;
}

/* ================================================================== *
 *  MMC5983 magnetometer  (I2CDEV_2 / HPM_I2C1 / PA25+PA26 / addr 0x30)
 * ================================================================== */

#define MAG_DEV              I2CDEV_2
#define MAG_I2C_ADDR         0x30
#define MAG_SCL_TAG          DEFIO_TAG_E__PA25
#define MAG_SDA_TAG          DEFIO_TAG_E__PA26
#define MAG_SCL_AF           IOC_PA25_FUNC_CTL_I2C1_SCL
#define MAG_SDA_AF           IOC_PA26_FUNC_CTL_I2C1_SDA

#define MMC5983_REG_PRODUCT_ID   0x2F
#define MMC5983_EXPECTED_ID      0x30
#define MMC5983_REG_STATUS       0x08
#define MMC5983_REG_CTRL0        0x09
#define MMC5983_REG_CTRL1        0x0A
#define MMC5983_REG_DATA         0x00

#define MMC5983_CMD_SW_RST       0x80
#define MMC5983_CMD_SET          0x08

#define MMC5983_STATUS_OTP_READ_DONE  0x10
#define MMC5983_STATUS_MDONE          0x01

static bool mag_initialized;

static bool mag_setup(void)
{
    return hil_i2c_setup(MAG_DEV, MAG_SCL_TAG, MAG_SDA_TAG,
                         MAG_SCL_AF, MAG_SDA_AF,
                         "I2C2/MAG", &mag_initialized);
}

/* ================================================================== *
 *  Test: MS5611 PROM read + CRC
 * ================================================================== */

void hil_test_i2c_ms5611_prom(void)
{
    if (!baro_setup()) return;

    /* Reset the sensor */
    if (!i2c_write_check(BARO_DEV, BARO_I2C_ADDR, MS5611_CMD_RESET, 0x00)) {
        HIL_FAIL("MS5611 reset failed — device not responding on I2C1");
        return;
    }
    hil_delay_ms(3);

    /* Quick presence check */
    uint16_t c0 = ms5611_read_prom(0);
    HIL_INFO("PROM[0] = 0x%04X", c0);
    HIL_ASSERT_NE(c0, 0x0000);
    HIL_ASSERT_NE(c0, 0xFFFF);

    /* Read all 8 PROM coefficients */
    uint16_t prom[MS5611_PROM_NB];
    for (int i = 0; i < MS5611_PROM_NB; i++) {
        prom[i] = ms5611_read_prom(i);
        HIL_INFO("PROM[%d] = 0x%04X", i, prom[i]);
    }

    for (int i = 0; i < MS5611_PROM_NB; i++) {
        if (i == 7) {
            HIL_ASSERT_NE(prom[i] & 0xFF00, 0x0000);
        } else {
            HIL_ASSERT_NE(prom[i], 0x0000);
        }
    }

    int8_t crc_result = ms5611_crc4(prom);
    HIL_ASSERT_EQ(crc_result, 0);
}

/* ================================================================== *
 *  Test: MS5611 D1/D2 ADC data
 * ================================================================== */

void hil_test_i2c_ms5611_data(void)
{
    if (!baro_setup()) return;

    if (!i2c_write_check(BARO_DEV, BARO_I2C_ADDR, MS5611_CMD_RESET, 0x00)) {
        HIL_FAIL("MS5611 reset failed");
        return;
    }
    hil_delay_ms(3);

    /* Quick PROM read to confirm alive */
    uint16_t c0 = ms5611_read_prom(0);
    HIL_INFO("PROM[0] = 0x%04X", c0);
    HIL_ASSERT_NE(c0, 0x0000);
    HIL_ASSERT_NE(c0, 0xFFFF);

    /* D2 (temperature) */
    uint8_t conv_cmd = MS5611_CMD_ADC_CONV | MS5611_CMD_ADC_D2 | MS5611_CMD_ADC_4096;
    if (!i2c_write_check(BARO_DEV, BARO_I2C_ADDR, conv_cmd, 0x00)) {
        HIL_FAIL("D2 conversion start failed");
        return;
    }
    hil_delay_ms(10);

    uint8_t buf[3] = {0};
    if (!i2c_read_check(BARO_DEV, BARO_I2C_ADDR, MS5611_CMD_ADC_READ, buf, 3)) {
        HIL_FAIL("D2 ADC read failed");
        return;
    }
    uint32_t d2_raw = ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];
    HIL_INFO("D2 (temp) raw = %lu (0x%06lX)", (unsigned long)d2_raw, (unsigned long)d2_raw);
    HIL_ASSERT_NE(d2_raw, 0x000000);
    HIL_ASSERT_NE(d2_raw, 0xFFFFFF);

    /* D1 (pressure) */
    conv_cmd = MS5611_CMD_ADC_CONV | MS5611_CMD_ADC_D1 | MS5611_CMD_ADC_4096;
    if (!i2c_write_check(BARO_DEV, BARO_I2C_ADDR, conv_cmd, 0x00)) {
        HIL_FAIL("D1 conversion start failed");
        return;
    }
    hil_delay_ms(10);

    if (!i2c_read_check(BARO_DEV, BARO_I2C_ADDR, MS5611_CMD_ADC_READ, buf, 3)) {
        HIL_FAIL("D1 ADC read failed");
        return;
    }
    uint32_t d1_raw = ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];
    HIL_INFO("D1 (press) raw = %lu (0x%06lX)", (unsigned long)d1_raw, (unsigned long)d1_raw);
    HIL_ASSERT_NE(d1_raw, 0x000000);
    HIL_ASSERT_NE(d1_raw, 0xFFFFFF);

    HIL_ASSERT_TRUE(d1_raw > 100000);
    HIL_ASSERT_TRUE(d2_raw > 100000);
}

/* ================================================================== *
 *  Test: MMC5983 Product ID
 * ================================================================== */

void hil_test_i2c_mmc5983_id(void)
{
    if (!mag_setup()) return;

    uint8_t id = 0;
    if (!i2c_read_check(MAG_DEV, MAG_I2C_ADDR, MMC5983_REG_PRODUCT_ID, &id, 1)) {
        HIL_FAIL("MMC5983 Product ID read failed — device not responding on I2C2");
        return;
    }

    HIL_INFO("MMC5983 Product ID = 0x%02X (expected 0x%02X)", id, MMC5983_EXPECTED_ID);
    HIL_ASSERT_EQ(id, MMC5983_EXPECTED_ID);
}

/* ================================================================== *
 *  Test: MMC5983 status + magnetometer data
 * ================================================================== */

void hil_test_i2c_mmc5983_data(void)
{
    if (!mag_setup()) return;

    /* Read STATUS */
    uint8_t status = 0;
    if (!i2c_read_check(MAG_DEV, MAG_I2C_ADDR, MMC5983_REG_STATUS, &status, 1)) {
        HIL_FAIL("Cannot read MMC5983 STATUS");
        return;
    }
    HIL_INFO("STATUS=0x%02X OTP_READY=%d MDONE=%d", status,
             (status & MMC5983_STATUS_OTP_READ_DONE) ? 1 : 0,
             (status & MMC5983_STATUS_MDONE) ? 1 : 0);

    /* SW reset if OTP not ready */
    if (!(status & MMC5983_STATUS_OTP_READ_DONE)) {
        HIL_INFO("OTP not ready — SW reset");
        if (!i2c_write_check(MAG_DEV, MAG_I2C_ADDR, MMC5983_REG_CTRL1, MMC5983_CMD_SW_RST)) {
            HIL_FAIL("SW reset failed");
            return;
        }
        hil_delay_ms(50);
        if (!i2c_read_check(MAG_DEV, MAG_I2C_ADDR, MMC5983_REG_STATUS, &status, 1)) {
            HIL_FAIL("STATUS after reset failed");
            return;
        }
        HIL_INFO("STATUS after reset=0x%02X OTP_READY=%d", status,
                 (status & MMC5983_STATUS_OTP_READ_DONE) ? 1 : 0);
        HIL_ASSERT_TRUE((status & MMC5983_STATUS_OTP_READ_DONE) != 0);
    }

    /* SET to trigger a measurement */
    HIL_INFO("Issuing SET");
    if (!i2c_write_check(MAG_DEV, MAG_I2C_ADDR, MMC5983_REG_CTRL0, MMC5983_CMD_SET)) {
        HIL_FAIL("SET command failed");
        return;
    }
    hil_delay_ms(1);

    /* Wait for MDONE */
    int timeout = 20;
    bool mdone = false;
    while (timeout-- > 0) {
        if (!i2c_read_check(MAG_DEV, MAG_I2C_ADDR, MMC5983_REG_STATUS, &status, 1)) break;
        if (status & MMC5983_STATUS_MDONE) { mdone = true; break; }
    }
    HIL_INFO("After SET: STATUS=0x%02X MDONE=%d", status,
             (status & MMC5983_STATUS_MDONE) ? 1 : 0);
    HIL_ASSERT_TRUE(mdone);

    /* Read 3-axis data */
    uint8_t buf[6] = {0};
    if (!i2c_read_check(MAG_DEV, MAG_I2C_ADDR, MMC5983_REG_DATA, buf, 6)) {
        HIL_FAIL("Mag data read failed");
        return;
    }
    int16_t mx = (int16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    int16_t my = (int16_t)(((uint16_t)buf[2] << 8) | buf[3]);
    int16_t mz = (int16_t)(((uint16_t)buf[4] << 8) | buf[5]);
    HIL_INFO("Mag: X=%d Y=%d Z=%d", mx, my, mz);
    HIL_ASSERT_FALSE(mx == 0 && my == 0 && mz == 0);
}
