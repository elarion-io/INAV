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
#include "drivers/barometer/barometer_lps22hb.h"

#if defined(USE_BARO) && defined(USE_BARO_LPS22HB)

#define LPS22HB_CHIP_ID             0xB1

#define LPS22HB_REG_WHO_AM_I        0x0F
#define LPS22HB_REG_CTRL_REG1       0x10
#define LPS22HB_REG_CTRL_REG2       0x11
#define LPS22HB_REG_STATUS          0x27
#define LPS22HB_REG_PRESS_OUT_XL    0x28

#define LPS22HB_ODR_75_HZ           (0x05 << 4)
#define LPS22HB_EN_LPFP             (1 << 3)
#define LPS22HB_BDU                 (1 << 1)

#define LPS22HB_IF_ADD_INC          (1 << 4)
#define LPS22HB_SWRESET             (1 << 2)

#define LPS22HB_DATA_FRAME_SIZE     5
#define LPS22HB_RESET_TIMEOUT_MS    10

#define LPS22HB_ODR_PERIOD_US       13333

static int32_t lps22hb_up = 0;  // 1/4096 hPa
static int16_t lps22hb_ut = 0;  // 0.01 degC

static bool lps22hbDummy(baroDev_t *baro)
{
    UNUSED(baro);
    return true;
}

static bool lps22hbGetUP(baroDev_t *baro)
{
    uint8_t data[LPS22HB_DATA_FRAME_SIZE];

    if (!busReadBuf(baro->busDev, LPS22HB_REG_PRESS_OUT_XL, data, LPS22HB_DATA_FRAME_SIZE)) {
        return false;
    }

    int32_t raw = (int32_t)((uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16));
    if (raw & 0x800000) {
        raw -= 0x1000000;
    }
    lps22hb_up = raw;
    lps22hb_ut = (int16_t)((uint16_t)data[3] | ((uint16_t)data[4] << 8));

    return true;
}

static bool lps22hbCalculate(baroDev_t *baro, int32_t *pressure, int32_t *temperature)
{
    UNUSED(baro);

    if (pressure) {
        *pressure = (int32_t)(((int64_t)lps22hb_up * 100) / 4096);
    }

    if (temperature) {
        *temperature = lps22hb_ut;
    }

    return true;
}

#define DETECTION_MAX_RETRY_COUNT   3
static bool deviceDetect(busDevice_t *busDev)
{
    for (int retry = 0; retry < DETECTION_MAX_RETRY_COUNT; retry++) {
        uint8_t chipId = 0;

        delay(20);

        if (busRead(busDev, LPS22HB_REG_WHO_AM_I, &chipId) && chipId == LPS22HB_CHIP_ID) {
            return true;
        }
    }

    return false;
}

static bool lps22hbConfigure(busDevice_t *busDev)
{
    uint8_t ctrl2 = 0;

    busWrite(busDev, LPS22HB_REG_CTRL_REG2, LPS22HB_SWRESET);

    const timeMs_t start = millis();
    do {
        if (millis() - start > LPS22HB_RESET_TIMEOUT_MS) {
            return false;
        }
        busRead(busDev, LPS22HB_REG_CTRL_REG2, &ctrl2);
    } while (ctrl2 & LPS22HB_SWRESET);

    busWrite(busDev, LPS22HB_REG_CTRL_REG2, LPS22HB_IF_ADD_INC);
    busWrite(busDev, LPS22HB_REG_CTRL_REG1, LPS22HB_ODR_75_HZ | LPS22HB_EN_LPFP | LPS22HB_BDU);

    return true;
}

bool lps22hbDetect(baroDev_t *baro)
{
    baro->busDev = busDeviceInit(BUSTYPE_ANY, DEVHW_LPS22HB, 0, OWNER_BARO);
    if (baro->busDev == NULL) {
        return false;
    }

    busSetSpeed(baro->busDev, BUS_SPEED_STANDARD);

    if (!deviceDetect(baro->busDev) || !lps22hbConfigure(baro->busDev)) {
        busDeviceDeInit(baro->busDev);
        return false;
    }

    baro->ut_delay = 0;
    baro->start_ut = lps22hbDummy;
    baro->get_ut = lps22hbDummy;

    baro->up_delay = LPS22HB_ODR_PERIOD_US;
    baro->start_up = lps22hbDummy;
    baro->get_up = lps22hbGetUP;

    baro->calculate = lps22hbCalculate;

    return true;
}

#endif
