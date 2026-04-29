//
// Copyright (c) .NET Foundation and Contributors
// See LICENSE file in the project root for full license information.
//
//

#include "Graphics.h"
#include "DisplayInterface.h"
#include "Display.h"

struct DisplayDriver g_DisplayDriver;
extern DisplayInterface g_DisplayInterface;
extern DisplayInterfaceConfig g_DisplayInterfaceConfig;

// A pointer on the Set window function to make things faster
bool (*SetWindowPointer)(CLR_INT16 x1, CLR_INT16 y1, CLR_INT16 x2, CLR_INT16 y2);

/*
Process command function to send commands or sleep.
*/
void ProcessCommand(CLR_RT_HeapBlock_Array *array)
{

    CLR_UINT32 numberElements = array->m_numOfElements;
    ets_printf("ProcessCommand called %X  len %d\n", array, numberElements);


    CLR_UINT32 inc = 0;
    CLR_UINT8 *cmd;
    CLR_UINT8 *size;
    while (inc < numberElements)
    {
        cmd = array->GetElement(inc++);
        size = array->GetElement(inc++);
        // This is a sleep instruction
        if (*cmd == GraphicDriverCommandType::GraphicDriverCommandType_Sleep)
        {
            OS_DELAY(*cmd * *size * 10);
        }
        // This is the normal command
        else if (*cmd == GraphicDriverCommandType::GraphicDriverCommandType_Command)
        {
            // Send the command followed by the data, size is the all up size, so reduce by 1
            ets_printf("ProcessCommand: cmd %d size %d\n", *cmd, *size);
    //         g_DisplayInterface.WriteToFrameBuffer(*array->GetElement(inc), array->GetElement(inc + 1), *size - 1, 0);

            // increase the increment
            inc += *size;
        }
        else
        {
            // Should no arrive, increment by 1 in case
            inc++;
        }
    }
}

/*
The next functions are the primitives depending on the driver set windowing behavior
*/
bool SetWindowNoWindowing(CLR_INT16 x1, CLR_INT16 y1, CLR_INT16 x2, CLR_INT16 y2)
{
    (void)x1; // To avoid gcc/g++ warnings
    (void)y1; // To avoid gcc/g++ warnings
    (void)x2; // To avoid gcc/g++ warnings
    (void)y2; // To avoid gcc/g++ warnings

    ets_printf("SetWindowNoWindowing called %d %d %d %d\n", x1, y1, x2, y2);
    // If no windowing, nothing is done here.
    return true;
}

bool SetWindowX8bitsY1Bit(CLR_INT16 x1, CLR_INT16 y1, CLR_INT16 x2, CLR_INT16 y2)
{
    (void)x1; // To avoid gcc/g++ warnings
    (void)x2; // To avoid gcc/g++ warnings
    (void)y1; // To avoid gcc/g++ warnings
    (void)y2; // To avoid gcc/g++ warnings
    ets_printf("SetWindowX8bitsY1Bit called %d %d %d %d\n", x1, y1, x2, y2);

    // // Start & End column address
    // g_DisplayInterface.SendCommand(3, g_DisplayInterfaceConfig.GenericDriverCommands.SetColumnAddress, x1, x2);

    // // Start & End page address  0 to 7
    // g_DisplayInterface.SendCommand(3, g_DisplayInterfaceConfig.GenericDriverCommands.SetRowAddress, (y1 / 8), (y2 / 8));

    return true;
}

bool SetWindowX8bitsY8Bits(CLR_INT16 x1, CLR_INT16 y1, CLR_INT16 x2, CLR_INT16 y2)
{
    (void)x1; // To avoid gcc/g++ warnings
    (void)x2; // To avoid gcc/g++ warnings
    (void)y1; // To avoid gcc/g++ warnings
    (void)y2; // To avoid gcc/g++ warnings
    ets_printf("SetWindowX8bitsY8Bits called %d %d %d %d\n", x1, y1, x2, y2);

    // // Start & End column address
    // g_DisplayInterface.SendCommand(3, g_DisplayInterfaceConfig.GenericDriverCommands.SetColumnAddress, x1, x2);

    // // Start & End row address
    // g_DisplayInterface.SendCommand(3, g_DisplayInterfaceConfig.GenericDriverCommands.SetRowAddress, y1, y2);

    return true;
}

