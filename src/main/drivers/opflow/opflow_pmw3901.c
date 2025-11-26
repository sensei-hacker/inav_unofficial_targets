/*
 * This file is part of INAV Project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Alternatively, the contents of this file may be used under the terms
 * of the GNU General Public License Version 3, as described below:
 *
 * This file is free software: you may copy, redistribute and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see http://www.gnu.org/licenses/.
 */

#include <stdbool.h>
#include <stdint.h>

#include "platform.h"

#ifdef USE_OPFLOW_PMW3901

#include "build/build_config.h"
#include "build/debug.h"

#include "common/maths.h"
#include "common/utils.h"

#include "drivers/bus.h"
#include "drivers/io.h"
#include "drivers/time.h"

#include "drivers/opflow/opflow_virtual.h"
#include "drivers/opflow/opflow_pmw3901.h"

/* PMW3901 register addresses */
#define PMW3901_REG_PRODUCT_ID      0x00
#define PMW3901_REG_REVISION_ID     0x01
#define PMW3901_REG_MOTION          0x02
#define PMW3901_REG_DELTA_X_L       0x03
#define PMW3901_REG_DELTA_X_H       0x04
#define PMW3901_REG_DELTA_Y_L       0x05
#define PMW3901_REG_DELTA_Y_H       0x06
#define PMW3901_REG_SQUAL           0x07
#define PMW3901_REG_RAW_DATA_SUM    0x08
#define PMW3901_REG_MAX_RAW_DATA    0x09
#define PMW3901_REG_MIN_RAW_DATA    0x0A
#define PMW3901_REG_SHUTTER_LOWER   0x0B
#define PMW3901_REG_SHUTTER_UPPER   0x0C
#define PMW3901_REG_OBSERVATION     0x15
#define PMW3901_REG_MOTION_BURST    0x16
#define PMW3901_REG_POWER_UP_RESET  0x3A
#define PMW3901_REG_SHUTDOWN        0x3B
#define PMW3901_REG_INVERSE_PROD_ID 0x5F

/* Expected chip IDs */
#define PMW3901_PRODUCT_ID          0x49
#define PMW3901_INVERSE_PRODUCT_ID  0xB6

/* Motion register bits */
#define PMW3901_MOTION_DETECTED     0x80

/* SPI read/write flags */
#define PMW3901_SPI_READ            0x00
#define PMW3901_SPI_WRITE           0x80

static busDevice_t *busDev;

static bool pmw3901WriteRegister(uint8_t reg, uint8_t value)
{
    return busWrite(busDev, reg | PMW3901_SPI_WRITE, value);
}

static bool pmw3901ReadRegister(uint8_t reg, uint8_t *value)
{
    return busRead(busDev, reg | PMW3901_SPI_READ, value);
}

static void pmw3901InitSequence(void)
{
    /* Power-up reset */
    pmw3901WriteRegister(PMW3901_REG_POWER_UP_RESET, 0x5A);
    delay(50);

    /* Read motion registers to clear them */
    uint8_t dummy;
    pmw3901ReadRegister(PMW3901_REG_MOTION, &dummy);
    pmw3901ReadRegister(PMW3901_REG_DELTA_X_L, &dummy);
    pmw3901ReadRegister(PMW3901_REG_DELTA_X_H, &dummy);
    pmw3901ReadRegister(PMW3901_REG_DELTA_Y_L, &dummy);
    pmw3901ReadRegister(PMW3901_REG_DELTA_Y_H, &dummy);

    /*
     * Optimized performance initialization sequence
     * From PMW3901MB datasheet and reference implementations
     */
    pmw3901WriteRegister(0x7F, 0x00);
    pmw3901WriteRegister(0x61, 0xAD);
    pmw3901WriteRegister(0x7F, 0x03);
    pmw3901WriteRegister(0x40, 0x00);
    pmw3901WriteRegister(0x7F, 0x05);
    pmw3901WriteRegister(0x41, 0xB3);
    pmw3901WriteRegister(0x43, 0xF1);
    pmw3901WriteRegister(0x45, 0x14);
    pmw3901WriteRegister(0x5B, 0x32);
    pmw3901WriteRegister(0x5F, 0x34);
    pmw3901WriteRegister(0x7B, 0x08);
    pmw3901WriteRegister(0x7F, 0x06);
    pmw3901WriteRegister(0x44, 0x1B);
    pmw3901WriteRegister(0x40, 0xBF);
    pmw3901WriteRegister(0x4E, 0x3F);
    pmw3901WriteRegister(0x7F, 0x08);
    pmw3901WriteRegister(0x65, 0x20);
    pmw3901WriteRegister(0x6A, 0x18);
    pmw3901WriteRegister(0x7F, 0x09);
    pmw3901WriteRegister(0x4F, 0xAF);
    pmw3901WriteRegister(0x5F, 0x40);
    pmw3901WriteRegister(0x48, 0x80);
    pmw3901WriteRegister(0x49, 0x80);
    pmw3901WriteRegister(0x57, 0x77);
    pmw3901WriteRegister(0x60, 0x78);
    pmw3901WriteRegister(0x61, 0x78);
    pmw3901WriteRegister(0x62, 0x08);
    pmw3901WriteRegister(0x63, 0x50);
    pmw3901WriteRegister(0x7F, 0x0A);
    pmw3901WriteRegister(0x45, 0x60);
    pmw3901WriteRegister(0x7F, 0x00);
    pmw3901WriteRegister(0x4D, 0x11);
    pmw3901WriteRegister(0x55, 0x80);
    pmw3901WriteRegister(0x74, 0x1F);
    pmw3901WriteRegister(0x75, 0x1F);
    pmw3901WriteRegister(0x4A, 0x78);
    pmw3901WriteRegister(0x4B, 0x78);
    pmw3901WriteRegister(0x44, 0x08);
    pmw3901WriteRegister(0x45, 0x50);
    pmw3901WriteRegister(0x64, 0xFF);
    pmw3901WriteRegister(0x65, 0x1F);
    pmw3901WriteRegister(0x7F, 0x14);
    pmw3901WriteRegister(0x65, 0x60);
    pmw3901WriteRegister(0x66, 0x08);
    pmw3901WriteRegister(0x63, 0x78);
    pmw3901WriteRegister(0x7F, 0x15);
    pmw3901WriteRegister(0x48, 0x58);
    pmw3901WriteRegister(0x7F, 0x07);
    pmw3901WriteRegister(0x41, 0x0D);
    pmw3901WriteRegister(0x43, 0x14);
    pmw3901WriteRegister(0x4B, 0x0E);
    pmw3901WriteRegister(0x45, 0x0F);
    pmw3901WriteRegister(0x44, 0x42);
    pmw3901WriteRegister(0x4C, 0x80);
    pmw3901WriteRegister(0x7F, 0x10);
    pmw3901WriteRegister(0x5B, 0x02);
    pmw3901WriteRegister(0x7F, 0x07);
    pmw3901WriteRegister(0x40, 0x41);
    pmw3901WriteRegister(0x70, 0x00);

    delay(10);
    pmw3901WriteRegister(0x32, 0x44);
    pmw3901WriteRegister(0x7F, 0x07);
    pmw3901WriteRegister(0x40, 0x40);
    pmw3901WriteRegister(0x7F, 0x06);
    pmw3901WriteRegister(0x62, 0xF0);
    pmw3901WriteRegister(0x63, 0x00);
    pmw3901WriteRegister(0x7F, 0x0D);
    pmw3901WriteRegister(0x48, 0xC0);
    pmw3901WriteRegister(0x6F, 0xD5);
    pmw3901WriteRegister(0x7F, 0x00);
    pmw3901WriteRegister(0x5B, 0xA0);
    pmw3901WriteRegister(0x4E, 0xA8);
    pmw3901WriteRegister(0x5A, 0x50);
    pmw3901WriteRegister(0x40, 0x80);
}

