//
// Copyright (c) 2017 The nanoFramework project contributors
// See LICENSE file in the project root for full license information.
//

#define UNUSED(x) (void)x

#ifndef SPI_TO_TOUCHPANEL_CPP
#define SPI_TO_TOUCHPANEL_CPP

#include "nanoCLR_Types.h"
#include <nanoPAL.h>
#include <target_platform.h>
#include <esp32_idf.h>

#include "TouchInterface.h"

bool TouchInterface::Initialize()
{
    // Setup SPI configuration

    return true;
}

CLR_UINT8 *TouchInterface::Write_Read(
    CLR_UINT8 *valuesToSend,
    CLR_UINT16 numberOfValuesToSend,
    CLR_UINT16 numberValuesExpected)
{

    UNUSED(valuesToSend);
    UNUSED(numberOfValuesToSend);
    UNUSED(numberValuesExpected);

    return 0;
}


#include "TouchDevice.h"
#include "TouchInterface.h"

struct TouchDevice g_TouchDevice;
extern TouchInterface g_TouchInterface;

bool TouchDevice::Initialize()
{
    ReadsToIgnore = 1;
    ReadsPerSample = 1;
    MaxFilterDistance = 1; // This is actually squared value of the max distance allowed between two points.
    return true;
}

bool TouchDevice::Enable(GPIO_INTERRUPT_SERVICE_ROUTINE touchIsrProc)
{
    if (touchIsrProc == NULL)
    {
    };
    return TRUE;
}

bool TouchDevice::Disable()
{
    return true;
}

TouchPointDevice TouchDevice::GetPoint()
{
    // stub
    TouchPointDevice TouchValue;

    TouchValue.x = 0;
    TouchValue.y = 0;

    return TouchValue;
}
#endif // Gt911_i2c