bool SetWindowX16bitsY16Bit(CLR_INT16 x1, CLR_INT16 y1, CLR_INT16 x2, CLR_INT16 y2)
{
    (void)x1; // To avoid gcc/g++ warnings
    (void)x2; // To avoid gcc/g++ warnings
    (void)y1; // To avoid gcc/g++ warnings
    (void)y2; // To avoid gcc/g++ warnings
    ets_printf("SetWindowX16bitsY16Bit called %d %d %d %d\n", x1, y1, x2, y2);

    // CLR_UINT8 Column_Address_Set_Data[4];
    // Column_Address_Set_Data[0] = ((x1 + g_DisplayInterfaceConfig.Screen.x) >> 8) & 0xFF;
    // Column_Address_Set_Data[1] = (x1 + g_DisplayInterfaceConfig.Screen.x) & 0xFF;
    // Column_Address_Set_Data[2] = ((x2 + g_DisplayInterfaceConfig.Screen.x) >> 8) & 0xFF;
    // Column_Address_Set_Data[3] = (x2 + g_DisplayInterfaceConfig.Screen.x) & 0xFF;
    // g_DisplayInterface.SendCommand(
    //     5,
    //     g_DisplayInterfaceConfig.GenericDriverCommands.SetColumnAddress,
    //     Column_Address_Set_Data[0],
    //     Column_Address_Set_Data[1],
    //     Column_Address_Set_Data[2],
    //     Column_Address_Set_Data[3]);

    // CLR_UINT8 Page_Address_Set_Data[4];
    // Page_Address_Set_Data[0] = ((y1 + g_DisplayInterfaceConfig.Screen.y) >> 8) & 0xFF;
    // Page_Address_Set_Data[1] = (y1 + g_DisplayInterfaceConfig.Screen.y) & 0xFF;
    // Page_Address_Set_Data[2] = ((y2 + g_DisplayInterfaceConfig.Screen.y) >> 8) & 0xFF;
    // Page_Address_Set_Data[3] = (y2 + g_DisplayInterfaceConfig.Screen.y) & 0xFF;
    // g_DisplayInterface.SendCommand(
    //     5,
    //     g_DisplayInterfaceConfig.GenericDriverCommands.SetRowAddress,
    //     Page_Address_Set_Data[0],
    //     Page_Address_Set_Data[1],
    //     Page_Address_Set_Data[2],
    //     Page_Address_Set_Data[3]);
    return true;
}

bool DisplayDriver::Initialize()
{
    ets_printf("DisplayDriver::Initialize called\n");

    // SetWindowPointer to the correct function
    switch (g_DisplayInterfaceConfig.GenericDriverCommands.SetWindowType)
    {
        default:
        case SetWindowType::SetWindowType_NoWindowing:
            SetWindowPointer = &SetWindowNoWindowing;
            break;
        case SetWindowType::SetWindowType_X8bitsY1Bit:
            SetWindowPointer = &SetWindowX8bitsY1Bit;
            break;
        case SetWindowType::SetWindowType_X8bitsY8Bits:
            SetWindowPointer = &SetWindowX8bitsY8Bits;
            break;
        case SetWindowType::SetWindowType_X16bitsY16Bit:
            SetWindowPointer = &SetWindowX16bitsY16Bit;
            break;
    }

    SetupDisplayAttributes();

    ProcessCommand(g_DisplayInterfaceConfig.GenericDriverCommands.InitializationSequence);

    SetDefaultOrientation();

    return true;
}