static bool pmw3901Detect(void)
{
    busDev = busDeviceInit(BUSTYPE_SPI, DEVHW_PMW3901, 0, OWNER_OPFLOW);
    if (busDev == NULL) {
        return false;
    }

    busSetSpeed(busDev, BUS_SPEED_SLOW);

    uint8_t productId = 0;
    uint8_t invProductId = 0;

    delay(50);  /* Wait for chip power-up */

    if (!pmw3901ReadRegister(PMW3901_REG_PRODUCT_ID, &productId)) {
        busDeviceDeInit(busDev);
        return false;
    }

    if (!pmw3901ReadRegister(PMW3901_REG_INVERSE_PROD_ID, &invProductId)) {
        busDeviceDeInit(busDev);
        return false;
    }

    if (productId != PMW3901_PRODUCT_ID || invProductId != PMW3901_INVERSE_PRODUCT_ID) {
        busDeviceDeInit(busDev);
        return false;
    }

    return true;
}

static bool pmw3901Init(void)
{
    busSetSpeed(busDev, BUS_SPEED_STANDARD);
    pmw3901InitSequence();
    return true;
}

static bool pmw3901Update(opflowData_t *data)
{
    static timeUs_t previousTimeUs = 0;
    const timeUs_t currentTimeUs = micros();

    uint8_t motion;
    if (!pmw3901ReadRegister(PMW3901_REG_MOTION, &motion)) {
        return false;
    }

    if (!(motion & PMW3901_MOTION_DETECTED)) {
        return false;
    }

    /* Read delta X and Y */
    uint8_t deltaXL, deltaXH, deltaYL, deltaYH;
    uint8_t squal;

    if (!pmw3901ReadRegister(PMW3901_REG_DELTA_X_L, &deltaXL) ||
        !pmw3901ReadRegister(PMW3901_REG_DELTA_X_H, &deltaXH) ||
        !pmw3901ReadRegister(PMW3901_REG_DELTA_Y_L, &deltaYL) ||
        !pmw3901ReadRegister(PMW3901_REG_DELTA_Y_H, &deltaYH) ||
        !pmw3901ReadRegister(PMW3901_REG_SQUAL, &squal)) {
        return false;
    }

    int16_t deltaX = (int16_t)((deltaXH << 8) | deltaXL);
    int16_t deltaY = (int16_t)((deltaYH << 8) | deltaYL);

    data->deltaTime = currentTimeUs - previousTimeUs;
    data->flowRateRaw[0] = deltaX;
    data->flowRateRaw[1] = deltaY;
    data->flowRateRaw[2] = 0;
    /* Scale quality from 0-169 to 0-100 */
    data->quality = constrain((squal * 100) / 169, 0, 100);

    previousTimeUs = currentTimeUs;

    return true;
}

virtualOpflowVTable_t opflowPmw3901Vtable = {
    .detect = pmw3901Detect,
    .init = pmw3901Init,
    .update = pmw3901Update
};

#endif /* USE_OPFLOW_PMW3901 */
