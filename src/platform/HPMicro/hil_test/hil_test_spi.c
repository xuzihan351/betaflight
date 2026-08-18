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

#include "drivers/bus_spi.h"
#include "drivers/bus_spi_impl.h"
#include "drivers/io.h"
#include "drivers/io_impl.h"
#include "../io_hpmicro.h"
#include "hpm_ioc_regs.h"
#include "hil_test.h"

/* ------------------------------------------------------------------ *
 *  SPI1 -- IMU6200AL  (ICM-42688-P)  ->  SPIDEV_1  ->  HPM_SPI0
 *
 *  WHO_AM_I: reg 0x75 (read: 0x75 | 0x80 = 0xF5)
 *  Expected: 0x47
 * ------------------------------------------------------------------ */

#define SPI1_DEV            SPIDEV_1
#define SPI1_INSTANCE       ((spiResource_t *)SPI1)   /* SPI1 -> HPM_SPI0 */
#define SPI1_SCK_TAG        DEFIO_TAG_E__PA30
#define SPI1_SDO_TAG        DEFIO_TAG_E__PA29
#define SPI1_SDI_TAG        DEFIO_TAG_E__PA31
#define SPI1_CS_TAG         DEFIO_TAG_E__PA28
#define SPI1_SCK_AF         5   /* IOC_PA30_FUNC_CTL_SPI0_SCLK */
#define SPI1_SDO_AF         5   /* IOC_PA29_FUNC_CTL_SPI0_MISO */
#define SPI1_SDI_AF         5   /* IOC_PA31_FUNC_CTL_SPI0_MOSI */
#define SPI1_RCC            clock_spi0
#define SPI1_WHO_AM_I_REG   0x00
#define SPI1_WHO_AM_I_EXP   0xF9

/* ------------------------------------------------------------------ *
 *  Reference the global spiDevice[] from bus_spi_impl.h
 * ------------------------------------------------------------------ */

extern spiDevice_t spiDevice[SPIDEV_COUNT];

// void spiWriteRegMic6200(const extDevice_t *dev, uint8_t reg, uint8_t data)
// {
//     // This routine blocks so no need to use static data
//     uint8_t tmp[3] = {reg & 0x7F, reg & 0x80, data};
//     busDevice_t *bus = dev->bus;
//     SPI_Type *instance = bus->busType_u.spi.instance;
//     IOLo(dev->busType_u.spi.csnPin);
//     uint32_t state;
//     state = hpm_spi_transmit_blocking(instance, (uint8_t *)&tmp, 3, 0xFFFFFFFF);
//     if (state != status_success) {
//         //while (1);
//     }
//     IOHi(dev->busType_u.spi.csnPin);
// }

extern uint8_t spiReadRegMic6200(const extDevice_t *dev, uint8_t reg);
// {
//     uint8_t data[3] = { 0 };
//     uint8_t regs[3] = { reg | 0x80, reg & 0x80};
//     IOLo(dev->busType_u.spi.csnPin);
//     busDevice_t *bus = dev->bus;
//     SPI_Type *instance = bus->busType_u.spi.instance;

//     if (hpm_spi_transmit_receive_blocking(instance, (uint8_t *)&regs, (uint8_t *)&data[0], 3, 0xFFFFFFFF) != status_success) {
//         //printf("hpm_spi_transmit_receive_blocking fail\n");
//         //while (1);
//     }
//     IOHi(dev->busType_u.spi.csnPin);
//     return data[2];
// }

/* ------------------------------------------------------------------ *
 *  Print IOC pad register values for a SPI pin
 * ------------------------------------------------------------------ */

static void hil_spi_print_ioc_pin(const char *label, ioTag_t tag)
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
           label, (unsigned long)ioc_idx, func_ctl, pad_ctl);

    // Decode key PAD_CTL fields
    printf("  [");
    if (pad_ctl & IOC_PAD_PAD_CTL_PE_MASK)    printf(" PE");
    if (pad_ctl & IOC_PAD_PAD_CTL_PS_MASK)    printf(" PS(up)");
    else                                      printf(" PS(down)");
    if (pad_ctl & IOC_PAD_PAD_CTL_OD_MASK)    printf(" OD");
#ifdef IOC_PAD_PAD_CTL_SMT_MASK
    if (pad_ctl & IOC_PAD_PAD_CTL_SMT_MASK)   printf(" SMT");
#endif
#ifdef IOC_PAD_PAD_CTL_MS_MASK
    if (pad_ctl & IOC_PAD_PAD_CTL_MS_MASK)    printf(" MS(1.8V)");
#endif
    printf(" DS=%lu ]\n", IOC_PAD_PAD_CTL_DS_GET(pad_ctl));

    // Decode key FUNC_CTL fields
    printf("#        FUNC_CTL decode: ALT_SELECT=%lu",
           IOC_PAD_FUNC_CTL_ALT_SELECT_GET(func_ctl));
    if (func_ctl & IOC_PAD_FUNC_CTL_LOOP_BACK_MASK) printf(" LOOP_BACK");
    if (func_ctl & IOC_PAD_FUNC_CTL_ANALOG_MASK)    printf(" ANALOG");
    printf("\n");
}