void DisplayDriver::SetupDisplayAttributes()
{
    ets_printf("DisplayDriver::SetupDisplayAttributes called w=%d h=%d\n", g_DisplayInterfaceConfig.GenericDriverCommands.Width, g_DisplayInterfaceConfig.GenericDriverCommands.Height);

    // Define the LCD/TFT resolution
    if (g_DisplayInterfaceConfig.GenericDriverCommands.Width == 0)
    {
        Attributes.LongerSide = g_DisplayInterfaceConfig.Screen.width;
    }
    else
    {
        Attributes.LongerSide = g_DisplayInterfaceConfig.GenericDriverCommands.Width;
    }

    if (g_DisplayInterfaceConfig.GenericDriverCommands.Height == 0)
    {
        Attributes.ShorterSide = g_DisplayInterfaceConfig.Screen.height;
        ;
    }
    else
    {
        Attributes.ShorterSide = g_DisplayInterfaceConfig.GenericDriverCommands.Height;
    }

    ets_printf("DisplayDriver::SetupDisplayAttributes: LongerSide %d ShorterSide %d\n", Attributes.LongerSide, Attributes.ShorterSide);

    g_DisplayInterface.GetTransferBuffer(Attributes.TransferBuffer, Attributes.TransferBufferSize);
}

bool DisplayDriver::ChangeOrientation(DisplayOrientation orientation)
{
    ets_printf("DisplayDriver::ChangeOrientation called %d\n", orientation);
    switch (orientation)
    {
        case DisplayOrientation::DisplayOrientation_Portrait:
            if (g_DisplayInterfaceConfig.GenericDriverCommands.OrientationPortrait != NULL)
            {
                Attributes.Height = Attributes.LongerSide;
                Attributes.Width = Attributes.ShorterSide;
    //             ProcessCommand(g_DisplayInterfaceConfig.GenericDriverCommands.OrientationPortrait);
                return true;
            }

            break;
        case DisplayOrientation::DisplayOrientation_Portrait180:
            if (g_DisplayInterfaceConfig.GenericDriverCommands.OrientationPortrait180 != NULL)
            {
                Attributes.Height = Attributes.LongerSide;
                Attributes.Width = Attributes.ShorterSide;
    //             ProcessCommand(g_DisplayInterfaceConfig.GenericDriverCommands.OrientationPortrait180);
                return true;
            }

            break;
        case DisplayOrientation::DisplayOrientation_Landscape:
            if (g_DisplayInterfaceConfig.GenericDriverCommands.OrientationLandscape != NULL)
            {
                Attributes.Height = Attributes.ShorterSide;
                Attributes.Width = Attributes.LongerSide;
    //             ProcessCommand(g_DisplayInterfaceConfig.GenericDriverCommands.OrientationLandscape);
                return true;
            }

            break;
        case DisplayOrientation::DisplayOrientation_Landscape180:
            if (g_DisplayInterfaceConfig.GenericDriverCommands.OrientationLandscape180 != NULL)
            {
                Attributes.Height = Attributes.ShorterSide;
                Attributes.Width = Attributes.LongerSide;
    //             ProcessCommand(g_DisplayInterfaceConfig.GenericDriverCommands.OrientationLandscape180);
                return true;
            }

            break;
    }

    return false;
}

void DisplayDriver::SetDefaultOrientation()
{

    ets_printf("DisplayDriver::SetDefaultOrientation called\n");
    ChangeOrientation((DisplayOrientation)g_DisplayInterfaceConfig.GenericDriverCommands.DefaultOrientation);
}

