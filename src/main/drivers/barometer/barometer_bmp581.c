/*
 * This file is part of INAV.
 *
 * INAV is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * INAV is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with INAV. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdint.h>

#include <platform.h>
#include "build/build_config.h"
#include "common/utils.h"

#include "drivers/time.h"
#include "drivers/bus.h"
#include "drivers/barometer/barometer.h"
#include "drivers/barometer/barometer_bmp581.h"

#if defined(USE_BARO) && defined(USE_BARO_BMP581)

#define BMP580_CHIP_ID              0x50
#define BMP581_CHIP_ID              0x51

#define BMP581_REG_CHIP_ID          0x01
#define BMP581_REG_TEMP_DATA_XLSB   0x1D
#define BMP581_REG_STATUS           0x28
#define BMP581_REG_DSP_CONFIG       0x30
#define BMP581_REG_DSP_IIR          0x31
#define BMP581_REG_OSR_CONFIG       0x36
#define BMP581_REG_ODR_CONFIG       0x37
#define BMP581_REG_CMD              0x7E

#define BMP581_CMD_SOFT_RESET       0xB6

#define BMP581_STATUS_NVM_RDY       (1 << 1)
#define BMP581_STATUS_NVM_ERR       (1 << 2)

#define BMP581_MODE_STANDBY         0x00
#define BMP581_MODE_NORMAL          0x01
#define BMP581_DEEP_STANDBY_DISABLE (1 << 7)
#define BMP581_ODR_50_HZ            (0x0F << 2)

#define BMP581_OSR_T_4X             0x02
#define BMP581_OSR_P_64X            (0x06 << 3)
#define BMP581_OSR_PRESS_EN         (1 << 6)

#define BMP581_DSP_COMP_PRESS_TEMP  0x03
#define BMP581_DSP_SHDW_SEL_IIR_T   (1 << 3)
#define BMP581_DSP_SHDW_SEL_IIR_P   (1 << 5)

#define BMP581_IIR_COEF_3           0x02
#define BMP581_IIR                  (BMP581_IIR_COEF_3 | (BMP581_IIR_COEF_3 << 3))

#define BMP581_DATA_FRAME_SIZE      6
#define BMP581_DATA_NOT_READY_BYTE  0x7F

#define BMP581_ODR_PERIOD_US        20000

static int32_t bmp581_ut = 0;   // 1/65536 degC
static uint32_t bmp581_up = 0;  // 1/64 Pa

static bool bmp581Dummy(baroDev_t *baro)
{
    UNUSED(baro);
    return true;
}

static bool bmp581GetUP(baroDev_t *baro)
{
    uint8_t data[BMP581_DATA_FRAME_SIZE];

    if (!busReadBuf(baro->busDev, BMP581_REG_TEMP_DATA_XLSB, data, BMP581_DATA_FRAME_SIZE)) {
        return false;
    }

    if (data[0] != BMP581_DATA_NOT_READY_BYTE || data[1] != BMP581_DATA_NOT_READY_BYTE || data[2] != BMP581_DATA_NOT_READY_BYTE) {
        int32_t raw = (int32_t)((uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16));
        if (raw & 0x800000) {
            raw -= 0x1000000;
        }
        bmp581_ut = raw;
    }

    if (data[3] != BMP581_DATA_NOT_READY_BYTE || data[4] != BMP581_DATA_NOT_READY_BYTE || data[5] != BMP581_DATA_NOT_READY_BYTE) {
        bmp581_up = (uint32_t)data[3] | ((uint32_t)data[4] << 8) | ((uint32_t)data[5] << 16);
    }

    return true;
}

static bool bmp581Calculate(baroDev_t *baro, int32_t *pressure, int32_t *temperature)
{
    UNUSED(baro);

    if (pressure) {
        *pressure = (int32_t)(bmp581_up / 64);
    }

    if (temperature) {
        *temperature = (int32_t)(((int64_t)bmp581_ut * 100) / 65536);
    }

    return true;
}

#define DETECTION_MAX_RETRY_COUNT   3
static bool deviceDetect(busDevice_t *busDev)
{
    for (int retry = 0; retry < DETECTION_MAX_RETRY_COUNT; retry++) {
        uint8_t chipId = 0;

        delay(20);

        if (busRead(busDev, BMP581_REG_CHIP_ID, &chipId) && (chipId == BMP581_CHIP_ID || chipId == BMP580_CHIP_ID)) {
            return true;
        }
    }

    return false;
}

static bool bmp581Configure(busDevice_t *busDev)
{
    uint8_t status = 0;

    busWrite(busDev, BMP581_REG_CMD, BMP581_CMD_SOFT_RESET);
    delay(5);

    if (!busRead(busDev, BMP581_REG_STATUS, &status) || !(status & BMP581_STATUS_NVM_RDY) || (status & BMP581_STATUS_NVM_ERR)) {
        return false;
    }

    busWrite(busDev, BMP581_REG_ODR_CONFIG, BMP581_DEEP_STANDBY_DISABLE | BMP581_ODR_50_HZ | BMP581_MODE_STANDBY);
    busWrite(busDev, BMP581_REG_DSP_CONFIG, BMP581_DSP_COMP_PRESS_TEMP | BMP581_DSP_SHDW_SEL_IIR_P | BMP581_DSP_SHDW_SEL_IIR_T);
    busWrite(busDev, BMP581_REG_DSP_IIR, BMP581_IIR);
    busWrite(busDev, BMP581_REG_OSR_CONFIG, BMP581_OSR_P_64X | BMP581_OSR_T_4X | BMP581_OSR_PRESS_EN);
    busWrite(busDev, BMP581_REG_ODR_CONFIG, BMP581_DEEP_STANDBY_DISABLE | BMP581_ODR_50_HZ | BMP581_MODE_NORMAL);

    return true;
}

bool bmp581Detect(baroDev_t *baro)
{
    baro->busDev = busDeviceInit(BUSTYPE_ANY, DEVHW_BMP581, 0, OWNER_BARO);
    if (baro->busDev == NULL) {
        return false;
    }

    busSetSpeed(baro->busDev, BUS_SPEED_STANDARD);

    if (!deviceDetect(baro->busDev) || !bmp581Configure(baro->busDev)) {
        busDeviceDeInit(baro->busDev);
        return false;
    }

    baro->ut_delay = 0;
    baro->start_ut = bmp581Dummy;
    baro->get_ut = bmp581Dummy;

    baro->up_delay = BMP581_ODR_PERIOD_US;
    baro->start_up = bmp581Dummy;
    baro->get_up = bmp581GetUP;

    baro->calculate = bmp581Calculate;

    return true;
}

#endif