static void hil_spi_print_ioc(const char *name,
                               ioTag_t sck, ioTag_t miso, ioTag_t mosi,
                               ioTag_t cs)
{
    printf("# --- %s IOC registers ---\n", name);
    hil_spi_print_ioc_pin("SCK",  sck);
    hil_spi_print_ioc_pin("MISO", miso);
    hil_spi_print_ioc_pin("MOSI", mosi);
    hil_spi_print_ioc_pin("CS",   cs);
    printf("#\n");
}

/* ------------------------------------------------------------------ *
 *  Configure a SPI device and initialise it.
 *
 *  Populates spiDevice[dev] directly, bypassing the PG / spiPinConfigure
 *  system, since HIL tests don't have a full config loaded.
 * ------------------------------------------------------------------ */

static void hil_spi_setup(spiDevice_e dev,
                          spiResource_t *instance,
                          ioTag_t sck_tag, uint8_t sck_af,
                          ioTag_t sdo_tag, uint8_t sdo_af,
                          ioTag_t sdi_tag, uint8_t sdi_af,
                          rccPeriphTag_t rcc)
{
    spiDevice_t *spi = &spiDevice[dev];

    spi->dev     = instance;
    spi->sck     = sck_tag;
    spi->miso    = sdo_tag;
    spi->mosi    = sdi_tag;
    spi->sckAF   = sck_af;
    spi->misoAF  = sdo_af;
    spi->mosiAF  = sdi_af;
    spi->rcc     = rcc;
    spi->leadingEdge = false;

    spiInit(dev);
}

/* ------------------------------------------------------------------ *
 *  Test helper: setup, read WHO_AM_I via bus API, verify
 * ------------------------------------------------------------------ */

static void hil_spi_test_one(const char *name,
                             spiDevice_e dev,
                             spiResource_t *instance,
                             ioTag_t sck_tag, uint8_t sck_af,
                             ioTag_t sdo_tag, uint8_t sdo_af,
                             ioTag_t sdi_tag, uint8_t sdi_af,
                             rccPeriphTag_t rcc,
                             ioTag_t cs_tag,
                             uint8_t who_am_i_reg, uint8_t expected,
                             uint8_t (*readReg)(const extDevice_t *dev, uint8_t reg))
{
    /* Setup SPI device */
    hil_spi_setup(dev, instance,
                  sck_tag, sck_af,
                  sdo_tag, sdo_af,
                  sdi_tag, sdi_af,
                  rcc);

    /* Print IOC pad register values after init */
    hil_spi_print_ioc(name, sck_tag, sdo_tag, sdi_tag, cs_tag);

    /* Set up external device for bus API (spiReadReg / spiSequence / spiWait) */
    extDevice_t extDev = {0};
    extDev.busType_u.spi.csnPin = IOGetByTag(cs_tag);
    if (!extDev.busType_u.spi.csnPin) return;
    IOInit(extDev.busType_u.spi.csnPin, OWNER_SPI_CS, 0);
    IOConfigGPIO(extDev.busType_u.spi.csnPin, IOCFG_OUT_PP);
    IOHi(extDev.busType_u.spi.csnPin);   /* CS inactive (high) */

    extDev.busType_u.spi.speed = spiCalculateDivider(12000000);  /* divisor, not Hz */
    extDev.busType_u.spi.leadingEdge = false;

    if (!spiSetBusInstance(&extDev, dev + 1)) {  /* +1 for 1-based config index */
        printf("# ERROR: spiSetBusInstance failed\n");
        return;
    }
    extDev.useDMA = false;  /* force polling mode */

    /* Read WHO_AM_I using the selected register-read interface */
    uint8_t id = readReg(&extDev, who_am_i_reg);

    HIL_INFO("%s WHO_AM_I (reg 0x%02X) = 0x%02X  (expected 0x%02X)",
             name, who_am_i_reg, id, expected);

    HIL_ASSERT_EQ(id, expected);
}

/* ------------------------------------------------------------------ *
 *  Test: SPI1 -- IMU6200AL WHO_AM_I
 * ------------------------------------------------------------------ */

void hil_test_spi1_who_am_i(void)
{
    hil_spi_test_one("SPI1/IMU6200AL",
                     SPI1_DEV, SPI1_INSTANCE,
                     SPI1_SCK_TAG, SPI1_SCK_AF,
                     SPI1_SDO_TAG, SPI1_SDO_AF,
                     SPI1_SDI_TAG, SPI1_SDI_AF,
                     SPI1_RCC,
                     SPI1_CS_TAG,
                     SPI1_WHO_AM_I_REG, SPI1_WHO_AM_I_EXP,
                     spiReadRegMic6200);
}