void DisplayDriver::PowerSave(PowerSaveState powerState)
{
    ets_printf("DisplayDriver::PowerSave called %d\n", powerState);

    switch (powerState)
    {
        default:
            // Illegal fall through to Power on
        case PowerSaveState::NORMAL:
            if (g_DisplayInterfaceConfig.GenericDriverCommands.PowerModeNormal != NULL)
            {
    //             ProcessCommand(g_DisplayInterfaceConfig.GenericDriverCommands.PowerModeNormal);
            }

            break;
        case PowerSaveState::SLEEP:
            if (g_DisplayInterfaceConfig.GenericDriverCommands.PowerModeSleep != NULL)
            {
    //             ProcessCommand(g_DisplayInterfaceConfig.GenericDriverCommands.PowerModeSleep);
            }

            break;
    }
}

void DisplayDriver::Clear()
{
    ets_printf("DisplayDriver::Clear called\n");

    // // Default behavior
    // if (g_DisplayInterfaceConfig.GenericDriverCommands.Clear == NULL)
    // {
    //     SetWindow(0, 0, Attributes.Width - 1, Attributes.Height - 1);

    //     g_DisplayInterface.SendCommand(1, g_DisplayInterfaceConfig.GenericDriverCommands.MemoryWrite);

    //     g_DisplayInterface.FillData16(0, Attributes.Width * Attributes.Height);
    // }
    // else
    // {
    //     ProcessCommand(g_DisplayInterfaceConfig.GenericDriverCommands.Clear);
    // }
}

void DisplayDriver::DisplayBrightness(CLR_INT16 brightness)
{
    ets_printf("DisplayDriver::DisplayBrightness called %d\n", brightness);
    // _ASSERTE(brightness >= 0 && brightness <= 100);
    // g_DisplayInterface.SendCommand(2, g_DisplayInterfaceConfig.GenericDriverCommands.Brightness, (CLR_UINT8)brightness);
}

bool DisplayDriver::SetWindow(CLR_INT16 x1, CLR_INT16 y1, CLR_INT16 x2, CLR_INT16 y2)
{
    ets_printf("DisplayDriver::SetWindow called %d %d %d %d\n", x1, y1, x2, y2);
    return true;

    //return (*SetWindowPointer)(x1, y1, x2, y2);
}

void DisplayDriver::BitBlt(
    int srcX,
    int srcY,
    int width,
    int height,
    int stride,
    int screenX,
    int screenY,
    CLR_UINT32 data[])
{
    
    ets_printf("DisplayDriver::BitBlt called %d %d %d %d %d %d %d\n", srcX, srcY, width, height, stride, screenX, screenY);

    // // 16 bit colour  RRRRRGGGGGGBBBBB mode 565

    // ASSERT((screenX >= 0) && ((screenX + width) <= Attributes.Width));
    // ASSERT((screenY >= 0) && ((screenY + height) <= Attributes.Height));

    // SetWindow(screenX, screenY, (screenX + width - 1), (screenY + height - 1));

    // g_DisplayInterface.SendCommand(1, g_DisplayInterfaceConfig.GenericDriverCommands.MemoryWrite);

    g_DisplayInterface.SendData16Windowed((CLR_UINT16 *)&data[0], srcX, srcY, width, height, stride, true);

    return;
}

CLR_UINT32 DisplayDriver::PixelsPerWord()
{
    ets_printf("DisplayDriver::PixelsPerWord called\n");
    return (32 / Attributes.BitsPerPixel);
}

CLR_UINT32 DisplayDriver::WidthInWords()
{
    ets_printf("DisplayDriver::WidthInWords called\n");
    // Calculate the number of words needed to cover the width of the display   
    return ((Attributes.Width + (PixelsPerWord() - 1)) / PixelsPerWord());
}

CLR_UINT32 DisplayDriver::SizeInWords()
{
    ets_printf("DisplayDriver::SizeInWords called\n");
    // Calculate the size in words of the display
    return (WidthInWords() * Attributes.Height);
}

CLR_UINT32 DisplayDriver::SizeInBytes()
{
    ets_printf("DisplayDriver::SizeInBytes called\n");
    // Calculate the size in bytes of the display
    return (SizeInWords() * sizeof(CLR_UINT32));
}
